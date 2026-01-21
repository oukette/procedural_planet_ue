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
            const int SampleCount = Resolution + 1;
            const int TotalVoxels = SampleCount * SampleCount * SampleCount;

            TArray<float> LocalDensity;
            LocalDensity.SetNumUninitialized(TotalVoxels);

            const FVector CenterOffset = FVector(Resolution / 2.0f);

            // Optimization: Transform PlanetCenter to local space
            FVector LocalPlanetCenter = Transform.InverseTransformPosition(Center);

            // Generate density values - fill the entire array
            int32 Index = 0;
            for (int32 z = 0; z < SampleCount; z++)
            {
                for (int32 y = 0; y < SampleCount; y++)
                {
                    for (int32 x = 0; x < SampleCount; x++)
                    {
                        FVector LocalPos = (FVector(x, y, z) - CenterOffset) * Size;

                        // Optimization: Calculate distance in local space
                        float DistanceToCenter = FVector::Distance(LocalPos, LocalPlanetCenter);

                        // Shift surface slightly off-center (0.52f) to avoid integer harmonics with grid nodes.
                        // This prevents "circle of holes" artifacts where the sphere surface aligns perfectly with voxel layers.
                        float BaseDensity = (Radius - DistanceToCenter) / Size;

                        // LocalDensity[Index] = BaseDensity + Noise * Amp;
                        LocalDensity[Index] = BaseDensity;
                        Index++;
                    }
                }
            }

            // 2. Generate Mesh (Marching Cubes)
            TArray<FVector> Vertices;
            TArray<int32> Triangles;
            TArray<FVector> Normals;

            auto DensityAt = [&LocalDensity, SampleCount](int32 x, int32 y, int32 z) -> float
            {
                // Bounds check
                if (x < 0 || x >= SampleCount || y < 0 || y >= SampleCount || z < 0 || z >= SampleCount)
                {
                    return -1.0f;  // Return "outside" value for out of bounds
                }
                const int32 Idx = x + y * SampleCount + z * SampleCount * SampleCount;
                return LocalDensity[Idx];
            };

            auto VertexInterp = [&](const FVector &P1, const FVector &P2, float D1, float D2)
            {
                const float Epsilon = 1e-6f;
                float Denom = D1 - D2;

                if (FMath::Abs(Denom) < Epsilon)
                    return (P1 + P2) * 0.5f;

                float T = D1 / Denom;
                return P1 + T * (P2 - P1);
            };

            const FVector CornerOffsets[8] = {
                FVector(0, 0, 0), FVector(1, 0, 0), FVector(1, 1, 0), FVector(0, 1, 0), FVector(0, 0, 1), FVector(1, 0, 1), FVector(1, 1, 1), FVector(0, 1, 1)};

            const int EdgeIndex[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};

            // Process each cube in the grid
            for (int32 z = 0; z < Resolution; z++)
            {
                for (int32 y = 0; y < Resolution; y++)
                {
                    for (int32 x = 0; x < Resolution; x++)
                    {
                        // Get density and position for all 8 corners of this cube
                        float D[8];
                        FVector P[8];

                        for (int32 i = 0; i < 8; i++)
                        {
                            int32 ix = x + static_cast<int32>(CornerOffsets[i].X);
                            int32 iy = y + static_cast<int32>(CornerOffsets[i].Y);
                            int32 iz = z + static_cast<int32>(CornerOffsets[i].Z);

                            D[i] = DensityAt(ix, iy, iz);
                            P[i] = (FVector(ix, iy, iz) - CenterOffset) * Size;
                        }

                        // Determine cube configuration (which corners are inside/outside)
                        constexpr float DensityEpsilon = 1e-4f;

                        auto IsInside = [DensityEpsilon](float D) { return D < -DensityEpsilon; };

                        int32 CubeIndex = 0;
                        for (int32 i = 0; i < 8; i++)
                        {
                            if (IsInside(D[i]))
                                CubeIndex |= (1 << i);
                        }

                        // Skip if cube is entirely inside or outside
                        if (CubeIndex == 0 || CubeIndex == 255)
                            continue;

                        // Get edges that are intersected by the surface
                        int32 edges = EdgeTable[CubeIndex];
                        FVector EdgeVertex[12] = {FVector::ZeroVector};

                        for (int32 e = 0; e < 12; e++)
                        {
                            if (edges & (1 << e))
                            {
                                EdgeVertex[e] = VertexInterp(P[EdgeIndex[e][0]], P[EdgeIndex[e][1]], D[EdgeIndex[e][0]], D[EdgeIndex[e][1]]);
                            }
                        }

                        // Generate triangles for this cube
                        for (int32 i = 0; TriTable[CubeIndex][i] != -1; i += 3)
                        {
                            // Get vertices in LOCAL space
                            FVector V0_Local = EdgeVertex[TriTable[CubeIndex][i + 0]];
                            FVector V1_Local = EdgeVertex[TriTable[CubeIndex][i + 1]];
                            FVector V2_Local = EdgeVertex[TriTable[CubeIndex][i + 2]];

                            // Add vertices to array (in local space)
                            int32 Idx0 = Vertices.Add(V0_Local);
                            int32 Idx1 = Vertices.Add(V1_Local);
                            int32 Idx2 = Vertices.Add(V2_Local);

                            // Add triangles with correct winding
                            Triangles.Add(Idx0);
                            Triangles.Add(Idx1);
                            Triangles.Add(Idx2);

                            // Compute normals in WORLD space, then transform to local
                            // Transform each vertex to world space
                            FVector V0_World = Transform.TransformPosition(V0_Local);
                            FVector V1_World = Transform.TransformPosition(V1_Local);
                            FVector V2_World = Transform.TransformPosition(V2_Local);

                            // Compute outward normals in world space
                            FVector N0_World = (V0_World - Center).GetSafeNormal();
                            FVector N1_World = (V1_World - Center).GetSafeNormal();
                            FVector N2_World = (V2_World - Center).GetSafeNormal();

                            // Transform normals to local space (direction only, no scale)
                            FVector N0_Local = Transform.InverseTransformVectorNoScale(N0_World);
                            FVector N1_Local = Transform.InverseTransformVectorNoScale(N1_World);
                            FVector N2_Local = Transform.InverseTransformVectorNoScale(N2_World);

                            // Add normals
                            Normals.Add(N0_Local);
                            Normals.Add(N1_Local);
                            Normals.Add(N2_Local);
                        }
                    }
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
