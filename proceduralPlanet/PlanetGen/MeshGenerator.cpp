#include "MeshGenerator.h"
#include "../MarchingCubesTables.h"



FChunkMeshData MeshGenerator::GenerateMesh(const GenData &GenData, int32 Resolution, const FTransform &ChunkTransform, const FTransform &PlanetTransform,
                                           int32 LODLevel, const DensityGenerator &DensityGen)
{
    FChunkMeshData MeshData;

    // Use the SampleCount from the generated data.
    const int32 SampleCount = GenData.SampleCount;
    if (SampleCount <= 1)
    {
        return MeshData;
    }

    // Automatic Debug Colors for multiple LODs.
    const static TArray<FColor> LODColors = {
        FColor::Green,        // LOD 0
        FColor::Yellow,       // LOD 1
        FColor(255, 165, 0),  // LOD 2 (Orange)
        FColor::Red,          // LOD 3
        FColor::Magenta,      // LOD 4
        FColor::Cyan,         // LOD 5
        FColor(0, 255, 128),  // LOD 6 (Spring Green)
        FColor(128, 0, 255)   // LOD 7 (Purple)
    };

    FColor DebugColor = (LODLevel >= 0 && LODLevel < LODColors.Num()) ? LODColors[LODLevel] : FColor::White;

    const FVector CornerOffsets[8] = {
        FVector(0, 0, 0), FVector(1, 0, 0), FVector(1, 1, 0), FVector(0, 1, 0), FVector(0, 0, 1), FVector(1, 0, 1), FVector(1, 1, 1), FVector(0, 1, 1)};

    const int EdgeIndex[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

    for (int32 z = 0; z < Resolution; z++)
    {
        for (int32 y = 0; y < Resolution; y++)
        {
            for (int32 x = 0; x < Resolution; x++)
            {
                float D[8];
                FVector P[8];
                int32 CubeIndex = 0;

                for (int32 i = 0; i < 8; i++)
                {
                    int32 ix = x + (int32)CornerOffsets[i].X;
                    int32 iy = y + (int32)CornerOffsets[i].Y;
                    int32 iz = z + (int32)CornerOffsets[i].Z;

                    D[i] = GenData.Densities[ix + iy * SampleCount + iz * SampleCount * SampleCount];
                    P[i] = GenData.Positions[ix + iy * SampleCount + iz * SampleCount * SampleCount];

                    if (D[i] > 0.0f)
                        CubeIndex |= (1 << i);
                }

                if (CubeIndex == 0 || CubeIndex == 255)
                    continue;

                int32 edges = MarchingCubesTables::EdgeTable[CubeIndex];
                FVector EdgeVertex[12];

                for (int32 e = 0; e < 12; e++)
                {
                    if (edges & (1 << e))
                    {
                        EdgeVertex[e] = FMathUtils::VertexInterp(P[EdgeIndex[e][0]], P[EdgeIndex[e][1]], D[EdgeIndex[e][0]], D[EdgeIndex[e][1]]);
                    }
                }

                for (int32 i = 0; MarchingCubesTables::TriTable[CubeIndex][i] != -1; i += 3)
                {
                    FVector PlanetSpaceVertices[] = {EdgeVertex[MarchingCubesTables::TriTable[CubeIndex][i]],
                                                     EdgeVertex[MarchingCubesTables::TriTable[CubeIndex][i + 1]],
                                                     EdgeVertex[MarchingCubesTables::TriTable[CubeIndex][i + 2]]};

                    for (const FVector &PlanetSpaceVertex : PlanetSpaceVertices)
                    {
                        // --- 1. Calculate Vertex Position ---
                        // Transform from Planet-Relative space to Chunk Local space
                        FVector WorldPos = PlanetTransform.TransformPosition(PlanetSpaceVertex);
                        FVector ChunkLocalPos = ChunkTransform.InverseTransformPosition(WorldPos);

                        int32 Index = MeshData.Vertices.Add(ChunkLocalPos);
                        MeshData.Triangles.Add(Index);

                        // --- 2. Calculate Vertex Normal ---
                        FVector PlanetNormal = DensityGen.GetNormalAtPos(PlanetSpaceVertex);
                        FVector WorldNormal = PlanetTransform.TransformVector(PlanetNormal);
                        FVector ChunkLocalNormal = ChunkTransform.InverseTransformVector(WorldNormal);

                        MeshData.Normals.Add(ChunkLocalNormal.GetSafeNormal());

                        // --- 3. Add Debug Color ---
                        MeshData.Colors.Add(DebugColor);
                    }
                }
            }
        }
    }
    return MeshData;
}