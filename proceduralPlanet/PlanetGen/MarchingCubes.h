#pragma once

#include "CoreMinimal.h"
#include "ChunkMeshData.h"
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
                int32 GridResolution = 33;  // 33^3 grid points (32 cells per axis)
                float CellSize = 10.0f;     // Meters per cell
                float IsoLevel = 0.0f;      // Surface threshold

                // Ghost layers (for seamless chunks)
                bool bUseGhostLayers = true;
                int32 GhostLayers = 1;  // Extra voxels on each side

                FConfig() = default;
        };

    public:
        FMarchingCubes() = default;
        ~FMarchingCubes() = default;

        /**
         * Generate mesh from a density field.
         * @param DensityField Flat array of density values [Z][Y][X]
         * @param Resolution Grid resolution (cubic)
         * @param CellSize World size of each cell
         * @param OutMesh Generated mesh data
         */
        void GenerateMesh(const TArray<float> &DensityField, const FIntVector &Resolution, float CellSize, float IsoLevel, FChunkMeshData &OutMesh) const;

        /**
         * Generate mesh using a density generator function.
         * Samples density on-the-fly (no precomputed field).
         */
        void GenerateMeshFromFunction(const FDensityGenerator &DensityGen, const FVector &ChunkOrigin, const FVector &LocalAxisX, const FVector &LocalAxisY,
                                      const FVector &LocalAxisZ, const FConfig &Config, FChunkMeshData &OutMesh) const;

        static const int32 *GetEdgeTable() { return MarchingCubesTables::EdgeTable; }
        static const int32 (*GetTriTable())[16] { return MarchingCubesTables::TriTable; }

    private:
        // Internal helper functions
        FVector VertexInterpolation(const FVector &P1, const FVector &P2, float V1, float V2, float IsoLevel) const;
        FVector CalculateNormal(const FVector &Position, const FDensityGenerator &DensityGen, float Epsilon = 0.1f) const;
        void CalculateNormals(FChunkMeshData &Mesh, const FDensityGenerator &DensityGen) const;
        void CalculateUVs(FChunkMeshData &Mesh, const FVector &LocalAxisX, const FVector &LocalAxisY) const;

        // Marching Cubes tables (static)
        static const int32 EdgeTable[256];
        static const int32 TriTable[256][16];
};