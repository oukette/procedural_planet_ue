// Fill out your copyright notice in the Description page of Project Settings.

#include "Planet.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"


// Sets default values
APlanet::APlanet()
{
    // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
    PrimaryActorTick.bCanEverTick = true;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    // Default LOD settings. LOD 0 is highest detail, closest.
    if (LODSettings.LODLayers.Num() == 0)
    {
        LODSettings.LODLayers.Add({5000.f, 64});   // LOD 0
        LODSettings.LODLayers.Add({15000.f, 32});  // LOD 1
        LODSettings.LODLayers.Add({30000.f, 16});  // LOD 2
        LODSettings.LODLayers.Add({60000.f, 8});   // LOD 3
    }
}


void APlanet::OnConstruction(const FTransform &Transform) { Super::OnConstruction(Transform); }


void APlanet::BeginPlay()
{
    Super::BeginPlay();
    if (bGenerateOnBeginPlay)
    {
        // DEBUG
        DrawDebugSphere(GetWorld(), GetActorLocation(), GenSettings.PlanetRadius, 32, FColor::Red, false, 60.0f, 0, 20.0f);

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
    Context.ViewDistance = LODSettings.RenderDistance;
    Context.MaxAllowedLOD = LODSettings.LODLayers.Num() - 1;

    // 2. Update Manager
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

        // 3. Debug Output
        if (GEngine)
        {
            int32 Total = ChunkManager->GetChunkCount();
            int32 Visible = ChunkManager->GetVisibleChunkCount();

            FColor TextColor = (Visible == 0) ? FColor::Red : FColor::Green;

            GEngine->AddOnScreenDebugMessage(
                10,
                0.f,
                TextColor,
                FString::Printf(TEXT("Manager: %d Active / %d Total | Grid: %dx%d"), Visible, Total, RuntimeConfig.ChunksPerFace, RuntimeConfig.ChunksPerFace));
        }
    }

    // Debug : Distance to Center and Surface
    if (GEngine)
    {
        FVector ObserverPos = GetObserverPosition();
        float DistToCenter = FVector::Dist(GetActorLocation(), ObserverPos);
        float DistToSurface = FMath::Max(0.0f, DistToCenter - GenSettings.PlanetRadius);
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
    if (!GenSettings.FarPlanetModel)
    {
        CreateFarModel();
    }
    // Update Far Model scale if needed (logic from PrepareGeneration)
    if (GenSettings.FarPlanetModel && bIsFarModelAutoCreated)
    {
        GenSettings.FarPlanetModel->SetActorScale3D(FVector(GenSettings.PlanetRadius / 50.0f));
    }

    // --- STEP 2: Calculate "Auto" Settings (Configuration) ---
    int32 FinalChunksPerFace;
    float FinalVoxelSize;
    int32 FinalResolution;

    CalculateAutoGrid(FinalChunksPerFace, FinalVoxelSize, FinalResolution);

    // --- STEP 3: Calculate Auto LODs ---
    TArray<FLODInfo> FinalLODs;
    // Calculate the physical arc length of a single chunk (Face Arc / Count)
    float ChunkArcLength = (GenSettings.PlanetRadius * HALF_PI) / FinalChunksPerFace;

    CalculateAutoLODs(FinalLODs, ChunkArcLength);

    // Initialize the planet config struct to feed the ChunkManager
    RuntimeConfig = FPlanetConfig();
    RuntimeConfig.PlanetRadius = GenSettings.PlanetRadius;
    RuntimeConfig.ChunksPerFace = FinalChunksPerFace;  // The calculated value!
    RuntimeConfig.Seed = GenSettings.Seed;
    RuntimeConfig.bEnableCollision = GenSettings.bEnableCollision;
    RuntimeConfig.bCastShadows = GenSettings.bCastShadows;
    RuntimeConfig.VoxelSize = FinalVoxelSize;
    RuntimeConfig.GridResolution = FinalResolution;
    RuntimeConfig.LODLayers = FinalLODs;  // The calculated LODs!
    RuntimeConfig.CollisionDistance = LODSettings.CollisionDistance;
    RuntimeConfig.FarDistanceThreshold = LODSettings.RenderDistance;
    RuntimeConfig.LODHysteresis = LODSettings.Hysteresis;
    RuntimeConfig.LODDespawnHysteresis = LODSettings.DespawnHysteresis;
    RuntimeConfig.MaxConcurrentGenerations = PerformanceSettings.MaxConcurrentGenerations;
    RuntimeConfig.ChunkGenerationRate = PerformanceSettings.ChunksToSpawnPerFrame;
    RuntimeConfig.MeshUpdatesPerFrame = PerformanceSettings.MeshUpdatesPerFrame;

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

    // 1. Calculate the arc length of a face (90 degrees)
    const float FaceArcLength = GenSettings.PlanetRadius * HALF_PI;

    if (GridSettings.bAutoChunkSizing)
    {
        // 2. Determine chunk count based on a target physical size.
        // We aim for chunks to be roughly 4000 units wide to balance draw calls vs culling.
        // This ensures we don't have too many chunks on small planets, and chunks aren't too small.
        const float TargetChunkSize = 4000.0f;

        int32 RawCount = FMath::RoundToInt(FaceArcLength / TargetChunkSize);

        // 3. Clamp count to user settings
        OutChunksPerFace = FMath::Clamp(RawCount, GridSettings.MinChunksPerFace, GridSettings.MaxChunksPerFace);
    }
    else
    {
        OutChunksPerFace = GridSettings.ChunksPerFace;
    }

    // 4. CRITICAL: Recalculate VoxelSize to ensure the grid perfectly covers the face arc.
    // FaceArcLength = Chunks * (Resolution * VoxelSize)
    OutVoxelSize = FaceArcLength / (OutChunksPerFace * OutResolution);
}


void APlanet::CalculateAutoLODs(TArray<FLODInfo> &OutLODs, float ChunkArcLength) const
{
    OutLODs.Empty();

    if (LODSettings.bAutoLOD)
    {
        int32 BaseRes = GridSettings.Resolution;

        // Define distance thresholds relative to chunk size.
        // This ensures that as chunks get bigger (bigger planet), the LOD transitions push out.
        float D0 = ChunkArcLength * 1.25f;
        float D1 = ChunkArcLength * 2.5f;
        float D2 = ChunkArcLength * 5.0f;

        // The last LOD should extend to the render distance
        float D3 = FMath::Max(ChunkArcLength * 10.0f, LODSettings.RenderDistance);

        OutLODs.Add({D0, BaseRes});
        OutLODs.Add({D1, FMath::Max(4, BaseRes / 2)});
        OutLODs.Add({D2, FMath::Max(4, BaseRes / 4)});
        OutLODs.Add({D3, FMath::Max(4, BaseRes / 8)});
    }
    else
    {
        OutLODs = LODSettings.LODLayers;
        // Ensure sorted
        OutLODs.Sort([](const FLODInfo &A, const FLODInfo &B) { return A.Distance < B.Distance; });
    }
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
        float SphereScale = GenSettings.PlanetRadius / 50.0f;
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
        // This works for both Editor Viewports and Runtime Cameras!
        if (GetWorld()->ViewLocationsRenderedLastFrame.Num() > 0)
        {
            Pos = GetWorld()->ViewLocationsRenderedLastFrame[0];
        }
    }
    return Pos;
}