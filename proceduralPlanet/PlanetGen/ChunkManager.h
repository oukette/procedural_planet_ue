#pragma once

#include "CoreMinimal.h"
#include "Chunk.h"
#include "DensityGenerator.h"
#include "ChunkRenderer.h"
#include "ChunkGenerator.h"
#include "PlanetQuadtree.h"


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
        const DensityGenerator *Generator;            // Reference to the density generator (owned by APlanet)
        TUniquePtr<ChunkRenderer> Renderer;           // Handles visual components
        TUniquePtr<FChunkGenerator> ChunkGenerator;   // Handles async generation
        TUniquePtr<FPlanetQuadtree> Quadtree;         // Handles LOD and Culling logic
        TMap<FChunkId, TUniquePtr<FChunk>> ChunkMap;  // The central registry of all chunks

        // Helper to create a new chunk entry
        FChunk *CreateChunk(const FChunkId &Id);

        // Helper to get a chunk from the map if it exists, otherwise create it
        FChunk *GetChunk(const FChunkId &Id);

        // Phase 2: State Machine
        void ProcessChunkStates(const TSet<FChunkId> &VisibleChunks, const TSet<FChunkId> &CachedChunks);

        // Helper to check if a chunk is in memory and has mesh data
        bool IsChunkReady(const FChunkId &Id) const;

        // Helper to calculate where the observer will be, for predictive LOD splitting
        FVector CalculatePredictedLocation(const FPlanetViewContext &Context) const;

        // Callback executed on Game Thread when async generation finishes
        void OnGenerationComplete(const FChunkId &Id, uint32 GenId, TUniquePtr<FChunkMeshData> MeshData);
};