#pragma once

#include "CoreMinimal.h"
#include "Chunk.h"
#include "DensityGenerator.h"
#include "ChunkRenderer.h"
#include "ChunkGenerator.h"
#include "PlanetQuadtree.h"


// Chunks hidden after a merge, waiting to be released after a delay
struct FDeferredRelease
{
        FChunkId Id;
        int32 FrameCountdown;
};


// Manages the lifecycle of all chunks (Quadtree logic, LOD selection, Async requests).
// Owned strictly by the APlanet actor.
class FChunkManager
{
    public:
        FChunkManager(const FPlanetConfig &planetConfig, const DensityGenerator *densityGen);
        ~FChunkManager();

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
        FPlanetConfig Config;
        const DensityGenerator *Generator;           // Reference to the density generator (owned by APlanet)
        TUniquePtr<ChunkRenderer> Renderer;          // Handles visual components
        TUniquePtr<FChunkGenerator> ChunkGenerator;  // Handles async generation
        TUniquePtr<FPlanetQuadtree> Quadtree;        // Handles LOD and Culling logic

        TMap<FChunkId, TUniquePtr<FChunk>> ChunkMap;        // The central registry of all chunks
        TMap<FChunkId, FLODTransition> PendingTransitions;  // keyed on parent ID
        TSet<FChunkId> RenderSet;                           // ground truth of what is rendered
        TSet<FChunkId> LoadSet;                             // All chunk IDs that must be kept alive this frame
        TSet<FChunkId> DeferredReleaseIds;                  // O(1) mirror of DeferredReleaseQueue
        TArray<FDeferredRelease> DeferredReleaseQueue;

        // Helper to create a new chunk entry
        FChunk *CreateChunk(const FChunkId &Id);

        // Helper to get a chunk from the map if it exists, otherwise create it
        FChunk *GetChunk(const FChunkId &Id);

        // Derives LoadSet from RenderSet + all active PendingTransitions
        void BuildLoadSet();

        // Safety net: any chunk in ChunkMap not in LoadSet and not in flight gets deferred
        void PruneOrphans();

        // Explicit initialization of the 6 root chunks directly into RenderSet
        void InitializeRoots();

        // Quadtree reconciliation, diff desired vs committed, build PendingTransitions
        void ReconcileTransitions(const TSet<FChunkId> &DesiredLeaves);

        // Ensure all needed chunks are generating/uploading
        void AdvanceLoading();

        // Atomic show/hide for complete groups
        void CommitReadyTransitions();

        // Atomic release of deferred chunks
        void ProcessDeferredReleases();

        // Pure math helpers
        static FChunkId GetParentId(const FChunkId &Child);
        static TArray<FChunkId> GetChildrenIds(const FChunkId &Parent);
        static bool IsRootNode(const FChunkId &Id);

        // Helper to check if a chunk is in memory and has mesh data
        bool IsChunkReady(const FChunkId &Id) const;

        // Callback executed on Game Thread when async generation finishes
        void OnGenerationComplete(const FChunkId &Id, uint32 GenId, TUniquePtr<FChunkMeshData> MeshData);

        void DebugRootNodes();
};