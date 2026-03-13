#include "ChunkManager.h"
#include "DrawDebugHelpers.h"
#include "MathUtils.h"


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
    // CRITICAL: Stop the generator first.
    // This prevents new async tasks from being started and invalidates pending ones (if handled correctly).
    if (ChunkGenerator)
    {
        ChunkGenerator->Stop();
    }

    DeferredReleaseQueue.Empty();

    // Systematically clean up active chunks.
    if (Renderer && !GIsRequestingExit)
    {
        for (auto &Pair : ChunkMap)
        {
            FChunk *Chunk = Pair.Value.Get();

            if (UProceduralMeshComponent *Comp = Chunk->RenderProxy.Get())
            {
                // Hand the component back to the renderer for immediate safe disposal.
                // We do NOT use ReleaseChunk (which pools it) because we are shutting down.
                Renderer->DiscardComponent(Comp);
                Chunk->RenderProxy.Reset();
            }
        }

        // 3. Finally, destroy any components remaining in the pool.
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

    InitializeRoots();

    // DEBUG LOG
    UE_LOG(LogTemp, Log, TEXT("FChunkManager initialized."));
}


FChunk *FChunkManager::CreateChunk(const FChunkId &Id)
{
    if (TUniquePtr<FChunk> *Existing = ChunkMap.Find(Id))
        return Existing->Get();  // security to prevent overwriting an existing entry if called twice for the same ID

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
        const EChunkState S = Found->Get()->State;
        return (S == EChunkState::MeshReady || S == EChunkState::Visible) && !DeferredReleaseIds.Contains(Id);
    }

    return false;
}


void FChunkManager::Update(const FPlanetViewContext &Context)
{
    const float DistToSurface = Context.ObserverLocation.Size() - Config.PlanetRadius;
    const bool bShouldGenerateChunks = DistToSurface < (Config.FarDistanceThreshold * FPlanetStatics::FarDistanceSafetyMargin);

    LastObserverLocalPos = Context.ObserverLocation;

    if (bShouldGenerateChunks && Quadtree)
        Quadtree->Update(Context);

    const TSet<FChunkId> &DesiredLeaves = (bShouldGenerateChunks && Quadtree) ? Quadtree->GetDesiredLeaves() : TSet<FChunkId>();

    BuildLoadSet(DesiredLeaves);

    ReconcileTransitions(DesiredLeaves);
    AdvanceLoading();
    CommitReadyTransitions();
    ProcessDeferredReleases();
    PruneOrphans();

    if (ChunkGenerator)
        ChunkGenerator->Update();

    // DebugRootNodes();
}


void FChunkManager::BuildLoadSet(const TSet<FChunkId> &DesiredLeaves)
{
    LoadSet.Reset();

    // Everything currently rendered must stay alive
    for (const FChunkId &Id : RenderSet)
        LoadSet.Add(Id);

    // Both sides of every pending transition must stay alive
    for (const auto &Pair : PendingTransitions)
    {
        const FLODTransition &T = Pair.Value;
        LoadSet.Add(T.Parent);

        for (const FChunkId &ChildId : T.Children)
            LoadSet.Add(ChildId);
    }

    // Ensure desired roots are added to LoadSet so they generate.
    // Since they aren't in RenderSet yet, and no transition exists to spawn them (no parent),
    // we must explicitly request them here to kickstart the lifecycle.
    for (const FChunkId &Id : DesiredLeaves)
    {
        if (IsRootNode(Id) && !RenderSet.Contains(Id))
            LoadSet.Add(Id);
    }
}


void FChunkManager::PruneOrphans()
{
    TArray<FChunkId> ToRemove;

    for (const auto &Pair : ChunkMap)
    {
        const FChunkId &Id = Pair.Key;
        const FChunk *Chunk = Pair.Value.Get();

        if (LoadSet.Contains(Id))
            continue;  // Actively needed

        if (DeferredReleaseIds.Contains(Id))
            continue;  // Already on its way out

        // Only prune chunks that are not in flight
        if (Chunk->State == EChunkState::Pending || Chunk->State == EChunkState::Generating)
            continue;

        UE_LOG(LogTemp, Warning, TEXT("PruneOrphans: removing LOD:%d Face:%d State:%d"), Id.LODLevel, Id.FaceIndex, (int32)Chunk->State);

        ToRemove.Add(Id);
    }

    for (const FChunkId &Id : ToRemove)
    {
        FChunk *Chunk = GetChunk(Id);
        if (Chunk && Chunk->State == EChunkState::MeshReady)
            Renderer->ReleaseChunk(Chunk);
        ChunkMap.Remove(Id);
    }
}


void FChunkManager::InitializeRoots()
{
    for (uint8 Face = 0; Face < 6; ++Face)
    {
        FChunkId RootId(Face, FIntVector(0, 0, 0), 0);
        CreateChunk(RootId);
        // FIX: Do not add to RenderSet yet. They are not visible.
        // CommitReadyTransitions will promote them when they are MeshReady.
    }
}


void FChunkManager::ReconcileTransitions(const TSet<FChunkId> &DesiredLeaves)
{
    // --- A1. Desired but not rendered → find committed ancestor → register Split ---
    for (const FChunkId &Id : DesiredLeaves)
    {
        if (RenderSet.Contains(Id))
            continue;  // Already rendered, nothing to do

        if (!IsRootNode(Id) && PendingTransitions.Contains(GetParentId(Id)))
            continue;  // Parent already has a pending transition

        // Walk up to find the closest ancestor that is currently rendered
        FChunkId AncestorId = Id;
        while (!IsRootNode(AncestorId))
        {
            AncestorId = GetParentId(AncestorId);
            if (RenderSet.Contains(AncestorId))
            {
                // Only register if no conflicting transition exists
                if (!PendingTransitions.Contains(AncestorId))
                {
                    FLODTransition T;
                    T.Type = ELeafTransitionType::Split;
                    T.Parent = AncestorId;
                    T.Children = GetChildrenIds(AncestorId);
                    PendingTransitions.Add(AncestorId, MoveTemp(T));
                    UE_LOG(LogTemp, Log, TEXT("Split registered — parent LOD:%d Face:%d"), AncestorId.LODLevel, AncestorId.FaceIndex);
                }
                break;
            }
        }

        // Root node case: if a root is desired and not yet rendered,
        // it will be promoted by CommitReadyTransitions once its mesh is ready.
        // No transition needed — InitializeRoots already put it in RenderSet.
    }

    // --- A2. Rendered but not desired → find desired ancestor → register Merge ---
    TArray<FChunkId> ToUnrender;

    for (const FChunkId &Id : RenderSet)
    {
        if (DesiredLeaves.Contains(Id))
            continue;  // Still desired, nothing to do

        if (PendingTransitions.Contains(Id))
            continue;  // Already being replaced by a split from this node

        // Check if this node is already the child side of a pending transition
        bool bAlreadyHandled = false;
        for (const auto &Pair : PendingTransitions)
        {
            for (const FChunkId &ChildId : Pair.Value.Children)
            {
                if (ChildId == Id)
                {
                    bAlreadyHandled = true;
                    break;
                }
            }
            if (bAlreadyHandled)
                break;
        }
        if (bAlreadyHandled)
            continue;

        // Walk up to find the closest desired ancestor, including the root itself
        FChunkId AncestorId = Id;
        bool bFoundDesiredAncestor = false;

        while (true)
        {
            if (DesiredLeaves.Contains(AncestorId))
            {
                bFoundDesiredAncestor = true;
                if (!PendingTransitions.Contains(AncestorId))
                {
                    FLODTransition T;
                    T.Type = ELeafTransitionType::Merge;
                    T.Parent = AncestorId;
                    T.Children = GetChildrenIds(AncestorId);
                    PendingTransitions.Add(AncestorId, MoveTemp(T));
                    UE_LOG(LogTemp, Log, TEXT("Merge registered — parent LOD:%d Face:%d"), AncestorId.LODLevel, AncestorId.FaceIndex);
                }
                break;
            }

            if (IsRootNode(AncestorId))
            {
                // FIX: We reached the root and even the root is not desired.
                // This means the Far Model has taken over and we should unrender this branch.
                ToUnrender.Add(Id);
                break;  // No desired ancestor exists anywhere up the chain
            }

            AncestorId = GetParentId(AncestorId);
        }
    }

    // Process unrendering (Far Model overlap logic)
    for (const FChunkId &Id : ToUnrender)
    {
        FChunk *Chunk = GetChunk(Id);
        if (Chunk && (Chunk->State == EChunkState::Visible || Chunk->State == EChunkState::MeshReady))
        {
            Renderer->HideChunk(Chunk);
            Chunk->State = EChunkState::MeshReady;

            // Use deferred release to prevent thrashing at the hysteresis boundary
            DeferredReleaseQueue.Add({Id, Config.ChunkDemotionFrameDelay});
            DeferredReleaseIds.Add(Id);
        }
        RenderSet.Remove(Id);
    }

    // --- A3. Conflict resolution: cancel Split if Merge now exists for same region, and vice versa ---
    TArray<FChunkId> ToCancel;
    for (const auto &Pair : PendingTransitions)
    {
        const FLODTransition &T = Pair.Value;
        if (T.Type == ELeafTransitionType::Split)
        {
            // If none of the children are in DesiredLeaves anymore, this split is stale
            bool bAnyChildDesired = false;
            for (const FChunkId &ChildId : T.Children)
            {
                if (DesiredLeaves.Contains(ChildId))
                {
                    bAnyChildDesired = true;
                    break;
                }
            }
            if (!bAnyChildDesired)
                ToCancel.Add(Pair.Key);
        }
        else  // Merge
        {
            // If the parent is no longer desired, this merge is stale
            if (!DesiredLeaves.Contains(T.Parent))
                ToCancel.Add(Pair.Key);
        }
    }

    for (const FChunkId &Id : ToCancel)
    {
        UE_LOG(LogTemp, Log, TEXT("Transition cancelled — LOD:%d Face:%d"), Id.LODLevel, Id.FaceIndex);
        PendingTransitions.Remove(Id);
    }
}


void FChunkManager::AdvanceLoading()
{
    int32 MeshUploadsThisFrame = 0;

    // Advance each required chunk through its lifecycle
    for (const FChunkId &Id : LoadSet)
    {
        FChunk *Chunk = GetChunk(Id);
        if (!Chunk)
        {
            Chunk = CreateChunk(Id);
        }

        switch (Chunk->State)
        {
            case EChunkState::None:
            {
                UE_LOG(LogTemp, Warning, TEXT("AdvanceLoading: requesting LOD:%d Face:%d"), Id.LODLevel, Id.FaceIndex);
                Chunk->GenerationId++;
                Chunk->State = EChunkState::Pending;
                float DistSq = FVector::DistSquared(FMathUtils::GetChunkCenter(Id, Config.PlanetRadius), LastObserverLocalPos);
                ChunkGenerator->RequestChunk(Id, Chunk->GenerationId, DistSq);
                break;
            }

            case EChunkState::DataReady:
                if (MeshUploadsThisFrame < Config.MeshUpdatesPerFrame)
                {
                    Renderer->PrepareChunk(Chunk, Config.bEnableCollision);
                    Chunk->State = EChunkState::MeshReady;
                    MeshUploadsThisFrame++;
                }
                break;

            case EChunkState::Pending:
            case EChunkState::Generating:
            case EChunkState::MeshReady:
            case EChunkState::Visible:
                break;  // Already progressing or ready
        }
    }
}


void FChunkManager::CommitReadyTransitions()
{
    for (const auto &Pair : ChunkMap)
    {
        const FChunkId &Id = Pair.Key;
        const FChunk *Chunk = Pair.Value.Get();
        if (IsRootNode(Id) && !RenderSet.Contains(Id) && IsChunkReady(Id))
        {
            FChunk *Root = GetChunk(Id);
            Renderer->ShowChunk(Root);
            Root->State = EChunkState::Visible;
            RenderSet.Add(Id);
        }
    }

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
                    RenderSet.Add(ChildId);
                }
            }

            // Hide and defer parent
            if (!IsRootNode(T.Parent))
            {
                FChunk *Parent = GetChunk(T.Parent);
                if (Parent && (Parent->State == EChunkState::Visible || Parent->State == EChunkState::MeshReady))
                {
                    Renderer->HideChunk(Parent);
                    Parent->State = EChunkState::MeshReady;
                    DeferredReleaseQueue.Add({T.Parent, Config.ChunkDemotionFrameDelay});
                    DeferredReleaseIds.Add(T.Parent);
                }
                RenderSet.Remove(T.Parent);
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
                RenderSet.Add(T.Parent);
            }

            // Collect all committed descendants of T.Parent (depth-first from CommittedLeaves)
            TArray<FChunkId> ToCleanup;
            for (const FChunkId &RenderedId : RenderSet)
            {
                if (RenderedId == T.Parent)
                    continue;

                FChunkId AncestorId = RenderedId;
                while (!IsRootNode(AncestorId))
                {
                    AncestorId = GetParentId(AncestorId);
                    if (AncestorId == T.Parent)
                    {
                        ToCleanup.Add(RenderedId);
                        break;
                    }
                }
            }

            // Hide and defer-release every collected descendant
            for (const FChunkId &ChildId : ToCleanup)
            {
                FChunk *Child = GetChunk(ChildId);
                if (Child)
                {
                    if (Child->State == EChunkState::Visible || Child->State == EChunkState::MeshReady)
                    {
                        Renderer->HideChunk(Child);
                        Child->State = EChunkState::MeshReady;
                        DeferredReleaseQueue.Add({ChildId, Config.ChunkDemotionFrameDelay});
                        DeferredReleaseIds.Add(ChildId);
                    }
                }
                RenderSet.Remove(ChildId);
            }

            ToRemove.Add(Pair.Key);
        }
    }

    // Removal from pending transitions
    for (const FChunkId &Id : ToRemove)
        PendingTransitions.Remove(Id);
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

        // Chunk was re-committed before countdown expired — keep it, remove from deferred
        if (RenderSet.Contains(Entry.Id))
        {
            DeferredReleaseIds.Remove(Entry.Id);
            continue;
        }

        FChunk *Chunk = GetChunk(Entry.Id);
        if (Chunk && Chunk->State == EChunkState::MeshReady)
        {
            // Guard against GC'd render proxy on shutdown
            if (Chunk->RenderProxy.IsValid())
                Renderer->ReleaseChunk(Chunk);
            else
                Chunk->RenderProxy.Reset();

            DeferredReleaseIds.Remove(Entry.Id);
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
    UE_LOG(LogTemp, Warning, TEXT("OnGenerationComplete: LOD:%d Face:%d"), Id.LODLevel, Id.FaceIndex);

    FChunk *Chunk = GetChunk(Id);
    if (!Chunk)
        return;  // Chunk was unloaded while generating

    if (Chunk->GenerationId != GenId)
        return;  // Stale task (Chunk was reset/regenerated)

    // Only accept the result if the chunk is still in the generation pipeline.
    // MeshReady or Visible chunks must not be overwritten by a late callback.
    if (Chunk->State == EChunkState::MeshReady || Chunk->State == EChunkState::Visible)
        return;

    // Store Data
    Chunk->MeshData = MoveTemp(MeshData);
    Chunk->Transform = FMathUtils::ComputeChunkTransform(Id, Config.PlanetRadius);
    Chunk->State = EChunkState::DataReady;
}


void FChunkManager::DebugRootNodes()
{
    for (uint8 Face = 0; Face < 6; ++Face)
    {
        FChunkId RootId(Face, FIntVector(0, 0, 0), 0);

        const bool bInChunkMap = ChunkMap.Contains(RootId);
        const bool bInRenderSet = RenderSet.Contains(RootId);
        const bool bInLoadSet = LoadSet.Contains(RootId);
        const bool bInDeferred = DeferredReleaseIds.Contains(RootId);
        const bool bInTransition = PendingTransitions.Contains(RootId);
        const bool bInDesiredLeaves = Quadtree && Quadtree->GetDesiredLeaves().Contains(RootId);

        EChunkState State = EChunkState::None;
        if (bInChunkMap)
            State = ChunkMap[RootId]->State;

        UE_LOG(LogTemp,
               Warning,
               TEXT("ROOT Face:%d | State:%d | ChunkMap:%d RenderSet:%d LoadSet:%d Deferred:%d Transition:%d Desired:%d"),
               Face,
               (int32)State,
               bInChunkMap,
               bInRenderSet,
               bInLoadSet,
               bInDeferred,
               bInTransition,
               bInDesiredLeaves);
    }
}