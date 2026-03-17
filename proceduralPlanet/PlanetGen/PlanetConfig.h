#pragma once

#include "CoreMinimal.h"
#include "PlanetConfig.generated.h"


// Planet generation settings.
USTRUCT(BlueprintType)
struct FPlanetGenSettings
{
        GENERATED_BODY()

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        int32 Seed = 1337;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        float PlanetRadius = 10000.f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        float RenderDistanceMultiplier = 8.0f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        bool bEnableCollision = false;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        bool bCastShadows = false;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        UMaterialInterface *DebugMaterial = nullptr;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        AActor *FarPlanetModel = nullptr;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        bool bShowDebugTrueSphere = false;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        bool bShowDebugChunkGrid = false;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        bool bShowDebugChunkBounds = false;

        // UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (DisplayName = "Show Debug Prediction"))
        // bool bShowDebugPrediction = false;
};


// Planet grid settings.
USTRUCT(BlueprintType)
struct FPlanetGridSettings
{
        GENERATED_BODY()

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        int32 Resolution = 32;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        float VoxelSize = 100.f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (DisplayName = "LOD Split Multiplier", ClampMin = "1.0", ClampMax = "5.0"))
        float LODSplitScreenFraction = 0.25f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet",
                  meta = (DisplayName = "LOD Merge Hysteresis Ratio", ClampMin = "0.15", ClampMax = "0.95"))
        float LODMergeHysteresisRatio = 0.75f;
};


// Planet perf settings.
USTRUCT(BlueprintType)
struct FPlanetPerformanceSettings
{
        GENERATED_BODY()

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Performance", meta = (ClampMin = "1", ClampMax = "100"))
        int32 MeshUpdatesPerFrame = 8;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Performance", meta = (ClampMin = "1", ClampMax = "100"))
        int32 ChunksToSpawnPerFrame = 8;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Performance", meta = (ClampMin = "1", ClampMax = "512"))
        int32 MaxConcurrentGenerations = 32;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|LOD Look-Ahead", meta = (ClampMin = "0.0", ClampMax = "10.0"))
        float MaxLookAheadTime = 2.5f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|LOD Look-Ahead", meta = (ClampMin = "0.0", ClampMax = "10.0"))
        float MinLookAheadTime = 0.5f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|LOD Look-Ahead", meta = (ClampMin = "0.01", ClampMax = "20.0"))
        float LookAheadAltitudeRadiusFactor = 4.0f;
};


// Grouped Noise Settings for cleaner propagation
USTRUCT(BlueprintType)
struct FNoiseSettings
{
        GENERATED_BODY()

        UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Amplitude"))
        float Amplitude = 500.f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Base Frequency"))
        float Frequency = 0.0003f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Octaves", ClampMin = "1", ClampMax = "12"))
        int32 Octaves = 6;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Lacunarity", ClampMin = "1.0"))
        float Lacunarity = 2.0f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (DisplayName = "Persistence", ClampMin = "0.0", ClampMax = "1.0"))
        float Persistence = 0.5f;
};


// Static configuration for the planet. Passed to the ChunkManager once at startup.
USTRUCT(BlueprintType)
struct FPlanetConfig
{
        GENERATED_BODY()
        // Basic Dimensions
        float PlanetRadius = 10000.f;

        // Generation Settings
        int32 Seed = 1337;
        bool bEnableCollision = false;
        bool bCastShadows = false;

        // Voxel Settings
        float VoxelSize = 100.f;    // true size of a voxel in the UE world
        int32 GridResolution = 32;  // resolution of the voxel grid in voxels

        // Throttling
        int32 MaxConcurrentGenerations = 32;
        int32 ChunkGenerationRate = 8;  // Chunks to start generating per tick
        int32 MeshUpdatesPerFrame = 8;
        int32 CacheSoftCap = 300;
        int32 CacheHardCap = 512;
        int32 DeferredReleaseDelay = 8;     // Normal frame countdown
        int32 DeferredReleaseDelayMin = 1;  // Minimum under full pressure


        // LOD Rules
        int32 MaxLOD = 8;
        float FarDistanceThreshold = 100000.0f;
        float LODSplitScreenFraction = 0.25f;  // Fraction of screen height a chunk must subtend to trigger a split.
                                               // 0.25 means "split when the chunk covers 25% of the vertical screen".

        float LODMergeHysteresisRatio = 0.75f;  // Merge threshold = Split threshold * this ratio

        // Look ahead params
        float MaxLookAheadTime = 2.5f;
        float MinLookAheadTime = 0.5f;
        float LookAheadAltitudeScale = 50000.0f;

        int32 ChunkDemotionFrameDelay = 8;  // X frames. A rendered chunk must be absent before hiding
};


// Configuration structure to keep parameters organized
struct DensityConfig
{
        int32 Seed = 1337;
        float PlanetRadius = 10000.f;
        float VoxelSize = 100.f;
        FNoiseSettings Noise;

        // Future expansion: biomes, caves, etc.
};


// Container for generated field data to avoid re-calculating positions
struct GenData
{
        TArray<float> Densities;
        TArray<FVector> Positions;
        int32 SampleCount = 0;
};

