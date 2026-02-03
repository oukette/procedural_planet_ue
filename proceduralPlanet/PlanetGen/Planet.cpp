// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet.h"
#include "DrawDebugHelpers.h"


// Sets default values
APlanet::APlanet()
{
    // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
    PrimaryActorTick.bCanEverTick = false;

    TestsPassed = 0;
    TestsTotal = 0;

    // Create a root component so the actor has transform controls
    USceneComponent *SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
    RootComponent = SceneRoot;

    // Optional: Make root visible in editor
    SceneRoot->bVisualizeComponent = true;

    DebugMeshComponent = nullptr;
}


// Called when the game starts or when spawned
void APlanet::BeginPlay()
{
    Super::BeginPlay();

    // Run validation tests
    UE_LOG(LogTemp, Log, TEXT("=== Planet Math Validation Tests ==="));
    TestCubeSphereProjection();
    TestFaceContinuity();
    TestPrecision();
    TestEdgeCases();

    UE_LOG(LogTemp, Log, TEXT("=== Seed Utils Validation Tests ==="));
    TestSeedUtils();

    UE_LOG(LogTemp, Log, TEXT("=== Noise and Density Validation Tests ==="));
    TestNoiseAndDensity();

    // Summary
    UE_LOG(LogTemp, Log, TEXT("=== Test Summary ==="));
    UE_LOG(LogTemp, Log, TEXT("Passed: %d / %d"), TestsPassed, TestsTotal);

    if (TestsPassed == TestsTotal)
    {
        UE_LOG(LogTemp, Log, TEXT("✅ ALL TESTS PASSED"));

        // test density sampling
        TestDensitySampling();

        // Test vertex interp
        TestVertexInterpolation();

        // Generate and render a test chunk
        TestMarchingCubesClean();
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("❌ SOME TESTS FAILED"));
    }

    // Also print to screen for easy viewing
    if (GEngine)
    {
        FString ScreenMessage = FString::Printf(TEXT("Tests: %d/%d Passed"), TestsPassed, TestsTotal);

        GEngine->AddOnScreenDebugMessage(-1, 10.0f, (TestsPassed == TestsTotal) ? FColor::Green : FColor::Red, ScreenMessage);
    }
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


void APlanet::TestCubeSphereProjection()
{
    UE_LOG(LogTemp, Log, TEXT("--- Testing Cube-Sphere Projection ---"));

    // Test 1: Basic round-trip for random directions
    {
        const int NumTests = 100;
        int PassCount = 0;

        for (int i = 0; i < NumTests; i++)
        {
            // Generate random normalized direction
            FVector RandomDir = FVector(FMath::RandRange(-1.0f, 1.0f), FMath::RandRange(-1.0f, 1.0f), FMath::RandRange(-1.0f, 1.0f)).GetSafeNormal();

            uint8 Face;
            float U, V;

            // Sphere → Cube
            FPlanetMath::SphereToCubeFace(RandomDir, Face, U, V);

            // Cube → Sphere
            FVector ReconstructedDir = FPlanetMath::CubeFaceToSphere(Face, U, V);

            // Check if they match (within tolerance)
            float Error = (RandomDir - ReconstructedDir).Size();
            if (Error < 0.001f)
            {
                PassCount++;
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("   Round-trip error: %f for dir %s"), Error, *RandomDir.ToString());
            }
        }

        bool bPassed = (PassCount == NumTests);
        LogTest("Random Direction Round-Trip", bPassed, FString::Printf(TEXT("%d/%d passed"), PassCount, NumTests));
    }

    // Test 2: Test each face center
    {
        bool bAllPassed = true;

        for (uint8 Face = 0; Face < FPlanetMath::FaceCount; Face++)
        {
            // Face center should project to the face normal
            FVector CenterDir = FPlanetMath::CubeFaceToSphere(Face, 0.0f, 0.0f);
            FVector ExpectedNormal = FPlanetMath::CubeFaceNormals[Face];

            float Error = (CenterDir - ExpectedNormal).Size();
            if (Error > 0.001f)
            {
                bAllPassed = false;
                UE_LOG(LogTemp, Warning, TEXT("   Face %d center error: %f"), Face, Error);
            }
        }

        LogTest("Face Center Projection", bAllPassed);
    }

    // Test 3: Test cube corners (should map to sphere)
    {
        // Cube corners in local cube coordinates
        TArray<FVector> CubeCorners = {FVector(1, 1, 1),
                                       FVector(1, 1, -1),
                                       FVector(1, -1, 1),
                                       FVector(1, -1, -1),
                                       FVector(-1, 1, 1),
                                       FVector(-1, 1, -1),
                                       FVector(-1, -1, 1),
                                       FVector(-1, -1, -1)};

        bool bAllPassed = true;

        for (const FVector &Corner : CubeCorners)
        {
            FVector SphereDir = Corner.GetSafeNormal();
            uint8 Face;
            float U, V;

            FPlanetMath::SphereToCubeFace(SphereDir, Face, U, V);

            // Corner should map to U=±1, V=±1 on some face
            if (FMath::Abs(FMath::Abs(U) - 1.0f) > 0.001f || FMath::Abs(FMath::Abs(V) - 1.0f) > 0.001f)
            {
                bAllPassed = false;
                UE_LOG(LogTemp, Warning, TEXT("   Corner %s -> Face %d, UV=(%f, %f)"), *Corner.ToString(), Face, U, V);
            }
        }

        LogTest("Cube Corner Mapping", bAllPassed);
    }
}


void APlanet::TestFaceContinuity()
{
    UE_LOG(LogTemp, Log, TEXT("--- Testing Face Continuity ---"));

    // Test 1: Seam between +X and +Z faces
    {
        // Start at the seam between +X and +Z faces
        // Direction that's exactly between them: (1, 0, 1).GetSafeNormal()
        FVector SeamDir = FVector(1, 0, 1).GetSafeNormal();

        uint8 Face;
        float U, V;
        FPlanetMath::SphereToCubeFace(SeamDir, Face, U, V);

        // This direction could belong to either +X or +Z face
        // Both are acceptable
        bool bValidFace = (Face == FPlanetMath::FaceX_Pos || Face == FPlanetMath::FaceZ_Pos);

        LogTest("Seam Direction Handling", bValidFace, FString::Printf(TEXT("Face %d, UV=(%f, %f)"), Face, U, V));
    }

    // Test 2: Small perturbation across face boundary
    {
        // We need to test that points near a seam map to nearby positions on the sphere
        // regardless of which face they're assigned to

        // Take a direction near the +X/+Z seam
        FVector NearSeam = FVector(1, 0, 0.99f).GetSafeNormal();  // Just inside +X face

        uint8 Face1;
        float U1, V1;
        FPlanetMath::SphereToCubeFace(NearSeam, Face1, U1, V1);

        // Take another direction also near the seam but from the other side
        FVector OtherSide = FVector(0.99f, 0, 1).GetSafeNormal();  // Just inside +Z face

        uint8 Face2;
        float U2, V2;
        FPlanetMath::SphereToCubeFace(OtherSide, Face2, U2, V2);

        // These two points should be close on the sphere
        float Distance = (NearSeam - OtherSide).Size();

        LogTest("Face Boundary Proximity",
                Distance < 0.1f,  // They should be close
                FString::Printf(TEXT("Distance: %f, Faces: %d and %d"), Distance, Face1, Face2));
    }

    // Test 3: Walk across a face edge
    {
        // Walk from U=-0.99 to U=0.99 across V=0 on +X face
        // Check that directions change smoothly
        const int Steps = 20;
        float MaxAngleChange = 0.0f;

        FVector PrevDir = FVector::ZeroVector;

        for (int i = 0; i <= Steps; i++)
        {
            float U = FMath::Lerp(-0.99f, 0.99f, i / (float)Steps);
            FVector Dir = FPlanetMath::CubeFaceToSphere(FPlanetMath::FaceX_Pos, U, 0.0f);

            if (i > 0)
            {
                float AngleChange = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(PrevDir, Dir)));
                MaxAngleChange = FMath::Max(MaxAngleChange, AngleChange);
            }

            PrevDir = Dir;
        }

        // Angle change between steps should be small and consistent
        float ExpectedAnglePerStep = 180.0f / Steps;  // Approximate
        bool bSmooth = (MaxAngleChange < ExpectedAnglePerStep * 1.5f);

        LogTest("Smooth Face Traversal", bSmooth, FString::Printf(TEXT("Max angle change: %f degrees"), MaxAngleChange));
    }
}


void APlanet::TestPrecision()
{
    UE_LOG(LogTemp, Log, TEXT("--- Testing Precision ---"));

    // Test 1: Normalization preservation
    {
        const int NumTests = 50;
        int PassCount = 0;

        for (int i = 0; i < NumTests; i++)
        {
            float U = FMath::RandRange(-1.0f, 1.0f);
            float V = FMath::RandRange(-1.0f, 1.0f);
            uint8 Face = FMath::RandRange(0, FPlanetMath::FaceCount - 1);

            FVector Dir = FPlanetMath::CubeFaceToSphere(Face, U, V);

            // Check if normalized
            float LengthError = FMath::Abs(Dir.Size() - 1.0f);

            if (LengthError < 0.0001f)
            {
                PassCount++;
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("   Normalization error: %f for Face %d, UV=(%f, %f)"), LengthError, Face, U, V);
            }
        }

        LogTest("Normalization Precision", PassCount == NumTests, FString::Printf(TEXT("%d/%d within tolerance"), PassCount, NumTests));
    }

    // Test 2: Double precision helpers
    {
        FVector A(1000000.0f, 2000000.0f, 3000000.0f);
        FVector B(4000000.0f, 5000000.0f, 6000000.0f);

        // Single precision dot product
        float Dot32 = FVector::DotProduct(A, B);

        // Double precision dot product
        double Dot64 = FPlanetMath::DotProduct64(A, B);

        // Relative error
        double RelError = FMath::Abs(Dot64 - (double)Dot32) / FMath::Abs(Dot64);

        LogTest("Double Precision Dot Product",
                RelError < 1e-6,  // Should be very small
                FString::Printf(TEXT("Rel error: %e"), RelError));
    }

    // Test 3: Stretch factor sanity
    {
        bool bAllValid = true;

        for (uint8 Face = 0; Face < FPlanetMath::FaceCount; Face++)
        {
            for (float U = -1.0f; U <= 1.0f; U += 0.5f)
            {
                for (float V = -1.0f; V <= 1.0f; V += 0.5f)
                {
                    float Stretch = FPlanetMath::ComputeStretchFactor(Face, U, V);

                    // Updated bounds: 0.65 to 1.05 (allowing some tolerance)
                    if (Stretch < 0.65f || Stretch > 1.05f)
                    {
                        bAllValid = false;
                        UE_LOG(LogTemp, Warning, TEXT("   Bad stretch factor: %f for Face %d, UV=(%f, %f)"), Stretch, Face, U, V);
                    }
                }
            }
        }

        LogTest("Stretch Factor Bounds", bAllValid);
    }
}


void APlanet::TestEdgeCases()
{
    UE_LOG(LogTemp, Log, TEXT("--- Testing Edge Cases ---"));

    // Test 1: Zero vector (should handle gracefully)
    {
        FVector ZeroVec = FVector::ZeroVector;
        uint8 Face;
        float U, V;

        // This should not crash
        FPlanetMath::SphereToCubeFace(ZeroVec, Face, U, V);

        // Result is undefined but shouldn't crash
        LogTest("Zero Vector Handling", true, "No crash on zero vector");
    }

    // Test 2: Very small vectors
    {
        FVector TinyVec(1e-10f, 2e-10f, 3e-10f);
        uint8 Face;
        float U, V;

        FPlanetMath::SphereToCubeFace(TinyVec, Face, U, V);

        // Should assign to some face (implementation dependent)
        LogTest("Tiny Vector Handling", Face < FPlanetMath::FaceCount, FString::Printf(TEXT("Assigned to face %d"), Face));
    }

    // Test 3: Poles (Z faces)
    {
        // North pole
        FVector NorthPole = FVector(0, 0, 1);
        uint8 Face;
        float U, V;

        FPlanetMath::SphereToCubeFace(NorthPole, Face, U, V);

        bool bIsZPos = (Face == FPlanetMath::FaceZ_Pos);

        LogTest("North Pole Mapping", bIsZPos, FString::Printf(TEXT("Face %d, UV=(%f, %f)"), Face, U, V));

        // South pole
        FVector SouthPole = FVector(0, 0, -1);
        FPlanetMath::SphereToCubeFace(SouthPole, Face, U, V);

        bool bIsZNeg = (Face == FPlanetMath::FaceZ_Neg);

        LogTest("South Pole Mapping", bIsZNeg, FString::Printf(TEXT("Face %d, UV=(%f, %f)"), Face, U, V));
    }

    // Test 4: UV clamping at extremes
    {
        // Try UV values outside [-1, 1]
        FVector Dir = FPlanetMath::CubeFaceToSphere(FPlanetMath::FaceX_Pos, 1.5f, -1.5f);

        // Should still produce a valid normalized direction
        float LengthError = FMath::Abs(Dir.Size() - 1.0f);

        LogTest("UV Clamping", LengthError < 0.001f, FString::Printf(TEXT("Length error: %f"), LengthError));
    }
}


void APlanet::TestSeedUtils()
{
    UE_LOG(LogTemp, Log, TEXT("--- Testing Seed Utils ---"));

    // Test 1: Determinism
    {
        uint64 Seed = 123456789;
        float Result1 = FSeedUtils::RandomFloat(Seed);
        float Result2 = FSeedUtils::RandomFloat(Seed);

        LogTest("Deterministic Random", FMath::Abs(Result1 - Result2) < 1e-6f, FString::Printf(TEXT("Results: %f vs %f"), Result1, Result2));
    }

    // Test 2: Spatial hashing consistency
    {
        uint64 Seed = 987654321;
        uint64 Hash1 = FSeedUtils::HashPosition(100.0f, 200.0f, 300.0f, Seed);
        uint64 Hash2 = FSeedUtils::HashPosition(100.0f, 200.0f, 300.0f, Seed);

        LogTest("Spatial Hash Consistency", Hash1 == Hash2, FString::Printf(TEXT("Hashes: %llu vs %llu"), Hash1, Hash2));
    }

    // Test 3: Chunk seed derivation
    {
        uint64 PlanetSeed = 555555;
        uint64 ChunkSeed1 = FSeedUtils::GetChunkSeed(PlanetSeed, 0, 2, 10, 20);
        uint64 ChunkSeed2 = FSeedUtils::GetChunkSeed(PlanetSeed, 0, 2, 10, 20);
        uint64 DifferentSeed = FSeedUtils::GetChunkSeed(PlanetSeed, 1, 2, 10, 20);

        bool bSame = (ChunkSeed1 == ChunkSeed2);
        bool bDifferent = (ChunkSeed1 != DifferentSeed);

        LogTest("Chunk Seed Logic", bSame && bDifferent, FString::Printf(TEXT("Same: %d, Different: %d"), bSame, bDifferent));
    }

    // Test 4: Random range
    {
        uint64 Seed = 111111;
        float Min = 10.0f;
        float Max = 20.0f;
        float Value = FSeedUtils::RandomFloat(Seed, Min, Max);

        bool bInRange = (Value >= Min && Value < Max);

        LogTest("Random Range", bInRange, FString::Printf(TEXT("Value: %f in [%f, %f)"), Value, Min, Max));
    }
}


void APlanet::TestNoiseAndDensity()
{
    UE_LOG(LogTemp, Log, TEXT("--- Testing Noise & Density ---"));

    // Test 1: Noise determinism
    {
        uint64 Seed = 123456;
        auto Noise = MakeShared<FSimpleNoise>(Seed);

        FVector Pos(100.0f, 200.0f, 300.0f);

        // Create noise context
        FNoiseContext Context1(Pos, 1000.0, Seed);
        FNoiseContext Context2(Pos, 1000.0, Seed);  // Same inputs

        float Val1 = Noise->Sample(Context1, 0.001f, 0);
        float Val2 = Noise->Sample(Context2, 0.001f, 0);

        LogTest("Noise Determinism", FMath::Abs(Val1 - Val2) < 1e-6f, FString::Printf(TEXT("Values: %f vs %f"), Val1, Val2));
    }

    // Test 2: Density generator basics
    {
        uint64 Seed = 987654;
        auto TerrainNoise = MakeShared<FSimpleNoise>(Seed);

        FDensityGenerator::FParameters Params;
        Params.Radius = 1000.0f;
        Params.TerrainAmplitude = 100.0f;
        Params.TerrainFrequency = 0.001f;

        FDensityGenerator DensityGen(Params, TerrainNoise);

        // Test at planet surface (WITHOUT terrain, to verify sphere SDF)
        FVector SurfacePoint(0.0f, 0.0f, 1000.0f);

        // First test base sphere without terrain
        float BaseDensity = DensityGen.SampleBaseSphere(SurfacePoint);

        LogTest("Base Sphere SDF", FMath::Abs(BaseDensity) < 0.001f, FString::Printf(TEXT("Base density at surface: %f (should be ~0)"), BaseDensity));

        // Now test with terrain - it will be non-zero
        float Density = DensityGen.SampleDensity(SurfacePoint);

        // Should be in reasonable range [-Amplitude, Amplitude] roughly
        bool bReasonable = FMath::Abs(Density) < Params.TerrainAmplitude * 1.5f;

        LogTest("Surface Density with Terrain", bReasonable, FString::Printf(TEXT("Density at surface: %f (reasonable range)"), Density));

        // Test inside planet
        FVector InsidePoint(0.0f, 0.0f, 500.0f);
        float InsideDensity = DensityGen.SampleDensity(InsidePoint);

        LogTest("Inside Planet", InsideDensity < 0.0f, FString::Printf(TEXT("Density inside: %f (should be negative)"), InsideDensity));

        // Test outside planet (well outside terrain range)
        FVector OutsidePoint(0.0f, 0.0f, 1500.0f);
        float OutsideDensity = DensityGen.SampleDensity(OutsidePoint);

        LogTest("Outside Planet", OutsideDensity > 0.0f, FString::Printf(TEXT("Density outside: %f (should be positive)"), OutsideDensity));
    }

    // Test 3: Noise range
    {
        uint64 Seed = 555555;
        auto Noise = MakeShared<FSimpleNoise>(Seed);

        bool bAllInRange = true;
        for (int i = 0; i < 100; i++)
        {
            FVector RandomPos = FVector(FMath::RandRange(-1000.0f, 1000.0f), FMath::RandRange(-1000.0f, 1000.0f), FMath::RandRange(-1000.0f, 1000.0f));

            // Create context for this position
            FNoiseContext Context(RandomPos, 1000.0, Seed);

            float NoiseVal = Noise->SampleFractal(Context);

            if (NoiseVal < -1.1f || NoiseVal > 1.1f)  // Some tolerance
            {
                bAllInRange = false;
                break;
            }
        }

        LogTest("Noise Value Range", bAllInRange);
    }
}


void APlanet::TestDensitySampling()
{
    UE_LOG(LogTemp, Log, TEXT("--- Quick Density Test ---"));

    // Simple sphere
    FDensityGenerator::FParameters Params;
    Params.Radius = 100.0f;
    Params.TerrainAmplitude = 0.0f;

    auto NullNoise = MakeShared<FSimpleNoise>(0);
    FDensityGenerator DensityGen(Params, NullNoise);

    // Test points
    FVector Center(0, 0, 0);
    FVector Surface(100, 0, 0);  // Exactly at radius
    FVector Inside(50, 0, 0);    // Inside sphere
    FVector Outside(150, 0, 0);  // Outside sphere

    UE_LOG(LogTemp, Log, TEXT("Center (0,0,0): %f"), DensityGen.SampleDensity(Center));
    UE_LOG(LogTemp, Log, TEXT("Surface (100,0,0): %f"), DensityGen.SampleDensity(Surface));
    UE_LOG(LogTemp, Log, TEXT("Inside (50,0,0): %f"), DensityGen.SampleDensity(Inside));
    UE_LOG(LogTemp, Log, TEXT("Outside (150,0,0): %f"), DensityGen.SampleDensity(Outside));

    // Also test chunk origin position
    FVector ChunkOrigin(75, 0, 0);
    UE_LOG(LogTemp, Log, TEXT("ChunkOrigin (75,0,0): %f"), DensityGen.SampleDensity(ChunkOrigin));
}


void APlanet::TestMarchingCubesClean()
{
    UE_LOG(LogTemp, Log, TEXT("=== CLEAN MARCHING CUBES TEST ==="));

    // 1. PLANET POSITION (in world space)
    FVector PlanetWorldPosition = GetActorLocation();
    UE_LOG(LogTemp, Log, TEXT("Planet at world position: %s"), *PlanetWorldPosition.ToString());

    // 2. Create density generator with planet position
    FDensityGenerator::FParameters Params;
    Params.PlanetPosition = PlanetWorldPosition;  // CRITICAL!
    Params.Radius = 200.0f;
    Params.TerrainAmplitude = 0.0f;  // No noise for clean test

    // Draw planet and its center
    DrawDebugSphere(GetWorld(), Params.PlanetPosition, Params.Radius, 24, FColor::Red, true, 30.0f);
    DrawDebugPoint(GetWorld(), Params.PlanetPosition, 5.0f, FColor::Red, true, 30.0f);

    auto NullNoise = MakeShared<FSimpleNoise>(123);
    FDensityGenerator DensityGen(Params, NullNoise);

    // 3. Create a chunk RELATIVE TO PLANET
    //    Chunk center is 150 units along +X from planet center
    FVector ChunkCenterRelativeToPlanet = FVector(150, 0, 0);

    // Convert to world for visualization
    FVector ChunkWorldCenter = PlanetWorldPosition + ChunkCenterRelativeToPlanet;

    // Draw chunk center
    DrawDebugSphere(GetWorld(), ChunkWorldCenter, 5.0f, 8, FColor::Green, true, 30.0f);
    DrawDebugLine(GetWorld(), PlanetWorldPosition, ChunkWorldCenter, FColor::Yellow, true, 30.0f);

    // 4. Chunk orientation (simple axes)
    FVector LocalX = FVector(1, 0, 0);
    FVector LocalY = FVector(0, 1, 0);
    FVector LocalZ = FVector(0, 0, 1);

    // 5. Marching Cubes config
    FMarchingCubes::FConfig MCConfig;
    MCConfig.GridResolution = FIntVector(9, 9, 9);  // Small
    MCConfig.CellSize = 25.0f;
    MCConfig.IsoLevel = 0.0f;
    MCConfig.bUseGhostLayers = true;

    // Draw chunk bounds
    FVector ChunkExtent = FVector(MCConfig.GridResolution.X * MCConfig.CellSize * 0.5f,
                                  MCConfig.GridResolution.Y * MCConfig.CellSize * 0.5f,
                                  MCConfig.GridResolution.Z * MCConfig.CellSize * 0.5f);
    DrawDebugBox(GetWorld(), ChunkWorldCenter, ChunkExtent, FColor::Blue, true, 30.0f);

    // 6. Generate mesh
    FMarchingCubes MarchingCubes;
    FChunkMeshData MeshData;

    MarchingCubes.GenerateMesh(DensityGen,
                               ChunkCenterRelativeToPlanet,  // Relative to planet!
                               LocalX,
                               LocalY,
                               LocalZ,
                               MCConfig,
                               MeshData);

    // 7. Log results
    UE_LOG(LogTemp, Log, TEXT("Mesh: %d vertices, %d triangles"), MeshData.GetVertexCount(), MeshData.GetTriangleCount());

    if (!MeshData.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("No mesh generated!"));
        return;
    }

    // 8. Create and position mesh component
    //    Mesh vertices are in CHUNK-LOCAL SPACE
    //    So we position component at CHUNK WORLD CENTER
    if (!DebugMeshComponent)
    {
        DebugMeshComponent = NewObject<UProceduralMeshComponent>(this, TEXT("DebugMesh"));
    }

    if (DebugMeshComponent)
    {
        DebugMeshComponent->ClearAllMeshSections();
        DebugMeshComponent->SetupAttachment(RootComponent);
        DebugMeshComponent->RegisterComponent();

        // Create mesh section
        DebugMeshComponent->CreateMeshSection(0,
                                              MeshData.Vertices,  // Chunk-local vertices
                                              MeshData.Triangles,
                                              MeshData.Normals,
                                              MeshData.UVs,
                                              TArray<FColor>(),
                                              TArray<FProcMeshTangent>(),
                                              true);

        // POSITION CRITICAL: Set component to chunk world position
        DebugMeshComponent->SetWorldLocation(ChunkWorldCenter);

        // Add material
        static UMaterialInterface *Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        if (Material)
            DebugMeshComponent->SetMaterial(0, Material);

        UE_LOG(LogTemp, Log, TEXT("✅ Mesh rendered at chunk world position"));

        // Draw first vertex position (should be near chunk center)
        if (MeshData.Vertices.Num() > 0)
        {
            FVector FirstVertexLocal = MeshData.Vertices[0];
            FVector FirstVertexWorld = ChunkWorldCenter + FirstVertexLocal;
            DrawDebugSphere(GetWorld(), FirstVertexWorld, 2.0f, 8, FColor::Cyan, true, 30.0f);

            UE_LOG(LogTemp, Log, TEXT("First vertex: Local=%s, World=%s"), *FirstVertexLocal.ToString(), *FirstVertexWorld.ToString());
        }
    }
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
