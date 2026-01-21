// Fill out your copyright notice in the Description page of Project Settings.


#include "CubeSpherePlanet.h"
#include "Kismet/KismetMathLibrary.h"


// Sets default values
ACubeSpherePlanet::ACubeSpherePlanet()
{
    // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
    PrimaryActorTick.bCanEverTick = true;

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
    bEnableCollision = false;  // Default to false for performance
    bCastShadows = false;      // Default to false for performance
    ChunksToProcessPerFrame = 2;
}


void ACubeSpherePlanet::OnConstruction(const FTransform &Transform)
{
    Super::OnConstruction(Transform);

    GenerateVoxelChunks();
}


void ACubeSpherePlanet::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Process a limited number of chunks per frame to prevent lag spikes
    int32 ProcessedCount = 0;
    while (MeshUpdateQueue.Num() > 0 && ProcessedCount < ChunksToProcessPerFrame)
    {
        AVoxelChunk *Chunk = MeshUpdateQueue.Pop();  // LIFO is fine, or use RemoveAt(0) for FIFO
        if (Chunk && IsValid(Chunk))
        {
            Chunk->UploadMesh();
        }
        ProcessedCount++;
    }
}


bool ACubeSpherePlanet::ShouldTickIfViewportsOnly() const { return true; }


void ACubeSpherePlanet::EnqueueChunkForMeshUpdate(AVoxelChunk *Chunk) { MeshUpdateQueue.Add(Chunk); }


int32 ACubeSpherePlanet::CalculateAutoChunksPerFace() const
{
    // If auto-sizing is disabled, return the manual setting
    if (!bAutoChunkSizing)
    {
        return ChunksPerFace;
    }

    // 1. Face Width (Arc Length)
    // The arc length of a cube face projected on a sphere is exactly PI/2 * Radius.
    // This replaces the "EquatorToFaceRatio = 4.0" logic (Circumference / 4).
    float FaceArcLength = PlanetRadius * HALF_PI;

    // 2. Chunk Size
    // Calculate chunk physical size based on voxel settings
    float ChunkPhysicalSize = VoxelResolution * VoxelSize;

    // 3. Curvature Overlap (Adaptive)
    // We need more overlap when the chunk size is large relative to the planet radius.
    // Instead of a fixed scale (2000.0f), we derive it from ChunkPhysicalSize.
    // If chunks are physically larger, they diverge more, requiring a higher factor.
    const float BaseOverlapFactor = 1.1f;      // 10% base overlap for precision
    const float CurvatureSensitivity = 0.75f;  // Controls how much overlap is added per unit of relative curvature
    const float MinRadiusForCurvature = 100.0f;

    float CurvatureFactor = BaseOverlapFactor + ((ChunkPhysicalSize * CurvatureSensitivity) / FMath::Max(MinRadiusForCurvature, PlanetRadius));

    // 4. Final Calculation
    float NeededChunks = (FaceArcLength / ChunkPhysicalSize) * ChunkDensityFactor * CurvatureFactor;

    // Round up to ensure coverage, then clamp
    int32 CalculatedChunks = FMath::CeilToInt(NeededChunks);
    CalculatedChunks = FMath::Clamp(CalculatedChunks, MinChunksPerFace, MaxChunksPerFace);

    // Ensure it's at least 1 and a reasonable value
    return FMath::Max(1, CalculatedChunks);
}


void ACubeSpherePlanet::Destroyed()
{
    Super::Destroyed();

    // Clean up chunks when the planet itself is destroyed (e.g. deleting from scene or preview actor cleanup)
    for (AVoxelChunk *Chunk : VoxelChunks)
    {
        if (Chunk && IsValid(Chunk))
        {
            Chunk->Destroy();
        }
    }

    // Fallback cleanup for any attached actors not in the array (handles edge cases during editor interaction)
    TArray<AActor *> AttachedActors;
    GetAttachedActors(AttachedActors);
    for (AActor *Child : AttachedActors)
    {
        if (Child && Child->IsA(AVoxelChunk::StaticClass()))
        {
            Child->Destroy();
        }
    }
}


void ACubeSpherePlanet::GenerateVoxelChunks()
{
    // Clear pending updates from previous generation
    MeshUpdateQueue.Empty();

    // Fix: Robustly destroy all attached chunks to prevent duplication
    TArray<AActor *> AttachedActors;
    GetAttachedActors(AttachedActors);

    for (AActor *Child : AttachedActors)
    {
        // Only destroy VoxelChunks, in case you have other things attached
        if (Child && Child->IsA(AVoxelChunk::StaticClass()))
        {
            Child->Destroy();
        }
    }
    VoxelChunks.Empty();

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

    // --- ADAPTIVE LOGIC ---
    if (bAutoChunkSizing)
    {
        // 1. Adapt Voxel Size: Maintain relative smoothness (Radius ~ 150x VoxelSize)
        // This prevents "blocky" small planets and "noisy" large planets.
        float TargetVoxelSize = PlanetRadius / 150.0f;
        VoxelSize = FMath::Clamp(TargetVoxelSize, 25.0f, 400.0f);

        // 2. Adapt Resolution:
        // 32 is the sweet spot for Marching Cubes.
        // We drop to 16 only for very small planets to save performance.
        VoxelResolution = (PlanetRadius < 3000.0f) ? 16 : 32;
    }

    // Calculate chunks per face (auto or manual)
    int32 ActualChunksPerFace = bAutoChunkSizing ? CalculateAutoChunksPerFace() : ChunksPerFace;
    ChunksPerFace = ActualChunksPerFace;

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
                    Chunk->bEnableCollision = bEnableCollision;
                    Chunk->ProceduralMesh->SetCastShadow(bCastShadows);

                    // Finish spawning. This will call OnConstruction on the chunk with the correct parameters.
                    Chunk->FinishSpawning(ChunkTransform);

                    // Attach with SNAP to target - this converts world pos to relative
                    Chunk->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
                    Chunk->SetOwner(this);  // Ensure ownership for lifecycle management

                    // Trigger the async generation now that parameters are set
                    Chunk->GenerateChunkAsync();

                    VoxelChunks.Add(Chunk);
                }
            }
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Generated %d chunks for planet radius %.1f"), VoxelChunks.Num(), PlanetRadius);
}