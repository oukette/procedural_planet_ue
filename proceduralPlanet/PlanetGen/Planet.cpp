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
        if (DebugSettings.bShowDebugTrueSphere)
            DrawDebugSphere(GetWorld(),
                            GetActorLocation(),
                            GenSettings.PlanetRadius,
                            PlanetStatics::DebugSphereSegments,
                            FColor::Red,
                            false,
                            PlanetStatics::DebugSphereLifetime,
                            0,
                            PlanetStatics::DebugSphereThickness);

        initPlanet();
    }
}


void APlanet::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Build Contexts
    const PlanetViewContext WorldContext = PlanetViewContext::BuildWorldContext(this);
    const PlanetViewContext LocalContext = PlanetViewContext::BuildLocalContext(WorldContext,
                                                                                this,
                                                                                m_planetConfig.PlanetRadius,
                                                                                m_planetConfig.FarDistanceThreshold,
                                                                                PlanetStatics::FarDistanceSafetyMargin);

    // Update Manager with LOCAL context
    UpdateChunkManager(LocalContext);

    // Update Far Model
    UpdateFarModelVisibility(WorldContext);

    // DEBUG stuff
    DrawDebugInfo(WorldContext);
    if (DebugSettings.bShowDebugPredictivePos)
        DrawPredictiveDebug(LocalContext);
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
    m_chunkManager.Reset();
    m_densityGen.Reset();
    m_noiseProvider.Reset();

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
        CreateFarModel();

    // Update Far Model scale if needed (logic from PrepareGeneration)
    if (GenSettings.FarPlanetModel && bIsFarModelAutoCreated)
        GenSettings.FarPlanetModel->SetActorScale3D(FVector(GenSettings.PlanetRadius / PlanetStatics::DefaultEngineSphereRadius));

    // Calculate "Auto" Settings (Configuration)
    float computedVoxelSize;
    ComputeAutoVoxelSize(computedVoxelSize);

    // Build configs — all assembly logic lives in the builders
    m_planetConfig = BuildPlanetConfig(computedVoxelSize);

    // Wire up subsystems
    m_noiseProvider = MakeShared<SimpleNoise, ESPMode::ThreadSafe>();
    m_densityGen = MakeUnique<DensityGenerator>(DensityConfig::From(m_planetConfig, NoiseSettings), m_noiseProvider.Get());
    m_chunkManager = MakeUnique<ChunkManager>(m_planetConfig, m_densityGen.Get(), m_noiseProvider);
    m_chunkManager->Initialize(this, GenSettings.DebugMaterial);
}


void APlanet::ComputeAutoVoxelSize(float &OutVoxelSize) const
{
    // Default to settings
    auto Resolution = FMath::Max(4, GridSettings.Resolution);

    // Recalculate VoxelSize to ensure the grid perfectly covers the face arc.
    const float FaceArcLength = GenSettings.PlanetRadius * HALF_PI;
    OutVoxelSize = FaceArcLength / Resolution;
}


FPlanetConfig APlanet::BuildPlanetConfig(float VoxelSize) const
{
    FPlanetConfig Cfg;
    Cfg.PlanetRadius = GenSettings.PlanetRadius;
    Cfg.Seed = GenSettings.Seed;
    Cfg.bEnableCollision = GenSettings.bEnableCollision;
    Cfg.bCastShadows = GenSettings.bCastShadows;
    Cfg.VoxelSize = VoxelSize;
    Cfg.GridResolution = FMath::Max(4, GridSettings.Resolution);
    Cfg.FarDistanceThreshold = GenSettings.PlanetRadius * GenSettings.RenderDistanceMultiplier;
    Cfg.LODSplitScreenFraction = GridSettings.LODSplitScreenFraction;
    Cfg.LODMergeHysteresisRatio = GridSettings.LODMergeHysteresisRatio;
    Cfg.MaxConcurrentGenerations = PerformanceSettings.MaxConcurrentGenerations;
    Cfg.ChunkGenerationRate = PerformanceSettings.ChunksToSpawnPerFrame;
    Cfg.MeshUpdatesPerFrame = PerformanceSettings.MeshUpdatesPerFrame;
    return Cfg;
}


DensityConfig APlanet::BuildDensityConfig(float VoxelSize) const { return DensityConfig::From(BuildPlanetConfig(VoxelSize), NoiseSettings); }


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
        float SphereScale = GenSettings.PlanetRadius / PlanetStatics::DefaultEngineSphereRadius;
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


void APlanet::UpdateChunkManager(const PlanetViewContext &Context)
{
    if (m_chunkManager.IsValid())
    {
        m_chunkManager->Update(Context);

        if (DebugSettings.bShowDebugChunkGrid)
        {
            m_chunkManager->DrawDebugGrid(GetWorld());
        }

        if (DebugSettings.bShowDebugChunkBounds)
        {
            m_chunkManager->DrawDebugChunkBounds(GetWorld());
        }
    }
}


void APlanet::UpdateFarModelVisibility(const PlanetViewContext &Context)
{
    // We do this here because the Actor owns the FarModel component/actor.
    if (GenSettings.FarPlanetModel)
    {
        float DistToCenter = FVector::Dist(GetActorLocation(), Context.ObserverLocation);
        float DistToSurface = DistToCenter - GenSettings.PlanetRadius;

        // Hysteresis logic for Far Model
        // ShowThreshold: Distance to SHOW the far model (getting farther)
        float ShowThreshold = m_planetConfig.FarDistanceThreshold * PlanetStatics::FarModelDistanceRatio;

        // HideThreshold: Distance to HIDE the far model (getting closer)
        // We keep it visible a bit longer to ensure chunks have fully spawned underneath.
        float HideThreshold = m_planetConfig.FarDistanceThreshold * PlanetStatics::FarModelHideRatio;

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


void APlanet::DrawDebugInfo(const PlanetViewContext &Context) const
{
    if (!GEngine)
        return;

    const float DistToCenter = FVector::Dist(GetActorLocation(), Context.ObserverLocation);
    const float DistToSurface = DistToCenter - GenSettings.PlanetRadius;
    const float SpeedKmh = Context.ObserverVelocity.Size() * 0.036f;

    // --- ONSCREEN DEBUG LINE 0: Altitude and speed ---
    const FString StatusStr = (DistToSurface < 0.f) ? TEXT("UNDERGROUND") : TEXT("SURFACE");
    GEngine->AddOnScreenDebugMessage(PlanetStatics::DebugKey_DistanceInfo,
                                     0.f,
                                     FColor::Cyan,
                                     FString::Printf(TEXT("[Planet] %s | Alt: %.0f m | Speed: %.0f km/h"), *StatusStr, DistToSurface / 100.f, SpeedKmh));

    if (m_chunkManager.IsValid())
    {
        const FChunkManagerStats CMStats = m_chunkManager->GetDebugStats();
        const FChunkGeneratorStats CGStats = m_chunkManager->GetChunkGeneratorStats();

        const float FrameMs = 1000.f / 60.f;
        const float MinAgeBudgetMs = m_planetConfig.StaleTransitionMinAge * FrameMs;

        const FColor GenTimeColor = (CGStats.AvgGenMs > MinAgeBudgetMs)           ? FColor::Red
                                    : (CGStats.AvgGenMs > MinAgeBudgetMs * 0.75f) ? FColor::Yellow
                                                                                  : FColor::Green;

        // --- ONSCREEN DEBUG LINE 1: Chunk counts per state ---
        const FColor ChunkStatusColor = (CMStats.Visible == 0) ? FColor::Red : FColor::Green;
        GEngine->AddOnScreenDebugMessage(PlanetStatics::DebugKey_ManagerStats_1,
                                         0.f,
                                         ChunkStatusColor,
                                         FString::Printf(TEXT("[Chunks] Total:%d | None:%d Pend:%d Gen:%d Ready:%d Mesh:%d Vis:%d"),
                                                         CMStats.Total,
                                                         CMStats.None,
                                                         CMStats.Pending,
                                                         CMStats.Generating,
                                                         CMStats.DataReady,
                                                         CMStats.MeshReady,
                                                         CMStats.Visible));

        // --- ONSCREEN DEBUG LINE 2: ChunkManager chunk sets contents ---
        GEngine->AddOnScreenDebugMessage(PlanetStatics::DebugKey_ManagerStats_2,
                                         0.f,
                                         GenTimeColor,
                                         FString::Printf(TEXT("[Pipeline] Deferred:%d LoadSet:%d RenderSet:%d Trans:%d"),
                                                         CMStats.Deferred,
                                                         CMStats.LoadSet,
                                                         CMStats.RenderSet,
                                                         CMStats.Transitions));

        // --- ONSCREEN DEBUG LINE 3: Chunk generation time ---
        // At 60fps, MinAge=12 gives you 200ms before a high-LOD chunk gets cancelled.
        // Red if avg exceeds that budget, yellow if close, green if safe.
        GEngine->AddOnScreenDebugMessage(
            PlanetStatics::DebugKey_GenTime,
            0.f,
            GenTimeColor,
            FString::Printf(TEXT("[Gen Time] Avg: %.0f ms | Last: %.0f ms | Budget(MinAge): %.0f ms"), CGStats.AvgGenMs, CGStats.LastGenMs, MinAgeBudgetMs));

        // --- ONSCREEN DEBUG LINE 4: ChunkGenerator stats ---
        GEngine->AddOnScreenDebugMessage(PlanetStatics::DebugKey_GeneratorStats,
                                         0.f,
                                         GenTimeColor,
                                         FString::Printf(TEXT("[Generator] Queued:%d Active:%d Cancelled:%d Threads:%d"),
                                                         CGStats.Queued,
                                                         CGStats.Active,
                                                         CGStats.Cancelled,
                                                         CGStats.ActiveThreads));


        // --- ONSCREEN DEBUG LINE 5: Per-LOD visible chunk breakdown ---
        TArray<int32> PerLODCount;
        PerLODCount.Init(0, m_planetConfig.MaxLOD + 1);
        m_chunkManager->GetVisibleCountPerLOD(PerLODCount);

        FString LODStr = TEXT("[LOD] ");
        for (int32 i = 0; i <= m_planetConfig.MaxLOD; ++i)
        {
            if (PerLODCount[i] > 0)
            {
                LODStr += FString::Printf(TEXT("L%d:%d  "), i, PerLODCount[i]);
            }
        }
        GEngine->AddOnScreenDebugMessage(PlanetStatics::DebugKey_LODBreakdown, 0.f, FColor::White, LODStr);

        // --- ONSCREEN DEBUG LINE 6: Next split distance for current LOD ---
        // Show how far the observer is from the next LOD transition
        int32 CurrentMaxLOD = 0;
        for (int32 i = 0; i <= m_planetConfig.MaxLOD; ++i)
            if (PerLODCount[i] > 0)
                CurrentMaxLOD = i;

        float NextSplitNodeSize = (m_planetConfig.PlanetRadius * PI * 0.5f) / (float)(1 << CurrentMaxLOD);
        float NextSplitDist = NextSplitNodeSize * m_planetConfig.LODSplitScreenFraction;
        float NextMergeDist = NextSplitNodeSize * m_planetConfig.LODSplitScreenFraction * m_planetConfig.LODMergeHysteresisRatio;
        float ClosestChunkDist = DistToSurface;  // approximation

        GEngine->AddOnScreenDebugMessage(PlanetStatics::DebugKey_LODThreshold,
                                         0.f,
                                         FColor::Yellow,
                                         FString::Printf(TEXT("[LOD Threshold] Split < %.0fm | Merge > %.0fm | Dist: %.0fm"),
                                                         NextSplitDist / 100.f,
                                                         NextMergeDist / 100.f,
                                                         ClosestChunkDist / 100.f));
    }
}


void APlanet::DrawPredictiveDebug(const PlanetViewContext &LocalContext) const
{
    const UWorld *World = GetWorld();
    if (!World)
        return;

    const FTransform PlanetTransform = GetActorTransform();
    const float Speed = LocalContext.ObserverVelocity.Size();
    const float AltitudeFactor = FMath::Clamp(LocalContext.AltitudeAboveSurface / FMath::Max(m_planetConfig.PredictiveMinAltitude, 1.f), 0.f, 1.f);
    const float LookAheadSeconds =
        FMath::Clamp(Speed / FMath::Max(m_planetConfig.PredictiveLookAheadScale, 1.f), 0.f, m_planetConfig.PredictiveLookAheadMaxSeconds) * AltitudeFactor;

    if (LookAheadSeconds <= KINDA_SMALL_NUMBER || LocalContext.ObserverVelocity.IsNearlyZero())
        return;

    const FVector RawPredicted = LocalContext.ObserverLocation + LocalContext.ObserverVelocity * LookAheadSeconds;
    const FVector PredictedLocal = RawPredicted.GetSafeNormal() * LocalContext.ObserverLocation.Size();
    const FVector PredictedWorld = PlanetTransform.TransformPosition(PredictedLocal);
    const FVector RealWorld = PlanetTransform.TransformPosition(LocalContext.ObserverLocation);

    // Predicted position — cyan sphere
    DrawDebugSphere(World, PredictedWorld, 200.f, 8, FColor::Yellow, false, -1.f);
    // Line from real to predicted
    DrawDebugLine(World, RealWorld, PredictedWorld, FColor::Yellow, false, -1.f, 0, 50.f);
    // // Lookahead seconds as a label approximation via sphere size
    // DrawDebugString(World,
    //                 PredictedWorld + FVector(0, 0, 300.f),
    //                 FString::Printf(TEXT("LookAhead: %.2fs | Alt: %.0f"), LookAheadSeconds, LocalContext.AltitudeAboveSurface),
    //                 nullptr,
    //                 FColor::White,
    //                 -1.f);
}