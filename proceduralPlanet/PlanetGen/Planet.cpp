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

    // Build View Context in WORLD space
    FPlanetViewContext WorldContext = BuildViewContext();

    // Create a LOCAL space context for the ChunkManager
    // The ChunkManager and its subsystems (Quadtree, etc.) operate in the Planet's local space.
    // This allows the entire planet actor to be moved in the world without breaking the generation logic.
    FPlanetViewContext LocalContext;
    const FTransform PlanetTransform = GetActorTransform();
    LocalContext.ObserverLocation = PlanetTransform.InverseTransformPosition(WorldContext.ObserverLocation);
    LocalContext.ObserverForward = PlanetTransform.InverseTransformVector(WorldContext.ObserverForward);
    LocalContext.ObserverVelocity = PlanetTransform.InverseTransformVector(WorldContext.ObserverVelocity);
    LocalContext.ViewDistance = WorldContext.ViewDistance;

    // Update Manager with LOCAL context
    UpdateChunkManager(LocalContext);

    // Update Far Model & Debug with WORLD context
    UpdateFarModelVisibility(WorldContext);
    DrawDebugInfo(WorldContext);
}


bool APlanet::ShouldTickIfViewportsOnly() const { return true; }


void APlanet::Destroyed()
{
    // Explicitly clear the planet to clean up C++ managers (ChunkManager) and their UObject resources (Meshes)
    // while the Actor is still in a valid state. Leaving this to the destructor (GC time) causes crashes
    // because the UObjects may already be unreachable.
    ClearPlanet();

    Super::Destroyed();
}


void APlanet::GeneratePlanet()
{
    ClearPlanet();
    initPlanet();
}


void APlanet::ClearPlanet()
{
    // Reset Managers (Destroys ChunkManager, Renderer, and Chunks)
    ChunkManager.Reset();
    Generator.Reset();
    NoiseProvider.Reset();

    // Destroy Far Model if we created it
    if (bIsFarModelAutoCreated && GenSettings.FarPlanetModel)
    {
        if (IsValid(GenSettings.FarPlanetModel))
        {
            GenSettings.FarPlanetModel->Destroy();
        }
        GenSettings.FarPlanetModel = nullptr;
    }
    bIsFarModelAutoCreated = false;

    // The ChunkManager's destructor now handles all component cleanup robustly.
}


FVector APlanet::GetGravityDirection(const FVector &WorldLocation) const
{
    // Gravity pulls towards the actor location (Planet Center)
    return (GetActorLocation() - WorldLocation).GetSafeNormal();
}


void APlanet::initPlanet()
{
    // Handle Visuals (Far Model)
    if (!GenSettings.FarPlanetModel)
    {
        CreateFarModel();
    }
    // Update Far Model scale if needed (logic from PrepareGeneration)
    if (GenSettings.FarPlanetModel && bIsFarModelAutoCreated)
    {
        GenSettings.FarPlanetModel->SetActorScale3D(FVector(GenSettings.PlanetRadius / FPlanetStatics::DefaultEngineSphereRadius));
    }

    // Calculate "Auto" Settings (Configuration)
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
    RuntimeConfig.LODSplitScreenFraction = GridSettings.LODSplitScreenFraction;
    RuntimeConfig.LODMergeHysteresisRatio = GridSettings.LODMergeHysteresisRatio;
    RuntimeConfig.MaxLookAheadTime = PerformanceSettings.MaxLookAheadTime;
    RuntimeConfig.MinLookAheadTime = PerformanceSettings.MinLookAheadTime;
    RuntimeConfig.LookAheadAltitudeScale = PerformanceSettings.LookAheadAltitudeRadiusFactor * GenSettings.PlanetRadius;

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

    // Find the engine's default sphere mesh
    UStaticMesh *SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (!SphereMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("Could not load default sphere mesh for FarPlanetModel."));
        return;
    }

    // Spawn a StaticMeshActor
    AStaticMeshActor *SphereActor = GetWorld()->SpawnActor<AStaticMeshActor>(GetActorLocation(), GetActorRotation());
    if (SphereActor)
    {
        UStaticMeshComponent *MeshComponent = SphereActor->GetStaticMeshComponent();

        // Configure the actor
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

        // Store the reference and set the flag
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
            BuildVerticalFOV(PCM, Context);
            BuildViewFrustum(PCM, Context);
        }

        if (APawn *PlayerPawn = UGameplayStatics::GetPlayerPawn(World, 0))
        {
            if (IsValid(PlayerPawn))
                Context.ObserverVelocity = PlayerPawn->GetVelocity();
        }
    }


    return Context;
}


void APlanet::BuildViewFrustum(APlayerCameraManager *PCM, FPlanetViewContext &Context) const
{
    if (!PCM || !GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport)
        return;

    // Assemble minimal camera view
    FMinimalViewInfo CameraView;
    CameraView.Location = PCM->GetCameraLocation();
    CameraView.Rotation = PCM->GetCameraRotation();
    CameraView.FOV = PCM->GetFOVAngle();
    CameraView.ProjectionMode = ECameraProjectionMode::Perspective;

    FIntPoint ViewportSize = GEngine->GameViewport->Viewport->GetSizeXY();
    CameraView.AspectRatio = (ViewportSize.Y > 0) ? (float)ViewportSize.X / (float)ViewportSize.Y : 1.777f;  // fallback to 16:9

    // Guard: degenerate viewport on first frame — skip frustum build entirely
    // rather than producing planes that cull everything
    if (ViewportSize.X == 0 || ViewportSize.Y == 0)
        return;

    // Build ViewProjection matrix and extract world-space frustum planes
    FMatrix ViewMatrix, ProjectionMatrix, ViewProjectionMatrix;
    UGameplayStatics::GetViewProjectionMatrix(CameraView, ViewMatrix, ProjectionMatrix, ViewProjectionMatrix);

    FConvexVolume WorldFrustum;
    GetViewFrustumBounds(WorldFrustum, ViewProjectionMatrix, false);

    // Transform frustum planes from world space into planet local space
    // Chunk centers are computed in local space, so the frustum must match.
    const FMatrix LocalMatrix = GetActorTransform().ToMatrixWithScale().Inverse();
    Context.ViewFrustum.Planes.Empty(WorldFrustum.Planes.Num());
    for (const FPlane &WorldPlane : WorldFrustum.Planes)
    {
        Context.ViewFrustum.Planes.Add(WorldPlane.TransformBy(LocalMatrix));
    }

    // Recompute permuted planes used internally by IntersectSphere for SIMD performance
    Context.ViewFrustum.Init();
}


void APlanet::BuildVerticalFOV(APlayerCameraManager *PCM, FPlanetViewContext &Context) const
{
    if (!PCM)
        return;

    float AspectRatio = 1.777f; // fallback 16:9
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
        FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
        if (Size.X > 0 && Size.Y > 0)
            AspectRatio = (float)Size.X / (float)Size.Y;
    }

    float HFOVRad = FMath::DegreesToRadians(PCM->GetFOVAngle());
    float VFOVRad = 2.f * FMath::Atan(FMath::Tan(HFOVRad * 0.5f) / AspectRatio);
    Context.VerticalFOVRadians = VFOVRad;
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

    const float DistToCenter = FVector::Dist(GetActorLocation(), Context.ObserverLocation);
    const float DistToSurface = DistToCenter - GenSettings.PlanetRadius;
    const float SpeedKmh = Context.ObserverVelocity.Size() * 0.036f;

    // --- onscreen debug line 1: Altitude and speed ---
    const FString StatusStr = (DistToSurface < 0.f) ? TEXT("UNDERGROUND") : TEXT("SURFACE");
    GEngine->AddOnScreenDebugMessage(FPlanetStatics::DebugKey_DistanceInfo,
                                     0.f,
                                     FColor::Cyan,
                                     FString::Printf(TEXT("[Planet] %s | Alt: %.0f m | Speed: %.0f km/h"), *StatusStr, DistToSurface / 100.f, SpeedKmh));

    if (ChunkManager.IsValid())
    {
        const int32 Vis = ChunkManager->GetVisibleChunkCount();
        const int32 Mem = ChunkManager->GetTotalChunkCount();
        const int32 Pending = ChunkManager->GetPendingCount();

        // --- onscreen debug line 2: Chunk counts per state ---
        const FColor ChunkColor = (Vis == 0) ? FColor::Red : FColor::Green;
        GEngine->AddOnScreenDebugMessage(
            FPlanetStatics::DebugKey_ManagerStats, 0.f, ChunkColor, FString::Printf(TEXT("[Chunks] Visible: %d | Total: %d | Pending: %d"), Vis, Mem, Pending));

        // --- onscreen debug line 3: Per-LOD visible chunk breakdown ---
        TArray<int32> PerLODCount;
        PerLODCount.Init(0, RuntimeConfig.MaxLOD + 1);
        ChunkManager->GetVisibleCountPerLOD(PerLODCount);

        FString LODStr = TEXT("[LOD] ");
        for (int32 i = 0; i <= RuntimeConfig.MaxLOD; ++i)
        {
            if (PerLODCount[i] > 0)
            {
                LODStr += FString::Printf(TEXT("L%d:%d  "), i, PerLODCount[i]);
            }
        }
        GEngine->AddOnScreenDebugMessage(FPlanetStatics::DebugKey_LODBreakdown, 0.f, FColor::White, LODStr);

        // --- onscreen debug line 4: Next split distance for current LOD ---
        // Show how far the observer is from the next LOD transition
        int32 CurrentMaxLOD = 0;
        for (int32 i = 0; i <= RuntimeConfig.MaxLOD; ++i)
            if (PerLODCount[i] > 0)
                CurrentMaxLOD = i;

        float NextSplitNodeSize = (RuntimeConfig.PlanetRadius * PI * 0.5f) / (float)(1 << CurrentMaxLOD);
        float NextSplitDist = NextSplitNodeSize * RuntimeConfig.LODSplitScreenFraction;
        float NextMergeDist = NextSplitNodeSize * RuntimeConfig.LODSplitScreenFraction * RuntimeConfig.LODMergeHysteresisRatio;
        float ClosestChunkDist = DistToSurface;  // approximation

        GEngine->AddOnScreenDebugMessage(
            FPlanetStatics::DebugKey_LODThreshold,
            0.f,
            FColor::Yellow,
            FString::Printf(
                TEXT("[LOD Threshold] Split < %.0fm | Merge > %.0fm | Dist: %.0fm"), NextSplitDist / 100.f, NextMergeDist / 100.f, ClosestChunkDist / 100.f));
    }
}