#include "MarchingCubes.h"
#include <cmath>


void FMarchingCubes::GenerateMesh(const TArray<float> &DensityField, const FIntVector &Resolution, float CellSize, float IsoLevel,
                                  FChunkMeshData &OutMesh) const
{
    OutMesh.Clear();

    // For now, just create a simple test mesh
    // We'll implement full marching cubes in the next iteration

    // Create a simple quad for testing
    OutMesh.Vertices.Add(FVector(0, 0, 0));
    OutMesh.Vertices.Add(FVector(CellSize, 0, 0));
    OutMesh.Vertices.Add(FVector(CellSize, CellSize, 0));
    OutMesh.Vertices.Add(FVector(0, CellSize, 0));

    OutMesh.Normals.Add(FVector(0, 0, -1));
    OutMesh.Normals.Add(FVector(0, 0, -1));
    OutMesh.Normals.Add(FVector(0, 0, -1));
    OutMesh.Normals.Add(FVector(0, 0, -1));

    OutMesh.UVs.Add(FVector2D(0, 0));
    OutMesh.UVs.Add(FVector2D(1, 0));
    OutMesh.UVs.Add(FVector2D(1, 1));
    OutMesh.UVs.Add(FVector2D(0, 1));

    OutMesh.Triangles.Add(0);
    OutMesh.Triangles.Add(2);
    OutMesh.Triangles.Add(1);

    OutMesh.Triangles.Add(0);
    OutMesh.Triangles.Add(3);
    OutMesh.Triangles.Add(2);

    OutMesh.CalculateBounds();
}

void FMarchingCubes::GenerateMeshFromFunction(const FDensityGenerator &DensityGen, const FVector &ChunkOrigin, const FVector &LocalAxisX,
                                              const FVector &LocalAxisY, const FVector &LocalAxisZ, const FConfig &Config, FChunkMeshData &OutMesh) const
{
    // For Phase 3, we'll use the density field version
    // This function will be implemented in Phase 4

    // Create a test density field
    const int32 TotalPoints = Config.GridResolution * Config.GridResolution * Config.GridResolution;
    TArray<float> DensityField;
    DensityField.SetNum(TotalPoints);

    // Fill with simple SDF for testing
    for (int32 Z = 0; Z < Config.GridResolution; Z++)
    {
        for (int32 Y = 0; Y < Config.GridResolution; Y++)
        {
            for (int32 X = 0; X < Config.GridResolution; X++)
            {
                FVector LocalPos((X - Config.GridResolution / 2) * Config.CellSize,
                                 (Y - Config.GridResolution / 2) * Config.CellSize,
                                 (Z - Config.GridResolution / 2) * Config.CellSize);

                FVector WorldPos = ChunkOrigin + LocalAxisX * LocalPos.X + LocalAxisY * LocalPos.Y + LocalAxisZ * LocalPos.Z;

                int32 Index = Z * Config.GridResolution * Config.GridResolution + Y * Config.GridResolution + X;

                DensityField[Index] = DensityGen.SampleDensity(WorldPos);
            }
        }
    }

    FIntVector Res(Config.GridResolution, Config.GridResolution, Config.GridResolution);
    GenerateMesh(DensityField, Res, Config.CellSize, Config.IsoLevel, OutMesh);
}

FVector FMarchingCubes::VertexInterpolation(const FVector &P1, const FVector &P2, float V1, float V2, float IsoLevel) const
{
    if (FMath::Abs(IsoLevel - V1) < 0.00001f)
        return P1;
    if (FMath::Abs(IsoLevel - V2) < 0.00001f)
        return P2;
    if (FMath::Abs(V1 - V2) < 0.00001f)
        return P1;

    float T = (IsoLevel - V1) / (V2 - V1);
    return P1 + T * (P2 - P1);
}

FVector FMarchingCubes::CalculateNormal(const FVector &Position, const FDensityGenerator &DensityGen, float Epsilon) const
{
    // Finite difference gradient
    float DX = DensityGen.SampleDensity(Position + FVector(Epsilon, 0, 0)) - DensityGen.SampleDensity(Position - FVector(Epsilon, 0, 0));

    float DY = DensityGen.SampleDensity(Position + FVector(0, Epsilon, 0)) - DensityGen.SampleDensity(Position - FVector(0, Epsilon, 0));

    float DZ = DensityGen.SampleDensity(Position + FVector(0, 0, Epsilon)) - DensityGen.SampleDensity(Position - FVector(0, 0, Epsilon));

    FVector Normal(-DX, -DY, -DZ);  // Negative gradient points outward
    return Normal.GetSafeNormal();
}

void FMarchingCubes::CalculateNormals(FChunkMeshData &Mesh, const FDensityGenerator &DensityGen) const
{
    // For now, just keep our test normals
    // Will implement proper normal calculation in next phase
}

void FMarchingCubes::CalculateUVs(FChunkMeshData &Mesh, const FVector &LocalAxisX, const FVector &LocalAxisY) const
{
    // Simple planar projection for testing
    for (int32 i = 0; i < Mesh.Vertices.Num(); i++)
    {
        float U = FVector::DotProduct(Mesh.Vertices[i], LocalAxisX) * 0.01f;
        float V = FVector::DotProduct(Mesh.Vertices[i], LocalAxisY) * 0.01f;
        Mesh.UVs.Add(FVector2D(U, V));
    }
}