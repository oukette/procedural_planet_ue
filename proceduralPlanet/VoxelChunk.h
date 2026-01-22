// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "VoxelChunk.generated.h"


struct FChunkMeshData
{
        TArray<FVector> Vertices;
        TArray<int32> Triangles;
        TArray<FVector> Normals;
        TArray<FColor> Colors;
};


UCLASS()
class PROCEDURALPLANET_API AVoxelChunk : public AActor
{
        GENERATED_BODY()
        
    private:
        // Static helpers for async generation
        static TArray<float> GenerateDensityField(int32 Resolution, float VoxelSize, float PlanetRadius, const FVector &LocalPlanetCenter);
        static FChunkMeshData GenerateMeshFromDensity(const TArray<float> &Density, int32 Resolution, float VoxelSize, const FVector &LocalPlanetCenter,
                                                      int32 LODLevel);
        static FVector VertexInterp(const FVector &P1, const FVector &P2, float D1, float D2);

    protected:
        virtual void OnConstruction(const FTransform &Transform) override;

    public:
        // Sets default values for this actor's properties
        AVoxelChunk();

        // Voxel resolution per chunk (32x32x32 for prototype)
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
        int32 VoxelResolution;

        // Current LOD Level (0 = High, 1 = Low)
        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel")
        int32 CurrentLOD;

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

        UPROPERTY(VisibleAnywhere, Category = "Optimization")
        bool bEnableCollision;

        // New Async function
        void GenerateChunkAsync();

        // Called by Planet to apply mesh to GPU, with a specified material
        void UploadMesh(UMaterialInterface *MaterialToApply);

        // Efficiently toggle collision
        void SetCollisionEnabled(bool bEnabled);

        // Update resolution/size and regenerate mesh (LOD switching)
        void UpdateChunkLOD(int32 NewLOD, int32 NewResolution, float NewVoxelSize);

        FChunkMeshData GeneratedMeshData;
};
