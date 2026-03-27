#pragma once

#include "CoreMinimal.h"
#include "PlanetConfig.generated.h"


// Planet generation settings.
USTRUCT(BlueprintType) struct FPlanetGenSettings
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
};


// Planet debug settings.
USTRUCT(BlueprintType) struct FPlanetDebugSettings
{
        GENERATED_BODY()

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        bool bShowDebugTrueSphere = false;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        bool bShowDebugChunkGrid = false;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        bool bShowDebugChunkBounds = false;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        bool bShowDebugPredictivePos = false;
};


// Planet grid settings.
USTRUCT(BlueprintType) struct FPlanetGridSettings
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
USTRUCT(BlueprintType) struct FPlanetPerformanceSettings
{
        GENERATED_BODY()

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Performance", meta = (ClampMin = "1", ClampMax = "100"))
        int32 MeshUpdatesPerFrame = 8;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Performance", meta = (ClampMin = "1", ClampMax = "100"))
        int32 ChunksToSpawnPerFrame = 8;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Performance", meta = (ClampMin = "1", ClampMax = "512"))
        int32 MaxConcurrentGenerations = 32;
};


// Grouped Noise Settings for cleaner propagation
USTRUCT(BlueprintType) struct FNoiseSettings
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
USTRUCT(BlueprintType) struct FPlanetConfig
{
        GENERATED_BODY()

        // Basic Dimensions
        float PlanetRadius = 20000.f;

        // Generation Settings
        int32 Seed = 1337;
        bool bEnableCollision = false;
        bool bCastShadows = false;

        // Voxel Settings
        float VoxelSize = 100.f;    // true size of a voxel in the UE world
        int16 GridResolution = 32;  // resolution of the voxel grid in voxels

        // Throttling
        int16 MaxConcurrentGenerations = 32;
        int16 ChunkGenerationRate = 32;  // Chunks to start generating per tick
        int16 MeshUpdatesPerFrame = 8;
        int16 ChunkDemotionFrameDelay = 8;  // X frames. A rendered chunk must be absent before hiding
        int16 CacheSoftCap = 512;
        int16 CacheHardCap = 1024;
        int16 DeferredReleaseDelay = 8;     // Normal frame countdown
        int16 DeferredReleaseDelayMin = 1;  // Minimum under full pressure
        int16 TransitionMaxAge = 90;        // Maximum number of frames a pending LOD transition can stay alive before being force-cancelled.
        int16 MaxPendingTransitions = 60;

        // LOD Rules
        int16 MaxLOD = 8;
        int16 PredictiveMaxLOD = 4; // Deepest LOD level the predictive pass is allowed to request.
        float FarDistanceThreshold = 100000.0f;
        float LODSplitScreenFraction = 0.25f;  // Fraction of screen height a chunk must subtend to trigger a split.
                                               // 0.25 means "split when the chunk covers 25% of the vertical screen".

        float LODMergeHysteresisRatio = 0.75f;  // Merge threshold = Split threshold * this ratio

        // Predictive position pre-loading
        float PredictiveLookAheadMaxSeconds = 2.0f;  // Maximum lookahead time in seconds at full speed/altitude
        float PredictiveMinAltitude = 500.0f;        // Below this altitude, prediction blends to zero (no lookahead at surface level)
        float PredictiveLookAheadScale = 5000.0f;    // Speed divisor to scale lookahead time — higher = less aggressive prediction
};
