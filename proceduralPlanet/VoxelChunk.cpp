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
    int32 Resolution = VoxelResolution;
    float Size = VoxelSize;
    float Radius = PlanetRadius;
    FVector Center = PlanetCenter;
    float Amp = NoiseAmplitude;
    float Freq = NoiseFrequency;
    int32 S = Seed;
    FTransform Transform = GetActorTransform();  // Capture transform on main thread
    TWeakObjectPtr<AVoxelChunk> WeakThis(this);  // Weak pointer for safety

    // Run on background thread
    Async(
        EAsyncExecution::ThreadPool,
        [WeakThis, Resolution, Size, Radius, Center, Amp, Freq, S, Transform]()
        {
            // 1. Generate Density (Local Buffer)
            const int OverlapRes = Resolution + 1;
            const int NumVoxels = (OverlapRes + 1) * (OverlapRes + 1) * (OverlapRes + 1);

            TArray<float> LocalDensity;
            LocalDensity.SetNumUninitialized(NumVoxels);

            const FVector CenterOffset = FVector(Resolution / 2.0f);

            for (int z = 0; z <= OverlapRes; z++)
                for (int y = 0; y <= OverlapRes; y++)
                    for (int x = 0; x <= OverlapRes; x++)
                    {
                        const int Index = x + y * (OverlapRes + 1) + z * (OverlapRes + 1) * (OverlapRes + 1);

                        FVector LocalPos = (FVector(x, y, z) - CenterOffset) * Size;
                        FVector WorldPos = Transform.TransformPosition(LocalPos);

                        float DistanceToCenter = FVector::Distance(WorldPos, Center);
                        float BaseDensity = Radius - DistanceToCenter;
                        float Noise = FMath::PerlinNoise3D(WorldPos * Freq + FVector(S));

                        LocalDensity[Index] = BaseDensity + Noise * Amp;
                    }

            // 2. Generate Mesh (Marching Cubes)
            TArray<FVector> Vertices;
            TArray<int32> Triangles;
            TArray<FVector> Normals;

            auto DensityAt = [&](int x, int y, int z)
            {
                x = FMath::Clamp(x, 0, OverlapRes);
                y = FMath::Clamp(y, 0, OverlapRes);
                z = FMath::Clamp(z, 0, OverlapRes);
                return LocalDensity[x + y * (OverlapRes + 1) + z * (OverlapRes + 1) * (OverlapRes + 1)];
            };

            auto VertexInterp = [&](const FVector &P1, const FVector &P2, float D1, float D2)
            {
                if (FMath::IsNearlyZero(D1 - D2, 1e-8f))
                    return P1;
                float T = FMath::Clamp(D1 / (D1 - D2), 0.0f, 1.0f);
                return P1 + T * (P2 - P1);
            };

            const FVector CornerOffsets[8] = {
                FVector(0, 0, 0), FVector(1, 0, 0), FVector(1, 1, 0), FVector(0, 1, 0), FVector(0, 0, 1), FVector(1, 0, 1), FVector(1, 1, 1), FVector(0, 1, 1)};
            const int EdgeIndex[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

            for (int z = 0; z < Resolution; z++)
                for (int y = 0; y < Resolution; y++)
                    for (int x = 0; x < Resolution; x++)
                    {
                        float D[8];
                        FVector P[8];
                        for (int i = 0; i < 8; i++)
                        {
                            int ix = x + CornerOffsets[i].X;
                            int iy = y + CornerOffsets[i].Y;
                            int iz = z + CornerOffsets[i].Z;
                            D[i] = DensityAt(ix, iy, iz);
                            P[i] = (FVector(ix, iy, iz) - CenterOffset) * Size;
                        }

                        int CubeIndex = 0;
                        for (int i = 0; i < 8; i++)
                            if (D[i] > 0.f)
                                CubeIndex |= (1 << i);
                        if (CubeIndex == 0 || CubeIndex == 255)
                            continue;

                        int edges = EdgeTable[CubeIndex];
                        FVector EdgeVertex[12] = {FVector::ZeroVector};
                        for (int e = 0; e < 12; e++)
                        {
                            if (edges & (1 << e))
                            {
                                EdgeVertex[e] = VertexInterp(P[EdgeIndex[e][0]], P[EdgeIndex[e][1]], D[EdgeIndex[e][0]], D[EdgeIndex[e][1]]);
                            }
                        }

                        for (int i = 0; TriTable[CubeIndex][i] != -1; i += 3)
                        {
                            int v0 = Vertices.Add(EdgeVertex[TriTable[CubeIndex][i + 0]]);
                            int v1 = Vertices.Add(EdgeVertex[TriTable[CubeIndex][i + 1]]);
                            int v2 = Vertices.Add(EdgeVertex[TriTable[CubeIndex][i + 2]]);
                            Triangles.Add(v0);
                            Triangles.Add(v2);
                            Triangles.Add(v1);

                            // Calculate Normal
                            FVector Grad;
                            Grad.X = DensityAt(x + 1, y, z) - DensityAt(x - 1, y, z);
                            Grad.Y = DensityAt(x, y + 1, z) - DensityAt(x, y - 1, z);
                            Grad.Z = DensityAt(x, y, z + 1) - DensityAt(x, y, z - 1);
                            FVector N = (-Grad).GetSafeNormal();
                            Normals.Add(N);
                            Normals.Add(N);
                            Normals.Add(N);
                        }
                    }

            // 3. Apply to Main Thread
            AsyncTask(ENamedThreads::GameThread,
                      [WeakThis, Vertices, Triangles, Normals]()
                      {
                          if (AVoxelChunk *Chunk = WeakThis.Get())
                          {
                              // Store data and request update from Planet
                              Chunk->GeneratedMeshData.Vertices = Vertices;
                              Chunk->GeneratedMeshData.Triangles = Triangles;
                              Chunk->GeneratedMeshData.Normals = Normals;

                              if (ACubeSpherePlanet *Planet = Cast<ACubeSpherePlanet>(Chunk->GetOwner()))
                              {
                                  Planet->EnqueueChunkForMeshUpdate(Chunk);
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
