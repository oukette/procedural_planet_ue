// Fill out your copyright notice in the Description page of Project Settings.

#include "Planet.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "ProceduralMeshComponent.h"


// Sets default values
APlanet::APlanet()
{
    // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
    PrimaryActorTick.bCanEverTick = true;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;
}


void APlanet::OnConstruction(const FTransform &Transform) { Super::OnConstruction(Transform); }


void APlanet::BeginPlay()
{
    Super::BeginPlay();
    if (bGenerateOnBeginPlay)
    {
        // DEBUG
        if (GenSettings.bShowDebugTrueSphere)
            DrawDebugSphere(GetWorld(),
                            GetActorLocation(),
                            GenSettings.PlanetRadius,
                            FPlanetStatics::DebugSphereSegments,
                            FColor::Red,
                            false,
                            FPlanetStatics::DebugSphereLifetime,
                            0,
                            FPlanetStatics::DebugSphereThickness);

        initPlanet();
    }
}


void APlanet::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // 1. Build View Context
    FPlanetViewContext Context = BuildViewContext();

    // 2. Update Manager
    UpdateChunkManager(Context);

    // 3. Update Far Model
    UpdateFarModelVisibility(Context);

    // 4. Debug Output
    DrawDebugInfo(Context);
}


bool APlanet::ShouldTickIfViewportsOnly() const { return true; }


void APlanet::Destroyed()
{
    Super::Destroyed();

    // Chunk manager shall handle chunk clearing.
}


void APlanet::GeneratePlanet()
{
    ClearPlanet();
    initPlanet();
}


void APlanet::ClearPlanet()
{
    // 1. Reset Managers (Destroys ChunkManager, Renderer, and Chunks)
    ChunkManager.Reset();
    Generator.Reset();
    NoiseProvider.Reset();

    // 2. Destroy Far Model if we created it
    if (bIsFarModelAutoCreated && GenSettings.FarPlanetModel)
    {
        if (IsValid(GenSettings.FarPlanetModel))
        {
            GenSettings.FarPlanetModel->Destroy();
        }
        GenSettings.FarPlanetModel = nullptr;
    }
    bIsFarModelAutoCreated = false;

    // 3. Clean up any lingering Procedural Mesh Components
    // (ChunkRenderer might have left them attached to the actor)
    TArray<UProceduralMeshComponent *> ProcMeshes;
    GetComponents<UProceduralMeshComponent>(ProcMeshes);
    for (UProceduralMeshComponent *Comp : ProcMeshes)
    {
        if (Comp)
            Comp->DestroyComponent();
    }
}


void APlanet::initPlanet()
{
    // 1. Handle Visuals (Far Model)
    if (!GenSettings.FarPlanetModel)
    {
        CreateFarModel();
    }
    // Update Far Model scale if needed (logic from PrepareGeneration)
    if (GenSettings.FarPlanetModel && bIsFarModelAutoCreated)
    {
        GenSettings.FarPlanetModel->SetActorScale3D(FVector(GenSettings.PlanetRadius / FPlanetStatics::DefaultEngineSphereRadius));
    }

    // 2. Calculate "Auto" Settings (Configuration)
    int32 FinalChunksPerFace;
    float FinalVoxelSize;
    int32 FinalResolution;

    CalculateAutoGrid(FinalChunksPerFace, FinalVoxelSize, FinalResolution);

    // Initialize the planet config struct to feed the ChunkManager
    RuntimeConfig = FPlanetConfig();
    RuntimeConfig.PlanetRadius = GenSettings.PlanetRadius;
    RuntimeConfig.ChunksPerFace = FinalChunksPerFace;  // The calculated value!
    RuntimeConfig.Seed = GenSettings.Seed;
    RuntimeConfig.bEnableCollision = GenSettings.bEnableCollision;
    RuntimeConfig.bCastShadows = GenSettings.bCastShadows;
    RuntimeConfig.VoxelSize = FinalVoxelSize;
    RuntimeConfig.GridResolution = FinalResolution;
    RuntimeConfig.MaxConcurrentGenerations = PerformanceSettings.MaxConcurrentGenerations;
    RuntimeConfig.ChunkGenerationRate = PerformanceSettings.ChunksToSpawnPerFrame;
    RuntimeConfig.MeshUpdatesPerFrame = PerformanceSettings.MeshUpdatesPerFrame;
    RuntimeConfig.FarDistanceThreshold = GenSettings.PlanetRadius * GenSettings.RenderDistanceMultiplier;
    RuntimeConfig.LODSplitDistanceMultiplier = GridSettings.LODSplitMultiplier;

    // Create Noise Provider
    NoiseProvider = MakeUnique<SimpleNoise>();

    // Create density config and init the generator
    DensityConfig densityConfig;
    densityConfig.PlanetRadius = GenSettings.PlanetRadius;
    densityConfig.Seed = GenSettings.Seed;
    densityConfig.VoxelSize = FinalVoxelSize;
    densityConfig.Noise = NoiseSettings;
    Generator = MakeUnique<DensityGenerator>(densityConfig, NoiseProvider.Get());

    // Finally, init the ChunkManager
    ChunkManager = MakeUnique<FChunkManager>(RuntimeConfig, Generator.Get());
    ChunkManager->Initialize(this, GenSettings.DebugMaterial);  // Pass context for rendering
}


void APlanet::CalculateAutoGrid(int32 &OutChunksPerFace, float &OutVoxelSize, int32 &OutResolution) const
{
    // Default to settings
    OutResolution = FMath::Max(4, GridSettings.Resolution);

    // FORCE 1 Chunk per face for the "Clean State"
    OutChunksPerFace = 1;

    // Recalculate VoxelSize to ensure the grid perfectly covers the face arc.
    const float FaceArcLength = GenSettings.PlanetRadius * HALF_PI;
    OutVoxelSize = FaceArcLength / (OutChunksPerFace * OutResolution);
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
        MeshComponent->SetMaterial(0, GenSettings.DebugMaterial);

        // The default sphere has a diameter of 100 units (radius 50).
        // We need to scale it to match our PlanetRadius.
        float SphereScale = GenSettings.PlanetRadius / FPlanetStatics::DefaultEngineSphereRadius;
        SphereActor->SetActorScale3D(FVector(SphereScale));

        // Disable performance-intensive features
        MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        MeshComponent->SetCastShadow(false);

        // Attach to the planet so it moves with it
        SphereActor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);

        // Hide it initially, the LOD system will unhide it when needed
        SphereActor->SetActorHiddenInGame(true);

        // 4. Store the reference and set the flag
        GenSettings.FarPlanetModel = SphereActor;
        bIsFarModelAutoCreated = true;

        UE_LOG(LogTemp, Log, TEXT("Automatically created FarPlanetModel for planet."));
    }
}


FVector APlanet::GetObserverPosition() const
{
    FVector Pos = FVector::ZeroVector;
    if (GetWorld())
    {
        // This works for both Editor Viewports and Runtime Cameras
        if (GetWorld()->ViewLocationsRenderedLastFrame.Num() > 0)
        {
            Pos = GetWorld()->ViewLocationsRenderedLastFrame[0];
        }
    }
    return Pos;
}


FPlanetViewContext APlanet::BuildViewContext() const
{
    FPlanetViewContext Context;
    Context.ObserverLocation = GetObserverPosition();
    Context.ObserverForward = FVector::ZeroVector;  // If we can't find a camera (e.g. Editor Viewport), disable frustum culling to avoid "blind spots".
    Context.ObserverVelocity = FVector::ZeroVector;

    if (UWorld *World = GetWorld())
    {
        if (APlayerCameraManager *PCM = UGameplayStatics::GetPlayerCameraManager(World, 0))
        {
            Context.ObserverForward = PCM->GetCameraRotation().Vector();
        }

        if (APawn *PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0))
        {
            Context.ObserverVelocity = PlayerPawn->GetVelocity();
        }
    }


    return Context;
}


void APlanet::UpdateChunkManager(const FPlanetViewContext &Context)
{
    if (ChunkManager.IsValid())
    {
        ChunkManager->Update(Context);

        if (GenSettings.bShowDebugChunkGrid)
        {
            ChunkManager->DrawDebugGrid(GetWorld());
        }

        if (GenSettings.bShowDebugChunkBounds)
        {
            ChunkManager->DrawDebugChunkBounds(GetWorld());
        }
    }
}


void APlanet::UpdateFarModelVisibility(const FPlanetViewContext &Context)
{
    // We do this here because the Actor owns the FarModel component/actor.
    if (GenSettings.FarPlanetModel)
    {
        float DistToCenter = FVector::Dist(GetActorLocation(), Context.ObserverLocation);
        float DistToSurface = DistToCenter - GenSettings.PlanetRadius;

        // Hysteresis logic for Far Model
        // ShowThreshold: Distance to SHOW the far model (getting farther)
        float ShowThreshold = RuntimeConfig.FarDistanceThreshold * FPlanetStatics::FarModelDistanceRatio;

        // HideThreshold: Distance to HIDE the far model (getting closer)
        // We keep it visible a bit longer to ensure chunks have fully spawned underneath.
        float HideThreshold = RuntimeConfig.FarDistanceThreshold * FPlanetStatics::FarModelHideRatio;

        bool bIsVisible = !GenSettings.FarPlanetModel->IsHidden();

        if (bIsVisible)
        {
            // We are in Far Mode. Switch to Near only if we get close enough.
            if (DistToSurface < HideThreshold)
            {
                GenSettings.FarPlanetModel->SetActorHiddenInGame(true);
            }
        }
        else
        {
            // We are in Near Mode. Switch to Far only if we get far enough.
            if (DistToSurface > ShowThreshold)
            {
                GenSettings.FarPlanetModel->SetActorHiddenInGame(false);
            }
        }
    }
}


void APlanet::DrawDebugInfo(const FPlanetViewContext &Context) const
{
    if (!GEngine)
        return;

    // Manager Stats
    if (ChunkManager.IsValid())
    {
        int32 Vis = ChunkManager->GetVisibleChunkCount();
        int32 Mem = ChunkManager->GetChunkCount();
        int32 Pending = ChunkManager->GetPendingCount();

        FColor TextColor = (Vis == 0) ? FColor::Red : FColor::Green;

        GEngine->AddOnScreenDebugMessage(
            FPlanetStatics::DebugKey_ManagerStats, 0.f, TextColor, FString::Printf(TEXT("Chunks: %d Vis / %d Mem | Pending: %d"), Vis, Mem, Pending));
    }

    // Distance Info
    float DistToCenter = FVector::Dist(GetActorLocation(), Context.ObserverLocation);
    float DistToSurface = DistToCenter - GenSettings.PlanetRadius;
    FString Status = (DistToSurface < 0) ? TEXT("UNDERGROUND") : TEXT("SURFACE");
    GEngine->AddOnScreenDebugMessage(
        FPlanetStatics::DebugKey_DistanceInfo, 0.0f, FColor::Cyan, FString::Printf(TEXT("%s | Alt: %.0f"), *Status, DistToSurface));
}