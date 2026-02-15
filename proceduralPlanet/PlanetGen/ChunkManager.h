#pragma once

#include "CoreMinimal.h"
#include "Chunk.h"
#include "DensityGenerator.h"
#include "ChunkRenderer.h"


// Manages the lifecycle of all chunks (Quadtree logic, LOD selection, Async requests).
// Owned strictly by the APlanet actor.
class FChunkManager
{
    public:
        FChunkManager(const FPlanetConfig &planetConfig, const DensityGenerator *densityGen);
        ~FChunkManager();

        // Returns the total number of chunks currently tracked
        int32 GetChunkCount() const { return Chunks.Num(); }

        // Returns the number of chunks that currently have a valid LOD (are visible)
        int32 GetVisibleChunkCount() const;

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

        int32 MaxConcurrentGenerations;  // Limit total background threads
        int32 GenerationRate;            // Limit how many start per tick

        // Helper to create a new chunk entry
        FChunk *CreateChunk(const FChunkId &Id);

        // Helper to get a chunk from the map if it exists, otherwise create it
        FChunk *GetChunk(const FChunkId &Id);

        // Loop (Replaces UpdateAllChunksLOD)
        void UpdateFace(uint8 Face, const FPlanetViewContext &Context, TSet<FChunkId> &OutRequired);

        // Helper to calculate the world position of a chunk center
        FVector GetChunkCenter(uint8 Face, int32 X, int32 Y) const;

        // Math (Replaces DetermineTargetLOD)
        int32 CalculateTargetLOD(float DistanceSq, int32 CurrentLOD) const;

        // State Machine (Replaces ApplyChunkStateChange)
        void HandleChunkState(FChunk *Chunk, int32 TargetLOD);

        // Throttling (Replaces ProcessSpawnQueue)
        void ProcessQueues();
        void ProcessGenerationQueue();

        void StartAsyncGeneration(const FChunkId &Id);

        // Callback executed on Game Thread when async generation finishes
        void OnGenerationComplete(const FChunkId &Id, uint32 GenId, TUniquePtr<FChunkMeshData> MeshData);
};