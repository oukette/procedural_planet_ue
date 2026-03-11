#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "DataTypes.generated.h"


// The state of a chunk in its lifecycle
UENUM(BlueprintType)
enum class EChunkState : uint8
{
    None,        // Initial state
    Pending,     // In the generation queue
    Generating,  // Currently being processed by an async task
    DataReady,   // Mesh data is in RAM but not yet in the GPU
    MeshReady,   // Mesh is assigned to a component
    Visible      // Mesh is visible in the world
};


enum class ELeafTransitionType : uint8
{
    Split,
    Merge
};


// Unique identifier for a Chunk on the CubeSphere
USTRUCT(BlueprintType)
struct FChunkId
{
        GENERATED_BODY()
        UPROPERTY(EditAnywhere, BlueprintReadWrite)
        uint8 FaceIndex = 0;  // 0-5

        UPROPERTY(EditAnywhere, BlueprintReadWrite)
        FIntVector Coords = FIntVector::ZeroValue;  // Face-local grid coordinates (X, Y)

        UPROPERTY(EditAnywhere, BlueprintReadWrite)
        int32 LODLevel = 0;  // 0 = Root, Higher = Smaller/More Detailed

        FChunkId() = default;

        FChunkId(uint8 InFace, FIntVector InCoords, int32 InLOD) :
            FaceIndex(InFace),
            Coords(InCoords),
            LODLevel(InLOD)
        {
        }

        bool operator==(const FChunkId &Other) const { return FaceIndex == Other.FaceIndex && Coords == Other.Coords && LODLevel == Other.LODLevel; }

        friend uint32 GetTypeHash(const FChunkId &Other)
        {
            return HashCombine(HashCombine(GetTypeHash(Other.FaceIndex), GetTypeHash(Other.Coords)), GetTypeHash(Other.LODLevel));
        }
};

// Sentinels are never rendered — they exist only to unify the transition logic.
inline FChunkId MakeSentinelId(uint8 FaceIndex) { return FChunkId(FaceIndex, FIntVector(-1, -1, -1), -1); }

inline bool IsSentinelId(const FChunkId &Id) { return Id.LODLevel == -1; }


// All data required for a single Mesh Section
USTRUCT(BlueprintType)
struct FChunkMeshData
{
        GENERATED_BODY()

        UPROPERTY()
        TArray<FVector> Vertices;

        UPROPERTY()
        TArray<int32> Triangles;

        UPROPERTY()
        TArray<FVector> Normals;

        UPROPERTY()
        TArray<FVector2D> UV0;

        UPROPERTY()
        TArray<FColor> Colors;

        void Empty()
        {
            Vertices.Empty();
            Triangles.Empty();
            Normals.Empty();
            UV0.Empty();
            Colors.Empty();
        }
};


// Represents the physical placement of a chunk in planet-space.
USTRUCT(BlueprintType)
struct FChunkTransform
{
        GENERATED_BODY()
        UPROPERTY()
        FVector Location = FVector::ZeroVector;  // Center of the chunk in Planet Space

        UPROPERTY()
        float Scale = 1.0f;  // Uniform scale (derived from LOD)

        UPROPERTY()
        FVector FaceNormal = FVector::UpVector;  // Which cube face this belongs to

        UPROPERTY()
        FQuat Rotation = FQuat::Identity;  // Orientation on the sphere surface

        FChunkTransform() = default;

        FChunkTransform(FVector InLoc, float InScale, FVector InNormal, FQuat InRot = FQuat::Identity) :
            Location(InLoc),
            Scale(InScale),
            FaceNormal(InNormal),
            Rotation(InRot)
        {
        }
};


// Context provided to the Manager to evaluate LODs and visibility
USTRUCT(BlueprintType)
struct FPlanetViewContext
{
        GENERATED_BODY()

        UPROPERTY()
        FVector ObserverLocation;

        UPROPERTY()
        FVector ObserverForward;

        UPROPERTY()
        FVector ObserverVelocity;

        UPROPERTY()
        float ViewDistance;
};


// Global constants for easy tuning and static access
struct FPlanetStatics
{
        // Ratio of RenderDistance where the Far Model takes over.
        // 1.0 = Exactly at RenderDistance.
        // 0.9 = Far model appears slightly before chunks disappear (smoother overlap).
        static constexpr float FarModelDistanceRatio = 1.0f;

        // Ratio of RenderDistance where the Far Model disappears (Getting Closer).
        // 0.8 = Far model stays visible until we are at 80% of render distance.
        static constexpr float FarModelHideRatio = 0.8f;

        // Generation / Grid
        static constexpr float DefaultEngineSphereRadius = 50.0f;
        static constexpr float TargetAutoChunkSize = 8000.0f;
        static constexpr float FarDistanceSafetyMargin = 1.1f;

        // Culling & Visibility
        static constexpr float UndergroundThreshold = -100.0f;
        static constexpr float HorizonCullingDot = -0.5f;
        static constexpr float FrustumCullingDot = -0.5f;
        static constexpr float GridDebugRadiusScale = 1.002f;

        // Debug
        static constexpr int32 DebugSphereSegments = 32;
        static constexpr float DebugSphereLifetime = 60.0f;
        static constexpr float DebugSphereThickness = 20.0f;
        static constexpr float DebugLineLifetime = 30.0f;
        static constexpr float DebugBoxLifetime = 20.0f;
        static constexpr int32 DebugKey_ManagerStats = 10;
        static constexpr int32 DebugKey_DistanceInfo = 101;
        static constexpr int32 DebugKey_PredictionInfo = 102;
        static constexpr int32 DebugKey_LODBreakdown = 103;
        static constexpr int32 DebugKey_LODThreshold = 104;
};


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
        float LODSplitMultiplier = 2.0f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet",
                  meta = (DisplayName = "LOD Merge Hysteresis Ratio", ClampMin = "0.05", ClampMax = "2.0"))
        float LODMergeHysteresisRatio = 1.25f;
};


// Planet perf settings.
USTRUCT(BlueprintType)
struct FPlanetPerformanceSettings
{
        GENERATED_BODY()

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Performance", meta = (ClampMin = "1", ClampMax = "100"))
        int32 MeshUpdatesPerFrame = 2;

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
        int32 ChunksPerFace = 1;

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
        int32 MeshUpdatesPerFrame = 2;

        // LOD Rules
        int32 MaxLOD = 8;
        float FarDistanceThreshold = 100000.0f;
        float LODSplitDistanceMultiplier = 2.0f;
        float LODMergeHysteresisRatio = 1.25f;  // Merge threshold = Split threshold * this ratio

        // Look ahead params
        float MaxLookAheadTime = 2.5f;
        float MinLookAheadTime = 0.5f;
        float LookAheadAltitudeScale = 50000.0f;

        int32 ChunkDemotionFrameDelay = 3;  // X frames. A rendered chunk must be absent before hiding
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


struct FLODTransition
{
        FChunkId Parent;
        TArray<FChunkId> Children;  // Always 4 for a quadtree split
        ELeafTransitionType Type;
        bool bReadyToCommit = false;
};


// Automatic Debug Colors for multiple LODs.
const static TArray<FColor> LODColorsDebug = {
    FColor::Green,        // LOD 0
    FColor(128, 255, 0),  // LOD 1 (smooth green)
    FColor::Yellow,       // LOD 2
    FColor(0xF75B00),     // LOD 3 (Orange)
    FColor::Red,          // LOD 4
    FColor::Magenta,      // LOD 5
    FColor(128, 0, 255),  // LOD 6 (Purple)
    FColor(0, 255, 128),  // LOD 7 (Spring Green)
    FColor::Cyan          // LOD 8
};
