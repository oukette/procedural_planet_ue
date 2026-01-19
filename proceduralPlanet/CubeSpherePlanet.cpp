// Fill out your copyright notice in the Description page of Project Settings.


#include "CubeSpherePlanet.h"
#include "Kismet/KismetMathLibrary.h"


// Sets default values
ACubeSpherePlanet::ACubeSpherePlanet()
{
    // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
    PrimaryActorTick.bCanEverTick = false;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    // Defaults
    Seed = 1337;
    ChunksPerFace = 1;
    PlanetRadius = 5000.f;
    NoiseAmplitude = 500.f;
    NoiseFrequency = 0.0003f;
    VoxelResolution = 32;
    VoxelSize = 100.f;
}


void ACubeSpherePlanet::OnConstruction(const FTransform &Transform)
{
    Super::OnConstruction(Transform);

    GenerateVoxelChunks();
}


void ACubeSpherePlanet::GenerateVoxelChunks()
{
    // Clear existing chunks
    if (VoxelChunks.Num() > 0)
    {
        for (AVoxelChunk *Chunk : VoxelChunks)
        {
            if (Chunk && IsValid(Chunk))
            {
                Chunk->Destroy();
            }
        }
        VoxelChunks.Empty();
    }

    if (!GetWorld())
    {
        return;
    }

    // Define 6 cube faces - CORRECTED orientations
    struct FaceInfo
    {
            FVector Normal;  // Points outward from cube center
            FVector Right;   // Tangent direction 1
            FVector Up;      // Tangent direction 2
    };

    // Each face positioned along one axis, with proper right/up vectors
    FaceInfo Faces[6] = {// +X face (Right) - looking at +X, Y goes right, Z goes up
                         {FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1)},
                         // -X face (Left) - looking at -X, Y goes left, Z goes up
                         {FVector(-1, 0, 0), FVector(0, -1, 0), FVector(0, 0, 1)},
                         // +Y face (Forward) - looking at +Y, X goes left, Z goes up
                         {FVector(0, 1, 0), FVector(-1, 0, 0), FVector(0, 0, 1)},
                         // -Y face (Back) - looking at -Y, X goes right, Z goes up
                         {FVector(0, -1, 0), FVector(1, 0, 0), FVector(0, 0, 1)},
                         // +Z face (Top) - looking at +Z (down), X goes right, Y goes forward
                         {FVector(0, 0, 1), FVector(1, 0, 0), FVector(0, 1, 0)},
                         // -Z face (Bottom) - looking at -Z (up), X goes right, Y goes back
                         {FVector(0, 0, -1), FVector(1, 0, 0), FVector(0, -1, 0)}};

    const FVector PlanetCenterWorld = GetActorLocation();

    // Grid spans the face - normalized from -1 to +1
    float GridStep = 2.0f / ChunksPerFace;

    // For each cube face
    for (int faceIdx = 0; faceIdx < 6; faceIdx++)
    {
        FaceInfo face = Faces[faceIdx];

        // For each chunk in the face grid
        for (int gridY = 0; gridY < ChunksPerFace; gridY++)
        {
            for (int gridX = 0; gridX < ChunksPerFace; gridX++)
            {
                // Normalized position on face: -1 to +1, centered in each grid cell
                float u = -1.0f + (gridX + 0.5f) * GridStep;
                float v = -1.0f + (gridY + 0.5f) * GridStep;

                // 1. Calculate point on a unit cube face. This point represents the center of the chunk on the cube face.
                FVector pointOnUnitCube = face.Normal + face.Right * u + face.Up * v;

                // 2. Normalize to project onto a unit sphere. This gives the "up" direction for the chunk.
                FVector chunkUpDirection = pointOnUnitCube.GetSafeNormal();

                // 3. Scale by radius and offset by planet center to get the chunk's world position.
                // This places the center of the chunk volume on the sphere's surface.
                FVector chunkWorldPos = PlanetCenterWorld + chunkUpDirection * PlanetRadius;

                // 4. Determine chunk rotation to align it with the planet's curvature.
                // We use the face's original orientation vectors to create a stable orientation.
                FVector chunkRightDirection = FVector::CrossProduct(face.Up, chunkUpDirection).GetSafeNormal();
                FVector chunkForwardDirection = FVector::CrossProduct(chunkUpDirection, chunkRightDirection);
                FRotator chunkWorldRot = UKismetMathLibrary::MakeRotationFromAxes(chunkForwardDirection, chunkRightDirection, chunkUpDirection);

                // Use deferred spawning to set parameters before its construction script runs.
                FTransform ChunkTransform(chunkWorldRot, chunkWorldPos);
                AVoxelChunk *Chunk = GetWorld()->SpawnActorDeferred<AVoxelChunk>(AVoxelChunk::StaticClass(), ChunkTransform, this);

                if (Chunk)
                {
                    // Set chunk parameters BEFORE finishing spawn.
                    Chunk->VoxelResolution = VoxelResolution;
                    Chunk->VoxelSize = VoxelSize;
                    Chunk->PlanetRadius = PlanetRadius;
                    Chunk->PlanetCenter = PlanetCenterWorld;
                    Chunk->NoiseAmplitude = NoiseAmplitude;
                    Chunk->NoiseFrequency = NoiseFrequency;
                    Chunk->Seed = Seed;
                    Chunk->bGenerateMesh = true;  // Tell chunk to generate mesh in its OnConstruction.

                    // Finish spawning. This will call OnConstruction on the chunk with the correct parameters.
                    Chunk->FinishSpawning(ChunkTransform);

                    // Attach with SNAP to target - this converts world pos to relative
                    Chunk->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);

                    VoxelChunks.Add(Chunk);
                }
            }
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Generated %d chunks for planet radius %.1f"), VoxelChunks.Num(), PlanetRadius);
}