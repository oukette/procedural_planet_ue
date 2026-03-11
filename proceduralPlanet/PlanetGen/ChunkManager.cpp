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
    SeedSentinels();

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
        // Update Quadtree with the refined context
        if (Quadtree)
            Quadtree->Update(LocalContext, [this](const FChunkId &Id) { return IsChunkReady(Id); });
    }

    // 2. Process the quadtree's desired output.
    //    If too far, pass an empty set — this will clean up all existing chunks gracefully.
    const TSet<FChunkId> EmptySet;
    const TSet<FChunkId> &DesiredLeaves = (Quadtree && bShouldGenerateChunks) ? Quadtree->GetDesiredLeaves() : EmptySet;

    ReconcileTransitions(DesiredLeaves);
    AdvanceLoading();
    CommitReadyTransitions();
    ProcessDeferredReleases();

    // 3. Async Dispatch
    if (ChunkGenerator)
        ChunkGenerator->Update();
}


// ---------------------------------------------------------------------------
// Phase A: Reconcile — diff DesiredLeaves vs CommittedLeaves
// ---------------------------------------------------------------------------
void FChunkManager::ReconcileTransitions(const TSet<FChunkId> &DesiredLeaves)
{
    // Temporary diagnostic — remove after confirmation
    for (const FChunkId &Id : CommittedLeaves)
    {
        if (IsSentinelId(Id))
            continue;
        if (!DesiredLeaves.Contains(Id))
        {
            UE_LOG(LogTemp, Warning, TEXT("CommittedLeaf not in DesiredLeaves — LOD:%d Face:%d"), Id.LODLevel, Id.FaceIndex);
        }
    }

    // Rebuild flat child lookup to replace the O(N*M) scan below.
    // Cost: O(T*4) once per frame instead of O(committed * T * 4).
    PendingTransitionChildren.Reset();
    for (const auto &Pair : PendingTransitions)
        for (const FChunkId &ChildId : Pair.Value.Children)
            PendingTransitionChildren.Add(ChildId);

    // --- A1. Find nodes that are desired but not yet committed (new work) ---
    for (const FChunkId &Id : DesiredLeaves)
    {
        if (CommittedLeaves.Contains(Id))
            continue;  // Already committed — nothing to do

        const FChunkId ParentId = GetParentId_OrSentinel(Id);
        if (CommittedLeaves.Contains(ParentId))
        {
            if (!PendingTransitions.Contains(ParentId))
            {
                FLODTransition T;
                T.Type = ELeafTransitionType::Split;
                T.Parent = ParentId;
                // For sentinel parents, children are the 6 roots — but we only register per-face, so T.Children holds just this root chunk.
                // GetChildrenIds handles normal parents; roots need special handling:
                T.Children = IsSentinelId(ParentId) ? TArray<FChunkId>{Id} : GetChildrenIds(ParentId);
                PendingTransitions.Add(ParentId, MoveTemp(T));
            }
            continue;
        }
        // Parent not yet committed — nothing to do this frame, quadtree
        // will keep emitting this ID until the parent's transition commits.
    }

    // --- A2. Find nodes that were committed but are no longer desired (work to undo) ---
    TArray<FChunkId> Stale;
    for (const FChunkId &Id : CommittedLeaves)
    {
        if (IsSentinelId(Id))
            continue;  // Sentinels are permanent — never stale

        if (DesiredLeaves.Contains(Id))
            continue;  // Still desired — nothing to do

        // If this node is already the parent of a pending transition, leave it alone.
        if (PendingTransitions.Contains(Id))
            continue;

        if (PendingTransitionChildren.Contains(Id))
            continue;

        // Check if this is a merge case: parent is desired, this child is committed
        if (!IsRootNode(Id))
        {
            FChunkId AncestorId = GetParentId(Id);
            while (true)
            {
                if (DesiredLeaves.Contains(AncestorId))
                {
                    // Found a desired ancestor — register merge at that level if needed
                    if (!PendingTransitions.Contains(AncestorId))
                    {
                        FLODTransition T;
                        T.Type = ELeafTransitionType::Merge;
                        T.Parent = AncestorId;
                        T.Children = GetChildrenIds(AncestorId);
                        PendingTransitions.Add(AncestorId, MoveTemp(T));
                    }
                    break;
                }
                if (IsRootNode(AncestorId))
                    break;
                AncestorId = GetParentId(AncestorId);
            }
            continue;
        }

        // Truly gone — no transition, not desired, not root merge candidate.
        FChunk *Chunk = GetChunk(Id);
        if (Chunk)
        {
            if (ChunkGenerator)
                ChunkGenerator->CancelRequest(Id);

            UE_LOG(LogTemp, Warning, TEXT("Truly gone — LOD:%d State:%d"), Id.LODLevel, (int32)Chunk->State);
            if (IsRootNode(Id))
            {
                if (Chunk->State == EChunkState::Visible || Chunk->State == EChunkState::MeshReady)
                    Renderer->HideChunk(Chunk);
                Chunk->State = EChunkState::MeshReady;
                Stale.Add(Id);  // still remove from CommittedLeaves
                continue;       // do NOT ChunkMap.Remove
            }

            if (Chunk->State == EChunkState::Visible)
            {
                // Quadtree oscillation — chunk is still rendering, do not destroy.
                // It will be picked up correctly next frame.
                Renderer->HideChunk(Chunk);
                Chunk->State = EChunkState::MeshReady;
                Stale.Add(Id);
                continue;  // do NOT ChunkMap.Remove
            }
            if (Chunk->State == EChunkState::MeshReady)
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
        for (const FChunkId &Id : Quadtree->GetPendingChildIds())
            Required.Add(Id);
    }

    // Advance each required chunk through its lifecycle
    for (const FChunkId &Id : Required)
    {
        if (IsSentinelId(Id))
            continue;

        FChunk *Chunk = GetChunk(Id);
        if (!Chunk)
        {
            UE_LOG(LogTemp, Warning, TEXT("AdvanceLoading creating chunk — LOD:%d"), Id.LODLevel);
            Chunk = CreateChunk(Id);
        }

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

            if (!IsSentinelId(T.Parent))
            {
                FChunk *Parent = GetChunk(T.Parent);
                if (Parent && (Parent->State == EChunkState::Visible || Parent->State == EChunkState::MeshReady))
                {
                    Renderer->HideChunk(Parent);
                    Parent->State = EChunkState::MeshReady;
                }
                CommittedLeaves.Remove(T.Parent);
            }

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
                    DeferredReleaseQueue.Add({ChildId, Config.ChunkDemotionFrameDelay});
                }
                CommittedLeaves.Remove(ChildId);
            }

            ToRemove.Add(Pair.Key);
        }
    }

    // --- Removal of pending transition groups ---
    for (const FChunkId &Id : ToRemove)
    {
        // Clean up children from the fast lookup set
        if (const FLODTransition *T = PendingTransitions.Find(Id))
            for (const FChunkId &ChildId : T->Children)
                PendingTransitionChildren.Remove(ChildId);

        PendingTransitions.Remove(Id);
    }
}


void FChunkManager::ProcessDeferredReleases()
{
    TArray<FDeferredRelease> StillWaiting;

    for (FDeferredRelease &Entry : DeferredReleaseQueue)
    {
        Entry.FrameCountdown--;

        if (Entry.FrameCountdown > 0)
        {
            StillWaiting.Add(Entry);
            continue;
        }

        // Countdown reached zero — release if not re-committed in the meantime
        if (CommittedLeaves.Contains(Entry.Id))
        {
            // Chunk was re-committed (e.g. merge was cancelled) — do not release
            continue;
        }

        FChunk *Chunk = GetChunk(Entry.Id);
        if (Chunk && Chunk->State == EChunkState::MeshReady)
        {
            Renderer->ReleaseChunk(Chunk);
            ChunkMap.Remove(Entry.Id);
        }
    }

    DeferredReleaseQueue = MoveTemp(StillWaiting);
}


// ---------------------------------------------------------------------------
// Pure math helpers
// ---------------------------------------------------------------------------
FChunkId FChunkManager::GetParentId(const FChunkId &Child)
{
    // Integer divide coords by 2, step up one LOD level.
    return FChunkId(Child.FaceIndex, FIntVector(Child.Coords.X / 2, Child.Coords.Y / 2, 0), Child.LODLevel - 1);
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


bool FChunkManager::IsRootNode(const FChunkId &Id) { return Id.LODLevel == 0; }


FChunkId FChunkManager::GetParentId_OrSentinel(const FChunkId &Id) const
{
    if (IsRootNode(Id))
        return MakeSentinelId(Id.FaceIndex);
    return GetParentId(Id);
}


void FChunkManager::SeedSentinels()
{
    for (uint8 Face = 0; Face < 6; ++Face)
    {
        CommittedLeaves.Add(MakeSentinelId(Face));
    }
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

    // // Only accept the result if the chunk is still in the generation pipeline.
    // // MeshReady or Visible chunks must not be overwritten by a late callback.
    if (Chunk->State == EChunkState::MeshReady || Chunk->State == EChunkState::Visible)
        return;

    UE_LOG(LogTemp,
           Warning,
           TEXT("OnGenerationComplete — LOD:%d State:%d RenderProxy:%d"),
           Id.LODLevel,
           (int32)Chunk->State,
           Chunk->RenderProxy.IsValid() ? 1 : 0);

    // Store Data
    Chunk->MeshData = MoveTemp(MeshData);
    Chunk->Transform = FMathUtils::ComputeChunkTransform(Id, Config.PlanetRadius);
    Chunk->State = EChunkState::DataReady;
}