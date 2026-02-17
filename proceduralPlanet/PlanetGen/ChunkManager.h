#pragma once

#include "CoreMinimal.h"
#include "Chunk.h"
#include "DensityGenerator.h"
#include "ChunkRenderer.h"


// A logical node in the Quadtree.
// Does NOT hold mesh data directly (that is in FChunk).
// Used for traversing the hierarchy and deciding what to spawn.
struct FQuadtreeNode
{
        FChunkId Id;
        FQuadtreeNode *Parent = nullptr;
        TArray<TUniquePtr<FQuadtreeNode>> Children;

        FQuadtreeNode(const FChunkId &InId, FQuadtreeNode *InParent) :
            Id(InId),
            Parent(InParent)
        {
        }

        bool IsLeaf() const { return Children.Num() == 0; }
};

// Manages the lifecycle of all chunks (Quadtree logic, LOD selection, Async requests).
// Owned strictly by the APlanet actor.
class FChunkManager
{
    public:
        FChunkManager(const FPlanetConfig &planetConfig, const DensityGenerator *densityGen);
        ~FChunkManager();

        // Returns the total number of chunks in the grid (6 * N * N)
        int32 GetChunkCount() const;

        // Returns the number of chunks that currently have a valid LOD (are visible)
        int32 GetLoadedChunkCount() const;

        // Initialize the chunk manager for the given planet.
        void Initialize(AActor *Owner, UMaterialInterface *Material);

        // Main update loop called by APlanet::Tick
        void Update(const FPlanetViewContext &Context);

        // Debug: Draws the logical grid boundaries on the sphere
        void DrawDebugGrid(const UWorld *World) const;

        // Debug: Draws the bounding box of the actual generated meshes
        void DrawDebugChunkBounds(const UWorld *World) const;


    private:
        FPlanetConfig Config;
        const DensityGenerator *Generator;          // Reference to the density generator (owned by APlanet)
        TUniquePtr<ChunkRenderer> Renderer;         // Handles visual components
        TMap<FChunkId, TUniquePtr<FChunk>> Chunks;  // The central registry of all chunks
        TArray<FChunkId> PendingGenerationQueue;    // Chunks waiting to be processed
        TSet<FChunkId> CurrentlyGenerating;         // Chunks currently in a background thread

        // The 6 root nodes of the planet (one per face)
        TArray<TUniquePtr<FQuadtreeNode>> RootNodes;

        int32 MaxConcurrentGenerations;  // Limit total background threads
        int32 GenerationRate;            // Limit how many start per tick

        // Helper to create a new chunk entry
        FChunk *CreateChunk(const FChunkId &Id);

        // Helper to get a chunk from the map if it exists, otherwise create it
        FChunk *GetChunk(const FChunkId &Id);

        // Loop (Replaces UpdateAllChunksLOD)
        void UpdateFace(uint8 Face, const FPlanetViewContext &Context, TSet<FChunkId> &OutRequired);

        // Recursive traversal to determine visible chunks
        void UpdateNode(FQuadtreeNode *Node, const FPlanetViewContext &Context, TSet<FChunkId> &OutRequired);

        // Helper to determine if a node should split
        bool ShouldSplit(const FQuadtreeNode *Node, const FVector &ObserverLocal) const;

        // Helper to calculate the world position of a chunk center
        FVector GetChunkCenter(const FChunkId &Id) const;

        // Helper to get UV bounds (0..1) for a specific chunk ID
        void GetUVBounds(const FChunkId &Id, FVector2D &OutMin, FVector2D &OutMax) const;

        // Helper to find which chunk contains a specific local position
        FChunkId GetChunkIdAt(const FVector &LocalPosition) const;

        // State Machine (Replaces ApplyChunkStateChange)
        void HandleChunkState(FChunk *Chunk, int32 TargetLOD);

        // Throttling (Replaces ProcessSpawnQueue)
        void ProcessQueues();
        void ProcessGenerationQueue();

        void StartAsyncGeneration(const FChunkId &Id);

        // Callback executed on Game Thread when async generation finishes
        void OnGenerationComplete(const FChunkId &Id, uint32 GenId, TUniquePtr<FChunkMeshData> MeshData);
};