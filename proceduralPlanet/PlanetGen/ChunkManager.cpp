#include "ChunkManager.h"
#include "DrawDebugHelpers.h"


FChunkManager::FChunkManager(const FPlanetConfig &planetConfig, const DensityGenerator *densityGen) :
    Config(planetConfig),
    Generator(densityGen)
{
    // DEBUG LOG
    UE_LOG(LogTemp, Warning, TEXT("FChunkManager created."));
    UE_LOG(LogTemp, Warning, TEXT("Collision is globally %s"), Config.bEnableCollision ? TEXT("enabled") : TEXT("disabled"));
}


FChunkManager::~FChunkManager()
{
    // UniquePtrs in the TMap will automatically destroy all FChunks
    ChunkMap.Empty();
}


int32 FChunkManager::GetTotalChunkCount() const { return ChunkMap.Num(); }


int32 FChunkManager::GetVisibleChunkCount() const
{
    int32 Count = 0;
    for (const auto &Pair : ChunkMap)
    {
        if (Pair.Value->State == EChunkState::Visible)
        {
            Count++;
        }
    }
    return Count;
}


int32 FChunkManager::GetPendingCount() const { return ChunkGenerator ? ChunkGenerator->GetPendingCount() : 0; }


void FChunkManager::Initialize(AActor *Owner, UMaterialInterface *Material)
{
    Renderer = MakeUnique<ChunkRenderer>(Owner, Material);

    ChunkGenerator = MakeUnique<FChunkGenerator>(Config, Generator);
    ChunkGenerator->SetOnChunkGeneratedCallback([this](const FChunkId &Id, uint32 GenId, TUniquePtr<FChunkMeshData> MeshData)
                                                { OnGenerationComplete(Id, GenId, MoveTemp(MeshData)); });

    Quadtree = MakeUnique<FPlanetQuadtree>(Config);

    // DEBUG LOG
    UE_LOG(LogTemp, Log, TEXT("FChunkManager initialized. Total grid capacity is %d chunks."), GetTotalChunkCount());
}


FChunk *FChunkManager::CreateChunk(const FChunkId &Id)
{
    TUniquePtr<FChunk> NewChunk = MakeUnique<FChunk>(Id);
    FChunk *Ptr = NewChunk.Get();
    ChunkMap.Add(Id, MoveTemp(NewChunk));
    return Ptr;
}


FChunk *FChunkManager::GetChunk(const FChunkId &Id)
{
    // If it exists, return it
    if (TUniquePtr<FChunk> *Found = ChunkMap.Find(Id))
    {
        return Found->Get();
    }

    // Otherwise, create a new one
    auto Ptr = CreateChunk(Id);

    return Ptr;
}


bool FChunkManager::IsChunkReady(const FChunkId &Id) const
{
    if (const TUniquePtr<FChunk> *Found = ChunkMap.Find(Id))
    {
        return Found->Get()->State == EChunkState::Ready || Found->Get()->State == EChunkState::Visible;
    }
    return false;
}


FVector FChunkManager::CalculatePredictedLocation(const FPlanetViewContext &Context) const
{
    // Calculate distance to surface
    float DistToCenter = Context.ObserverLocation.Size();
    float DistToSurface = DistToCenter - Config.PlanetRadius;

    // Calculate Prediction based on Altitude and Velocity
    const float Altitude = FMath::Max(0.f, DistToSurface);
    const float AltitudeAlpha = FMath::Clamp(Altitude / Config.LookAheadAltitudeScale, 0.f, 1.f);
    const float CurrentLookAheadTime = FMath::Lerp(Config.MaxLookAheadTime, Config.MinLookAheadTime, AltitudeAlpha);

    // Smart Velocity Dampening: Ignore vertical velocity when high up to prevent LOD thrashing
    FVector RadialDir = Context.ObserverLocation.GetSafeNormal();
    if (RadialDir.IsZero())
        RadialDir = FVector::UpVector;

    float RadialSpeed = FVector::DotProduct(Context.ObserverVelocity, RadialDir);
    FVector TangentialVelocity = Context.ObserverVelocity - (RadialDir * RadialSpeed);

    float RadialWeight = AltitudeAlpha * AltitudeAlpha;
    FVector EffectiveVelocity = TangentialVelocity + (RadialDir * RadialSpeed * RadialWeight);

    FVector LODObserverLocal = Context.ObserverLocation + (EffectiveVelocity * CurrentLookAheadTime);

    // Clamp to Surface: Ensure prediction never goes underground
    if (LODObserverLocal.SizeSquared() < FMath::Square(Config.PlanetRadius))
    {
        LODObserverLocal = LODObserverLocal.GetSafeNormal() * Config.PlanetRadius;
    }

    return LODObserverLocal;
}


void FChunkManager::Update(const FPlanetViewContext &Context)
{
    // 1. Prepare View & Traversal
    // We create a local copy of the context to populate the PredictedObserverLocation
    FPlanetViewContext LocalContext = Context;

    // Calculate distance to surface for LOD and generation checks
    float DistToCenter = Context.ObserverLocation.Size();
    float DistToSurface = DistToCenter - Config.PlanetRadius;

    // Check if we should be generating chunks at all (Distance Check)
    // This prevents wasting CPU on quadtree updates when we are in space/far away
    bool bShouldGenerateChunks = DistToSurface < (Config.FarDistanceThreshold * FPlanetStatics::FarDistanceSafetyMargin);

    if (bShouldGenerateChunks)
    {
        LocalContext.PredictedObserverLocation = CalculatePredictedLocation(Context);

        // Update Quadtree with the refined context
        if (Quadtree)
        {
            Quadtree->Update(LocalContext, [this](const FChunkId &Id) { return IsChunkReady(Id); });
        }
    }

    // 2. State Machine & Cache Management
    // If we shouldn't generate chunks (too far), we pass empty sets to clear existing chunks.
    const auto &Results = (Quadtree && bShouldGenerateChunks) ? Quadtree->GetResults() : FQuadtreeTraversalOutput();
    ProcessChunkStates(Results.VisibleChunks, Results.CachedChunks);

    // 3. Async Dispatch
    if (ChunkGenerator)
    {
        ChunkGenerator->Update();
    }
}


void FChunkManager::ProcessChunkStates(const TSet<FChunkId> &VisibleChunks, const TSet<FChunkId> &CachedChunks)
{
    TArray<FChunkId> ChunksToRemove;

    // 1. Update existing chunks (Demotion & Cleanup)
    for (auto &Pair : ChunkMap)
    {
        FChunkId Id = Pair.Key;
        FChunk *Chunk = Pair.Value.Get();

        const bool bIsVisible = VisibleChunks.Contains(Id);

        // Demote Visible -> Ready if no longer visible
        if (Chunk->State == EChunkState::Visible && !bIsVisible)
        {
            Renderer->HideChunk(Chunk);
            Chunk->State = EChunkState::Ready;
        }

        // Cleanup: If a chunk is Unloaded (transient/zombie), remove it.
        // We strictly use 4 states: Requested, Generating, Ready, Visible.
        // Unloaded chunks are those that were created but never requested, or are invalid.
        if (Chunk->State == EChunkState::Unloaded)
        {
            ChunksToRemove.Add(Id);
        }
    }

    // Execute removal (Garbage Collection)
    for (const FChunkId &Id : ChunksToRemove)
    {
        ChunkMap.Remove(Id);
    }

    // 2. Process Requirements (Promotion & Requests)

    // A. Visible Chunks
    for (const FChunkId &Id : VisibleChunks)
    {
        FChunk *Chunk = GetChunk(Id);  // Creates with State = Unloaded if missing

        if (Chunk->State == EChunkState::Ready)
        {
            // Promote Ready -> Visible
            // Enable collision
            // We enable collision for the highest 2 LOD levels (e.g. MaxLOD and MaxLOD-1).
            // This ensures the player has a solid surface to land on without overloading physics for far chunks.
            const int32 CollisionLODThreshold = FMath::Max(0, Config.MaxLOD - 1);
            const bool bEnableCollision = Config.bEnableCollision && (Chunk->Id.LODLevel >= CollisionLODThreshold);

            // Ensure transform is up to date (Quadtree no longer does this)
            Chunk->Transform = FMathUtils::ComputeChunkTransform(Id, Config.PlanetRadius);

            Renderer->RenderChunk(Chunk, bEnableCollision);
            Chunk->State = EChunkState::Visible;
        }
        else if (Chunk->State == EChunkState::Unloaded)
        {
            // Request Generation
            Chunk->State = EChunkState::Requested;

            // Increment GenerationId to invalidate any previous stale tasks for this chunk
            Chunk->GenerationId++;
            ChunkGenerator->RequestChunk(Id, Chunk->GenerationId);
        }
    }

    // B. Cached Chunks
    for (const FChunkId &Id : CachedChunks)
    {
        FChunk *Chunk = GetChunk(Id);

        if (Chunk->State == EChunkState::Unloaded)
        {
            // Request Generation
            Chunk->State = EChunkState::Requested;

            Chunk->GenerationId++;
            ChunkGenerator->RequestChunk(Id, Chunk->GenerationId);
        }
    }
}


void FChunkManager::DrawDebugGrid(const UWorld *World) const
{
    if (!World || !Quadtree)
        return;

    // Get Planet Transform to draw grid in correct World Space
    FTransform PlanetTransform = FTransform::Identity;
    if (Renderer && Renderer->GetOwner())
    {
        PlanetTransform = Renderer->GetOwner()->GetActorTransform();
    }

    Quadtree->DrawDebugGrid(World, PlanetTransform);
}


void FChunkManager::DrawDebugChunkBounds(const UWorld *World) const
{
    if (!World)
        return;

    for (const auto &Pair : ChunkMap)
    {
        const FChunk *Chunk = Pair.Value.Get();
        // Only draw bounds for chunks that have a visible mesh component
        if (Chunk && Chunk->State == EChunkState::Visible && Chunk->RenderProxy.IsValid())
        {
            if (UProceduralMeshComponent *Comp = Chunk->RenderProxy.Get())
            {
                const int32 LOD = Chunk->Id.LODLevel;
                // Use LOD color if available, otherwise fallback to white
                const FColor BoxColor = (LOD >= 0 && LOD < LODColorsDebug.Num()) ? LODColorsDebug[LOD] : FColor::White;
                const FBox Box = Comp->Bounds.GetBox();
                DrawDebugBox(World, Box.GetCenter(), Box.GetExtent(), BoxColor, false, 0.f, 0, FPlanetStatics::DebugBoxLifetime);
            }
        }
    }
}


void FChunkManager::OnGenerationComplete(const FChunkId &Id, uint32 GenId, TUniquePtr<FChunkMeshData> MeshData)
{
    FChunk *Chunk = GetChunk(Id);
    if (!Chunk)
        return;  // Chunk was unloaded while generating

    if (Chunk->GenerationId != GenId)
        return;  // Stale task (Chunk was reset/regenerated)


    // Store Data
    Chunk->MeshData = MoveTemp(MeshData);
    Chunk->State = EChunkState::Ready;

    // Note: We do not spawn the component here.
    // The Update loop or a separate "ProcessMeshQueue" will handle visibility/spawning.
}