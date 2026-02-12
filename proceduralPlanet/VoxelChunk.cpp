// Fill out your copyright notice in the Description page of Project Settings.

#include "VoxelChunk.h"
#include "MarchingCubesTables.h"
#include "PlanetGen/MeshGenerator.h"
#include "Kismet/KismetMathLibrary.h"
#include "PlanetGen/Planet.h"  // Needed to access Planet queue
#include "Async/Async.h"
#include "DrawDebugHelpers.h"
#include "PlanetGen/SimpleNoise.h"


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
    // Capture these by VALUE for the lambda
    FTransform CapturedChunkTransform = GetActorTransform();
    FTransform CapturedPlanetTransform = ParentPlanet ? ParentPlanet->GetActorTransform() : FTransform::Identity;

    // Capture parameters by value for thread safety
    int32 resolution = VoxelResolution;
    float voxelSize = VoxelSize;
    int32 lodLevel = CurrentLOD;
    float planetRadius = PlanetRadius;
    FVector planetCenter = PlanetCenter;

    // Capture noise parameters
    float noiseAmp = NoiseAmplitude;
    float noiseFreq = NoiseFrequency;
    int32 noiseOctaves = NoiseOctaves;
    float noiseLacunarity = NoiseLacunarity;
    float noisePersistence = NoisePersistence;
    int32 seed = Seed;

    // Capture projection params
    FVector fNormal = FaceNormal;
    FVector fRight = FaceRight;
    FVector fUp = FaceUp;
    FVector2D uvMin = ChunkUVMin;
    FVector2D uvMax = ChunkUVMax;

    TWeakObjectPtr<AVoxelChunk> weakThis(this);  // Weak pointer for safety

    // Run on background thread
    Async(EAsyncExecution::ThreadPool,
          [weakThis,
           resolution,
           voxelSize,
           planetRadius,
           noiseAmp,
           noiseFreq,
           noiseOctaves,
           noiseLacunarity,
           noisePersistence,
           seed,
           CapturedChunkTransform,
           CapturedPlanetTransform,
           lodLevel,
           fNormal,
           fRight,
           fUp,
           uvMin,
           uvMax]()
          {
              // Create density generator with captured parameters
              DensityGenerator::DensityConfig DensityConfig;
              DensityConfig.PlanetRadius = planetRadius;
              DensityConfig.NoiseAmplitude = noiseAmp;
              DensityConfig.NoiseFrequency = noiseFreq;
              DensityConfig.NoiseOctaves = noiseOctaves;
              DensityConfig.NoiseLacunarity = noiseLacunarity;
              DensityConfig.NoisePersistence = noisePersistence;
              DensityConfig.Seed = seed;
              DensityConfig.VoxelSize = voxelSize;

              SimpleNoise LocalNoise;  // Temporary stack allocation
              DensityGenerator DensityGen(DensityConfig, &LocalNoise);

              // 1. Generate Density
              GenData GenData = DensityGen.GenerateDensityField(resolution, fNormal, fRight, fUp, uvMin, uvMax);

              // 2. Generate Mesh
              FChunkMeshData MeshData = MeshGenerator::GenerateMesh(GenData, resolution, CapturedChunkTransform, CapturedPlanetTransform, lodLevel, DensityGen);

              // 3. Apply to Main Thread
              AsyncTask(ENamedThreads::GameThread,
                        [weakThis, MeshData, resolution, planetRadius, voxelSize, fNormal, fRight, fUp, uvMin, uvMax, CapturedPlanetTransform]()
                        {
                            if (AVoxelChunk *Chunk = weakThis.Get())
                            {
                                // Store data and request update from Planet
                                Chunk->GeneratedMeshData = MeshData;

                                if (APlanet *Planet = Cast<APlanet>(Chunk->GetOwner()))
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
