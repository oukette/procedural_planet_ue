#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "DataTypes.generated.h"


// The state of a chunk in its lifecycle
UENUM(BlueprintType)
enum class EChunkState : uint8
{
    Unloaded,    // Data exists in manager, but no work is happening
    Requested,   // Marked for generation, waiting for thread pool
    Generating,  // Currently being processed by an async task
    Ready,       // Mesh data is ready in memory
    Visible,     // Mesh is assigned to a component and visible in world
    Unloading    // Marked for destruction/pooling
};


// Unique identifier for a Chunk on the CubeSphere
USTRUCT(BlueprintType)
struct FChunkId
{
        GENERATED_BODY()

        UPROPERTY(EditAnywhere, BlueprintReadWrite)
        uint8 FaceIndex;  // 0-5

        UPROPERTY(EditAnywhere, BlueprintReadWrite)
        FIntVector Coords;  // Face-local grid coordinates (X, Y)

        FChunkId() :
            FaceIndex(0),
            Coords(FIntVector::ZeroValue)
        {
        }
        FChunkId(uint8 InFace, FIntVector InCoords) :
            FaceIndex(InFace),
            Coords(InCoords)
        {
        }

        bool operator==(const FChunkId &Other) const { return FaceIndex == Other.FaceIndex && Coords == Other.Coords; }

        friend uint32 GetTypeHash(const FChunkId &Other) { return HashCombine(GetTypeHash(Other.FaceIndex), GetTypeHash(Other.Coords)); }
};


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
        FVector Location;  // Center of the chunk in Planet Space

        UPROPERTY()
        float Scale;  // Uniform scale (derived from LOD)

        UPROPERTY()
        FVector FaceNormal;  // Which cube face this belongs to

        UPROPERTY()
        FQuat Rotation;  // Orientation on the sphere surface

        // Default constructor
        FChunkTransform() :
            Location(FVector::ZeroVector),
            Scale(1.0f),
            FaceNormal(FVector::UpVector),
            Rotation(FQuat::Identity)
        {
        }

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
        static constexpr float TargetAutoChunkSize = 4000.0f;
        static constexpr float FarDistanceSafetyMargin = 1.1f;

        // Debug
        static constexpr int32 DebugSphereSegments = 32;
        static constexpr float DebugSphereLifetime = 60.0f;
        static constexpr float DebugSphereThickness = 20.0f;
        static constexpr float DebugLineLifetime = 30.0f;
        static constexpr float DebugBoxLifetime = 20.0f;
        static constexpr int32 DebugKey_ManagerStats = 10;
        static constexpr int32 DebugKey_DistanceInfo = 101;

        // Culling & Visibility
        static constexpr float UndergroundThreshold = -100.0f;
        static constexpr float HorizonCullingDot = -0.2f;
        static constexpr float FrustumCullingDot = -0.2f;
        static constexpr float GridDebugRadiusScale = 1.002f;
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
};


// Planet grid settings.
USTRUCT(BlueprintType)
struct FPlanetGridSettings
{
        GENERATED_BODY()

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        int32 ChunksPerFace = 16;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        int32 Resolution = 32;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        float VoxelSize = 100.f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        bool bAutoChunkSizing = true;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bAutoChunkSizing", ClampMin = "0.1", ClampMax = "10.0"))
        float ChunkDensityFactor = 1.0f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bAutoChunkSizing", ClampMin = "1", ClampMax = "64"))
        int32 MinChunksPerFace = 1;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bAutoChunkSizing", ClampMin = "1", ClampMax = "256"))
        int32 MaxChunksPerFace = 64;
};


// Planet perf settings.
USTRUCT(BlueprintType)
struct FPlanetPerformanceSettings
{
        GENERATED_BODY()

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "1", ClampMax = "100"))
        int32 MeshUpdatesPerFrame = 2;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "1", ClampMax = "100"))
        int32 ChunksToSpawnPerFrame = 8;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "1", ClampMax = "512"))
        int32 MaxConcurrentGenerations = 32;
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
        float PlanetRadius;
        int32 ChunksPerFace;  // The chunk grid size/res (e.g. 16 or 32)

        // Generation Settings
        int32 Seed;
        bool bEnableCollision;
        bool bCastShadows;

        // Voxel Settings (Finalized)
        float VoxelSize;       // true size of a voxel in the UE world
        int32 GridResolution;  // resolution of the voxel grid in voxels


        // Throttling
        int32 MaxConcurrentGenerations;
        int32 ChunkGenerationRate;  // Chunks to start generating per tick
        int32 MeshUpdatesPerFrame;

        // Default Constructor
        FPlanetConfig() :
            PlanetRadius(10000.f),
            ChunksPerFace(1),
            Seed(1337),
            bEnableCollision(false),
            bCastShadows(false),
            VoxelSize(100.f),
            GridResolution(32),
            MaxConcurrentGenerations(32),
            ChunkGenerationRate(8),
            MeshUpdatesPerFrame(2)
        {
        }
};


// Configuration structure to keep parameters organized
struct DensityConfig
{
        int32 Seed;
        float PlanetRadius;
        float VoxelSize;
        FNoiseSettings Noise;

        // Future expansion: biomes, caves, etc.

        DensityConfig() :
            Seed(1337),
            PlanetRadius(10000.f),
            VoxelSize(100.f)
        {
        }
};


// Container for generated field data to avoid re-calculating positions
struct GenData
{
        TArray<float> Densities;
        TArray<FVector> Positions;
        int32 SampleCount = 0;
};


// Automatic Debug Colors for multiple LODs.
const static TArray<FColor> LODColorsDebug = {
    FColor::Green,        // LOD 0
    FColor::Yellow,       // LOD 1
    FColor(255, 165, 0),  // LOD 2 (Orange)
    FColor::Red,          // LOD 3
    FColor::Magenta,      // LOD 4
    FColor::Cyan,         // LOD 5
    FColor(0, 255, 128),  // LOD 6 (Spring Green)
    FColor(128, 0, 255)   // LOD 7 (Purple)
};
