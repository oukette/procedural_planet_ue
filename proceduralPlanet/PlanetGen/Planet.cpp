// Fill out your copyright notice in the Description page of Project Settings.

#include "Planet.h"
#include "Math/RandomStream.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"


// Sets default values
APlanet::APlanet()
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
    bCastShadows = false;  // Default to false for performance
    ChunksMeshUpdatesPerFrame = 2;

    // Default LOD settings. LOD 0 is highest detail, closest.
    LODSettings.Add({5000.f, 64});   // LOD 0 (New, high detail for close-ups)
    LODSettings.Add({15000.f, 32});  // LOD 1
    LODSettings.Add({30000.f, 16});  // LOD 2
    LODSettings.Add({60000.f, 8});   // LOD 3

    // Far model (impostor) reference
    FarPlanetModel = nullptr;

    // Staggered generation defaults
    bGenerateOnBeginPlay = true;
    ChunksToSpawnPerFrame = 8;
    MaxConcurrentChunkGenerations = 32;
    RenderDistance = 150000.0f;
    CollisionDistance = 8000.0f;
    bAutoLOD = true;
}


void APlanet::OnConstruction(const FTransform &Transform)
{
    Super::OnConstruction(Transform);
}


void APlanet::BeginPlay()
{
    Super::BeginPlay();
    if (bGenerateOnBeginPlay)
    {
        // Ensure LOD settings are sorted by distance for the update logic to work correctly.
        // Sort from closest distance (LOD 0) to furthest.
        LODSettings.Sort([](const FLODInfo &A, const FLODInfo &B) { return A.Distance < B.Distance; });

        // DEBUG
        DrawDebugSphere(GetWorld(), GetActorLocation(), PlanetRadius, 32, FColor::Red, false, 60.0f, 0, 20.0f);

        initPlanet();
    }
}


void APlanet::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 1. Build View Context
    FPlanetViewContext Context;
    Context.ObserverLocation = GetObserverPosition();
    // We don't need to pass static config (LODs, Radius) here anymore,
    // because the Manager already has FPlanetConfig!

    // 2. Update Manager
    if (ChunkManager.IsValid())
    {
        ChunkManager->Update(Context);

        // 3. Debug Output
        if (GEngine)
        {
            int32 Total = ChunkManager->GetChunkCount();
            int32 Visible = ChunkManager->GetVisibleChunkCount();

            FColor TextColor = (Visible == 0) ? FColor::Red : FColor::Green;

            GEngine->AddOnScreenDebugMessage(10, 0.f, TextColor, FString::Printf(TEXT("Manager: %d Active / %d Total"), Visible, Total));
        }
    }

    // Debug : Distance to Center and Surface
    if (GEngine)
    {
        FVector ObserverPos = GetObserverPosition();
        float DistToCenter = FVector::Dist(GetActorLocation(), ObserverPos);
        float DistToSurface = FMath::Max(0.0f, DistToCenter - PlanetRadius);
        GEngine->AddOnScreenDebugMessage(
            101, 0.0f, FColor::Cyan, FString::Printf(TEXT("Dist to Center: %.0f | Dist to Surface: %.0f"), DistToCenter, DistToSurface));
    }
}


bool APlanet::ShouldTickIfViewportsOnly() const { return true; }


void APlanet::Destroyed()
{
    Super::Destroyed();

    // Chunk manager shall handle chunk clearing.
}


void APlanet::initPlanet()
{
    // --- STEP 1: Handle Visuals (Far Model) ---
    if (!FarPlanetModel)
    {
        CreateFarModel();
    }
    // Update Far Model scale if needed (logic from PrepareGeneration)
    if (FarPlanetModel && bIsFarModelAutoCreated)
    {
        FarPlanetModel->SetActorScale3D(FVector(PlanetRadius / 50.0f));
    }

    // --- STEP 2: Calculate "Auto" Settings (Configuration) ---
    int32 FinalChunksPerFace = ChunksPerFace;
    float FinalVoxelSize = VoxelSize;
    int32 FinalResolution = VoxelResolution;

    if (bAutoChunkSizing)
    {
        // 2a. Adapt Voxel Size
        float TargetVoxelSize = PlanetRadius / 150.0f;
        FinalVoxelSize = FMath::Clamp(TargetVoxelSize, 25.0f, 400.0f);

        // 2b. Adapt Resolution
        FinalResolution = (PlanetRadius < 3000.0f) ? 16 : 32;

        // 2c. Adapt Chunk Count
        float RequiredCoverage = PlanetRadius * HALF_PI;
        float ChunkPhysicalWidth = FinalResolution * FinalVoxelSize;
        int32 NeededChunks = FMath::CeilToInt(RequiredCoverage / ChunkPhysicalWidth);

        FinalChunksPerFace = FMath::Clamp(NeededChunks, MinChunksPerFace, MaxChunksPerFace);

        // Compensate VoxelSize if capped
        if (NeededChunks > MaxChunksPerFace)
        {
            FinalVoxelSize = RequiredCoverage / (FinalChunksPerFace * FinalResolution);
        }
    }

    // --- STEP 3: Calculate Auto LODs ---
    TArray<FLODInfo> FinalLODs = LODSettings;
    float FinalRenderDist = RenderDistance;

    if (bAutoLOD)
    {
        FinalRenderDist = FMath::Clamp(PlanetRadius * 3.0f, 30000.0f, 250000.0f);
        FinalLODs.Empty();

        // Replicate your AutoLOD logic exactly:
        FinalLODs.Add({5000.0f, FinalResolution * 2});                                 // LOD 0
        FinalLODs.Add({FinalRenderDist * 0.15f, FinalResolution});                     // LOD 1
        FinalLODs.Add({FinalRenderDist * 0.40f, FMath::Max(8, FinalResolution / 2)});  // LOD 2
        FinalLODs.Add({FinalRenderDist * 1.05f, FMath::Max(4, FinalResolution / 4)});  // LOD 3
    }

    // --- STEP 4: Initialize the Manager with FINALIZED Config ---
    // Fill the config struct
    FPlanetConfig ManagerConfig;
    ManagerConfig.PlanetRadius = PlanetRadius;
    ManagerConfig.ChunksPerFace = FinalChunksPerFace;  // The calculated value!
    ManagerConfig.Seed = Seed;
    ManagerConfig.LODSettings = FinalLODs;  // The calculated LODs!
    ManagerConfig.FarDistanceThreshold = FinalRenderDist;
    ManagerConfig.LODHysteresis = LODHysteresisFactor;
    ManagerConfig.LODDespawnHysteresis = LODDespawnHysteresisFactor;
    ManagerConfig.MaxConcurrentGenerations = MaxConcurrentChunkGenerations;
    ManagerConfig.GenerationRate = ChunksToSpawnPerFrame;

    // Create Noise Provider
    NoiseProvider = MakeUnique<SimpleNoise>();

    // Create Generator
    DensityConfig DenConfig;
    DenConfig.PlanetRadius = PlanetRadius;
    DenConfig.Seed = Seed;
    DenConfig.VoxelSize = FinalVoxelSize;
    DenConfig.NoiseAmplitude = NoiseAmplitude;
    DenConfig.NoiseFrequency = NoiseFrequency;

    Generator = MakeUnique<DensityGenerator>(DenConfig, NoiseProvider.Get());

    // Finally, spawn the Manager
    ChunkManager = MakeUnique<FChunkManager>(ManagerConfig, Generator.Get());
    ChunkManager->Initialize(this, DebugMaterial);  // Pass context for rendering
}


void APlanet::CreateFarModel()
{
    if (!GetWorld())
        return;

    // 1. Find the engine's default sphere mesh
    UStaticMesh *SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (!SphereMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("Could not load default sphere mesh for FarPlanetModel."));
        return;
    }

    // 2. Spawn a StaticMeshActor
    AStaticMeshActor *SphereActor = GetWorld()->SpawnActor<AStaticMeshActor>(GetActorLocation(), GetActorRotation());
    if (SphereActor)
    {
        UStaticMeshComponent *MeshComponent = SphereActor->GetStaticMeshComponent();

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


FVector APlanet::GetObserverPosition() const
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