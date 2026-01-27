// Fill out your copyright notice in the Description page of Project Settings.

#include "VoxelChunk.h"
#include "MarchingCubesTables.h"
#include "Kismet/KismetMathLibrary.h"
#include "CubeSpherePlanet.h"  // Needed to access Planet queue
#include "Async/Async.h"
#include "DrawDebugHelpers.h"


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
              PlanetDensityGenerator::DensityConfig DensityConfig;
              DensityConfig.PlanetRadius = planetRadius;
              DensityConfig.NoiseAmplitude = noiseAmp;
              DensityConfig.NoiseFrequency = noiseFreq;
              DensityConfig.NoiseOctaves = noiseOctaves;
              DensityConfig.NoiseLacunarity = noiseLacunarity;
              DensityConfig.NoisePersistence = noisePersistence;
              DensityConfig.Seed = seed;
              DensityConfig.VoxelSize = voxelSize;

              PlanetDensityGenerator DensityGen(DensityConfig);

              // 1. Generate Density
              PlanetDensityGenerator::GenData GenData = DensityGen.GenerateDensityField(resolution, fNormal, fRight, fUp, uvMin, uvMax);

              // 2. Generate Mesh
              FChunkMeshData MeshData = GenerateMeshFromDensity(
                  GenData, resolution, CapturedChunkTransform, CapturedPlanetTransform, fNormal, fRight, fUp, uvMin, uvMax, lodLevel, DensityGen);

              // 3. Apply to Main Thread
              AsyncTask(ENamedThreads::GameThread,
                        [weakThis,
                         MeshData,
                         resolution,
                         planetRadius,
                         voxelSize,
                         fNormal,
                         fRight,
                         fUp,
                         uvMin,
                         uvMax,
                         CapturedPlanetTransform]()
                        {
                            if (AVoxelChunk *Chunk = weakThis.Get())
                            {
                                // Store data and request update from Planet
                                Chunk->GeneratedMeshData = MeshData;

                                // --- DEBUG VISUALIZATION START ---
                                if (Chunk->GetWorld())
                                {
                                    // 1. Visualize Spikes: Check for vertices that are excessively far from chunk origin
                                    // A chunk shouldn't really be larger than its physical size * sqrt(3) plus some padding.
                                    const float MaxBoundsSq = FMath::Square(resolution * voxelSize * 3.0f);

                                    for (const FVector &Vert : MeshData.Vertices)
                                    {
                                        if (Vert.SizeSquared() > MaxBoundsSq)
                                        {
                                            // Draw a RED line to the spiked vertex
                                            FVector WorldVert = Chunk->GetActorTransform().TransformPosition(Vert);
                                            DrawDebugLine(Chunk->GetWorld(), Chunk->GetActorLocation(), WorldVert, FColor::Red, false, 5.0f, 0, 8.0f);
                                        }
                                    }

                                    // 2. Visualize Expected Corners: Draw where the chunk corners SHOULD be in World Space
                                    PlanetDensityGenerator::DensityConfig DebugConfig;
                                    DebugConfig.PlanetRadius = planetRadius;
                                    DebugConfig.VoxelSize = voxelSize;
                                    PlanetDensityGenerator DebugGen(DebugConfig);

                                    // Check Min (0,0,0) and Max (Res,Res,Res) corners
                                    int32 Corners[] = {0, resolution};
                                    for (int32 z : Corners)
                                    {
                                        for (int32 y : Corners)
                                        {
                                            for (int32 x : Corners)
                                            {
                                                FVector PlanetRel = DebugGen.GetProjectedPosition(x, y, z, resolution, fNormal, fRight, fUp, uvMin, uvMax);
                                                FVector WorldPos = CapturedPlanetTransform.TransformPosition(PlanetRel);
                                                DrawDebugPoint(Chunk->GetWorld(), WorldPos, 10.0f, FColor::Green, false, 5.0f);
                                            }
                                        }
                                    }

                                    // 3. Visualize Chunk Bounding Box
                                    // This shows the volume the mesh is supposed to occupy in world space.
                                    const FVector LocalBoxCenter = FVector(resolution * voxelSize / 2.0f);
                                    const FVector BoxExtent = FVector(resolution * voxelSize / 2.0f);
                                    const FVector WorldBoxCenter = Chunk->GetActorTransform().TransformPosition(LocalBoxCenter);
                                    DrawDebugBox(Chunk->GetWorld(),
                                                 WorldBoxCenter,
                                                 BoxExtent,
                                                 Chunk->GetActorTransform().GetRotation(),
                                                 FColor::Orange,
                                                 false,
                                                 5.0f,
                                                 0,
                                                 8.0f);
                                }
                                // --- DEBUG VISUALIZATION END ---

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


FChunkMeshData AVoxelChunk::GenerateMeshFromDensity(const PlanetDensityGenerator::GenData &GenData, int32 Resolution, FTransform CapturedChunkTransform,
                                                    FTransform CapturedPlanetTransform, const FVector &FaceNormal, const FVector &FaceRight,
                                                    const FVector &FaceUp, const FVector2D &UVMin, const FVector2D &UVMax, int32 LODLevel,
                                                    const PlanetDensityGenerator &DensityGenerator)
{
    FChunkMeshData MeshData;

    // Use the SampleCount from the generated data. This is the source of truth for the grid dimensions.
    const int32 SampleCount = GenData.SampleCount;
    if (SampleCount <= 1)
    {
        return MeshData; // Return empty mesh if data is invalid or has no volume
    }

    // Automatic Debug Colors for multiple LODs. Green (LOD0) -> Yellow -> Orange -> Red...
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

    // Use the LOD level as an index, with a fallback to white if out of bounds.
    FColor DebugColor = (LODLevel >= 0 && LODLevel < LODColors.Num()) ? LODColors[LODLevel] : FColor::White;

    // Pre-calculate Local Planet Center for normal direction check
    FVector LocalPlanetCenter = CapturedChunkTransform.InverseTransformPosition(CapturedPlanetTransform.GetLocation());

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
                    D[i] = GenData.Densities[ix + iy * SampleCount + iz * SampleCount * SampleCount];

                    // Calculate Warped Position in Planet Local Space (Vector from Planet Center)
                    FVector PlanetRelPos = GenData.Positions[ix + iy * SampleCount + iz * SampleCount * SampleCount];

                    // 1. Transform to World Space (Apply Planet Rotation/Location)
                    FVector WorldPos = CapturedPlanetTransform.TransformPosition(PlanetRelPos);

                    // 2. Convert to Chunk Actor Local Space for the mesh
                    P[i] = CapturedChunkTransform.InverseTransformPosition(WorldPos);

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
                    // 1. Get Chunk-Local Vertices (your existing code)
                    FVector V[3];
                    V[0] = EdgeVertex[TriTable[CubeIndex][i]];
                    V[1] = EdgeVertex[TriTable[CubeIndex][i + 1]];
                    V[2] = EdgeVertex[TriTable[CubeIndex][i + 2]];

                    for (int j = 0; j < 3; j++)
                    {
                        // 2. Add vertex and index
                        int32 Index = MeshData.Vertices.Add(V[j]);
                        MeshData.Triangles.Add(Index);

                        // 3. TRANSFORM MATH: Get the normal correctly
                        // 3A. Convert Chunk-Local vertex to World Space
                        FVector WorldPos = CapturedChunkTransform.TransformPosition(V[j]);

                        // 3B. Convert World Space to Planet-Local Space (where the generator lives)
                        FVector PlanetLocalPos = CapturedPlanetTransform.InverseTransformPosition(WorldPos);

                        // 3C. Get the normal from the gradient in Planet Space
                        FVector PlanetNormal = DensityGenerator.GetNormalAtPos(PlanetLocalPos);

                        // 3D. Convert the Normal vector back to Chunk-Local Space
                        // We use TransformVector/InverseTransformVector for directions (ignoring scale/translation)
                        FVector WorldNormal = CapturedPlanetTransform.TransformVector(PlanetNormal);
                        FVector ChunkLocalNormal = CapturedChunkTransform.InverseTransformVector(WorldNormal);

                        MeshData.Normals.Add(ChunkLocalNormal.GetSafeNormal());
                        MeshData.Colors.Add(DebugColor);  // Ensure mesh is colored
                    }
                }
            }
        }
    }
    return MeshData;
}
