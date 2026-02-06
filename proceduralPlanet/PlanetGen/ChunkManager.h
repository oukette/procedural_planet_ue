#pragma once

#include "CoreMinimal.h"
#include "Chunk/Chunk.h"
#include "Chunk/ChunkId.h"

class UProceduralMeshComponent;
class FDensityGenerator;
class FMarchingCubes;
class APlanet;


/**
 * View context for LOD and streaming decisions.
 * Passed from Planet to ChunkManager each frame.
 */
struct PROCEDURALPLANET_API FPlanetViewContext
{
        FVector ViewOrigin;
        float ViewDistance;
        int32 MaxLOD;

        FPlanetViewContext() :
            ViewOrigin(FVector::ZeroVector),
            ViewDistance(0.0f),
            MaxLOD(0)
        {
        }

        FPlanetViewContext(const FVector &InOrigin, float InDistance, int32 InMaxLOD) :
            ViewOrigin(InOrigin),
            ViewDistance(InDistance),
            MaxLOD(InMaxLOD)
        {
        }
};

/**
 * ChunkManager - Authoritative owner and orchestrator of all chunks.
 *
 * Responsibilities:
 * - Owns all FChunk instances
 * - Creates and destroys chunks
 * - Drives chunk lifecycle state machine
 * - Manages render proxy attachment/detachment
 * - Reconciles desired vs actual chunk set
 *
 * Does NOT:
 * - Generate geometry (delegates to MarchingCubes)
 * - Compute noise (uses DensityGenerator)
 * - Make LOD decisions (receives from Planet)
 * - Render meshes (uses render proxies)
 */
class PROCEDURALPLANET_API FChunkManager
{
    public:
        FChunkManager();
        ~FChunkManager();

        // === LIFECYCLE ===
        void Initialize(APlanet *InOwnerPlanet);
        void Shutdown();

        // === UPDATE ===
        void Update(const FPlanetViewContext &ViewContext);

        // === QUERY ===
        FChunk *FindChunk(const FChunkId &Id);
        const FChunk *FindChunk(const FChunkId &Id) const;
        int32 GetChunkCount() const { return Chunks.Num(); }
        EChunkState GetChunkState(const FChunkId &Id) const;

        // === DEBUG ===
        void DrawDebugVisualization(UWorld *World) const;
        void LogStatistics() const;

    private:
        // === CHUNK OPERATIONS ===
        void CreateChunk(const FChunkId &Id);
        void DestroyChunk(const FChunkId &Id);
        void GenerateChunkMesh(FChunk &Chunk);
        void AttachRenderProxy(FChunk &Chunk);
        void DetachRenderProxy(FChunk &Chunk);

        // === STREAMING LOGIC ===
        void UpdateChunkSet(const FPlanetViewContext &ViewContext);
        TSet<FChunkId> DetermineDesiredChunks(const FPlanetViewContext &ViewContext) const;
        bool ShouldChunkBeVisible(const FChunkId &Id, const FPlanetViewContext &ViewContext) const;

        // === HELPERS ===
        FChunkTransform ComputeChunkTransform(const FChunkId &Id) const;
        UProceduralMeshComponent *AcquireRenderProxy();
        void ReleaseRenderProxy(UProceduralMeshComponent *Proxy);

    private:
        // === DATA ===
        using FChunkMap = TMap<FChunkId, TUniquePtr<FChunk>>;
        FChunkMap Chunks;

        // Owner reference (non-owning)
        APlanet *OwnerPlanet;

        // Render proxy pool
        TArray<UProceduralMeshComponent *> AvailableProxies;
        TArray<UProceduralMeshComponent *> UsedProxies;

        // Statistics
        int32 TotalChunksCreated;
        int32 TotalChunksDestroyed;
};