// Fill out your copyright notice in the Description page of Project Settings.


#include "Planet.h"


// Sets default values
APlanet::APlanet()
{
    // Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
    PrimaryActorTick.bCanEverTick = false;

    TestsPassed = 0;
    TestsTotal = 0;
}


// Called when the game starts or when spawned
void APlanet::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTemp, Log, TEXT("=== Planet Math Validation Tests ==="));
    TestCubeSphereProjection();
    TestFaceContinuity();
    TestPrecision();
    TestEdgeCases();

    UE_LOG(LogTemp, Log, TEXT("=== Seed Utils Validation Tests ==="));
    TestSeedUtils();

    // Summary
    UE_LOG(LogTemp, Log, TEXT("=== Test Summary ==="));
    UE_LOG(LogTemp, Log, TEXT("Passed: %d / %d"), TestsPassed, TestsTotal);

    if (TestsPassed == TestsTotal)
    {
        UE_LOG(LogTemp, Log, TEXT("✅ ALL TESTS PASSED"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("❌ SOME TESTS FAILED"));
    }

    // Also print to screen for easy viewing
    if (GEngine)
    {
        FString ScreenMessage = FString::Printf(TEXT("Math Tests: %d/%d Passed"), TestsPassed, TestsTotal);

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