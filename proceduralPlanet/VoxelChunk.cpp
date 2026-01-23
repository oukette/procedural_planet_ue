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
    CurrentLOD = 0;
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
    int32 lodLevel = CurrentLOD;
    float planetRadius = PlanetRadius;
    FVector planetCenter = PlanetCenter;

    // Capture projection params
    FVector fNormal = FaceNormal;
    FVector fRight = FaceRight;
    FVector fUp = FaceUp;
    FVector2D uvMin = ChunkUVMin;
    FVector2D uvMax = ChunkUVMax;

    FTransform transform = GetActorTransform();  // Capture transform on main thread
    TWeakObjectPtr<AVoxelChunk> weakThis(this);  // Weak pointer for safety

    // Run on background thread
    Async(EAsyncExecution::ThreadPool,
          [weakThis, resolution, voxelSize, planetRadius, planetCenter, transform, lodLevel, fNormal, fRight, fUp, uvMin, uvMax]()
          {
              // Optimization: Transform PlanetCenter to local space
              FVector LocalPlanetCenter = transform.InverseTransformPosition(planetCenter);

              // 1. Generate Density
              TArray<float> LocalDensity =
                  GenerateDensityField(resolution, voxelSize, planetRadius, LocalPlanetCenter, transform, fNormal, fRight, fUp, uvMin, uvMax);

              // 2. Generate Mesh
              FChunkMeshData MeshData = GenerateMeshFromDensity(
                  LocalDensity, resolution, voxelSize, planetRadius, LocalPlanetCenter, transform, fNormal, fRight, fUp, uvMin, uvMax, lodLevel);

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


void AVoxelChunk::UploadMesh(UMaterialInterface *MaterialToApply)
{
    ProceduralMesh->ClearAllMeshSections();
    ProceduralMesh->CreateMeshSection(0,
                                      GeneratedMeshData.Vertices,
                                      GeneratedMeshData.Triangles,
                                      GeneratedMeshData.Normals,
                                      TArray<FVector2D>(),
                                      GeneratedMeshData.Colors,
                                      TArray<FProcMeshTangent>(),
                                      bEnableCollision);

    if (MaterialToApply)
    {
        ProceduralMesh->SetMaterial(0, MaterialToApply);
    }

    // Clear data to free memory
    GeneratedMeshData.Vertices.Empty();
    GeneratedMeshData.Triangles.Empty();
    GeneratedMeshData.Normals.Empty();
    GeneratedMeshData.Colors.Empty();
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


void AVoxelChunk::UpdateChunkLOD(int32 NewLOD, int32 NewResolution, float NewVoxelSize)
{
    if (CurrentLOD != NewLOD || VoxelResolution != NewResolution || !FMath::IsNearlyEqual(VoxelSize, NewVoxelSize))
    {
        CurrentLOD = NewLOD;
        VoxelResolution = NewResolution;
        VoxelSize = NewVoxelSize;
        GenerateChunkAsync();
    }
}


// Helper to calculate the exact world position of a voxel grid point using Spherified Cube mapping
static FVector GetProjectedPosition(int32 x, int32 y, int32 z, int32 Resolution, float VoxelSize, float PlanetRadius, 
                                    const FVector& FaceNormal, const FVector& FaceRight, const FVector& FaceUp, 
                                    const FVector2D& UVMin, const FVector2D& UVMax)
{
    // 1. Calculate normalized U, V for this specific voxel (0.0 to 1.0 within the chunk)
    float uPct = x / (float)Resolution;
    float vPct = y / (float)Resolution;

    // 2. Map to the Face U, V (-1.0 to 1.0 space of the whole cube face)
    float u = FMath::Lerp(UVMin.X, UVMax.X, uPct);
    float v = FMath::Lerp(UVMin.Y, UVMax.Y, vPct);

    // 3. Point on Cube
    FVector PointOnCube = FaceNormal + FaceRight * u + FaceUp * v;

    // 4. Spherify (using the same logic as Planet)
    FVector SphereDir = ACubeSpherePlanet::GetSpherifiedCubePoint(PointOnCube);

    // 5. Altitude (Z is radial height). Center Z around 0 (surface) or offset as needed.
    // Here we assume Z=Resolution/2 is the "surface" level to allow digging/building up.
    float Altitude = (z - Resolution / 2.0f) * VoxelSize;

    return SphereDir * (PlanetRadius + Altitude);
}


TArray<float> AVoxelChunk::GenerateDensityField(int32 Resolution, float VoxelSize, float PlanetRadius, const FVector &LocalPlanetCenter, const FTransform& Transform, const FVector& FaceNormal, const FVector& FaceRight, const FVector& FaceUp, const FVector2D& UVMin, const FVector2D& UVMax)
{
    const int SampleCount = Resolution + 1;
    const int TotalVoxels = SampleCount * SampleCount * SampleCount;
    TArray<float> LocalDensity;
    LocalDensity.SetNumUninitialized(TotalVoxels);

    int32 Index = 0;

    for (int32 z = 0; z < SampleCount; z++)
    {
        for (int32 y = 0; y < SampleCount; y++)
        {
            for (int32 x = 0; x < SampleCount; x++)
            {
                // Calculate the warped position in World Space
                FVector WorldPos = GetProjectedPosition(x, y, z, Resolution, VoxelSize, PlanetRadius, FaceNormal, FaceRight, FaceUp, UVMin, UVMax);
                
                // Transform to Local Space for density calculation relative to center (if needed)
                // But density is usually based on WorldPos (for noise) or Distance to Center.
                // Distance to Planet Center is simple:
                float DistToPlanetCenter = WorldPos.Size();

                // Simple sphere density
                LocalDensity[Index++] = (PlanetRadius - DistToPlanetCenter) / VoxelSize;
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


FChunkMeshData AVoxelChunk::GenerateMeshFromDensity(const TArray<float> &Density, int32 Resolution, float VoxelSize, float PlanetRadius, const FVector &LocalPlanetCenter,
                                                    const FTransform &Transform, const FVector &FaceNormal, const FVector &FaceRight, const FVector &FaceUp,
                                                    const FVector2D &UVMin, const FVector2D &UVMax, int32 LODLevel)
{
    FChunkMeshData MeshData;
    const int SampleCount = Resolution + 1;

    // Automatic Debug Colors for multiple LODs. Green (LOD0) -> Yellow -> Orange -> Red...
    const static TArray<FColor> LODColors = {FColor::Green,
                                             FColor::Yellow,
                                             FColor(255, 165, 0),  // Orange
                                             FColor::Red,
                                             FColor::Magenta,
                                             FColor::Cyan};

    // Use the LOD level as an index, with a fallback to white if out of bounds.
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

                    // Inline density access
                    D[i] = Density[ix + iy * SampleCount + iz * SampleCount * SampleCount];
                    
                    // Calculate Warped Position
                    FVector WorldPos = GetProjectedPosition(ix, iy, iz, Resolution, VoxelSize, PlanetRadius, FaceNormal, FaceRight, FaceUp, UVMin, UVMax);
                    
                    // Convert to Actor Local Space for the mesh
                    P[i] = Transform.InverseTransformPosition(WorldPos);

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

                    // Improved Normal Calculation: Face Normals
                    // Using the sphere normal ((V - Center)) ignores terrain noise/features.
                    // We calculate the cross product of the triangle edges for correct flat shading.
                    FVector TriNormal = FVector::CrossProduct(V1 - V0, V2 - V0).GetSafeNormal();

                    // Ensure normal points outwards from planet center
                    if ((TriNormal | (V0 - LocalPlanetCenter)) < 0.0f)
                    {
                        TriNormal *= -1.0f;
                    }

                    MeshData.Normals.Add(TriNormal);
                    MeshData.Normals.Add(TriNormal);
                    MeshData.Normals.Add(TriNormal);

                    MeshData.Colors.Add(DebugColor);
                    MeshData.Colors.Add(DebugColor);
                    MeshData.Colors.Add(DebugColor);
                }
            }
        }
    }
    return MeshData;
}
