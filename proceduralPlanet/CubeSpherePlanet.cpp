// Fill out your copyright notice in the Description page of Project Settings.

#include "CubeSpherePlanet.h"
#include "VoxelChunk.h"
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

    // Staggered generation defaults
    bGenerateOnBeginPlay = true;
    ChunksToSpawnPerFrame = 8;
    MaxConcurrentChunkGenerations = 32;
    ActiveGenerationTasks = 0;
    RenderDistance = 25000.0f;
    CollisionDistance = 8000.0f;
}


void ACubeSpherePlanet::OnConstruction(const FTransform &Transform)
{
    Super::OnConstruction(Transform);
    // IMPORTANT: We no longer generate here by default as it causes severe editor freezes
    // on large planets. Use the "Generate Planet" button in the details panel or enable
    // bGenerateOnBeginPlay for runtime generation.
}


void ACubeSpherePlanet::BeginPlay()
{
    Super::BeginPlay();
    if (bGenerateOnBeginPlay)
    {
        GeneratePlanet();
    }
}


void ACubeSpherePlanet::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    UpdateLODAndStreaming();
    ProcessSpawnQueue();
    ProcessMeshUpdateQueue();
}


void ACubeSpherePlanet::UpdateLODAndStreaming()
{
    // --- LOD & Streaming Logic ---
    // We check distances to manage spawning and collision.
    // Optimization: In a full production game, you might time-slice this loop
    // (check 100 chunks per frame) instead of checking all every frame.

    FVector ObserverPos = GetObserverPosition();
    float RenderDistSq = RenderDistance * RenderDistance;
    float CollisionDistSq = CollisionDistance * CollisionDistance;

    for (int32 i = 0; i < ChunkInfos.Num(); i++)
    {
        FChunkInfo &Info = ChunkInfos[i];
        float DistSq = FVector::DistSquared(Info.WorldLocation, ObserverPos);

        // 1. Spawning / Despawning
        if (DistSq < RenderDistSq)
        {
            // Should be visible
            if (Info.ActiveChunk == nullptr && !Info.bPendingSpawn)
            {
                ChunkSpawnQueue.Add(i);
                Info.bPendingSpawn = true;
            }
        }
        else
        {
            // Should be hidden (save RAM)
            if (Info.ActiveChunk)
            {
                Info.ActiveChunk->Destroy();
                Info.ActiveChunk = nullptr;
            }
            // If it was waiting to spawn, cancel it (simplistic handling)
            // Note: Removing from array is slow, so we just let it spawn and die next frame,
            // or handle it in the spawn loop below.
        }

        // 2. Collision LOD
        if (Info.ActiveChunk)
        {
            bool bShouldCollide = (DistSq < CollisionDistSq) && bEnableCollision;
            Info.ActiveChunk->SetCollisionEnabled(bShouldCollide);
        }
    }
}


void ACubeSpherePlanet::ProcessSpawnQueue()
{
    // Spawn new chunk actors if we have capacity in the async pipeline
    int32 SpawnedThisFrame = 0;
    const FVector PlanetCenterWorld = GetActorLocation();

    while (ChunkSpawnQueue.Num() > 0 && ActiveGenerationTasks < MaxConcurrentChunkGenerations && SpawnedThisFrame < ChunksToSpawnPerFrame)
    {
        // Get index of the chunk to spawn
        int32 ChunkIndex = ChunkSpawnQueue.Pop(false);

        // Calculate RenderDistSq locally since we are in a new method
        float RenderDistSq = RenderDistance * RenderDistance;
        FVector ObserverPos = GetObserverPosition();

        // Safety check
        if (!ChunkInfos.IsValidIndex(ChunkIndex))
            continue;

        FChunkInfo &Info = ChunkInfos[ChunkIndex];
        Info.bPendingSpawn = false;  // No longer pending

        // Double check distance before spawning (in case player moved fast)
        if (FVector::DistSquared(Info.WorldLocation, ObserverPos) > RenderDistSq)
            continue;

        // If already exists (rare edge case), skip
        if (Info.ActiveChunk != nullptr)
            continue;

        AVoxelChunk *Chunk = GetWorld()->SpawnActorDeferred<AVoxelChunk>(AVoxelChunk::StaticClass(), Info.Transform, this);
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

            // Finish spawning. This will call OnConstruction on the chunk.
            Chunk->FinishSpawning(Info.Transform);

            Chunk->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
            Chunk->SetOwner(this);

            // Trigger the async generation and track it.
            Chunk->GenerateChunkAsync();
            ActiveGenerationTasks++;

            // Link back to our info struct
            Info.ActiveChunk = Chunk;
        }
        SpawnedThisFrame++;
    }
}

void ACubeSpherePlanet::ProcessMeshUpdateQueue()
{
    // Process a limited number of finished chunks per frame to upload their mesh to the GPU
    int32 ProcessedCount = 0;
    while (MeshUpdateQueue.Num() > 0 && ProcessedCount < ChunksToProcessPerFrame)
    {
        AVoxelChunk *Chunk = MeshUpdateQueue.Pop(false);  // Use FIFO for better visual progression
        if (Chunk && IsValid(Chunk))
        {
            Chunk->UploadMesh();
        }
        ProcessedCount++;
    }
}


bool ACubeSpherePlanet::ShouldTickIfViewportsOnly() const { return true; }


void ACubeSpherePlanet::OnChunkGenerationFinished(AVoxelChunk *Chunk)
{
    if (Chunk)
    {
        if (ActiveGenerationTasks > 0)
        {
            ActiveGenerationTasks--;
        }
        MeshUpdateQueue.Add(Chunk);
    }
}


int32 ACubeSpherePlanet::CalculateAutoChunksPerFace() const
{
    // If auto-sizing is disabled, return the manual setting
    if (!bAutoChunkSizing)
    {
        return ChunksPerFace;
    }

    // 1. Face Width (Arc Length)
    // The arc length of a cube face projected on a sphere is exactly PI/2 * Radius.
    // float FaceArcLength = PlanetRadius * HALF_PI;
    float FaceArcLength = PlanetRadius * 2.0f;

    // 2. Chunk Size
    // Calculate chunk physical size based on voxel settings
    float ChunkPhysicalSize = VoxelResolution * VoxelSize;

    // 3. Curvature Overlap (Adaptive)
    // We need more overlap when the chunk size is large relative to the planet radius.
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

    // Ensure all generated chunks are destroyed with the planet
    ClearAllChunks();
}


void ACubeSpherePlanet::ClearAllChunks()
{
    // Stop any generation in progress
    ChunkSpawnQueue.Empty();
    MeshUpdateQueue.Empty();
    ActiveGenerationTasks = 0;

    // Destroy all tracked chunks
    for (FChunkInfo &Info : ChunkInfos)
    {
        if (Info.ActiveChunk && IsValid(Info.ActiveChunk))
        {
            Info.ActiveChunk->Destroy();
        }
        Info.ActiveChunk = nullptr;
        Info.bPendingSpawn = false;
    }
    ChunkInfos.Empty();

    // Fallback for any other attached chunks that might not have been in the VoxelChunks array
    // (e.g., during a partial generation).
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


void ACubeSpherePlanet::GeneratePlanet()
{
    // This is the new public-facing function to start generation.
    PrepareGeneration();
}


void ACubeSpherePlanet::PrepareGeneration()
{
    // 1. Clean up any previous state and stop ongoing generation.
    ClearAllChunks();
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

    if (bAutoChunkSizing)
    {
        // Calculate required chunks to cover the face center (worst case spacing)
        // ArcLength at center approx 2.0 * Radius.
        // We add a safety buffer (Overlap) to ensuring no gaps.
        float SafetyMultiplier = 1.05f;
        float RequiredCoverage = PlanetRadius * 2.0f * SafetyMultiplier;
        float ChunkPhysicalWidth = VoxelResolution * VoxelSize;

        int32 NeededChunks = FMath::CeilToInt(RequiredCoverage / ChunkPhysicalWidth);

        ChunksPerFace = FMath::Clamp(NeededChunks, MinChunksPerFace, MaxChunksPerFace);

        // If we are capped by MaxChunksPerFace, we MUST increase VoxelSize to bridge the gap
        if (NeededChunks > MaxChunksPerFace)
        {
            // Back-calculate VoxelSize: ChunksPerFace * Res * NewVoxelSize = RequiredCoverage
            VoxelSize = RequiredCoverage / (ChunksPerFace * VoxelResolution);
            UE_LOG(LogTemp, Warning, TEXT("Planet too large for MaxChunksPerFace! Increased VoxelSize to %.2f to ensure coverage."), VoxelSize);
        }
    }

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

                // Create the lightweight chunk info
                FTransform ChunkTransform(chunkWorldRot, chunkWorldPos);

                FChunkInfo NewChunk;
                NewChunk.Transform = ChunkTransform;
                NewChunk.WorldLocation = chunkWorldPos;
                NewChunk.ActiveChunk = nullptr;
                NewChunk.bPendingSpawn = false;

                ChunkInfos.Add(NewChunk);
            }
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Initialized %d potential chunks for planet radius %.1f"), ChunkInfos.Num(), PlanetRadius);
}


FVector ACubeSpherePlanet::GetObserverPosition() const
{
    FVector Pos = FVector::ZeroVector;
    if (GetWorld())
    {
        // This works for both Editor Viewports and Runtime Cameras!
        if (GetWorld()->ViewLocationsRenderedLastFrame.Num() > 0)
        {
            Pos = GetWorld()->ViewLocationsRenderedLastFrame[0];
        }
    }
    return Pos;
}