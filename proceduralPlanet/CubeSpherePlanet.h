#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "VoxelChunk.h"  // New class for volumetric chunks
#include "CubeSpherePlanet.generated.h"

UCLASS()
class PROCEDURALPLANET_API ACubeSpherePlanet : public AActor
{
        GENERATED_BODY()

    public:
        ACubeSpherePlanet();

    protected:
        virtual void OnConstruction(const FTransform &Transform) override;

        void GenerateCubeSphere();
        void GenerateVoxelChunks();  // New function for volumetric planet

    public:
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        int32 Seed;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        int32 ChunksPerFace;  // Grid resolution per cube face (e.g., 4 = 4x4 = 16 chunks)

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        float PlanetRadius;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        float NoiseAmplitude;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        float NoiseFrequency;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        int32 VoxelResolution;  // Per-chunk voxel count

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        float VoxelSize;

        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
        USceneComponent *Root;

        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
        TArray<class AVoxelChunk *> VoxelChunks;
};
