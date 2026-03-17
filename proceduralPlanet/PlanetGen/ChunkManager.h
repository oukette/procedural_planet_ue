#pragma once

#include "CoreMinimal.h"
#include "Chunk.h"
#include "DensityGenerator.h"
#include "ChunkRenderer.h"
#include "ChunkGenerator.h"
#include "PlanetQuadtree.h"


// Chunks hidden after a merge, waiting to be released after a delay
struct DeferredRelease
{
        FChunkId Id;
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
        FChunkId Parent;
        TArray<FChunkId> Children;  // Always 4 for a quadtree split
        LeafTransitionType Type;
        bool isReadyToCommit = false;
};


// Manages the lifecycle of all chunks (Quadtree logic, LOD selection, Async requests).
// Owned strictly by the APlanet actor.
class ChunkManager
{
    private:
        FPlanetConfig m_planetConfig;
        const DensityGenerator *m_densityGen;         // Reference to the density generator (owned by APlanet)
        TUniquePtr<ChunkRenderer> m_chunkRenderer;    // Handles visual components
        TUniquePtr<ChunkGenerator> m_chunkGenerator;  // Handles async generation
        TUniquePtr<FPlanetQuadtree> m_quadtree;       // Handles LOD and Culling logic

        TMap<FChunkId, TUniquePtr<FChunk>> m_chunksMap;         // The central registry of all chunks
        TSet<FChunkId> m_renderSet;                             // ground truth of what is rendered
        TSet<FChunkId> m_loadSet;                               // All chunk IDs that must be kept alive this frame
        TMap<FChunkId, LODTransition> m_pendingTransitionsMap;  // keyed on parent ID
        TSet<FChunkId> m_pendingChildSet;                       // O(1) mirror of all children in m_pendingTransitionsMap
        TArray<DeferredRelease> m_deferredReleaseQueue;         // queue of chunks to release after a delay
        TSet<FChunkId> m_deferredReleaseIdsMap;                 // O(1) mirror of m_deferredReleaseQueue

        FVector m_lastObserverLocalPos = FVector::ZeroVector;

    public:
        ChunkManager(const FPlanetConfig &planetConfig, const DensityGenerator *densityGen);
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
        void Update(const FPlanetViewContext &Context);

        // Debug: Draws the logical grid boundaries on the sphere.
        void DrawDebugGrid(const UWorld *World) const;

        // Debug: Draws the bounding box of the actual generated meshes.
        void DrawDebugChunkBounds(const UWorld *World) const;

    private:

        // Helper to create a new chunk entry
        FChunk *CreateChunk(const FChunkId &Id);

        // Helper to get a chunk from the map if it exists, otherwise create it
        FChunk *GetChunk(const FChunkId &Id);

        int32 GetDeferredReleaseDelay() const;

        // Derives m_loadSet from m_renderSet, m_pendingTransitionsMap, and desired roots.
        void BuildLoadSet(const TSet<FChunkId> &DesiredLeaves, const bool bShouldGenerateChunks);

        // Explicit initialization of the 6 root chunks directly into m_renderSet
        void InitializeRoots();

        // Quadtree reconciliation, diff desired vs committed, build m_pendingTransitionsMap
        void ReconcileTransitions(const TSet<FChunkId> &DesiredLeaves);

        // Ensure all needed chunks are generating/uploading
        void AdvanceLoading(const TMap<FChunkId, float> &DistanceSqCache);

        // Atomic show/hide for complete groups
        void CommitReadyTransitions(const bool bShouldGenerateChunks, const TMap<FChunkId, float> &DistanceSqCache);

        // Atomic release of deferred chunks
        void ProcessDeferredReleases();

        // Safety net: any chunk in m_chunksMap not in m_loadSet and not in flight gets deferred
        void PruneOrphans();

        // Helper to check if a chunk is in memory and has mesh data
        bool IsChunkReady(const FChunkId &Id) const;

        // Helper to defer hide a chunk
        void DeferHideChunk(FChunk *Chunk, const FChunkId &Id);

        // Callback executed on Game Thread when async generation finishes
        void OnGenerationComplete(const FChunkId &Id, uint32 GenId, TUniquePtr<FChunkMeshData> MeshData);

        void DebugRootNodes();
};