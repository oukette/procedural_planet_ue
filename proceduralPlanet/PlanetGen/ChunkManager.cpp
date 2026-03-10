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
    // --- Robust Shutdown Sequence ---
    // This ensures all resources are released in the correct order, preventing crashes.

    // 1. Stop all background work to prevent callbacks on a partially destroyed manager.
    //    (Assuming FChunkGenerator has a Stop() method to join threads and clear queues).
    if (ChunkGenerator)
        ChunkGenerator->Stop();

    // 2. Return all active visual components to the renderer's pool.
    if (Renderer)
    {
        for (auto &Pair : ChunkMap)
        {
            FChunk *Chunk = Pair.Value.Get();
            // Release any chunk that has a component assigned,
            // regardless of whether it is visible or just mesh-ready.
            if (Chunk->State == EChunkState::Visible || Chunk->State == EChunkState::MeshReady)
            {
                Renderer->ReleaseChunk(Chunk);
            }
            // Null remaining proxies defensively before GC can run
            Chunk->RenderProxy.Reset();
        }
        // 3. Now that all components are in the pool, command the renderer to destroy them.
        Renderer->ReleaseAllComponents();
    }
}


int32 FChunkManager::GetTotalChunkCount() const { return ChunkMap.Num(); }


int32 FChunkManager::GetVisibleChunkCount() const
{
    int32 Count = 0;
    for (const auto &Pair : ChunkMap)
    {
        if (Pair.Value->State == EChunkState::Visible)
            Count++;
    }

    return Count;
}


void FChunkManager::GetVisibleCountPerLOD(TArray<int32> &OutCounts) const
{
    for (const auto &Pair : ChunkMap)
    {
        const FChunk *Chunk = Pair.Value.Get();
        if (Chunk->State == EChunkState::Visible)
        {
            const int32 LOD = Chunk->Id.LODLevel;
            if (OutCounts.IsValidIndex(LOD))
                OutCounts[LOD]++;
        }
    }
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
        return Found->Get();

    return nullptr;
}


bool FChunkManager::IsChunkReady(const FChunkId &Id) const
{
    if (const TUniquePtr<FChunk> *Found = ChunkMap.Find(Id))
    {
        EChunkState S = Found->Get()->State;
        return S == EChunkState::MeshReady || S == EChunkState::Visible;
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

    FVector Predicted = Context.ObserverLocation + (EffectiveVelocity * CurrentLookAheadTime);

    // Clamp to Surface: Ensure prediction never goes underground
    if (Predicted.SizeSquared() < FMath::Square(Config.PlanetRadius))
    {
        Predicted = Predicted.GetSafeNormal() * Config.PlanetRadius;
    }

    return Predicted;
}


void FChunkManager::Update(const FPlanetViewContext &Context)
{
    // 1. Prepare View & Traversal
    // We create a local copy of the context to populate the PredictedObserverLocation
    FPlanetViewContext LocalContext = Context;

    // Calculate distance to surface for LOD and generation checks
    float DistToSurface = Context.ObserverLocation.Size() - Config.PlanetRadius;
    bool bShouldGenerateChunks = DistToSurface < (Config.FarDistanceThreshold * FPlanetStatics::FarDistanceSafetyMargin);

    if (bShouldGenerateChunks)
    {
        LocalContext.PredictedObserverLocation = CalculatePredictedLocation(Context);

        // Update Quadtree with the refined context
        if (Quadtree)
            Quadtree->Update(LocalContext, [this](const FChunkId &Id) { return IsChunkReady(Id); });
    }

    // 2. Process the quadtree's desired output.
    //    If too far, pass an empty set — this will clean up all existing chunks gracefully.
    const TSet<FChunkId> EmptySet;
    const TSet<FChunkId> &DesiredLeaves = (Quadtree && bShouldGenerateChunks) ? Quadtree->GetDesiredLeaves() : EmptySet;

    ReconcileTransitions(DesiredLeaves);  // diff desired vs committed → build PendingTransitions
    AdvanceLoading();                     // ensure all needed chunks are generating/uploading
    CommitReadyTransitions();             // atomic show/hide for complete groups

    // 3. Async Dispatch
    if (ChunkGenerator)
        ChunkGenerator->Update();
}


// ---------------------------------------------------------------------------
// Phase A: Reconcile — diff DesiredLeaves vs CommittedLeaves
// ---------------------------------------------------------------------------
void FChunkManager::ReconcileTransitions(const TSet<FChunkId> &DesiredLeaves)
{
    // Clean up standalone tracking — remove anything already committed or no longer desired
    TArray<FChunkId> StaleStandalone;
    for (const FChunkId &Id : StandaloneNewChunks)
    {
        if (CommittedLeaves.Contains(Id) || !DesiredLeaves.Contains(Id))
            StaleStandalone.Add(Id);
    }

    for (const FChunkId &Id : StaleStandalone)
        StandaloneNewChunks.Remove(Id);

    // --- A1. Find nodes that are desired but not yet committed (new work) ---
    for (const FChunkId &Id : DesiredLeaves)
    {
        if (CommittedLeaves.Contains(Id))
            continue;  // Already committed — nothing to do

        if (!IsRootNode(Id))
        {
            const FChunkId ParentId = GetParentId(Id);

            if (CommittedLeaves.Contains(ParentId))
            {
                // Parent is committed and desired children appeared → this is a Split.
                // Register a transition if one doesn't already exist for this parent.
                if (!PendingTransitions.Contains(ParentId))
                {
                    FLODTransition T;
                    T.Type = ELeafTransitionType::Split;
                    T.Parent = ParentId;
                    T.Children = GetChildrenIds(ParentId);
                    PendingTransitions.Add(ParentId, MoveTemp(T));
                }
                continue;
            }
        }

        // No committed parent found — standalone new chunk (first entry, LOD 0 roots, etc.)
        // Track it so CommitReadyTransitions can show it once ready.
        StandaloneNewChunks.Add(Id);
    }

    // --- A2. Find nodes that were committed but are no longer desired (work to undo) ---
    TArray<FChunkId> Stale;
    for (const FChunkId &Id : CommittedLeaves)
    {
        if (DesiredLeaves.Contains(Id))
            continue;  // Still desired — nothing to do

        // CRITICAL: If this node is already the parent of a pending transition,
        // leave it alone. CommitReadyTransitions owns it now.
        if (PendingTransitions.Contains(Id))
            continue;

        // CRITICAL: If any pending transition references this node as a child,
        // leave it alone — it's part of a merge or split in progress.
        bool bInvolvedInTransition = false;
        for (const auto &TPair : PendingTransitions)
        {
            for (const FChunkId &ChildId : TPair.Value.Children)
            {
                if (ChildId == Id)
                {
                    bInvolvedInTransition = true;
                    break;
                }
            }
            if (bInvolvedInTransition)
                break;
        }
        if (bInvolvedInTransition)
            continue;

        // Check if this is a merge case: parent is desired, this child is committed
        if (!IsRootNode(Id))
        {
            const FChunkId ParentId = GetParentId(Id);
            if (DesiredLeaves.Contains(ParentId))
            {
                if (!PendingTransitions.Contains(ParentId))
                {
                    FLODTransition T;
                    T.Type = ELeafTransitionType::Merge;
                    T.Parent = ParentId;
                    T.Children = GetChildrenIds(ParentId);
                    PendingTransitions.Add(ParentId, MoveTemp(T));
                }
                continue;
            }
        }

        // Truly gone — no transition, not desired, not root merge candidate.
        // Release immediately (Step 4 will make this deferred).
        FChunk *Chunk = GetChunk(Id);
        if (Chunk)
        {
            if (ChunkGenerator)
                ChunkGenerator->CancelRequest(Id);
            if (Chunk->State == EChunkState::Visible || Chunk->State == EChunkState::MeshReady)
                Renderer->ReleaseChunk(Chunk);
            ChunkMap.Remove(Id);
        }
        Stale.Add(Id);
    }

    for (const FChunkId &Id : Stale)
        CommittedLeaves.Remove(Id);
}

// ---------------------------------------------------------------------------
// Phase B: AdvanceLoading — ensure all chunks referenced by transitions exist
//          and are progressing through the pipeline.
//          DataReady → MeshReady uploads happen here, before the commit check.
// ---------------------------------------------------------------------------
void FChunkManager::AdvanceLoading()
{
    // Collect all chunk IDs we need to keep alive and progressing.
    // This includes both sides of every pending transition, plus any desired leaf that has no transition (brand new regions).
    TSet<FChunkId> Required;

    for (auto &Pair : PendingTransitions)
    {
        const FLODTransition &T = Pair.Value;
        Required.Add(T.Parent);
        for (const FChunkId &ChildId : T.Children)
            Required.Add(ChildId);
    }

    if (Quadtree)
    {
        // Brand-new standalone chunks (no transition, no committed parent)
        for (const FChunkId &Id : StandaloneNewChunks)
            Required.Add(Id);

        for (const FChunkId &Id : Quadtree->GetPendingChildIds())
            Required.Add(Id);
    }

    // Advance each required chunk through its lifecycle
    for (const FChunkId &Id : Required)
    {
        FChunk *Chunk = GetChunk(Id);
        if (!Chunk)
            Chunk = CreateChunk(Id);

        switch (Chunk->State)
        {
            case EChunkState::None:
                Chunk->GenerationId++;
                Chunk->State = EChunkState::Pending;
                ChunkGenerator->RequestChunk(Id, Chunk->GenerationId);
                break;

            case EChunkState::DataReady:
                // Upload to GPU now, while we wait for the group to be complete.
                // This is the key difference from Step 1: upload happens eagerly, but Show is deferred to CommitReadyTransitions.
                Renderer->PrepareChunk(Chunk, Config.bEnableCollision);
                Chunk->State = EChunkState::MeshReady;
                break;

            case EChunkState::Pending:
            case EChunkState::Generating:
            case EChunkState::MeshReady:
            case EChunkState::Visible:
                break;  // Already progressing or ready
        }
    }
}

// ---------------------------------------------------------------------------
// Phase C: CommitReadyTransitions — atomic show/hide when a group is complete
// ---------------------------------------------------------------------------
void FChunkManager::CommitReadyTransitions()
{
    TArray<FChunkId> ToRemove;

    for (auto &Pair : PendingTransitions)
    {
        FLODTransition &T = Pair.Value;

        if (T.Type == ELeafTransitionType::Split)
        {
            // Gate: all 4 children must be MeshReady (or already Visible)
            bool bAllReady = true;
            for (const FChunkId &ChildId : T.Children)
            {
                if (!IsChunkReady(ChildId))
                {
                    bAllReady = false;
                    break;
                }
            }

            if (!bAllReady)
                continue;

            // Atomic swap: show all children, hide parent
            for (const FChunkId &ChildId : T.Children)
            {
                FChunk *Child = GetChunk(ChildId);
                if (Child)
                {
                    Renderer->ShowChunk(Child);
                    Child->State = EChunkState::Visible;
                    CommittedLeaves.Add(ChildId);
                }
            }

            FChunk *Parent = GetChunk(T.Parent);
            if (Parent && (Parent->State == EChunkState::Visible || Parent->State == EChunkState::MeshReady))
            {
                Renderer->HideChunk(Parent);
                Parent->State = EChunkState::MeshReady;  // Keep in pool, don't release yet
            }
            CommittedLeaves.Remove(T.Parent);

            ToRemove.Add(Pair.Key);
        }
        else  // Merge
        {
            // Gate: parent must be MeshReady
            if (!IsChunkReady(T.Parent))
                continue;

            // Atomic swap: show parent, hide all children
            FChunk *Parent = GetChunk(T.Parent);
            if (Parent)
            {
                Renderer->ShowChunk(Parent);
                Parent->State = EChunkState::Visible;
                CommittedLeaves.Add(T.Parent);
            }

            for (const FChunkId &ChildId : T.Children)
            {
                FChunk *Child = GetChunk(ChildId);
                if (Child && (Child->State == EChunkState::Visible || Child->State == EChunkState::MeshReady))
                {
                    Renderer->HideChunk(Child);
                    Child->State = EChunkState::MeshReady;
                    // Step 4 will add these to a deferred release queue.
                    // For now, release immediately.
                    Renderer->ReleaseChunk(Child);
                    ChunkMap.Remove(ChildId);
                }
                CommittedLeaves.Remove(ChildId);
            }

            ToRemove.Add(Pair.Key);
        }
    }

    // --- Commit standalone new chunks (no transition group, just show when ready) ---
    TArray<FChunkId> CommittedStandalone;
    for (const FChunkId &Id : StandaloneNewChunks)
    {
        if (IsChunkReady(Id))
        {
            FChunk *Chunk = GetChunk(Id);
            if (Chunk)
            {
                Renderer->ShowChunk(Chunk);
                Chunk->State = EChunkState::Visible;
                CommittedLeaves.Add(Id);
                CommittedStandalone.Add(Id);
            }
        }
    }

    for (const FChunkId &Id : CommittedStandalone)
        StandaloneNewChunks.Remove(Id);


    // --- Removal of pending transition groups ---
    for (const FChunkId &Id : ToRemove)
        PendingTransitions.Remove(Id);
}


// ---------------------------------------------------------------------------
// Pure math helpers
// ---------------------------------------------------------------------------
bool FChunkManager::IsRootNode(const FChunkId &Id) { return Id.LODLevel == 0; }


FChunkId FChunkManager::GetParentId(const FChunkId &Child)
{
    // Integer divide coords by 2, step up one LOD level.
    return FChunkId(Child.FaceIndex, FIntVector(Child.Coords.X / 2, Child.Coords.Y / 2, 0), Child.LODLevel - 1);
}


FChunkId FChunkManager::GetParentId_Safe(const FChunkId &Id)
{
    if (IsRootNode(Id))
        return Id;
    return GetParentId(Id);
}


TArray<FChunkId> FChunkManager::GetChildrenIds(const FChunkId &Parent)
{
    const int32 NextLOD = Parent.LODLevel + 1;
    const int32 X = Parent.Coords.X;
    const int32 Y = Parent.Coords.Y;
    const uint8 Face = Parent.FaceIndex;

    return {
        FChunkId(Face, FIntVector(X * 2, Y * 2, 0), NextLOD),
        FChunkId(Face, FIntVector(X * 2 + 1, Y * 2, 0), NextLOD),
        FChunkId(Face, FIntVector(X * 2, Y * 2 + 1, 0), NextLOD),
        FChunkId(Face, FIntVector(X * 2 + 1, Y * 2 + 1, 0), NextLOD),
    };
}


// ---------------------------------------------------------------------------
// Debug stuff
// ---------------------------------------------------------------------------
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
    Chunk->Transform = FMathUtils::ComputeChunkTransform(Id, Config.PlanetRadius);
    Chunk->State = EChunkState::DataReady;
}