// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MathUtils.h"
#include "SeedUtils.h"
#include "SimpleNoise.h"
#include "DensityGenerator.h"
#include "MarchingCubes.h"
#include "ChunkManager.h"
#include "ProceduralMeshComponent.h"
#include "Planet.generated.h"


UCLASS()
class PROCEDURALPLANET_API APlanet : public AActor
{
        GENERATED_BODY()

    public:
        // Sets default values for this actor's properties
        APlanet();

        // === PLANET PARAMETERS ===
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Core")
        float PlanetRadius = 10000.0f;  // 100m in UE units

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Core")
        float PlanetMass = 1.0f;  // Arbitrary units for now

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Core")
        int32 PlanetSeed = 12345;

        // === CHUNK PARAMETERS ===
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Terrain")
        float VoxelSize = 100.0f;  // Size of one voxel in world units

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Terrain")
        int32 ChunkResolution = 16;  // Voxels per chunk dimension

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Terrain")
        float TerrainNoiseAmplitude = 150.0f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Rendering")
        int32 ViewDistanceInChunks = 3;  // How many chunks to load around camera

    protected:
        virtual void BeginPlay() override;
        virtual void Tick(float DeltaTime) override;

        // Validation functions
        void TestMarchingCubesChunk();
        void TestVertexInterpolation() const;
        void TestSpherifiedProjection();
        void TestChunkCreation();  // NEW!

        // Helper to log test results
        void LogTest(const FString &TestName, bool bPassed, const FString &Details = "");

    private:
        int32 TestsPassed;
        int32 TestsTotal;

        UProceduralMeshComponent *DebugMeshComponent;

        TUniquePtr<FChunkManager> ChunkManager;
};
