// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "PlanetDensityGenerator.h"
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
        static FChunkMeshData GenerateMeshFromDensity(const PlanetDensityGenerator::FGenData &GenData, int32 Resolution, FTransform CapturedChunkTransform,
                                                      FTransform CapturedPlanetTransform, const FVector &FaceNormal, const FVector &FaceRight,
                                                      const FVector &FaceUp, const FVector2D &UVMin, const FVector2D &UVMax, int32 LODLevel,
                                                      const PlanetDensityGenerator &DensityGenerator);

        static FVector VertexInterp(const FVector &P1, const FVector &P2, float D1, float D2);

    protected:
        virtual void OnConstruction(const FTransform &Transform) override;

        UPROPERTY()
        class ACubeSpherePlanet *ParentPlanet;

    public:
        // Sets default values for this actor's properties
        AVoxelChunk();

        // A simple setter to call when spawning
        void SetParentPlanet(ACubeSpherePlanet *Planet) { ParentPlanet = Planet; }

        // New Async function
        void GenerateChunkAsync();

        // Called by Planet to apply mesh to GPU, with a specified material
        void UploadMesh(UMaterialInterface *MaterialToApply);

        // Efficiently toggle collision
        void SetCollisionEnabled(bool bEnabled);

        // Update resolution/size and regenerate mesh (LOD switching)
        void UpdateChunkLOD(int32 NewLOD, int32 NewResolution, float NewVoxelSize);

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

        // Noise octaves
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
        int32 NoiseOctaves;

        // Noise lacunarity
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
        float NoiseLacunarity;

        // Noise persistence
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel")
        float NoisePersistence;

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

        // Projection Params
        FVector FaceNormal;
        FVector FaceRight;
        FVector FaceUp;
        FVector2D ChunkUVMin;
        FVector2D ChunkUVMax;

        FChunkMeshData GeneratedMeshData;
};
