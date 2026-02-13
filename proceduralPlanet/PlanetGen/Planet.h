#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SeedUtils.h"
#include "MathUtils.h"
#include "ChunkManager.h"
#include "DataTypes.h"
#include "SimpleNoise.h"
#include "Planet.generated.h"


UCLASS()
class PROCEDURALPLANET_API APlanet : public AActor
{
        GENERATED_BODY()

    private:
        UPROPERTY(VisibleAnywhere, Category = "Planet")
        USceneComponent *Root;

        TUniquePtr<FChunkManager> ChunkManager;
        TUniquePtr<SimpleNoise> NoiseProvider;
        TUniquePtr<DensityGenerator> Generator;

    protected:
        virtual void OnConstruction(const FTransform &Transform) override;

        // Initializes the generation process by populating the spawn queue.
        void initPlanet();

        // Helper to get the camera position in both Editor and Runtime
        FVector GetObserverPosition() const;

        // Creates the planet far model for optimized rendering in far distance.
        void CreateFarModel();

    public:
        APlanet();
        virtual void BeginPlay() override;
        virtual void Destroyed() override;
        virtual void Tick(float DeltaTime) override;
        virtual bool ShouldTickIfViewportsOnly() const override;

        // --- Generation Control ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Generation")
        bool bGenerateOnBeginPlay = true;

        // --- Planet Shape & Detail ---
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
                  meta = (EditCondition = "bAutoChunkSizing", ClampMin = "1", ClampMax = "256", DisplayName = "Max Chunks Per Face"))
        int32 MaxChunksPerFace = 64;

        // Automatically scales RenderDistance and LOD distances based on PlanetRadius.
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization|LOD", meta = (DisplayName = "Auto-Scale LODs"))
        bool bAutoLOD = true;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        float PlanetRadius;

        // --- Noise ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Noise", meta = (DisplayName = "Amplitude"))
        float NoiseAmplitude;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Noise", meta = (DisplayName = "Base Frequency"))
        float NoiseFrequency;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Noise", meta = (DisplayName = "Octaves", ClampMin = "1", ClampMax = "12"))
        int32 NoiseOctaves = 6;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Noise", meta = (DisplayName = "Lacunarity", ClampMin = "1.0"))
        float NoiseLacunarity = 2.0f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Noise", meta = (DisplayName = "Persistence (Gain)", ClampMin = "0.0", ClampMax = "1.0"))
        float NoisePersistence = 0.5f;

        // --- Voxel Settings ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Voxels")
        int32 VoxelResolution;  // Per-chunk voxel count (Resolution for LOD 0)

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        float VoxelSize;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization")
        bool bEnableCollision;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization")
        bool bCastShadows;

        // --- Rendering ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Rendering")
        UMaterialInterface *DebugMaterial;

        // Flag to track if the FarPlanetModel was created by code.
        bool bIsFarModelAutoCreated = false;

        // Actor representing the planet when viewed from a great distance (e.g., a sphere with a procedural material).
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Rendering")
        AActor *FarPlanetModel;

        // --- LOD & Streaming ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization|LOD", meta = (ClampMin = "1000.0"))
        float RenderDistance = 150000.0f;  // Distance at which we switch from chunks to the FarPlanetModel.

        // Defines the distance and resolution for each level of detail. Must be sorted by distance, ascending.
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization|LOD", meta = (DisplayName = "LOD Settings"))
        TArray<FLODInfo> LODSettings;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization|LOD", meta = (ClampMin = "100.0"))
        float CollisionDistance = 6000.0f;  // Distance at which collision is enabled

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization|LOD", meta = (ClampMin = "1.0", ClampMax = "2.0"))
        float LODHysteresisFactor = 1.1f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization|LOD", meta = (ClampMin = "1.0", ClampMax = "2.0"))
        float LODDespawnHysteresisFactor = 1.1f;

        // --- Staggered Generation ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization",
                  meta = (DisplayName = "Chunk Mesh Updates Per Frame", ClampMin = "1", ClampMax = "100"))
        int32 ChunksMeshUpdatesPerFrame;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization",
                  meta = (DisplayName = "Chunks To Spawn Per Frame", ClampMin = "1", ClampMax = "100"))
        int32 ChunksToSpawnPerFrame = 8;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization",
                  meta = (DisplayName = "Max Concurrent Generations", ClampMin = "1", ClampMax = "512"))
        int32 MaxConcurrentChunkGenerations = 32;
};
