// Fill out your copyright notice in the Description page of Project Settings.

#include "CubeSpherePlanet.h"
#include "VoxelChunk.h"
#include "Math/RandomStream.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"


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
    PlanetRadius = 50000.f;
    NoiseAmplitude = 500.f;
    NoiseFrequency = 0.0003f;
    VoxelResolution = 32;
    VoxelSize = 100.f;
    bEnableCollision = false;  // Default to false for performance
    DebugMaterial = nullptr;
    bCastShadows = false;      // Default to false for performance
    ChunksMeshUpdatesPerFrame = 2;

    // Default LOD settings. LOD 0 is highest detail, closest.
    LODSettings.Add({15000.f, 32}); // LOD 0
    LODSettings.Add({30000.f, 16}); // LOD 1
    LODSettings.Add({60000.f, 8});  // LOD 2

    // Far model (impostor) reference
    FarPlanetModel = nullptr;

    // Staggered generation defaults
    bGenerateOnBeginPlay = true;
    ChunksToSpawnPerFrame = 8;
    MaxConcurrentChunkGenerations = 32;
    ActiveGenerationTasks = 0;
    RenderDistance = 150000.0f;
    CollisionDistance = 8000.0f;
    bAutoLOD = true;
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
        // Ensure LOD settings are sorted by distance for the update logic to work correctly.
        // Sort from closest distance (LOD 0) to furthest.
        LODSettings.Sort([](const FLODInfo& A, const FLODInfo& B)
        {
            return A.Distance < B.Distance;
        });

        GeneratePlanet();
    }
}


void ACubeSpherePlanet::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Debug Display: Distance to Center and Surface
    if (GEngine)
    {
        FVector ObserverPos = GetObserverPosition();
        float DistToCenter = FVector::Dist(GetActorLocation(), ObserverPos);
        float DistToSurface = FMath::Max(0.0f, DistToCenter - PlanetRadius);
        GEngine->AddOnScreenDebugMessage(101, 0.0f, FColor::Cyan, FString::Printf(TEXT("Dist to Center: %.0f | Dist to Surface: %.0f"), DistToCenter, DistToSurface));
    }

    UpdateLODAndStreaming();
    ProcessSpawnQueue();
    ProcessMeshUpdateQueue();
}


void ACubeSpherePlanet::UpdateLODAndStreaming()
{
    FVector ObserverPos = GetObserverPosition();

    // --- Far Model & Visibility Transition ---
    float DistToSurface = FMath::Max(0.0f, FVector::Dist(GetActorLocation(), ObserverPos) - PlanetRadius);
    
    // Transition Logic:
    // 1. Far Model starts appearing at 75% of RenderDistance (Overlap start)
    // 2. Chunks disappear at 100% of RenderDistance (Overlap end)
    float FarModelActivateDist = RenderDistance * 0.75f;
    float ChunkCullDist = RenderDistance;

    bool bShowFarModel = DistToSurface > FarModelActivateDist;
    bool bShowChunks = DistToSurface < ChunkCullDist;

    if (FarPlanetModel)
    {
        FarPlanetModel->SetActorHiddenInGame(!bShowFarModel);
    }

    // If we are completely outside chunk range, destroy them.
    // Destroying is better than hiding for the Editor, as it clears them from the World Outliner
    // and ensures "despawn" behavior is visually confirmed.
    if (!bShowChunks)
    {
        // To prevent generating chunks that will just be hidden, we can clear the queues.
        ChunkSpawnQueue.Empty();
        for (FChunkInfo& Info : ChunkInfos)
        {
            if (Info.ActiveChunk)
            {
                Info.ActiveChunk->Destroy();
                Info.ActiveChunk = nullptr;
            }
            Info.bPendingSpawn = false;
            // Reset LOD level so they respawn correctly when re-entering
            Info.LODLevel = -1; 
        }
        return; // Stop here
    }

    // --- Per-Chunk LOD & Streaming Logic ---
    float CollisionDistSq = CollisionDistance * CollisionDistance;

    for (int32 i = 0; i < ChunkInfos.Num(); i++)
    {
        FChunkInfo &Info = ChunkInfos[i];
        FVector ChunkWorldLocation = GetActorTransform().TransformPosition(Info.LocalLocation);
        float DistSq = FVector::DistSquared(ChunkWorldLocation, ObserverPos);

        // Determine Target LOD: -1 (Hidden), 0 (Highest), 1, 2, ...
        int32 TargetLOD = -1;

        if (Info.ActiveChunk)
        {
            Info.ActiveChunk->SetActorHiddenInGame(false); // Ensure chunk is visible if we are in chunk-mode

            // Hysteresis logic for existing chunks
            const int32 CurrentLOD = Info.LODLevel;

            // Check for UPGRADE to a higher-detail LOD (smaller index)
            if (CurrentLOD > 0)
            {
                const float UpgradeDistSq = FMath::Square(LODSettings[CurrentLOD - 1].Distance);
                if (DistSq < UpgradeDistSq)
                {
                    TargetLOD = CurrentLOD - 1;
                }
            }

            // If no upgrade, check for DOWNGRADE to a lower-detail LOD (larger index)
            if (TargetLOD == -1 && CurrentLOD < LODSettings.Num() - 1)
            {
                const float DowngradeDistSq = FMath::Square(LODSettings[CurrentLOD].Distance * 1.1f); // Hysteresis
                if (DistSq > DowngradeDistSq)
                {
                    TargetLOD = CurrentLOD + 1;
                }
            }

            // If still no change, it stays at its current LOD unless it needs to be despawned
            if (TargetLOD == -1)
            {
                const float DespawnDistSq = FMath::Square(ChunkCullDist * 1.1f); // Hysteresis
                TargetLOD = (DistSq > DespawnDistSq) ? -1 : CurrentLOD;
            }
        }
        else // No active chunk, determine if we need to spawn one
        {
            for (int32 LodIndex = 0; LodIndex < LODSettings.Num(); ++LodIndex)
            {
                if (DistSq < FMath::Square(LODSettings[LodIndex].Distance))
                {
                    TargetLOD = LodIndex;
                    break;
                }
            }
        }

        // Apply State Changes
        if (Info.ActiveChunk)
        {
            if (TargetLOD == -1) {
                Info.ActiveChunk->Destroy();
                Info.ActiveChunk = nullptr;
            } else if (TargetLOD != Info.LODLevel) {
                // Switch Resolution
                int32 NewRes = LODSettings[TargetLOD].VoxelResolution;

                // Compensate VoxelSize to keep physical chunk size constant
                // Assumes VoxelResolution and VoxelSize on the planet are for the highest LOD (LOD 0)
                float NewVoxelSize = (VoxelResolution * VoxelSize) / (float)FMath::Max(1, NewRes);

                Info.ActiveChunk->UpdateChunkLOD(TargetLOD, NewRes, NewVoxelSize);
                Info.LODLevel = TargetLOD;
            }
        }
        else if (TargetLOD != -1 && !Info.bPendingSpawn)
        {
            // Queue for spawn
            Info.LODLevel = TargetLOD;
            ChunkSpawnQueue.Add(i);
            Info.bPendingSpawn = true;
        }

        // Collision LOD (Independent of visual LOD, usually matches High Res)
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

        // Calculate RenderDistSq locally
        float CurrentRenderDistSq = RenderDistance * RenderDistance;
        FVector ObserverPos = GetObserverPosition();

        // Safety check
        if (!ChunkInfos.IsValidIndex(ChunkIndex))
            continue;

        FChunkInfo &Info = ChunkInfos[ChunkIndex];
        Info.bPendingSpawn = false;  // No longer pending

        FVector ChunkWorldLocation = GetActorTransform().TransformPosition(Info.LocalLocation);

        // Double check distance before spawning (in case player moved fast)
        if (FVector::DistSquared(ChunkWorldLocation, ObserverPos) > CurrentRenderDistSq)
            continue;

        // If already exists (rare edge case), skip
        if (Info.ActiveChunk != nullptr)
            continue;

        // Calculate correct World Transform for spawning
        FTransform SpawnTransform = Info.Transform * GetActorTransform();

        AVoxelChunk *Chunk = GetWorld()->SpawnActorDeferred<AVoxelChunk>(AVoxelChunk::StaticClass(), SpawnTransform, this);
        if (Chunk)
        {
            // Set chunk parameters BEFORE finishing spawn.
            int32 TargetRes = LODSettings[Info.LODLevel].VoxelResolution;

            // Calculate compensated size so the chunk fills the same physical volume
            float TargetSize = (VoxelResolution * VoxelSize) / (float)FMath::Max(1, TargetRes);

            Chunk->CurrentLOD = Info.LODLevel;
            Chunk->VoxelResolution = TargetRes;
            Chunk->VoxelSize = TargetSize;
            Chunk->PlanetRadius = PlanetRadius;
            Chunk->PlanetCenter = PlanetCenterWorld;
            Chunk->NoiseAmplitude = NoiseAmplitude;
            Chunk->NoiseFrequency = NoiseFrequency;
            Chunk->Seed = Seed;
            Chunk->bEnableCollision = bEnableCollision;
            Chunk->FaceNormal = Info.FaceNormal;
            Chunk->FaceRight = Info.FaceRight;
            Chunk->FaceUp = Info.FaceUp;
            Chunk->ChunkUVMin = Info.UVMin;
            Chunk->ChunkUVMax = Info.UVMax;
            Chunk->ProceduralMesh->SetCastShadow(bCastShadows);

            // Finish spawning. This will call OnConstruction on the chunk.
            Chunk->FinishSpawning(SpawnTransform);

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
    while (MeshUpdateQueue.Num() > 0 && ProcessedCount < ChunksMeshUpdatesPerFrame)
    {
        AVoxelChunk *Chunk = MeshUpdateQueue.Pop(false);  // Use FIFO for better visual progression
        if (Chunk && IsValid(Chunk))
        {
            Chunk->UploadMesh(DebugMaterial);
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


// Helper function to map a point on a unit cube to a unit sphere with equal area distribution.
// (Spherified Cube mapping)
FVector ACubeSpherePlanet::GetSpherifiedCubePoint(const FVector& p)
{
    float x2 = p.X * p.X;
    float y2 = p.Y * p.Y;
    float z2 = p.Z * p.Z;
    float x = p.X * FMath::Sqrt(1.0f - y2 / 2.0f - z2 / 2.0f + y2 * z2 / 3.0f);
    float y = p.Y * FMath::Sqrt(1.0f - z2 / 2.0f - x2 / 2.0f + z2 * x2 / 3.0f);
    float z = p.Z * FMath::Sqrt(1.0f - x2 / 2.0f - y2 / 2.0f + x2 * y2 / 3.0f);
    return FVector(x, y, z);
}


int32 ACubeSpherePlanet::CalculateAutoChunksPerFace() const
{
    // If auto-sizing is disabled, return the manual setting
    if (!bAutoChunkSizing)
    {
        return ChunksPerFace;
    }

    // 1. Face Width (Arc Length)
    // With Projected Grid Mapping, we can use the average arc length (HALF_PI)
    // because the chunks warp to fill the gaps.
    float FaceArcLength = PlanetRadius * HALF_PI;

    // 2. Chunk Size
    // Calculate chunk physical size based on voxel settings
    float ChunkPhysicalSize = VoxelResolution * VoxelSize;

    // 3. Final Calculation
    // We no longer need massive overlap factors.
    float NeededChunks = (FaceArcLength / ChunkPhysicalSize) * ChunkDensityFactor;

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

    // Destroy auto-created far model if it exists
    if (bIsFarModelAutoCreated && FarPlanetModel && IsValid(FarPlanetModel))
    {
        FarPlanetModel->Destroy();
        FarPlanetModel = nullptr;
        bIsFarModelAutoCreated = false;
    }

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


void ACubeSpherePlanet::GenerateSeedBasedPlanet()
{
    // Use the seed to generate a random radius
    FRandomStream randStream(Seed);

    // Set radius based on seed, with a max of 250,000 as requested.
    // Using a reasonable minimum to avoid tiny planets.
    PlanetRadius = randStream.RandRange(2000, 250000);

    UE_LOG(LogTemp, Log, TEXT("Seed %d: Generated new PlanetRadius: %.2f"), Seed, PlanetRadius);

    // The rest of the generation logic will adapt to the new radius.
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

    // 2. Auto-create or update FarPlanetModel
    if (!FarPlanetModel)
    {
        CreateFarModel();
    }
    else if (bIsFarModelAutoCreated)
    {
        // If it was auto-created, update its scale in case the radius changed.
        // The default sphere has a diameter of 100 units (radius 50).
        float SphereScale = PlanetRadius / 50.0f;
        FarPlanetModel->SetActorScale3D(FVector(SphereScale));
        // Also update material in case it changed
        if (AStaticMeshActor* SMA = Cast<AStaticMeshActor>(FarPlanetModel))
        {
            SMA->GetStaticMeshComponent()->SetMaterial(0, DebugMaterial);
        }
    }

    // Define 6 cube faces
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

    // Adaptive voxel logic
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
        // LowResVoxelResolution is now handled by the LODSettings array
    }

    // Adaptive chunks per face logic
    if (bAutoChunkSizing)
    {
        // Calculate required chunks to cover the face center (worst case spacing)
        // With projection, we fit to the arc length.
        float RequiredCoverage = PlanetRadius * HALF_PI;
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

    // Adaptive LOD & Render Distance
    // This fixes the issue where LOD rings didn't scale with the planet.
    if (bAutoLOD)
    {
        // Scale RenderDistance with PlanetRadius, clamped to reasonable limits.
        // Small planets (10km) -> RenderDist ~30km. Large planets (1000km) -> RenderDist ~250km.
        RenderDistance = FMath::Clamp(PlanetRadius * 3.0f, 30000.0f, 250000.0f);

        LODSettings.Empty();
        // LOD 0: Close range (15% of view) - Uses full VoxelResolution
        LODSettings.Add({RenderDistance * 0.15f, VoxelResolution}); 
        
        // LOD 1: Mid range (40% of view) - Half resolution
        LODSettings.Add({RenderDistance * 0.40f, FMath::Max(4, VoxelResolution / 2)});
        
        // LOD 2: Far range (100% of view) - Quarter resolution
        // We extend slightly beyond 1.0 to ensure chunks don't flicker at the boundary
        LODSettings.Add({RenderDistance * 1.05f, FMath::Max(2, VoxelResolution / 4)});
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
                float uCenter = -1.0f + (gridX + 0.5f) * GridStep;
                float vCenter = -1.0f + (gridY + 0.5f) * GridStep;

                // Calculate Bounds for Projection
                float uMin = -1.0f + gridX * GridStep;
                float uMax = uMin + GridStep;
                float vMin = -1.0f + gridY * GridStep;
                float vMax = vMin + GridStep;

                // 1. Calculate point on a unit cube face. This point represents the center of the chunk on the cube face.
                FVector pointOnUnitCube = face.Normal + face.Right * uCenter + face.Up * vCenter;

                // 2. Apply Spherified Cube mapping to distribute chunks evenly on the sphere
                FVector chunkUpDirection = GetSpherifiedCubePoint(pointOnUnitCube);

                // 3. Scale by radius to get the chunk's LOCAL position relative to planet center.
                // We do NOT add PlanetCenterWorld here, keeping it local.
                FVector chunkLocalPos = chunkUpDirection * PlanetRadius;

                // 4. Determine chunk rotation to align it with the planet's curvature.
                // We use the face's original orientation vectors to create a stable orientation.
                FVector chunkRightDirection = FVector::CrossProduct(face.Up, chunkUpDirection).GetSafeNormal();
                FVector chunkForwardDirection = FVector::CrossProduct(chunkUpDirection, chunkRightDirection);
                FRotator chunkWorldRot = UKismetMathLibrary::MakeRotationFromAxes(chunkForwardDirection, chunkRightDirection, chunkUpDirection);

                // Create the lightweight chunk info
                FTransform ChunkTransform(chunkWorldRot, chunkLocalPos);

                FChunkInfo NewChunk;
                NewChunk.Transform = ChunkTransform;
                NewChunk.LocalLocation = chunkLocalPos;
                NewChunk.ActiveChunk = nullptr;
                NewChunk.bPendingSpawn = false;
                NewChunk.FaceNormal = face.Normal;
                NewChunk.FaceRight = face.Right;
                NewChunk.FaceUp = face.Up;
                NewChunk.UVMin = FVector2D(uMin, vMin);
                NewChunk.UVMax = FVector2D(uMax, vMax);

                ChunkInfos.Add(NewChunk);
            }
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("Initialized %d potential chunks for planet radius %.1f"), ChunkInfos.Num(), PlanetRadius);
}

void ACubeSpherePlanet::CreateFarModel()
{
    if (!GetWorld()) return;

    // 1. Find the engine's default sphere mesh
    UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (!SphereMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("Could not load default sphere mesh for FarPlanetModel."));
        return;
    }

    // 2. Spawn a StaticMeshActor
    AStaticMeshActor* SphereActor = GetWorld()->SpawnActor<AStaticMeshActor>(GetActorLocation(), GetActorRotation());
    if (SphereActor)
    {
        UStaticMeshComponent* MeshComponent = SphereActor->GetStaticMeshComponent();
        
        // 3. Configure the actor
        MeshComponent->SetMobility(EComponentMobility::Movable);
        MeshComponent->SetStaticMesh(SphereMesh);
        MeshComponent->SetMaterial(0, DebugMaterial);
        
        // The default sphere has a diameter of 100 units (radius 50).
        // We need to scale it to match our PlanetRadius.
        float SphereScale = PlanetRadius / 50.0f;
        SphereActor->SetActorScale3D(FVector(SphereScale));

        // Disable performance-intensive features
        MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        MeshComponent->SetCastShadow(false);

        // Attach to the planet so it moves with it
        SphereActor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);

        // Hide it initially, the LOD system will unhide it when needed
        SphereActor->SetActorHiddenInGame(true);

        // 4. Store the reference and set the flag
        FarPlanetModel = SphereActor;
        bIsFarModelAutoCreated = true;

        UE_LOG(LogTemp, Log, TEXT("Automatically created FarPlanetModel for planet."));
    }
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