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
        virtual void Destroyed() override;
        virtual void Tick(float DeltaTime) override;
        virtual bool ShouldTickIfViewportsOnly() const override;

        void EnqueueChunkForMeshUpdate(class AVoxelChunk *Chunk);

        // Calculate automatic chunks per face based on planet parameters
        UFUNCTION(BlueprintCallable, Category = "Planet|Chunking")
        int32 CalculateAutoChunksPerFace() const;

    protected:
        virtual void OnConstruction(const FTransform &Transform) override;
        void GenerateVoxelChunks();

    public:
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        int32 Seed;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        int32 ChunksPerFace;  // Grid resolution per cube face (e.g., 4 = 4x4 = 16 chunks)

        // Controls automatic chunk sizing
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Chunking", meta = (DisplayName = "Use Auto Chunk Sizing"))
        bool bAutoChunkSizing = true;

        // When auto-sizing is enabled, this determines chunk density
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Chunking",
                  meta = (EditCondition = "bAutoChunkSizing", ClampMin = "0.1", ClampMax = "10.0", DisplayName = "Chunk Density Factor"))
        float ChunkDensityFactor = 1.0f;

        // Minimum chunks per face (when auto-sizing)
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Chunking",
                  meta = (EditCondition = "bAutoChunkSizing", ClampMin = "1", ClampMax = "64", DisplayName = "Min Chunks Per Face"))
        int32 MinChunksPerFace = 1;

        // Maximum chunks per face (when auto-sizing)
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Chunking",
                  meta = (EditCondition = "bAutoChunkSizing", ClampMin = "1", ClampMax = "128", DisplayName = "Max Chunks Per Face"))
        int32 MaxChunksPerFace = 32;

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

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization")
        bool bEnableCollision;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization")
        bool bCastShadows;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization")
        int32 ChunksToProcessPerFrame;

        // Queue for chunks waiting to upload mesh to GPU
        TArray<class AVoxelChunk *> MeshUpdateQueue;


        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
        USceneComponent *Root;

        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
        TArray<class AVoxelChunk *> VoxelChunks;
};
