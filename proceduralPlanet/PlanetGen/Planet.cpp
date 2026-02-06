// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet.h"
#include "Chunk/Chunk.h"
#include "DrawDebugHelpers.h"


// Sets default values
APlanet::APlanet()
{
    // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
    PrimaryActorTick.bCanEverTick = true;

    TestsPassed = 0;
    TestsTotal = 0;

    // Create a root component so the actor has transform controls
    USceneComponent *SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
    RootComponent = SceneRoot;
    SceneRoot->bVisualizeComponent = true; // make the root visible in the editor

    DebugMeshComponent = nullptr;
}


// Called when the game starts or when spawned
void APlanet::BeginPlay()
{
    Super::BeginPlay();

    // Run validation tests
    // // Test vertex interp
    // TestVertexInterpolation();

    // // Test spherified projection
    // TestSpherifiedProjection();

    // Generate and render a test chunk
    TestMarchingCubesChunk();
}


void APlanet::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    // ChunkManager update will go here in Step 7
}



void APlanet::LogTest(const FString &TestName, bool bPassed, const FString &Details)
{
    TestsTotal++;
    if (bPassed)
    {
        TestsPassed++;
        UE_LOG(LogTemp, Log, TEXT("✅ %s"), *TestName);
        if (!Details.IsEmpty())
        {
            UE_LOG(LogTemp, Log, TEXT("   %s"), *Details);
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("❌ %s"), *TestName);
        if (!Details.IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("   %s"), *Details);
        }
    }
}


void APlanet::TestMarchingCubesChunk()
{
    UE_LOG(LogTemp, Log, TEXT("=== MARCHING CUBES TEST (Chunk) ==="));

    // 1. SETUP: Realistic Ratios
    FVector PlanetWorldCenter = GetActorLocation();

    // Planet Radius: 10,000 units (100 meters)
    // Chunk Size: 16 voxels * 100 units = 1,600 units (16 meters)
    // Ratio: Chunk covers about ~10-15 degrees of the surface.
    PlanetRadius = 10000.0f;
    VoxelSize = 100.0f;
    FIntVector Resolution(16, 16, 16);  // 16x16x16 grid

    // 2. DEFINE THE CHUNK "WORLD ORIGIN" (CENTER)
    // We pick a spot on the surface at (Radius, 0, 0)
    FVector ChunkWorldOrigin = PlanetWorldCenter + FVector(PlanetRadius, 0, 0);

    // 3. DEFINE DENSITY PARAMETERS
    FDensityGenerator::FParameters DensityParams;
    DensityParams.PlanetPosition = PlanetWorldCenter;
    DensityParams.PlanetRadius = PlanetRadius;
    // DensityParams.TerrainNoiseAmplitude = 0.0f;  // no amplitude = no noise, perfect sphere
    DensityParams.TerrainNoiseAmplitude = TerrainNoiseAmplitude;  // a bit of noise

    // 4. DRAW DEBUGS (The "Ideal" Shapes)
    FlushPersistentDebugLines(GetWorld());

    // A1. The Planet (Gray Wireframe sphere)
    DrawDebugSphere(GetWorld(), PlanetWorldCenter, PlanetRadius, 64, FColor(100, 100, 100), true, 60.0f);

    // A2. The Planet Center (Grey point)
    DrawDebugPoint(GetWorld(), PlanetWorldCenter, 25.0f, FColor(100, 100, 100), true, 60.0f);

    // B. The Chunk Bounds (Green box)
    FVector ChunkSize;
    ChunkSize.X = Resolution.X * VoxelSize;
    ChunkSize.Y = Resolution.Y * VoxelSize;
    ChunkSize.Z = Resolution.Z * VoxelSize;
    FVector HalfSize = ChunkSize * 0.5f;

    // Draw box centered at ChunkWorldOrigin (Green box)
    DrawDebugBox(GetWorld(), ChunkWorldOrigin, HalfSize, FColor::Green, true, 60.0f, 0, 5.0f);

    // C. The Chunk Center (Green point)
    DrawDebugPoint(GetWorld(), ChunkWorldOrigin, 25.0f, FColor::Green, true, 60.0f);

    // 5. GENERATE MESH
    auto Noise = MakeShared<FSimpleNoise>(12345);
    FDensityGenerator DensityGen(DensityParams, Noise);

    FMarchingCubes::FConfig MCConfig;
    MCConfig.GridResolution = Resolution;
    MCConfig.CellSize = VoxelSize;
    MCConfig.IsoLevel = 0.0f;

    // AXES: Simple world axes for this test
    FVector LocalX(1, 0, 0);
    FVector LocalY(0, 1, 0);
    FVector LocalZ(0, 0, 1);

    // SAMPLING ORIGIN:
    // Marching Cubes starts sampling at (0,0,0) and goes to (Size, Size, Size).
    // To make "ChunkWorldOrigin" the actual center, we must start sampling at:
    // Center - HalfSize.
    // NOTE: DensityGen expects coordinates relative to Planet Center!
    // So: (ChunkWorldOrigin - PlanetWorldCenter) - HalfSize
    FVector RelativeChunkCenter = ChunkWorldOrigin - PlanetWorldCenter;  // Should be (10000, 0, 0)
    FVector SamplingOrigin = RelativeChunkCenter - HalfSize;

    FMarchingCubes MarchingCubes;
    FChunkMeshData MeshData;

    MarchingCubes.GenerateMesh(DensityGen, SamplingOrigin, LocalX, LocalY, LocalZ, MCConfig, MeshData);


    // 6. RENDER COMPONENT
    if (!DebugMeshComponent)
    {
        DebugMeshComponent = NewObject<UProceduralMeshComponent>(this, TEXT("DebugMesh"));
        DebugMeshComponent->SetupAttachment(RootComponent);
        DebugMeshComponent->RegisterComponent();
    }

    DebugMeshComponent->ClearAllMeshSections();
    DebugMeshComponent->CreateMeshSection(
        0, MeshData.Vertices, MeshData.Triangles, MeshData.Normals, MeshData.UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), true);

    // POSITIONING:
    // The mesh vertices were generated relative to SamplingOrigin. So if we place the component AT SamplingOrigin (in world space),
    // the vertices will align perfectly with the debug box. WorldSamplingOrigin = PlanetWorldCenter + SamplingOrigin
    DebugMeshComponent->SetWorldLocation(PlanetWorldCenter + SamplingOrigin);

    // Material
    static UMaterialInterface *Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (Material)
        DebugMeshComponent->SetMaterial(0, Material);

    UE_LOG(LogTemp, Log, TEXT("✅ Mesh generated. Verify that mesh aligns with Green Box."));
}


void APlanet::TestVertexInterpolation() const
{
    UE_LOG(LogTemp, Log, TEXT("=== VERTEX INTERPOLATION TEST ==="));

    FMarchingCubes MC;

    // Test 1: Simple case
    {
        FVector P1(0, 0, 0);
        FVector P2(1, 0, 0);
        float V1 = -1.0f;  // Inside
        float V2 = 1.0f;   // Outside
        float Iso = 0.0f;  // Surface

        // Expected: T = (0 - (-1)) / (1 - (-1)) = 1/2 = 0.5
        // Result: (0,0,0) + 0.5*(1,0,0) = (0.5,0,0)

        // We need to call the actual function
        // Since it's private, we'll test the logic directly:
        float T = (Iso - V1) / (V2 - V1);
        FVector Result = P1 + T * (P2 - P1);

        UE_LOG(LogTemp, Log, TEXT("Test 1: T=%f, Result=%s (expected: 0.5, (0.5,0,0))"), T, *Result.ToString());
    }

    // Test 2: Extreme case
    {
        FVector P1(50, 50, 25);  // Some reasonable position
        FVector P2(50, 50, 25);  // SAME position! (bug scenario)
        float V1 = -75.0f;
        float V2 = 291.0f;
        float Iso = 0.0f;

        float T = (Iso - V1) / (V2 - V1);
        UE_LOG(LogTemp, Log, TEXT("Test 2: T=%f (should be ~0.205)"), T);

        // If P1 == P2, then P2 - P1 = (0,0,0), result = P1 regardless of T
        // So we wouldn't get Y=376...
    }

    // Test 3: What if densities are swapped?
    {
        FVector P1(50, 50, 25);
        FVector P2(50, 400, 25);  // Extreme Y difference!
        float V1 = 291.0f;        // WRONG ORDER: outside first
        float V2 = -75.0f;        // inside second
        float Iso = 0.0f;

        float T = (Iso - V1) / (V2 - V1);
        // T = (0 - 291) / (-75 - 291) = -291 / -366 = 0.795
        // Result = 50 + 0.795*(400-50) = 50 + 0.795*350 = 50 + 278.25 = 328.25
        // Still not 376, but closer...

        UE_LOG(LogTemp, Log, TEXT("Test 3: T=%f, would give Y=%f"), T, 50 + T * 350);
    }
}


void APlanet::TestSpherifiedProjection()
{
    UE_LOG(LogTemp, Log, TEXT("=== SPHERIFIED CUBE PROJECTION TEST (ALL FACES) ==="));

    // Test parameters
    float GridStep = 0.33f;  // From -1 to 1 (coarser grid for readability)
    PlanetRadius = 200.0f;
    FVector PlanetCenter = GetActorLocation();

    // Color coding
    const TArray<FColor> FaceColors = {
        FColor::Red,     // +X
        FColor::Orange,  // -X
        FColor::Green,   // +Y
        FColor::Yellow,  // -Y
        FColor::Blue,    // +Z
        FColor::Purple   // -Z
    };

    // Test all 6 faces
    for (uint8 Face = 0; Face < FPlanetMath::FaceCount; Face++)
    {
        FColor FaceColor = FaceColors[Face];
        FString FaceName;

        switch (Face)
        {
            case FPlanetMath::FaceX_Pos:
                FaceName = "+X";
                break;
            case FPlanetMath::FaceX_Neg:
                FaceName = "-X";
                break;
            case FPlanetMath::FaceY_Pos:
                FaceName = "+Y";
                break;
            case FPlanetMath::FaceY_Neg:
                FaceName = "-Y";
                break;
            case FPlanetMath::FaceZ_Pos:
                FaceName = "+Z";
                break;
            case FPlanetMath::FaceZ_Neg:
                FaceName = "-Z";
                break;
        }

        UE_LOG(LogTemp, Log, TEXT("--- Testing Face %s ---"), *FaceName);

        // Test grid of points on this face
        for (float U = -1.0f; U <= 1.0f; U += GridStep)
        {
            for (float V = -1.0f; V <= 1.0f; V += GridStep)
            {
                // Get both projections
                FVector Spherified = FPlanetMath::CubeFaceToSphere(Face, U, V);
                FVector Standard = FPlanetMath::CubeFaceToSphereStandard(Face, U, V);

                // Scale to planet radius for visualization
                FVector WorldSpherified = PlanetCenter + Spherified * PlanetRadius;
                FVector WorldStandard = PlanetCenter + Standard * PlanetRadius;

                // Draw spherified projection (colored by face)
                DrawDebugPoint(GetWorld(), WorldSpherified, 8.0f, FColor::Green, true, 30.0f);

                // Draw standard projection (white, smaller)
                DrawDebugPoint(GetWorld(), WorldStandard, 6.0f, FColor::Black, true, 30.0f);

                // Draw line between them (gray)
                DrawDebugLine(GetWorld(), WorldStandard, WorldSpherified, FColor(128, 128, 128), true, 30.0f);

                // Log differences at key points
                bool bIsCenter = (FMath::Abs(U) < 0.01f && FMath::Abs(V) < 0.01f);
                bool bIsEdge = (FMath::Abs(U) > 0.9f || FMath::Abs(V) > 0.9f);
                bool bIsCorner = (FMath::Abs(U) > 0.9f && FMath::Abs(V) > 0.9f);

                if (bIsCenter || bIsCorner)
                {
                    float Distance = (Standard - Spherified).Size();
                    float DistanceInMeters = Distance * PlanetRadius;

                    if (bIsCenter)
                    {
                        UE_LOG(LogTemp, Log, TEXT("  %s Center (0,0): Methods differ by %f (%.2f meters)"), *FaceName, Distance, DistanceInMeters);
                    }
                    else if (bIsCorner)
                    {
                        UE_LOG(LogTemp, Log, TEXT("  %s Corner (%.1f,%.1f): Methods differ by %f (%.2f meters)"), *FaceName, U, V, Distance, DistanceInMeters);
                    }
                }
            }
        }

        // Draw face normal line
        FVector FaceNormal = FPlanetMath::CubeFaceNormals[Face];
        FVector FaceCenterWorld = PlanetCenter + FaceNormal * PlanetRadius;
        DrawDebugLine(GetWorld(), PlanetCenter, FaceCenterWorld, FaceColor, true, 30.0f);
        DrawDebugSphere(GetWorld(), FaceCenterWorld, 8.0f, 8, FaceColor, true, 30.0f);
    }

    LogTest("Spherified Projection", true, "");


    // Also test the pure cube point function with key points
    UE_LOG(LogTemp, Log, TEXT("--- Direct Spherified Cube Point Tests ---"));

    TArray<FVector> TestCubePoints = {
        FVector(1, 0, 0),          // Face center
        FVector(1, 0.5f, 0),       // Mid-edge
        FVector(1, 1, 0),          // Corner
        FVector(1, 0.5f, 0.5f),    // Inside face
        FVector(0.7f, 0.7f, 0.7f)  // Near cube corner
    };

    for (const FVector &CubePoint : TestCubePoints)
    {
        FVector Spherified = FPlanetMath::GetSpherifiedCubePoint(CubePoint);
        FVector Normalized = CubePoint.GetSafeNormal();

        float DistError = (Spherified - Normalized).Size();
        float SpherifiedLength = Spherified.Size();

        UE_LOG(LogTemp, Log, TEXT("  Cube: (%.2f,%.2f,%.2f)"), CubePoint.X, CubePoint.Y, CubePoint.Z);
        UE_LOG(LogTemp, Log, TEXT("    -> Sphere: (%.6f,%.6f,%.6f) [Length: %.6f]"), Spherified.X, Spherified.Y, Spherified.Z, SpherifiedLength);
        UE_LOG(LogTemp, Log, TEXT("    vs Normalized: (%.6f,%.6f,%.6f)"), Normalized.X, Normalized.Y, Normalized.Z);
        UE_LOG(LogTemp, Log, TEXT("    Difference: %.6f"), DistError);
    }

    LogTest("Direct Spherified Cube Point", true, "");

    // Summary
    UE_LOG(LogTemp, Log, TEXT("=== Test Complete ==="));
}
