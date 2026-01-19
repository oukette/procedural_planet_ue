// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelChunk.h"
#include "MarchingCubesTables.h"
#include "Kismet/KismetMathLibrary.h"

// Sets default values
AVoxelChunk::AVoxelChunk()
{
    // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
    PrimaryActorTick.bCanEverTick = false;

    ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMesh"));
    RootComponent = ProceduralMesh;

    // Sensible defaults
    VoxelResolution = 32;
    VoxelSize = 100.f;
    PlanetRadius = 10000.f;
    PlanetCenter = FVector(0, 0, 0);
    NoiseAmplitude = 350.f;
    NoiseFrequency = 0.0005f;
    Seed = 1337;

    bGenerateMesh = false;
}


void AVoxelChunk::OnConstruction(const FTransform &Transform)
{
    Super::OnConstruction(Transform);

    GenerateDensity();
    if (bGenerateMesh)
    {
        GenerateMesh();
    }
}


void AVoxelChunk::GenerateDensity()
{
    // ADD +1 voxel overlap for seamless chunk stitching
    const int OverlapRes = VoxelResolution + 1;
    const int Size = (OverlapRes + 1) * (OverlapRes + 1) * (OverlapRes + 1);
    VoxelDensity.SetNum(Size);

    for (int z = 0; z <= OverlapRes; z++)
        for (int y = 0; y <= OverlapRes; y++)
            for (int x = 0; x <= OverlapRes; x++)
            {
                const int Index = x + y * (OverlapRes + 1) + z * (OverlapRes + 1) * (OverlapRes + 1);

                // Local voxel position, centered around the actor's origin
                const FVector CenterOffset = FVector(VoxelResolution / 2.0f);
                FVector LocalPos = (FVector(x, y, z) - CenterOffset) * VoxelSize;
                FVector WorldPos = GetActorTransform().TransformPosition(LocalPos);

                // Base spherical planet shape
                float DistanceToCenter = FVector::Distance(WorldPos, PlanetCenter);
                float BaseDensity = PlanetRadius - DistanceToCenter;

                // Terrain noise (surface deformation)
                float Noise = FMath::PerlinNoise3D(WorldPos * NoiseFrequency + FVector(Seed));

                float Density = BaseDensity + Noise * NoiseAmplitude;
                VoxelDensity[Index] = Density;
            }
}


void AVoxelChunk::GenerateMesh()
{
    ProceduralMesh->ClearAllMeshSections();

    const int OverlapRes = VoxelResolution + 1;

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;

    auto DensityAt = [&](int x, int y, int z)
    {
        x = FMath::Clamp(x, 0, OverlapRes);
        y = FMath::Clamp(y, 0, OverlapRes);
        z = FMath::Clamp(z, 0, OverlapRes);
        int idx = x + y * (OverlapRes + 1) + z * (OverlapRes + 1) * (OverlapRes + 1);
        return VoxelDensity[idx];
    };

    auto VertexInterp = [&](const FVector &P1, const FVector &P2, float D1, float D2)
    {
        float T = D1 / (D1 - D2);
        return P1 + T * (P2 - P1);
    };

    const FVector CornerOffsets[8] = {
        FVector(0, 0, 0), FVector(1, 0, 0), FVector(1, 1, 0), FVector(0, 1, 0), FVector(0, 0, 1), FVector(1, 0, 1), FVector(1, 1, 1), FVector(0, 1, 1)};

    const int EdgeIndex[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

    for (int z = 0; z < VoxelResolution; z++)
        for (int y = 0; y < VoxelResolution; y++)
            for (int x = 0; x < VoxelResolution; x++)
            {
                float D[8];
                FVector P[8];

                for (int i = 0; i < 8; i++)
                {
                    int ix = x + CornerOffsets[i].X;
                    int iy = y + CornerOffsets[i].Y;
                    int iz = z + CornerOffsets[i].Z;

                    D[i] = DensityAt(ix, iy, iz);
                    // Calculate vertex positions in local space, centered around the chunk's origin
                    const FVector CenterOffset = FVector(VoxelResolution / 2.0f);
                    P[i] = (FVector(ix, iy, iz) - CenterOffset) * VoxelSize;
                }

                int CubeIndex = 0;
                for (int i = 0; i < 8; i++)
                    if (D[i] > 0.f)
                        CubeIndex |= (1 << i);

                if (CubeIndex == 0 || CubeIndex == 255)
                    continue;

                int edges = EdgeTable[CubeIndex];
                FVector EdgeVertex[12];

                for (int e = 0; e < 12; e++)
                {
                    if (edges & (1 << e))
                    {
                        int a = EdgeIndex[e][0];
                        int b = EdgeIndex[e][1];
                        EdgeVertex[e] = VertexInterp(P[a], P[b], D[a], D[b]);
                    }
                }

                for (int i = 0; TriTable[CubeIndex][i] != -1; i += 3)
                {
                    // Add vertices for one triangle with standard winding order.
                    int v0 = Vertices.Add(EdgeVertex[TriTable[CubeIndex][i + 0]]);
                    int v1 = Vertices.Add(EdgeVertex[TriTable[CubeIndex][i + 1]]);
                    int v2 = Vertices.Add(EdgeVertex[TriTable[CubeIndex][i + 2]]);

                    Triangles.Add(v0);
                    Triangles.Add(v2);  // Swap to correct winding for outward-facing normals
                    Triangles.Add(v1);

                    // Gradient-based normal from density field
                    auto SampleDensity = [&](int sx, int sy, int sz)
                    {
                        sx = FMath::Clamp(sx, 0, OverlapRes);
                        sy = FMath::Clamp(sy, 0, OverlapRes);
                        sz = FMath::Clamp(sz, 0, OverlapRes);
                        return DensityAt(sx, sy, sz);
                    };

                    FVector Grad;
                    Grad.X = SampleDensity(x + 1, y, z) - SampleDensity(x - 1, y, z);
                    Grad.Y = SampleDensity(x, y + 1, z) - SampleDensity(x, y - 1, z);
                    Grad.Z = SampleDensity(x, y, z + 1) - SampleDensity(x, y, z - 1);

                    FVector N = (-Grad).GetSafeNormal();

                    Normals.Add(N);
                    Normals.Add(N);
                    Normals.Add(N);
                }
            }

    TArray<FVector2D> UVs;
    UVs.Init(FVector2D::ZeroVector, Vertices.Num());
    TArray<FProcMeshTangent> Tangents;
    Tangents.Init(FProcMeshTangent(1, 0, 0), Vertices.Num());

    ProceduralMesh->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, TArray<FColor>(), Tangents, true);
    UE_LOG(LogTemp, Warning, TEXT("Chunk generated %d vertices, %d triangles"), Vertices.Num(), Triangles.Num() / 3);
}


void AVoxelChunk::Initialize(int32 InVoxelResolution, float InVoxelSize, float InPlanetRadius, FVector InPlanetCenter, float InNoiseAmplitude,
                             float InNoiseFrequency, int32 InSeed)
{
    VoxelResolution = InVoxelResolution;
    VoxelSize = InVoxelSize;
    PlanetRadius = InPlanetRadius;
    PlanetCenter = InPlanetCenter;
    NoiseAmplitude = InNoiseAmplitude;
    NoiseFrequency = InNoiseFrequency;
    Seed = InSeed;

    bGenerateMesh = true;

    // Manually trigger generation
    GenerateDensity();
    GenerateMesh();
}