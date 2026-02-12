#pragma once

#include "CoreMinimal.h"
#include "Chunk.h"
#include "DensityGenerator.h"


/**
 * Manages the lifecycle of all chunks (Quadtree logic, LOD selection, Async requests).
 * Owned strictly by the APlanet actor.
 */
class FChunkManager
{
    public:
        FChunkManager(const FPlanetConfig &InConfig, const DensityGenerator *InGenerator);
        ~FChunkManager();

        /** Returns the total number of chunks currently tracked */
        int32 GetChunkCount() const { return Chunks.Num(); }

        // Returns the number of chunks that currently have a valid LOD (are visible)
        int32 GetVisibleChunkCount() const;

        // Replaces the loop in your PrepareGeneration()
        void Initialize();

        /** Main update loop called by APlanet::Tick */
        void Update(const FPlanetViewContext &Context);


    private:
        FPlanetConfig Config;
        const DensityGenerator *Generator;          // Reference to the density generator (owned by APlanet)
        TMap<FChunkId, TUniquePtr<FChunk>> Chunks;  // The central registry of all chunks
        TQueue<FChunkId> GenerationQueue;           // Queue of chunks waiting to be generated (to throttle thread usage)

        // Helper to create a new chunk entry
        FChunk *CreateChunk(const FChunkId &Id);

        // Helper to get a chunk from the map if it exists, otherwise create it
        FChunk *GetChunk(const FChunkId &Id);

        // 1. The Loop (Replaces UpdateAllChunksLOD)
        void UpdateFace(uint8 Face, const FPlanetViewContext &Context, TSet<FChunkId> &OutRequired);

        // 2. The Math (Replaces DetermineTargetLOD)
        int32 CalculateTargetLOD(float DistanceSq) const;

        // 3. The State Machine (Replaces ApplyChunkStateChange)
        void HandleChunkState(FChunk *Chunk, int32 TargetLOD);

        // 4. The Throttling (Replaces ProcessSpawnQueue)
        void ProcessQueues();
};