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

    // Capture noise parameters
    float noiseAmp = NoiseAmplitude;
    float noiseFreq = NoiseFrequency;
    int32 seed = Seed;

    // Capture projection params
    FVector fNormal = FaceNormal;
    FVector fRight = FaceRight;
    FVector fUp = FaceUp;
    FVector2D uvMin = ChunkUVMin;
    FVector2D uvMax = ChunkUVMax;

    FTransform chunkTransform = GetActorTransform();  // Capture transform on main thread
    
    // Capture Planet Transform to correctly calculate world positions relative to the planet
    FTransform planetTransform = FTransform::Identity;
    if (AActor* Owner = GetOwner())
    {
        planetTransform = Owner->GetActorTransform();
    }

    TWeakObjectPtr<AVoxelChunk> weakThis(this);  // Weak pointer for safety

    // Run on background thread
    Async(EAsyncExecution::ThreadPool,
          [weakThis,
           resolution,
           voxelSize,
           planetRadius,
           noiseAmp,
           noiseFreq,
           seed,
           planetTransform,
           chunkTransform,
           lodLevel,
           fNormal,
           fRight,
           fUp,
           uvMin,
           uvMax]()
          {
              // Create density generator with captured parameters
              PlanetDensityGenerator::DensityConfig DensityConfig;
              DensityConfig.PlanetRadius = planetRadius;
              DensityConfig.NoiseAmplitude = noiseAmp;
              DensityConfig.NoiseFrequency = noiseFreq;
              DensityConfig.Seed = seed;
              DensityConfig.VoxelSize = voxelSize;

              PlanetDensityGenerator DensityGen(DensityConfig);

              // 1. Generate Density
              TArray<float> LocalDensity = DensityGen.GenerateDensityField(resolution, fNormal, fRight, fUp, uvMin, uvMax);

              // 2. Generate Mesh
              FChunkMeshData MeshData = GenerateMeshFromDensity(LocalDensity, resolution, voxelSize, planetRadius, planetTransform, chunkTransform, 
                                                                fNormal, fRight, fUp, uvMin, uvMax, lodLevel, DensityGen);

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


FChunkMeshData AVoxelChunk::GenerateMeshFromDensity(const TArray<float> &Density, int32 Resolution, float VoxelSize, float PlanetRadius,
                                                    const FTransform &PlanetTransform, const FTransform &ChunkTransform, const FVector &FaceNormal,
                                                    const FVector &FaceRight, const FVector &FaceUp, const FVector2D &UVMin, const FVector2D &UVMax,
                                                    int32 LODLevel, const PlanetDensityGenerator &DensityGenerator)
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

    // Pre-calculate Local Planet Center for normal direction check
    FVector LocalPlanetCenter = ChunkTransform.InverseTransformPosition(PlanetTransform.GetLocation());

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

                    // Calculate Warped Position in Planet Local Space (Vector from Planet Center)
                    FVector PlanetRelPos = DensityGenerator.GetProjectedPosition(ix, iy, iz, Resolution, FaceNormal, FaceRight, FaceUp, UVMin, UVMax);

                    // 1. Transform to World Space (Apply Planet Rotation/Location)
                    FVector WorldPos = PlanetTransform.TransformPosition(PlanetRelPos);

                    // 2. Convert to Chunk Actor Local Space for the mesh
                    P[i] = ChunkTransform.InverseTransformPosition(WorldPos);

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
