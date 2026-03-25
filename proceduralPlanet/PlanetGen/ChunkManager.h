#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Templates/UniquePtr.h"

#include "Chunk.h"
#include "DensityGenerator.h"
#include "ChunkRenderer.h"
#include "ChunkGenerator.h"
#include "PlanetQuadtree.h"


class UWorld;
class INoise;


// Chunks hidden after a merge, waiting to be released after a delay
struct DeferredRelease
{
        ChunkId Id;
        int32 FrameCountdown;
};


enum class LeafTransitionType : uint8
{
    Split,
    Merge
};


// Represent a transition from a parent to one or more children.
struct LODTransition
{
        ChunkId Parent;
        TArray<ChunkId> Children;  // Always 4 for a quadtree split
        LeafTransitionType Type;
        bool isReadyToCommit = false;
        int32 FrameAge = 0;
};


// Manages the lifecycle of all chunks (Quadtree logic, LOD selection, Async requests).
// Owned strictly by the APlanet actor.
class ChunkManager
{
    private:
        FPlanetConfig m_planetConfig;
        const DensityGenerator *m_densityGen;                     // Reference to the density generator (owned by APlanet)
        TSharedPtr<INoise, ESPMode::ThreadSafe> m_noiseProvider;  // Reference to the noise provider (owned by APlanet)
        TUniquePtr<ChunkRenderer> m_chunkRenderer;                // Handles visual components
        TUniquePtr<ChunkGenerator> m_chunkGenerator;              // Handles async generation
        TUniquePtr<PlanetQuadtree> m_quadtree;                    // Handles LOD and Culling logic

        TMap<ChunkId, TUniquePtr<Chunk>> m_chunksMap;          // The central registry of all chunks
        TSet<ChunkId> m_renderSet;                             // ground truth of what is rendered
        TSet<ChunkId> m_loadSet;                               // All chunk IDs that must be kept alive this frame
        TMap<ChunkId, LODTransition> m_pendingTransitionsMap;  // keyed on parent ID
        TSet<ChunkId> m_pendingChildSet;                       // O(1) mirror of all children in m_pendingTransitionsMap
        TArray<DeferredRelease> m_deferredReleaseQueue;        // queue of chunks to release after a delay
        TSet<ChunkId> m_deferredReleaseIdsMap;                 // O(1) mirror of m_deferredReleaseQueue

        FVector m_lastObserverLocalPos = FVector::ZeroVector;
        FVector m_lastObserverVelocity = FVector::ZeroVector;
        FVector m_lastObserverForward = FVector::ZeroVector;

    public:
        ChunkManager(const FPlanetConfig &planetConfig, const DensityGenerator *densityGen, TSharedPtr<INoise, ESPMode::ThreadSafe> noiseProvider);
        ~ChunkManager();

        // Returns the total number of chunks in memory.
        int32 GetTotalChunkCount() const;

        // Returns the number of chunks currently being rendered.
        int32 GetVisibleChunkCount() const;

        // Returns per-LOD count of currently visible chunks. Array must be pre-sized to MaxLOD+1.
        void GetVisibleCountPerLOD(TArray<int32> &OutCounts) const;

        // Returns the number of chunks waiting for generation.
        int32 GetPendingCount() const;

        // Initialize the chunk manager for the given planet.
        void Initialize(AActor *Owner, UMaterialInterface *Material);

        // Main update loop called by APlanet::Tick
        void Update(const PlanetViewContext &Context);

        // Debug: Draws the logical grid boundaries on the sphere.
        void DrawDebugGrid(const UWorld *World) const;

        // Debug: Draws the bounding box of the actual generated meshes.
        void DrawDebugChunkBounds(const UWorld *World) const;

    private:
        // Helper to create a new chunk entry
        Chunk *CreateChunk(const ChunkId &Id);

        // Helper to get a chunk from the map if it exists, otherwise create it
        Chunk *GetChunk(const ChunkId &Id);

        int32 GetDeferredReleaseDelay() const;

        // Derives m_loadSet from m_renderSet, m_pendingTransitionsMap, and desired roots.
        void BuildLoadSet(const TSet<ChunkId> &DesiredLeaves, const bool bShouldGenerateChunks);

        // Explicit initialization of the 6 root chunks directly into m_renderSet
        void InitializeRoots();

        // Quadtree reconciliation, diff desired vs committed, build m_pendingTransitionsMap
        void ReconcileTransitions(const TSet<ChunkId> &DesiredLeaves);

        // Ensure all needed chunks are generating/uploading
        void AdvanceLoading(const TMap<ChunkId, float> &DistanceSqCache);

        // Atomic show/hide for complete groups
        void CommitReadyTransitions(const bool bShouldGenerateChunks, const TMap<ChunkId, float> &DistanceSqCache);

        // Atomic release of deferred chunks
        void ProcessDeferredReleases();

        // Cap based chunk eviction mechanism
        void EvictChunksOverCap(const FVector& MoveDir);

        // Safety net: any chunk in m_chunksMap not in m_loadSet and not in flight gets deferred
        void PruneOrphans();

        // Helper to check if a chunk is in memory and has mesh data
        bool IsChunkReady(const ChunkId &Id) const;

        // Helper to defer hide a chunk
        void DeferHideChunk(Chunk *Chunk, const ChunkId &Id);

        // Callback executed on Game Thread when async generation finishes
        void OnGenerationComplete(const ChunkId &Id, uint32 GenId, TUniquePtr<ChunkMeshData> MeshData);

        void DebugRootNodes();
};