#include "ChunkManager.h"
#include "../Utils/MathUtils.h"
#include "PlanetConfig.h"
#include "PlanetConstants.h"

#include "Engine/World.h"
#include "DrawDebugHelpers.h"


ChunkManager::ChunkManager(const FPlanetConfig &planetConfig, const DensityGenerator *densityGen, TSharedPtr<INoise, ESPMode::ThreadSafe> noiseProvider) :
    m_planetConfig(planetConfig),
    m_densityGen(densityGen),
    m_noiseProvider(noiseProvider)
{
    m_chunkGenerator = MakeUnique<ChunkGenerator>(m_planetConfig, m_densityGen, m_noiseProvider);

    // DEBUG LOG
    UE_LOG(LogTemp, Warning, TEXT("ChunkManager created."));
    UE_LOG(LogTemp, Warning, TEXT("Collision is globally %s"), m_planetConfig.bEnableCollision ? TEXT("enabled") : TEXT("disabled"));
}


ChunkManager::~ChunkManager()
{
    // CRITICAL: Stop the generator first.
    // This prevents new async tasks from being started and invalidates pending ones (if handled correctly).
    if (m_chunkGenerator)
    {
        m_chunkGenerator->Stop();
    }

    m_deferredReleaseQueue.Empty();

    // Systematically clean up active chunks.
    if (m_chunkRenderer && !GIsRequestingExit)
    {
        for (auto &Pair : m_chunksMap)
        {
            Chunk *Chunk = Pair.Value.Get();

            if (UProceduralMeshComponent *Comp = Chunk->m_renderProxy.Get())
            {
                // Hand the component back to the renderer for immediate safe disposal.
                // We do NOT use ReleaseChunk (which pools it) because we are shutting down.
                m_chunkRenderer->DiscardComponent(Comp);
                Chunk->m_renderProxy.Reset();
            }
        }

        // 3. Finally, destroy any components remaining in the pool.
        m_chunkRenderer->ReleaseAllComponents();
    }
}


int32 ChunkManager::GetTotalChunkCount() const { return m_chunksMap.Num(); }


int32 ChunkManager::GetVisibleChunkCount() const { return m_renderSet.Num(); }


void ChunkManager::GetVisibleCountPerLOD(TArray<int32> &OutCounts) const
{
    for (const auto &Pair : m_chunksMap)
    {
        const Chunk *Chunk = Pair.Value.Get();
        if (Chunk->m_state == ChunkState::Visible)
        {
            const int32 LOD = Chunk->m_ID.LODLevel;
            if (OutCounts.IsValidIndex(LOD))
                OutCounts[LOD]++;
        }
    }
}


int32 ChunkManager::GetPendingCount() const { return m_chunkGenerator ? m_chunkGenerator->GetPendingCount() : 0; }


void ChunkManager::Initialize(AActor *Owner, UMaterialInterface *Material)
{
    m_chunkRenderer = MakeUnique<ChunkRenderer>(Owner, Material);

    // m_chunkGenerator = MakeUnique<ChunkGenerator>(m_planetConfig, m_densityGen);
    m_chunkGenerator->SetOnChunkGeneratedCallback([this](const ChunkId &Id, uint32 GenId, TUniquePtr<ChunkMeshData> MeshData)
                                                  { OnGenerationComplete(Id, GenId, MoveTemp(MeshData)); });

    m_quadtree = MakeUnique<PlanetQuadtree>(m_planetConfig);

    InitializeRoots();

    // DEBUG LOG
    UE_LOG(LogTemp, Log, TEXT("ChunkManager initialized."));
}


Chunk *ChunkManager::CreateChunk(const ChunkId &Id)
{
    if (TUniquePtr<Chunk> *Existing = m_chunksMap.Find(Id))
        return Existing->Get();  // security to prevent overwriting an existing entry if called twice for the same ID

    TUniquePtr<Chunk> NewChunk = MakeUnique<Chunk>(Id);
    Chunk *Ptr = NewChunk.Get();
    m_chunksMap.Add(Id, MoveTemp(NewChunk));
    return Ptr;
}


Chunk *ChunkManager::GetChunk(const ChunkId &Id)
{
    // If it exists, return it
    if (TUniquePtr<Chunk> *Found = m_chunksMap.Find(Id))
        return Found->Get();

    return nullptr;
}


int32 ChunkManager::GetDeferredReleaseDelay() const
{
    const int32 Total = m_chunksMap.Num();
    if (Total <= m_planetConfig.CacheSoftCap)
        return m_planetConfig.DeferredReleaseDelay;

    float Pressure =
        FMath::Clamp((float)(Total - m_planetConfig.CacheSoftCap) / (float)FMath::Max(m_planetConfig.CacheHardCap - m_planetConfig.CacheSoftCap, 1), 0.f, 1.f);

    return FMath::RoundToInt(FMath::Lerp((float)m_planetConfig.DeferredReleaseDelay, (float)m_planetConfig.DeferredReleaseDelayMin, Pressure));
}


bool ChunkManager::IsChunkReady(const ChunkId &Id) const
{
    if (const TUniquePtr<Chunk> *Found = m_chunksMap.Find(Id))
    {
        const ChunkState S = Found->Get()->m_state;
        return (S == ChunkState::MeshReady || S == ChunkState::Visible) && !m_deferredReleaseIdsMap.Contains(Id);
    }

    return false;
}


void ChunkManager::DeferHideChunk(Chunk *Chunk, const ChunkId &Id)
{
    check(Chunk != nullptr);  // replaces a classic if nullptr

    const bool bWasNeverRendered = !m_renderSet.Contains(Id);
    m_chunkRenderer->HideChunk(Chunk);
    Chunk->m_state = ChunkState::MeshReady;

    const int32 Delay = bWasNeverRendered ? 1 : GetDeferredReleaseDelay();
    m_deferredReleaseQueue.Add({Id, Delay});
    m_deferredReleaseIdsMap.Add(Id);
}


void ChunkManager::Update(const PlanetViewContext &Context)
{
    const float DistToSurface = Context.ObserverLocation.Size() - m_planetConfig.PlanetRadius;
    const bool bShouldGenerateChunks = DistToSurface < (m_planetConfig.FarDistanceThreshold * PlanetStatics::FarDistanceSafetyMargin);

    m_lastObserverLocalPos = Context.ObserverLocation;
    m_lastObserverVelocity = Context.ObserverVelocity;
    m_lastObserverForward = Context.ObserverForward;

    // === TEMP DIAGNOSTIC — remove before shipping ===
    {
        TMap<ChunkState, int32> StateCounts;
        for (const auto &Pair : m_chunksMap)
            StateCounts.FindOrAdd(Pair.Value->m_state)++;

        UE_LOG(LogTemp,
               Warning,
               TEXT("ChunkMap:%d | None:%d Pending:%d Generating:%d DataReady:%d MeshReady:%d Visible:%d | Deferred:%d LoadSet:%d RenderSet:%d Transitions:%d"),
               m_chunksMap.Num(),
               StateCounts.FindRef(ChunkState::None),
               StateCounts.FindRef(ChunkState::Pending),
               StateCounts.FindRef(ChunkState::Generating),
               StateCounts.FindRef(ChunkState::DataReady),
               StateCounts.FindRef(ChunkState::MeshReady),
               StateCounts.FindRef(ChunkState::Visible),
               m_deferredReleaseQueue.Num(),
               m_loadSet.Num(),
               m_renderSet.Num(),
               m_pendingTransitionsMap.Num());
    }
    // === END DIAGNOSTIC ===

    if (bShouldGenerateChunks && m_quadtree)
        m_quadtree->Update(Context);

    const TSet<ChunkId> &DesiredLeaves = (bShouldGenerateChunks && m_quadtree) ? m_quadtree->GetDesiredLeaves() : TSet<ChunkId>();

    // Build distance cache once — reused by AdvanceLoading and CommitReadyTransitions
    TMap<ChunkId, float> DistanceSqCache;
    DistanceSqCache.Reserve(m_chunksMap.Num());
    for (const auto &Pair : m_chunksMap)
    {
        const ChunkId &Id = Pair.Key;
        DistanceSqCache.Add(Id, FVector::DistSquared(FMathUtils::GetChunkCenter(Id, m_planetConfig.PlanetRadius), m_lastObserverLocalPos));
    }

    BuildLoadSet(DesiredLeaves, bShouldGenerateChunks);
    ReconcileTransitions(DesiredLeaves);
    AdvanceLoading(DistanceSqCache);
    CommitReadyTransitions(bShouldGenerateChunks, DistanceSqCache);
    ProcessDeferredReleases();
    PruneOrphans();

    if (m_chunkGenerator)
        m_chunkGenerator->Update();

    // DebugRootNodes();
}


void ChunkManager::BuildLoadSet(const TSet<ChunkId> &DesiredLeaves, const bool bShouldGenerateChunks)
{
    m_loadSet.Reset();

    // Everything currently rendered must stay alive
    for (const ChunkId &Id : m_renderSet)
        m_loadSet.Add(Id);

    // Both sides of every pending transition must stay alive
    for (const auto &Pair : m_pendingTransitionsMap)
    {
        const LODTransition &T = Pair.Value;
        m_loadSet.Add(T.Parent);

        for (const ChunkId &ChildId : T.Children)
            m_loadSet.Add(ChildId);
    }

    // Bootstrap: keep unrendered roots in m_loadSet so they can be generated. only do this while NO descendant of that root face is already
    // rendered. Once a split has committed, the root has served its bootstrap purpose and must be left to the normal deferred-release lifecycle.
    if (bShouldGenerateChunks)
    {
        for (uint8 Face = 0; Face < 6; ++Face)
        {
            ChunkId RootId(Face, FIntVector(0, 0, 0), 0);

            if (m_renderSet.Contains(RootId))
                continue;  // Already rendered, normal lifecycle handles it

            // Check whether any rendered chunk belongs to this face
            bool bFaceAlreadyCovered = false;
            for (const ChunkId &RenderedId : m_renderSet)
            {
                if (RenderedId.FaceIndex == Face)
                {
                    bFaceAlreadyCovered = true;
                    break;
                }
            }

            if (!bFaceAlreadyCovered)
                m_loadSet.Add(RootId);
        }
    }
}


void ChunkManager::InitializeRoots()
{
    for (uint8 Face = 0; Face < 6; ++Face)
    {
        ChunkId RootId(Face, FIntVector(0, 0, 0), 0);
        CreateChunk(RootId);
        // FIX: Do not add to m_renderSet yet. They are not visible.
        // CommitReadyTransitions will promote them when they are MeshReady.
    }
}


void ChunkManager::ReconcileTransitions(const TSet<ChunkId> &DesiredLeaves)
{
    // --- A0. Age all transitions — force-cancel ones that have been pending too long ---
    {
        TArray<ChunkId> ToCancel;
        for (auto &Pair : m_pendingTransitionsMap)
        {
            LODTransition &T = Pair.Value;
            T.FrameAge++;

            if (T.FrameAge >= m_planetConfig.TransitionMaxAge)
            {
                UE_LOG(
                    LogTemp, Log, TEXT("Stale transition force-cancelled — LOD:%d Face:%d Age:%d frames"), Pair.Key.LODLevel, Pair.Key.FaceIndex, T.FrameAge);
                ToCancel.Add(Pair.Key);
            }
        }

        for (const ChunkId &Id : ToCancel)
        {
            const LODTransition &T = m_pendingTransitionsMap[Id];
            for (const ChunkId &ChildId : T.Children)
            {
                m_pendingChildSet.Remove(ChildId);

                // Cancel any in-flight generation for stale children
                if (Chunk *Child = GetChunk(ChildId))
                {
                    if (Child->m_state == ChunkState::Pending || Child->m_state == ChunkState::Generating)
                    {
                        m_chunkGenerator->CancelRequest(ChildId);
                        Child->m_generationId++;
                        Child->m_state = ChunkState::None;
                    }
                }
            }

            m_pendingTransitionsMap.Remove(Id);
        }
    }

    // --- A1. Desired but not rendered → find committed ancestor → register Split ---
    for (const ChunkId &Id : DesiredLeaves)
    {
        if (m_renderSet.Contains(Id))
            continue;  // Already rendered, nothing to do

        if (!IsRootNode(Id) && m_pendingTransitionsMap.Contains(GetParentId(Id)))
            continue;  // Parent already has a pending transition

        // Walk up to find the closest ancestor that is currently rendered
        ChunkId AncestorId = Id;
        while (!IsRootNode(AncestorId))
        {
            AncestorId = GetParentId(AncestorId);
            if (m_renderSet.Contains(AncestorId))
            {
                // Only register if no conflicting transition exists
                if (!m_pendingTransitionsMap.Contains(AncestorId))
                {
                    LODTransition T;
                    T.Type = LeafTransitionType::Split;
                    T.Parent = AncestorId;
                    T.Children = GetChildrenIds(AncestorId);
                    for (const ChunkId &ChildId : T.Children)
                        m_pendingChildSet.Add(ChildId);

                    m_pendingTransitionsMap.Add(AncestorId, MoveTemp(T));
                    // UE_LOG(LogTemp, Log, TEXT("Split registered — parent LOD:%d Face:%d"), AncestorId.LODLevel, AncestorId.FaceIndex);
                }
                break;
            }
        }
    }

    // --- A2. Rendered but not desired → find desired ancestor → register Merge ---
    TArray<ChunkId> ToUnrender;

    for (const ChunkId &Id : m_renderSet)
    {
        if (DesiredLeaves.Contains(Id))
            continue;  // Still desired, nothing to do

        if (m_pendingTransitionsMap.Contains(Id))
            continue;  // Already being replaced by a split from this node

        // Check if this node is already the child side of a pending transition
        if (m_pendingChildSet.Contains(Id))
            continue;

        // Walk up to find the closest desired ancestor, including the root itself
        ChunkId AncestorId = Id;
        bool bFoundDesiredAncestor = false;

        while (true)
        {
            if (DesiredLeaves.Contains(AncestorId))
            {
                bFoundDesiredAncestor = true;
                if (!m_pendingTransitionsMap.Contains(AncestorId))
                {
                    LODTransition T;
                    T.Type = LeafTransitionType::Merge;
                    T.Parent = AncestorId;
                    T.Children = GetChildrenIds(AncestorId);
                    for (const ChunkId &ChildId : T.Children)
                        m_pendingChildSet.Add(ChildId);

                    m_pendingTransitionsMap.Add(AncestorId, MoveTemp(T));
                    // UE_LOG(LogTemp, Log, TEXT("Merge registered — parent LOD:%d Face:%d"), AncestorId.LODLevel, AncestorId.FaceIndex);
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
    for (const ChunkId &Id : ToUnrender)
    {
        Chunk *Chunk = GetChunk(Id);
        if (Chunk && (Chunk->m_state == ChunkState::Visible || Chunk->m_state == ChunkState::MeshReady))
        {
            DeferHideChunk(Chunk, Id);
        }
        m_renderSet.Remove(Id);
    }

    // --- A3. Conflict resolution: cancel Split if Merge now exists for same region, and vice versa ---
    TArray<ChunkId> ToCancel;
    for (const auto &Pair : m_pendingTransitionsMap)
    {
        const LODTransition &T = Pair.Value;
        if (T.Type == LeafTransitionType::Split)
        {
            // Cancel only if the desired leaves have moved back UP the tree — i.e. the parent itself or an ancestor is now desired (observer moved away).
            // Do NOT cancel just because children aren't direct leaves — they may themselves need to split further, meaning deeper descendants are desired.
            bool bParentOrAncestorDesired = false;
            ChunkId WalkId = T.Parent;
            while (true)
            {
                if (DesiredLeaves.Contains(WalkId))
                {
                    bParentOrAncestorDesired = true;
                    break;
                }
                if (IsRootNode(WalkId))
                    break;
                WalkId = GetParentId(WalkId);
            }

            // Also check: is any desired leaf a descendant of the transition parent?
            bool bAnyDescendantDesired = false;
            for (const ChunkId &LeafId : DesiredLeaves)
            {
                // Walk up from each desired leaf — if we hit T.Parent, it's a descendant
                ChunkId AncestorWalk = LeafId;
                while (!IsRootNode(AncestorWalk))
                {
                    AncestorWalk = GetParentId(AncestorWalk);
                    if (AncestorWalk == T.Parent)
                    {
                        bAnyDescendantDesired = true;
                        break;
                    }
                }
                if (bAnyDescendantDesired)
                    break;
            }

            // Cancel only if neither the parent's ancestor nor any descendant is desired
            if (!bParentOrAncestorDesired && !bAnyDescendantDesired)
                ToCancel.Add(Pair.Key);
        }
        else  // Merge
        {
            // If the parent is no longer desired, this merge is stale
            if (!DesiredLeaves.Contains(T.Parent))
                ToCancel.Add(Pair.Key);
        }
    }

    for (const ChunkId &Id : ToCancel)
    {
        // UE_LOG(LogTemp, Log, TEXT("Transition cancelled — LOD:%d Face:%d"), Id.LODLevel, Id.FaceIndex);
        const LODTransition &T = m_pendingTransitionsMap[Id];
        for (const ChunkId &ChildId : T.Children)
            m_pendingChildSet.Remove(ChildId);

        m_pendingTransitionsMap.Remove(Id);
    }
}


void ChunkManager::AdvanceLoading(const TMap<ChunkId, float> &DistanceSqCache)
{
    // Cancel generation for any Pending/Generating chunk no longer needed
    // This is the primary fix for cache growth at high speed
    for (auto &Pair : m_chunksMap)
    {
        const ChunkId &Id = Pair.Key;
        Chunk *Chunk = Pair.Value.Get();

        if (!m_loadSet.Contains(Id) && (Chunk->m_state == ChunkState::Pending || Chunk->m_state == ChunkState::Generating))
        {
            m_chunkGenerator->CancelRequest(Id);
            Chunk->m_generationId++;  // invalidate any in-flight task for this chunk
            Chunk->m_state = ChunkState::None;
            // Now PruneOrphans can collect it this frame
        }
    }

    int32 MeshUploadsThisFrame = 0;

    // First pass: request generation for all None-state chunks (order doesn't matter, priority is handled inside m_chunkGenerator's heap)
    for (const ChunkId &Id : m_loadSet)
    {
        Chunk *Chunk = GetChunk(Id);
        if (!Chunk)
            Chunk = CreateChunk(Id);

        if (Chunk->m_state == ChunkState::None)
        {
            Chunk->m_generationId++;
            Chunk->m_state = ChunkState::Pending;
            float DistSq = DistanceSqCache.Contains(Id)
                               ? DistanceSqCache[Id]
                               : FVector::DistSquared(FMathUtils::GetChunkCenter(Id, m_planetConfig.PlanetRadius), m_lastObserverLocalPos);
            m_chunkGenerator->RequestChunk(Id, Chunk->m_generationId, DistSq);
        }
    }

    // Second pass: upload meshes in distance order — closest chunk gets GPU memory first
    TArray<ChunkId> DataReadyChunks;
    for (const ChunkId &Id : m_loadSet)
    {
        Chunk *Chunk = GetChunk(Id);
        if (Chunk && Chunk->m_state == ChunkState::DataReady)
            DataReadyChunks.Add(Id);
    }

    DataReadyChunks.Sort(
        [&DistanceSqCache, this](const ChunkId &A, const ChunkId &B)
        {
            const float DistA = DistanceSqCache.Contains(A) ? DistanceSqCache[A] : 0.f;
            const float DistB = DistanceSqCache.Contains(B) ? DistanceSqCache[B] : 0.f;
            return DistA < DistB;
        });


    for (const ChunkId &Id : DataReadyChunks)
    {
        if (MeshUploadsThisFrame >= m_planetConfig.MeshUpdatesPerFrame)
            break;

        Chunk *Chunk = GetChunk(Id);
        if (Chunk)
        {
            m_chunkRenderer->PrepareChunk(Chunk, m_planetConfig.bEnableCollision);
            Chunk->m_state = ChunkState::MeshReady;
            MeshUploadsThisFrame++;
        }
    }
}


void ChunkManager::CommitReadyTransitions(const bool bShouldGenerateChunks, const TMap<ChunkId, float> &DistanceSqCache)
{
    // Promote root chunks first. Only bootstrap-promote roots when L0 chunks are supposed to be visible.
    if (bShouldGenerateChunks)
    {
        for (const auto &Pair : m_chunksMap)
        {
            const ChunkId &Id = Pair.Key;
            if (IsRootNode(Id) && !m_renderSet.Contains(Id) && IsChunkReady(Id))
            {
                Chunk *Root = GetChunk(Id);
                m_chunkRenderer->ShowChunk(Root);
                Root->m_state = ChunkState::Visible;
                m_renderSet.Add(Id);
            }
        }
    }

    // Collect and sort pending transitions by distance — closest commits first
    TArray<ChunkId> SortedTransitionKeys;
    for (const auto &Pair : m_pendingTransitionsMap)
        SortedTransitionKeys.Add(Pair.Key);

    SortedTransitionKeys.Sort(
        [&DistanceSqCache, this](const ChunkId &A, const ChunkId &B)
        {
            const float DistA = DistanceSqCache.Contains(A) ? DistanceSqCache[A] : 0.f;
            const float DistB = DistanceSqCache.Contains(B) ? DistanceSqCache[B] : 0.f;
            return DistA < DistB;
        });

    // Update pending transitions (split and merge) and mark unwanted ones for removal
    TArray<ChunkId> ToRemove;
    for (const ChunkId &Key : SortedTransitionKeys)
    {
        LODTransition &T = m_pendingTransitionsMap[Key];

        if (T.Type == LeafTransitionType::Split)
        {
            // Gate: all 4 children must be MeshReady (or already Visible)
            bool bAllReady = true;
            for (const ChunkId &ChildId : T.Children)
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
            for (const ChunkId &ChildId : T.Children)
            {
                Chunk *Child = GetChunk(ChildId);
                if (Child)
                {
                    m_chunkRenderer->ShowChunk(Child);
                    Child->m_state = ChunkState::Visible;
                    m_renderSet.Add(ChildId);
                }
            }

            // Hide and defer parent
            if (!IsRootNode(T.Parent))
            {
                Chunk *Parent = GetChunk(T.Parent);
                if (Parent && (Parent->m_state == ChunkState::Visible || Parent->m_state == ChunkState::MeshReady))
                {
                    DeferHideChunk(Parent, T.Parent);
                }
                m_renderSet.Remove(T.Parent);
            }

            ToRemove.Add(Key);
        }
        else  // Merge
        {
            // Gate: parent must be MeshReady
            if (!IsChunkReady(T.Parent))
                continue;

            // Atomic swap: show parent, hide all children
            Chunk *Parent = GetChunk(T.Parent);
            if (Parent)
            {
                m_chunkRenderer->ShowChunk(Parent);
                Parent->m_state = ChunkState::Visible;
                m_renderSet.Add(T.Parent);
            }

            // Collect all committed descendants of T.Parent (depth-first from CommittedLeaves)
            TArray<ChunkId> ToCleanup;
            for (const ChunkId &RenderedId : m_renderSet)
            {
                if (RenderedId == T.Parent)
                    continue;

                ChunkId AncestorId = RenderedId;
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
            for (const ChunkId &ChildId : ToCleanup)
            {
                Chunk *Child = GetChunk(ChildId);
                if (Child)
                {
                    if (Child->m_state == ChunkState::Visible || Child->m_state == ChunkState::MeshReady)
                    {
                        DeferHideChunk(Child, ChildId);
                    }
                }
                m_renderSet.Remove(ChildId);
            }

            ToRemove.Add(Key);
        }
    }

    // Removal from pending transitions
    for (const ChunkId &Id : ToRemove)
    {
        const LODTransition &T = m_pendingTransitionsMap[Id];
        for (const ChunkId &ChildId : T.Children)
            m_pendingChildSet.Remove(ChildId);
        m_pendingTransitionsMap.Remove(Id);
    }
}


void ChunkManager::ProcessDeferredReleases()
{
    // 1. HARD CAP ENFORCEMENT - position, velocity and direction based
    if (m_chunksMap.Num() > m_planetConfig.CacheHardCap)
    {
        FVector MoveDir = m_lastObserverVelocity.IsNearlyZero() ? m_lastObserverForward : m_lastObserverVelocity.GetSafeNormal();

        EvictChunksOverCap(MoveDir);
    }

    // 2. NORMAL HYSTERESIS
    TArray<DeferredRelease> StillWaiting;

    // Evaluate pressure for remaining chunks
    bool bUnderPressure = m_chunksMap.Num() > m_planetConfig.CacheSoftCap;
    int32 Floor = m_planetConfig.DeferredReleaseDelayMin;

    for (DeferredRelease &Entry : m_deferredReleaseQueue)
    {
        if (bUnderPressure && Entry.FrameCountdown > Floor)
            Entry.FrameCountdown = Floor;

        Entry.FrameCountdown--;

        if (Entry.FrameCountdown > 0)
        {
            StillWaiting.Add(Entry);
            continue;
        }

        // Chunk was re-committed before countdown expired — keep it, remove from deferred
        if (m_renderSet.Contains(Entry.Id))
        {
            m_deferredReleaseIdsMap.Remove(Entry.Id);
            continue;
        }

        // Time expired safely release
        Chunk *Chunk = GetChunk(Entry.Id);
        if (Chunk && Chunk->m_state == ChunkState::MeshReady)
        {
            // Guard against GC'd render proxy on shutdown
            if (Chunk->m_renderProxy.IsValid())
                m_chunkRenderer->ReleaseChunk(Chunk);

            m_deferredReleaseIdsMap.Remove(Entry.Id);
            m_chunksMap.Remove(Entry.Id);
        }
    }

    m_deferredReleaseQueue = MoveTemp(StillWaiting);
}


void ChunkManager::EvictChunksOverCap(const FVector &MoveDir)
{
    while (m_chunksMap.Num() > m_planetConfig.CacheHardCap && m_deferredReleaseQueue.Num() > 0)
    {
        // O(n) single pass — find the worst eviction candidate
        int32 WorstIdx = -1;
        float WorstScore = -1.f;

        for (int32 i = 0; i < m_deferredReleaseQueue.Num(); ++i)
        {
            const ChunkId &Id = m_deferredReleaseQueue[i].Id;
            FVector Center = FMathUtils::GetChunkCenter(Id, m_planetConfig.PlanetRadius);

            float DistSq = FVector::DistSquared(Center, m_lastObserverLocalPos);
            FVector DirToChunk = (Center - m_lastObserverLocalPos).GetSafeNormal();
            float Dot = FVector::DotProduct(MoveDir, DirToChunk);

            // Behind player = high multiplier = evict first
            float EvictMult = FMath::GetMappedRangeValueClamped(FVector2D(-1.f, 1.f), FVector2D(10.f, 0.1f), Dot);
            float Score = DistSq * EvictMult;

            if (Score > WorstScore)
            {
                WorstScore = Score;
                WorstIdx = i;
            }
        }

        if (WorstIdx == -1)
            break;

        // O(1) removal — order doesn't need to be preserved
        DeferredRelease Target = m_deferredReleaseQueue[WorstIdx];
        m_deferredReleaseQueue.RemoveAtSwap(WorstIdx);

        Chunk *Chunk = GetChunk(Target.Id);
        if (Chunk && Chunk->m_state == ChunkState::MeshReady && !m_renderSet.Contains(Target.Id))
        {
            if (Chunk->m_renderProxy.IsValid())
                m_chunkRenderer->ReleaseChunk(Chunk);

            m_deferredReleaseIdsMap.Remove(Target.Id);
            m_chunksMap.Remove(Target.Id);
        }
    }

    // Diagnostic: we're over cap but have nothing left to evict
    if (m_chunksMap.Num() > m_planetConfig.CacheHardCap)
    {
        UE_LOG(LogTemp,
               Warning,
               TEXT("Cache over hard cap (%d/%d) but deferred queue is empty — cannot evict. "
                    "Consider raising CacheHardCap or reducing MaxConcurrentGenerations."),
               m_chunksMap.Num(),
               m_planetConfig.CacheHardCap);
    }
}


void ChunkManager::PruneOrphans()
{
    TArray<ChunkId> ToRemove;

    for (const auto &Pair : m_chunksMap)
    {
        const ChunkId &Id = Pair.Key;
        const Chunk *Chunk = Pair.Value.Get();

        if (m_loadSet.Contains(Id))
            continue;  // Actively needed

        if (m_deferredReleaseIdsMap.Contains(Id))
            continue;  // Already on its way out

        // Only prune chunks that are not in flight
        if (Chunk->m_state == ChunkState::Pending || Chunk->m_state == ChunkState::Generating)
            continue;

        // UE_LOG(LogTemp, Warning, TEXT("PruneOrphans: removing LOD:%d Face:%d State:%d"), Id.LODLevel, Id.FaceIndex, (int32)Chunk->m_state);

        ToRemove.Add(Id);
    }

    for (const ChunkId &Id : ToRemove)
    {
        Chunk *Chunk = GetChunk(Id);
        if (!Chunk)
            continue;

        if (Chunk->m_state == ChunkState::MeshReady)
        {
            // Has GPU resources — must go through deferred release, not immediate destroy
            DeferHideChunk(Chunk, Id);  // calls HideChunk which is safe even if not currently Visible
        }
        else
        {
            // m_state is None or DataReady — no m_renderProxy, safe to destroy immediately
            m_chunksMap.Remove(Id);
        }
    }
}


// ---------------------------------------------------------------------------
// Callback for async generation
// ---------------------------------------------------------------------------
void ChunkManager::OnGenerationComplete(const ChunkId &Id, uint32 GenId, TUniquePtr<ChunkMeshData> MeshData)
{
    // UE_LOG(LogTemp, Warning, TEXT("OnGenerationComplete: LOD:%d Face:%d"), Id.LODLevel, Id.FaceIndex);

    Chunk *Chunk = GetChunk(Id);
    if (!Chunk)
        return;  // Chunk was unloaded while generating

    if (Chunk->m_generationId != GenId)
        return;  // Stale task (Chunk was reset/regenerated)

    // Only accept the result if the chunk is still in the generation pipeline.
    // MeshReady or Visible chunks must not be overwritten by a late callback.
    if (Chunk->m_state == ChunkState::MeshReady || Chunk->m_state == ChunkState::Visible)
        return;

    // Store Data
    Chunk->m_meshData = MoveTemp(MeshData);
    Chunk->m_transform = FMathUtils::ComputeChunkTransform(Id, m_planetConfig.PlanetRadius);
    Chunk->m_state = ChunkState::DataReady;
}


// ---------------------------------------------------------------------------
// Debug stuff
// ---------------------------------------------------------------------------
void ChunkManager::DrawDebugGrid(const UWorld *World) const
{
    if (!World || !m_quadtree)
        return;

    // Get Planet Transform to draw grid in correct World Space
    FTransform PlanetTransform = FTransform::Identity;
    if (m_chunkRenderer && m_chunkRenderer->GetOwner())
    {
        PlanetTransform = m_chunkRenderer->GetOwner()->GetActorTransform();
    }

    m_quadtree->DrawDebugGrid(World, PlanetTransform);
}


void ChunkManager::DrawDebugChunkBounds(const UWorld *World) const
{
    if (!World)
        return;

    for (const auto &Pair : m_chunksMap)
    {
        const Chunk *Chunk = Pair.Value.Get();
        // Only draw bounds for chunks that have a visible mesh component
        if (Chunk && Chunk->m_state == ChunkState::Visible && Chunk->m_renderProxy.IsValid())
        {
            if (UProceduralMeshComponent *Comp = Chunk->m_renderProxy.Get())
            {
                const int32 LOD = Chunk->m_ID.LODLevel;
                // Use LOD color if available, otherwise fallback to white
                const FColor BoxColor = (LOD >= 0 && LOD < LODColorsDebug.Num()) ? LODColorsDebug[LOD] : FColor::White;
                const FBox Box = Comp->Bounds.GetBox();
                DrawDebugBox(World, Box.GetCenter(), Box.GetExtent(), BoxColor, false, 0.f, 0, PlanetStatics::DebugBoxLifetime);
            }
        }
    }
}


void ChunkManager::DebugRootNodes()
{
    for (uint8 Face = 0; Face < 6; ++Face)
    {
        ChunkId RootId(Face, FIntVector(0, 0, 0), 0);

        const bool bInChunkMap = m_chunksMap.Contains(RootId);
        const bool bInRenderSet = m_renderSet.Contains(RootId);
        const bool bInLoadSet = m_loadSet.Contains(RootId);
        const bool bInDeferred = m_deferredReleaseIdsMap.Contains(RootId);
        const bool bInTransition = m_pendingTransitionsMap.Contains(RootId);
        const bool bInDesiredLeaves = m_quadtree && m_quadtree->GetDesiredLeaves().Contains(RootId);

        ChunkState State = ChunkState::None;
        if (bInChunkMap)
            State = m_chunksMap[RootId]->m_state;

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