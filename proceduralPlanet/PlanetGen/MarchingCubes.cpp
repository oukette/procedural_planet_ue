#include "MarchingCubes.h"
#include <cmath>
#include "DrawDebugHelpers.h"


void FMarchingCubes::GenerateMesh(const FDensityGenerator &DensityGen, const FVector &ChunkCenterPlanetSpace, const FVector &LocalAxisX,
                                  const FVector &LocalAxisY, const FVector &LocalAxisZ, const FConfig &Config, FChunkMeshData &OutMesh) const
{
    OutMesh.Clear();

    // 1. Sample density field
    TArray<float> DensityField;
    SampleDensityField(DensityGen, ChunkCenterPlanetSpace, LocalAxisX, LocalAxisY, LocalAxisZ, Config, DensityField);

    // 2. Process grid cells with Marching Cubes
    ProcessGridCells(DensityField, Config, OutMesh);

    // 3. Calculate normals from density gradient
    CalculateNormals(OutMesh, DensityGen);

    // 4. Calculate UVs
    CalculateUVs(OutMesh, LocalAxisX, LocalAxisY);

    // 5. Optional: Weld vertices
    if (Config.bWeldVertices)
    {
        WeldVertices(OutMesh, Config.WeldTolerance);
    }

    // 6. Calculate bounds
    OutMesh.CalculateBounds();
}


void FMarchingCubes::SampleDensityField(const FDensityGenerator &DensityGen, const FVector &ChunkCenterPlanetSpace, const FVector &LocalAxisX,
                                        const FVector &LocalAxisY, const FVector &LocalAxisZ, const FConfig &Config, TArray<float> &OutDensityField) const
{
    FIntVector TotalRes = Config.GetTotalResolution();
    int32 TotalPoints = TotalRes.X * TotalRes.Y * TotalRes.Z;

    OutDensityField.Empty(TotalPoints);
    OutDensityField.AddZeroed(TotalPoints);

    int32 GhostOffset = Config.bUseGhostLayers ? -Config.GhostLayers : 0;

    // Sample density at each grid point
    for (int32 Z = 0; Z < TotalRes.Z; Z++)
    {
        for (int32 Y = 0; Y < TotalRes.Y; Y++)
        {
            for (int32 X = 0; X < TotalRes.X; X++)
            {
                // 1. Position in chunk-local space (meters from chunk center)
                FVector ChunkLocalPos((X + GhostOffset) * Config.CellSize, (Y + GhostOffset) * Config.CellSize, (Z + GhostOffset) * Config.CellSize);

                // 2. Convert to planet-relative space
                //    ChunkLocal -> PlanetRelative:
                //    ChunkLocalPos in chunk axes -> add to chunk center
                FVector PlanetRelativePos = ChunkCenterPlanetSpace + LocalAxisX * ChunkLocalPos.X + LocalAxisY * ChunkLocalPos.Y + LocalAxisZ * ChunkLocalPos.Z;

                // 3. Sample density (DensityGen expects planet-relative positions)
                float Density = DensityGen.SampleDensity(PlanetRelativePos);

                // Store in flat array [Z][Y][X]
                int32 Index = Z * TotalRes.Y * TotalRes.X + Y * TotalRes.X + X;
                OutDensityField[Index] = Density;
            }
        }
    }
}


void FMarchingCubes::ProcessGridCells(const TArray<float> &DensityField, const FConfig &Config, FChunkMeshData &OutMesh) const
{
    const int32 *EdgeTable = GetEdgeTable();
    const int32(*TriTable)[16] = GetTriTable();

    FIntVector TotalRes = Config.GetTotalResolution();
    FIntVector GridRes = Config.GridResolution;

    // Calculate offsets for ghost layers
    int32 GhostOffset = Config.bUseGhostLayers ? Config.GhostLayers : 0;

    // Pre-allocate arrays
    OutMesh.Vertices.Reserve(GridRes.X * GridRes.Y * GridRes.Z * 3);
    OutMesh.Triangles.Reserve(GridRes.X * GridRes.Y * GridRes.Z * 15);

    // Process each cell (excluding ghost region for mesh generation)
    for (int32 Z = 0; Z < GridRes.Z - 1; Z++)
    {
        for (int32 Y = 0; Y < GridRes.Y - 1; Y++)
        {
            for (int32 X = 0; X < GridRes.X - 1; X++)
            {
                // Get density values at cube corners
                float CubeValues[8];

                // Corner indices in the density field (with ghost offset)
                int32 BaseX = X + GhostOffset;
                int32 BaseY = Y + GhostOffset;
                int32 BaseZ = Z + GhostOffset;

                // Calculate indices for all 8 corners
                int32 Indices[8];
                Indices[0] = (BaseZ)*TotalRes.Y * TotalRes.X + (BaseY)*TotalRes.X + (BaseX);
                Indices[1] = (BaseZ)*TotalRes.Y * TotalRes.X + (BaseY)*TotalRes.X + (BaseX + 1);
                Indices[2] = (BaseZ)*TotalRes.Y * TotalRes.X + (BaseY + 1) * TotalRes.X + (BaseX + 1);
                Indices[3] = (BaseZ)*TotalRes.Y * TotalRes.X + (BaseY + 1) * TotalRes.X + (BaseX);
                Indices[4] = (BaseZ + 1) * TotalRes.Y * TotalRes.X + (BaseY)*TotalRes.X + (BaseX);
                Indices[5] = (BaseZ + 1) * TotalRes.Y * TotalRes.X + (BaseY)*TotalRes.X + (BaseX + 1);
                Indices[6] = (BaseZ + 1) * TotalRes.Y * TotalRes.X + (BaseY + 1) * TotalRes.X + (BaseX + 1);
                Indices[7] = (BaseZ + 1) * TotalRes.Y * TotalRes.X + (BaseY + 1) * TotalRes.X + (BaseX);

                // Get density values
                for (int32 i = 0; i < 8; i++)
                {
                    CubeValues[i] = DensityField[Indices[i]];
                }

                // Determine cube index
                const float Epsilon = 0.0001f;
                int32 CubeIndex = 0;
                if (CubeValues[0] < Config.IsoLevel - Epsilon)
                    CubeIndex |= 1;
                if (CubeValues[1] < Config.IsoLevel - Epsilon)
                    CubeIndex |= 2;
                if (CubeValues[2] < Config.IsoLevel - Epsilon)
                    CubeIndex |= 4;
                if (CubeValues[3] < Config.IsoLevel - Epsilon)
                    CubeIndex |= 8;
                if (CubeValues[4] < Config.IsoLevel - Epsilon)
                    CubeIndex |= 16;
                if (CubeValues[5] < Config.IsoLevel - Epsilon)
                    CubeIndex |= 32;
                if (CubeValues[6] < Config.IsoLevel - Epsilon)
                    CubeIndex |= 64;
                if (CubeValues[7] < Config.IsoLevel - Epsilon)
                    CubeIndex |= 128;


                // Get edges from table
                int32 Edges = EdgeTable[CubeIndex];
                if (Edges == 0)
                    continue;  // Cube is entirely inside/outside

                // Calculate positions in CHUNK-LOCAL SPACE (meters from chunk center)
                // These are the positions where we interpolate vertices
                FVector CornerPositions[8] = {FVector((BaseX)*Config.CellSize, (BaseY)*Config.CellSize, (BaseZ)*Config.CellSize),
                                              FVector((BaseX + 1) * Config.CellSize, (BaseY)*Config.CellSize, (BaseZ)*Config.CellSize),
                                              FVector((BaseX + 1) * Config.CellSize, (BaseY + 1) * Config.CellSize, (BaseZ)*Config.CellSize),
                                              FVector((BaseX)*Config.CellSize, (BaseY + 1) * Config.CellSize, (BaseZ)*Config.CellSize),
                                              FVector((BaseX)*Config.CellSize, (BaseY)*Config.CellSize, (BaseZ + 1) * Config.CellSize),
                                              FVector((BaseX + 1) * Config.CellSize, (BaseY)*Config.CellSize, (BaseZ + 1) * Config.CellSize),
                                              FVector((BaseX + 1) * Config.CellSize, (BaseY + 1) * Config.CellSize, (BaseZ + 1) * Config.CellSize),
                                              FVector((BaseX)*Config.CellSize, (BaseY + 1) * Config.CellSize, (BaseZ + 1) * Config.CellSize)};

                // Interpolated vertices on edges
                FVector EdgeVertices[12];

                // Edge tables define which corners each edge connects
                // Edge 0: between corner 0 and 1
                if (Edges & 1)
                    EdgeVertices[0] = VertexInterpolation(CornerPositions[0], CornerPositions[1], CubeValues[0], CubeValues[1], Config.IsoLevel);

                // Edge 1: between corner 1 and 2
                if (Edges & 2)
                    EdgeVertices[1] = VertexInterpolation(CornerPositions[1], CornerPositions[2], CubeValues[1], CubeValues[2], Config.IsoLevel);

                // Edge 2: between corner 2 and 3
                if (Edges & 4)
                    EdgeVertices[2] = VertexInterpolation(CornerPositions[2], CornerPositions[3], CubeValues[2], CubeValues[3], Config.IsoLevel);

                // Edge 3: between corner 3 and 0
                if (Edges & 8)
                    EdgeVertices[3] = VertexInterpolation(CornerPositions[3], CornerPositions[0], CubeValues[3], CubeValues[0], Config.IsoLevel);

                // Edge 4: between corner 4 and 5
                if (Edges & 16)
                    EdgeVertices[4] = VertexInterpolation(CornerPositions[4], CornerPositions[5], CubeValues[4], CubeValues[5], Config.IsoLevel);

                // Edge 5: between corner 5 and 6
                if (Edges & 32)
                    EdgeVertices[5] = VertexInterpolation(CornerPositions[5], CornerPositions[6], CubeValues[5], CubeValues[6], Config.IsoLevel);

                // Edge 6: between corner 6 and 7
                if (Edges & 64)
                    EdgeVertices[6] = VertexInterpolation(CornerPositions[6], CornerPositions[7], CubeValues[6], CubeValues[7], Config.IsoLevel);

                // Edge 7: between corner 7 and 4
                if (Edges & 128)
                    EdgeVertices[7] = VertexInterpolation(CornerPositions[7], CornerPositions[4], CubeValues[7], CubeValues[4], Config.IsoLevel);

                // Edge 8: between corner 0 and 4
                if (Edges & 256)
                    EdgeVertices[8] = VertexInterpolation(CornerPositions[0], CornerPositions[4], CubeValues[0], CubeValues[4], Config.IsoLevel);

                // Edge 9: between corner 1 and 5
                if (Edges & 512)
                    EdgeVertices[9] = VertexInterpolation(CornerPositions[1], CornerPositions[5], CubeValues[1], CubeValues[5], Config.IsoLevel);

                // Edge 10: between corner 2 and 6
                if (Edges & 1024)
                    EdgeVertices[10] = VertexInterpolation(CornerPositions[2], CornerPositions[6], CubeValues[2], CubeValues[6], Config.IsoLevel);

                // Edge 11: between corner 3 and 7
                if (Edges & 2048)
                    EdgeVertices[11] = VertexInterpolation(CornerPositions[3], CornerPositions[7], CubeValues[3], CubeValues[7], Config.IsoLevel);


                // Create triangles from triTable
                for (int32 i = 0; TriTable[CubeIndex][i] != -1; i += 3)
                {
                    int32 V0 = TriTable[CubeIndex][i];
                    int32 V1 = TriTable[CubeIndex][i + 1];
                    int32 V2 = TriTable[CubeIndex][i + 2];

                    // Add triangle (scale vertices by cell size)
                    int32 BaseIndex = OutMesh.Vertices.Num();

                    OutMesh.Vertices.Add(EdgeVertices[V0]);
                    OutMesh.Vertices.Add(EdgeVertices[V1]);
                    OutMesh.Vertices.Add(EdgeVertices[V2]);

                    OutMesh.Triangles.Add(BaseIndex);
                    OutMesh.Triangles.Add(BaseIndex + 1);
                    OutMesh.Triangles.Add(BaseIndex + 2);
                }
            }
        }
    }
}


FVector FMarchingCubes::VertexInterpolation(const FVector &P1, const FVector &P2, float V1, float V2, float IsoLevel) const
{
    // If both on same side of surface, no valid intersection
    // (This shouldn't happen if cube index is correct)
    if ((V1 < IsoLevel && V2 < IsoLevel) || (V1 > IsoLevel && V2 > IsoLevel))
    {
        // Return midpoint as fallback
        return (P1 + P2) * 0.5f;
    }

    // Handle exact matches
    if (FMath::Abs(IsoLevel - V1) < 0.00001f)
        return P1;
    if (FMath::Abs(IsoLevel - V2) < 0.00001f)
        return P2;

    float T = (IsoLevel - V1) / (V2 - V1);

    // CLAMP to [0,1] - surface MUST be between P1 and P2
    T = FMath::Clamp(T, 0.0f, 1.0f);

    return P1 + T * (P2 - P1);
}


FVector FMarchingCubes::CalculateNormalFromDensity(const FDensityGenerator &DensityGen, const FVector &Position, float Epsilon) const
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
    Mesh.Normals.Empty(Mesh.Vertices.Num());
    Mesh.Normals.AddZeroed(Mesh.Vertices.Num());

    // Calculate normals from density gradient at each vertex
    for (int32 i = 0; i < Mesh.Vertices.Num(); i++)
    {
        Mesh.Normals[i] = CalculateNormalFromDensity(DensityGen, Mesh.Vertices[i]);
    }
}


void FMarchingCubes::CalculateUVs(FChunkMeshData &Mesh, const FVector &LocalAxisX, const FVector &LocalAxisY) const
{
    Mesh.UVs.Empty(Mesh.Vertices.Num());

    for (const FVector &Vertex : Mesh.Vertices)
    {
        float U = FVector::DotProduct(Vertex, LocalAxisX) * 0.01f;
        float V = FVector::DotProduct(Vertex, LocalAxisY) * 0.01f;
        Mesh.UVs.Add(FVector2D(U, V));
    }
}


void FMarchingCubes::WeldVertices(FChunkMeshData &Mesh, float Tolerance) const
{
    // Simple welding: combine vertices within tolerance
    // TODO: add this optimization later. This would involve creating a hash grid and merging nearby vertices
}