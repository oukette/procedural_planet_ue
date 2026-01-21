// Fill out your copyright notice in the Description page of Project Settings.

#include "VoxelChunk.h"
#include "MarchingCubesTables.h"
#include "Kismet/KismetMathLibrary.h"
#include "CubeSpherePlanet.h"  // Needed to access Planet queue
#include "Async/Async.h"


// Sets default values
AVoxelChunk::AVoxelChunk()
{
    // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
    PrimaryActorTick.bCanEverTick = false;

    ProceduralMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProceduralMesh"));
    ProceduralMesh->bUseAsyncCooking = true;  // Critical for performance if collision is enabled
    RootComponent = ProceduralMesh;

    // Sensible defaults
    VoxelResolution = 32;
    VoxelSize = 100.f;
    PlanetRadius = 10000.f;
    PlanetCenter = FVector(0, 0, 0);
    NoiseAmplitude = 350.f;
    NoiseFrequency = 0.0005f;
    Seed = 1337;
    bEnableCollision = false;
}


void AVoxelChunk::OnConstruction(const FTransform &Transform) { Super::OnConstruction(Transform); }


void AVoxelChunk::GenerateChunkAsync()
{
    // Capture parameters by value for thread safety
    int32 resolution = VoxelResolution;
    float voxelSize = VoxelSize;
    float planetRadius = PlanetRadius;
    FVector planetCenter = PlanetCenter;
    FTransform transform = GetActorTransform();  // Capture transform on main thread
    TWeakObjectPtr<AVoxelChunk> weakThis(this);  // Weak pointer for safety

    // Run on background thread
    Async(EAsyncExecution::ThreadPool,
          [weakThis, resolution, voxelSize, planetRadius, planetCenter, transform]()
          {
              // Optimization: Transform PlanetCenter to local space
              FVector LocalPlanetCenter = transform.InverseTransformPosition(planetCenter);

              // 1. Generate Density
              TArray<float> LocalDensity = GenerateDensityField(resolution, voxelSize, planetRadius, LocalPlanetCenter);

              // 2. Generate Mesh
              FChunkMeshData MeshData = GenerateMeshFromDensity(LocalDensity, resolution, voxelSize, LocalPlanetCenter);

              // 3. Apply to Main Thread
              AsyncTask(ENamedThreads::GameThread,
                        [weakThis, MeshData]()
                        {
                            if (AVoxelChunk *Chunk = weakThis.Get())
                            {
                                // Store data and request update from Planet
                                Chunk->GeneratedMeshData = MeshData;

                                if (ACubeSpherePlanet *Planet = Cast<ACubeSpherePlanet>(Chunk->GetOwner()))
                                {
                                    Planet->OnChunkGenerationFinished(Chunk);
                                }
                            }
                        });
          });
}


void AVoxelChunk::UploadMesh()
{
    ProceduralMesh->ClearAllMeshSections();
    ProceduralMesh->CreateMeshSection(0,
                                      GeneratedMeshData.Vertices,
                                      GeneratedMeshData.Triangles,
                                      GeneratedMeshData.Normals,
                                      TArray<FVector2D>(),
                                      TArray<FColor>(),
                                      TArray<FProcMeshTangent>(),
                                      bEnableCollision);

    // Clear data to free memory
    GeneratedMeshData.Vertices.Empty();
    GeneratedMeshData.Triangles.Empty();
    GeneratedMeshData.Normals.Empty();
}


void AVoxelChunk::SetCollisionEnabled(bool bEnabled)
{
    if (ProceduralMesh->bUseComplexAsSimpleCollision != bEnabled)
    {
        ProceduralMesh->bUseComplexAsSimpleCollision = bEnabled;
        ProceduralMesh->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
        // Note: Changing collision settings on ProcMesh might require a mesh update or
        // might not take effect immediately depending on engine version,
        // but SetCollisionEnabled is usually sufficient for runtime toggles.
    }
}


TArray<float> AVoxelChunk::GenerateDensityField(int32 Resolution, float VoxelSize, float PlanetRadius, const FVector &LocalPlanetCenter)
{
    const int SampleCount = Resolution + 1;
    const int TotalVoxels = SampleCount * SampleCount * SampleCount;
    TArray<float> LocalDensity;
    LocalDensity.SetNumUninitialized(TotalVoxels);

    const FVector CenterOffset = FVector(Resolution / 2.0f);
    int32 Index = 0;

    for (int32 z = 0; z < SampleCount; z++)
    {
        for (int32 y = 0; y < SampleCount; y++)
        {
            for (int32 x = 0; x < SampleCount; x++)
            {
                FVector LocalPos = (FVector(x, y, z) - CenterOffset) * VoxelSize;

                // Use double precision for distance calculation to prevent faceting/gaps on large planets
                double DX = (double)LocalPos.X - (double)LocalPlanetCenter.X;
                double DY = (double)LocalPos.Y - (double)LocalPlanetCenter.Y;
                double DZ = (double)LocalPos.Z - (double)LocalPlanetCenter.Z;
                float DistanceToCenter = (float)FMath::Sqrt(DX * DX + DY * DY + DZ * DZ);

                // Shift surface slightly off-center logic preserved from original
                LocalDensity[Index++] = (PlanetRadius - DistanceToCenter) / VoxelSize;
            }
        }
    }
    return LocalDensity;
}


FVector AVoxelChunk::VertexInterp(const FVector &P1, const FVector &P2, float D1, float D2)
{
    const float Epsilon = 1e-6f;
    float Denom = D1 - D2;
    if (FMath::Abs(Denom) < Epsilon)
    {
        return (P1 + P2) * 0.5f;
    }
    float T = D1 / Denom;
    return P1 + T * (P2 - P1);
}


AVoxelChunk::FChunkMeshData AVoxelChunk::GenerateMeshFromDensity(const TArray<float> &Density, int32 Resolution, float VoxelSize,
                                                                 const FVector &LocalPlanetCenter)
{
    FChunkMeshData MeshData;
    const int SampleCount = Resolution + 1;
    const FVector CenterOffset = FVector(Resolution / 2.0f);

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

                    // Inline density access
                    D[i] = Density[ix + iy * SampleCount + iz * SampleCount * SampleCount];
                    P[i] = (FVector(ix, iy, iz) - CenterOffset) * VoxelSize;

                    if (D[i] < -1e-4f)
                        CubeIndex |= (1 << i);
                }

                if (CubeIndex == 0 || CubeIndex == 255)
                    continue;

                int32 edges = EdgeTable[CubeIndex];
                FVector EdgeVertex[12];

                for (int32 e = 0; e < 12; e++)
                {
                    if (edges & (1 << e))
                    {
                        EdgeVertex[e] = VertexInterp(P[EdgeIndex[e][0]], P[EdgeIndex[e][1]], D[EdgeIndex[e][0]], D[EdgeIndex[e][1]]);
                    }
                }

                for (int32 i = 0; TriTable[CubeIndex][i] != -1; i += 3)
                {
                    FVector V0 = EdgeVertex[TriTable[CubeIndex][i]];
                    FVector V1 = EdgeVertex[TriTable[CubeIndex][i + 1]];
                    FVector V2 = EdgeVertex[TriTable[CubeIndex][i + 2]];

                    MeshData.Triangles.Add(MeshData.Vertices.Add(V0));
                    MeshData.Triangles.Add(MeshData.Vertices.Add(V1));
                    MeshData.Triangles.Add(MeshData.Vertices.Add(V2));

                    // Optimized Normal Calculation: (Vertex - LocalCenter) is the radial vector in local space
                    MeshData.Normals.Add((V0 - LocalPlanetCenter).GetSafeNormal());
                    MeshData.Normals.Add((V1 - LocalPlanetCenter).GetSafeNormal());
                    MeshData.Normals.Add((V2 - LocalPlanetCenter).GetSafeNormal());
                }
            }
        }
    }
    return MeshData;
}
