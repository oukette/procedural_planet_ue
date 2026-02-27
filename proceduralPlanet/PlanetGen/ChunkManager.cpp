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

    // Chunks are now created on-demand in the Update loop via GetChunk().
    // Pre-allocating them here is wasteful as most would be immediately garbage collected on the first frame.

    // Initialize Quadtree Roots (LOD 0)
    RootNodes.Empty();
    for (uint8 i = 0; i < 6; ++i)
    {
        FChunkId RootId(i, FIntVector(0, 0, 0), 0);
        RootNodes.Add(MakeUnique<FQuadtreeNode>(RootId, nullptr));
    }

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


void FChunkManager::Update(const FPlanetViewContext &Context)
{
    // 1. Prepare View & Traversal
    TSet<FChunkId> VisibleChunks;
    TSet<FChunkId> CachedChunks;
    DetermineChunkVisibility(Context, VisibleChunks, CachedChunks);

    // 2. State Machine & Cache Management
    ProcessChunkStates(VisibleChunks, CachedChunks);

    // 3. Async Dispatch
    if (ChunkGenerator)
    {
        ChunkGenerator->Update();
    }
}

void FChunkManager::DetermineChunkVisibility(const FPlanetViewContext &Context, TSet<FChunkId> &OutVisibleChunks, TSet<FChunkId> &OutCachedChunks)
{
    FVector ObserverLocal = Context.ObserverLocation;
    FVector ObserverVelocityLocal = Context.ObserverVelocity;
    FVector ObserverForwardLocal = Context.ObserverForward;

    if (Renderer && Renderer->GetOwner())
    {
        FTransform OwnerTM = Renderer->GetOwner()->GetActorTransform();
        ObserverLocal = OwnerTM.InverseTransformPosition(Context.ObserverLocation);
        ObserverVelocityLocal = OwnerTM.InverseTransformVector(Context.ObserverVelocity);
        ObserverForwardLocal = OwnerTM.InverseTransformVector(Context.ObserverForward);
    }

    // Check distance for chunk generation
    float DistToCenter = ObserverLocal.Size();
    float DistToSurface = DistToCenter - Config.PlanetRadius;

    // We want chunks to exist slightly beyond the render distance so they are ready when we get closer.
    bool bShouldGenerateChunks = DistToSurface < (Config.FarDistanceThreshold * FPlanetStatics::FarDistanceSafetyMargin);

    if (bShouldGenerateChunks)
    {
        // --- Predictive LOD ---
        // Calculate a look-ahead time based on altitude. More time at low altitude, less at high altitude.
        const float Altitude = FMath::Max(0.f, DistToSurface);
        const float AltitudeAlpha = FMath::Clamp(Altitude / Config.LookAheadAltitudeScale, 0.f, 1.f);
        const float CurrentLookAheadTime = FMath::Lerp(Config.MaxLookAheadTime, Config.MinLookAheadTime, AltitudeAlpha);

        // --- Smart Velocity Dampening ---
        // Decompose velocity into Radial (Vertical) and Tangential (Horizontal) components.
        // We progressively decrease the weight of the Z-axis (Radial) prediction as we get closer to the surface.
        FVector RadialDir = ObserverLocal.GetSafeNormal();
        if (RadialDir.IsZero())
            RadialDir = FVector::UpVector;

        float RadialSpeed = FVector::DotProduct(ObserverVelocityLocal, RadialDir);
        FVector TangentialVelocity = ObserverVelocityLocal - (RadialDir * RadialSpeed);

        // Weight the radial component based on altitude (Square curve for smoother falloff near surface)
        float RadialWeight = AltitudeAlpha * AltitudeAlpha;
        FVector EffectiveVelocity = TangentialVelocity + (RadialDir * RadialSpeed * RadialWeight);

        FVector LODObserverLocal = ObserverLocal + (EffectiveVelocity * CurrentLookAheadTime);

        // Clamp to Surface: Ensure prediction never goes underground.
        // This is the ultimate safety net against "bouncing" caused by falling predictions.
        if (LODObserverLocal.SizeSquared() < FMath::Square(Config.PlanetRadius))
        {
            LODObserverLocal = LODObserverLocal.GetSafeNormal() * Config.PlanetRadius;
        }

        // Build a new context with local-space and predicted data for the quadtree update.
        FPlanetViewContext LocalContext = Context;
        LocalContext.ObserverLocation = ObserverLocal;  // Current position for culling
        LocalContext.ObserverForward = ObserverForwardLocal;
        LocalContext.PredictedObserverLocation = LODObserverLocal;  // Predicted position for LOD splitting

        for (uint8 Face = 0; Face < 6; ++Face)
        {
            UpdateNode(RootNodes[Face].Get(), LocalContext, OutVisibleChunks, OutCachedChunks);
        }
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


void FChunkManager::UpdateNode(FQuadtreeNode *Node, const FPlanetViewContext &Context, TSet<FChunkId> &OutVisibleChunks, TSet<FChunkId> &OutCachedChunks)
{
    FVector Center = FMathUtils::GetChunkCenter(Node->Id, Config.PlanetRadius);
    FVector ToChunk = Center - Context.ObserverLocation;  // Use current position for culling
    float DistSq = ToChunk.SizeSquared();

    FVector ChunkNormal = Center.GetSafeNormal();
    FVector ToObserverDir = -ToChunk.GetSafeNormal();

    // Special handling for Root Nodes (LOD 0)
    // They are too large to cull accurately with a single center point.
    // We use a very permissive threshold for them.
    float HorizonThreshold = (Node->Id.LODLevel == 0) ? -0.9f : FPlanetStatics::HorizonCullingDot;
    float FrustumThreshold = (Node->Id.LODLevel == 0) ? -0.9f : FPlanetStatics::FrustumCullingDot;

    // 1. Horizon Culling (Backface)
    if ((ChunkNormal | ToObserverDir) < HorizonThreshold)
        return;

    // Calculate Node Size (Arc Length) for culling safety check
    float NodeSize = (Config.PlanetRadius * PI * 0.5f) / (float)(1 << Node->Id.LODLevel);

    // 2. Frustum Culling
    // We skip culling if we are very close to the chunk (e.g. standing on it).
    // This prevents the ground directly below/behind the camera from disappearing when looking at the horizon.
    // We use a generous factor (1.5x node size) to ensure we are well "outside" before culling takes over.
    bool bIsClose = DistSq < (NodeSize * NodeSize * 2.25f);  // 1.5^2 = 2.25

    if (!bIsClose && !Context.ObserverForward.IsZero() && (ToChunk.GetSafeNormal() | Context.ObserverForward) < FrustumThreshold)
        return;

    // 3. Split Check
    // Use the predicted observer location to decide if we need to split for higher detail.
    if (ShouldSplit(Node, Context.PredictedObserverLocation))
    {
        if (Node->IsLeaf())
        {
            // Create Children
            int32 NextLOD = Node->Id.LODLevel + 1;
            int32 X = Node->Id.Coords.X;
            int32 Y = Node->Id.Coords.Y;
            uint8 Face = Node->Id.FaceIndex;

            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2, Y * 2, 0), NextLOD), Node));
            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2 + 1, Y * 2, 0), NextLOD), Node));
            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2, Y * 2 + 1, 0), NextLOD), Node));
            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2 + 1, Y * 2 + 1, 0), NextLOD), Node));
        }

        // --- BLINKING FIX: SPLIT HYSTERESIS ---
        // Only switch to children if ALL children are ready.
        bool bAllChildrenReady = true;
        for (const auto &Child : Node->Children)
        {
            if (!IsChunkReady(Child->Id))
            {
                bAllChildrenReady = false;
                break;
            }
        }

        if (bAllChildrenReady)
        {
            // Children are ready, we can safely split without blinking.
            for (auto &Child : Node->Children)
            {
                UpdateNode(Child.Get(), Context, OutVisibleChunks, OutCachedChunks);
            }
        }
        else
        {
            // Children are NOT ready. Keep rendering Parent (Self) to avoid holes.
            OutVisibleChunks.Add(Node->Id);

            // Update Transform for Parent (since we are rendering it)
            FChunk *Chunk = GetChunk(Node->Id);
            if (Chunk)
            {
                Chunk->Transform = FMathUtils::ComputeChunkTransform(Node->Id, Config.PlanetRadius);
            }

            // Force load children in background so they become ready for next frames
            for (auto &Child : Node->Children)
            {
                OutCachedChunks.Add(Child->Id);
            }
        }
    }
    else
    {
        // --- BLINKING FIX: MERGE HYSTERESIS ---
        // We want to merge (render Self).
        // If Self is NOT ready, keep rendering children if they exist.

        bool bSelfReady = IsChunkReady(Node->Id);

        if (bSelfReady)
        {
            // Safe to merge
            if (!Node->IsLeaf())
            {
                Node->Children.Empty();
            }
            OutVisibleChunks.Add(Node->Id);
        }
        else
        {
            // Parent not ready. Request Parent generation.
            OutVisibleChunks.Add(Node->Id);  // This will trigger generation but won't render until ready

            // Keep children visible for now
            if (!Node->IsLeaf())
            {
                for (auto &Child : Node->Children)
                {
                    UpdateNode(Child.Get(), Context, OutVisibleChunks, OutCachedChunks);
                }
            }
        }

        // Update Transform (Always needed if we might render it)
        if (bSelfReady || OutVisibleChunks.Contains(Node->Id))
        {
            FChunk *Chunk = GetChunk(Node->Id);
            if (Chunk)
            {
                Chunk->Transform = FMathUtils::ComputeChunkTransform(Node->Id, Config.PlanetRadius);
            }
        }
    }
}


bool FChunkManager::ShouldSplit(const FQuadtreeNode *Node, const FVector &ObserverLocal) const
{
    if (Node->Id.LODLevel >= Config.MaxLOD)
        return false;

    FVector Center = FMathUtils::GetChunkCenter(Node->Id, Config.PlanetRadius);

    // We use the full 3D distance here. The "ObserverLocal" passed in is already the
    // sanitized/clamped predicted position from Update(), so it won't be underground.
    float Dist = FVector::Dist(Center, ObserverLocal);

    // Calculate physical size of this node (arc length)
    // Base Arc Length (LOD 0) is (PI * R) / 2
    float NodeSize = (Config.PlanetRadius * PI * 0.5f) / (float)(1 << Node->Id.LODLevel);

    return Dist < (NodeSize * Config.LODSplitDistanceMultiplier);
}


void FChunkManager::DrawDebugGrid(const UWorld *World) const
{
    if (!World)
        return;

    // Get Planet Transform to draw grid in correct World Space
    FTransform PlanetTransform = FTransform::Identity;
    if (Renderer && Renderer->GetOwner())
    {
        PlanetTransform = Renderer->GetOwner()->GetActorTransform();
    }

    int32 GridSize = Config.ChunksPerFace;
    float Step = 1.0f / GridSize;
    // Draw slightly offset to avoid z-fighting with the mesh
    float Radius = Config.PlanetRadius * FPlanetStatics::GridDebugRadiusScale;

    for (uint8 Face = 0; Face < 6; ++Face)
    {
        FVector Normal = FMathUtils::getFaceNormal(Face);
        FVector Right = FMathUtils::getFaceRight(Face);
        FVector Up = FMathUtils::getFaceUp(Face);

        for (int32 x = 0; x < GridSize; ++x)
        {
            for (int32 y = 0; y < GridSize; ++y)
            {
                float UMin = x * Step;
                float UMax = (x + 1) * Step;
                float VMin = y * Step;
                float VMax = (y + 1) * Step;

                auto ToWorld = [&](float U, float V)
                {
                    FVector CubePos = Normal + (Right * (U * 2.0f - 1.0f)) + (Up * (V * 2.0f - 1.0f));
                    FVector SpherePos = FMathUtils::projectCubeToSphere(CubePos) * Radius;
                    // Apply Planet Transform
                    return PlanetTransform.TransformPosition(SpherePos);
                };

                FVector P0 = ToWorld(UMin, VMin);
                FVector P1 = ToWorld(UMax, VMin);
                FVector P2 = ToWorld(UMax, VMax);
                FVector P3 = ToWorld(UMin, VMax);

                DrawDebugLine(World, P0, P1, FColor::Cyan, false, -1.0f, 0, FPlanetStatics::DebugLineLifetime);
                DrawDebugLine(World, P1, P2, FColor::Cyan, false, -1.0f, 0, FPlanetStatics::DebugLineLifetime);
                DrawDebugLine(World, P2, P3, FColor::Cyan, false, -1.0f, 0, FPlanetStatics::DebugLineLifetime);
                DrawDebugLine(World, P3, P0, FColor::Cyan, false, -1.0f, 0, FPlanetStatics::DebugLineLifetime);
            }
        }
    }
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