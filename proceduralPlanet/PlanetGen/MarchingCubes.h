#pragma once

#include "CoreMinimal.h"
#include "Chunk/ChunkMeshData.h"
#include "DensityGenerator.h"
#include "MarchingCubesTables.h"


/**
 * Marching Cubes 33 implementation (classic).
 * Pure, stateless, deterministic.
 */
class PROCEDURALPLANET_API FMarchingCubes
{
    public:
        struct FGridCell
        {
                FVector Points[8];  // Cube corners
                float Values[8];    // Density at corners
        };

        struct FConfig
        {
                FIntVector GridResolution = FIntVector(33, 33, 33);  // 33^3 grid
                float CellSize = 10.0f;                              // Meters per cell
                float IsoLevel = 0.0f;                               // Surface threshold

                // Ghost layers (for seamless chunks)
                bool bUseGhostLayers = true;
                int32 GhostLayers = 1;  // Extra voxels on each side

                // Mesh optimization
                bool bWeldVertices = true;
                float WeldTolerance = 0.001f;

                FConfig() = default;

                // Helper to get total points including ghost layers
                FIntVector GetTotalResolution() const
                {
                    if (bUseGhostLayers)
                    {
                        return GridResolution + FIntVector(GhostLayers * 2);
                    }
                    return GridResolution;
                }
        };

    public:
        FMarchingCubes() = default;
        ~FMarchingCubes() = default;

        /**
         * Generate mesh using a density generator function.
         * Samples density on-the-fly (no precomputed field).
         */
        void GenerateMesh(const FDensityGenerator &DensityGen, const FVector &ChunkCenterPlanetSpace, const FVector &LocalAxisX, const FVector &LocalAxisY,
                          const FVector &LocalAxisZ, const FConfig &Config, FChunkMeshData &OutMesh) const;

        static const int32 *GetEdgeTable() { return MarchingCubesTables::EdgeTable; }

        static const int32 (*GetTriTable())[16] { return MarchingCubesTables::TriTable; }

    private:
        void SampleDensityField(const FDensityGenerator &DensityGen, const FVector &ChunkOrigin, const FVector &LocalAxisX, const FVector &LocalAxisY,
                                const FVector &LocalAxisZ, const FConfig &Config, TArray<float> &OutDensityField) const;

        void ProcessGridCells(const TArray<float> &DensityField, const FConfig &Config, FChunkMeshData &OutMesh) const;

        FVector VertexInterpolation(const FVector &P1, const FVector &P2, float V1, float V2, float IsoLevel) const;

        FVector CalculateNormalFromDensity(const FDensityGenerator &DensityGen, const FVector &Position, float Epsilon = 0.1f) const;

        void CalculateNormals(FChunkMeshData &Mesh, const FDensityGenerator &DensityGen) const;

        void CalculateUVs(FChunkMeshData &Mesh, const FVector &LocalAxisX, const FVector &LocalAxisY) const;

        void WeldVertices(FChunkMeshData &Mesh, float Tolerance) const;
};