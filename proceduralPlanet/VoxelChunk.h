// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "VoxelChunk.generated.h"

UCLASS()
class PROCEDURALPLANET_API AVoxelChunk : public AActor
{
        GENERATED_BODY()

    public:
        // Sets default values for this actor's properties
        AVoxelChunk();

        // Voxel resolution per chunk (32x32x32 for prototype)
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
        int32 VoxelResolution;

        // Size of one voxel in world units
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
        float VoxelSize;

        // Base radius for cube-sphere projection
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
        float PlanetRadius;

        // Center of the planet
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
        FVector PlanetCenter;

        // Noise amplitude
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
        float NoiseAmplitude;

        // Noise frequency
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
        float NoiseFrequency;

        // Seed for procedural generation
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
        int32 Seed;

        // Procedural mesh component for this chunk
        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel")
        UProceduralMeshComponent *ProceduralMesh;

        UPROPERTY(EditAnywhere, Category = "Debug")
        bool bGenerateMesh;

        void GenerateDensity();  // Create voxel density array
        void GenerateMesh();     // Apply Marching Cubes and build mesh

    protected:
        virtual void OnConstruction(const FTransform &Transform) override;

        void Initialize(int32 InVoxelResolution, float InVoxelSize, float InPlanetRadius, FVector InPlanetCenter, float InNoiseAmplitude,
                        float InNoiseFrequency, int32 InSeed);

        // 3D array for voxel density
        TArray<float> VoxelDensity;
};
