#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "DataTypes.generated.h"


/** The state of a chunk in its lifecycle */
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


/** Unique identifier for a chunk on the CubeSphere */
USTRUCT(BlueprintType)
struct FChunkId
{
        GENERATED_BODY()

        UPROPERTY(EditAnywhere, BlueprintReadWrite)
        uint8 FaceIndex;  // 0-5

        UPROPERTY(EditAnywhere, BlueprintReadWrite)
        FIntVector Coords;  // Face-local grid coordinates (X, Y)

        UPROPERTY(EditAnywhere, BlueprintReadWrite)
        int32 LOD;

        FChunkId() :
            FaceIndex(0),
            Coords(FIntVector::ZeroValue),
            LOD(0)
        {
        }
        FChunkId(uint8 InFace, int32 InLOD, FIntVector InCoords) :
            FaceIndex(InFace),
            Coords(InCoords),
            LOD(InLOD)
        {
        }

        bool operator==(const FChunkId &Other) const { return FaceIndex == Other.FaceIndex && LOD == Other.LOD && Coords == Other.Coords; }

        friend uint32 GetTypeHash(const FChunkId &Other)
        {
            return HashCombine(HashCombine(GetTypeHash(Other.FaceIndex), GetTypeHash(Other.LOD)), GetTypeHash(Other.Coords));
        }
};


/** All data required for a single Mesh Section */
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


/** Represents the physical placement of a chunk in planet-space. */
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

        // Default constructor
        FChunkTransform() :
            Location(FVector::ZeroVector),
            Scale(1.0f),
            FaceNormal(FVector::UpVector)
        {
        }

        FChunkTransform(FVector InLoc, float InScale, FVector InNormal) :
            Location(InLoc),
            Scale(InScale),
            FaceNormal(InNormal)
        {
        }
};


/** Context provided to the Manager to evaluate LODs and visibility */
USTRUCT(BlueprintType)
struct FPlanetViewContext
{
        GENERATED_BODY()

        UPROPERTY()
        FVector ObserverLocation;

        UPROPERTY()
        float ViewDistance;

        UPROPERTY()
        int32 MaxAllowedLOD;
};


// A struct to define settings for a single Level of Detail.
USTRUCT(BlueprintType)
struct FLODInfo
{
        GENERATED_BODY()

        // Distance at which this LOD (and higher detail ones) becomes active.
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD")
        float Distance = 10000.f;

        // Voxel resolution for chunks at this LOD.
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD")
        int32 VoxelResolution = 32;
};


/**
 * Static configuration for the planet.
 * Passed to the Manager once at startup.
 */
struct FPlanetConfig
{
        // Basic Dimensions
        double PlanetRadius;
        int32 ChunksPerFace;  // The grid size (e.g. 16 or 32)

        // Generation Settings
        int32 Seed;

        // LOD Rules
        TArray<FLODInfo> LODSettings;
        float LODHysteresis;
        float LODDespawnHysteresis;
        float FarDistanceThreshold;  // Distance to switch to Far Model

        // Default Constructor
        FPlanetConfig() :
            PlanetRadius(50000.0),
            ChunksPerFace(16),
            Seed(1337),
            LODHysteresis(1.1f),
            LODDespawnHysteresis(1.1f),
            FarDistanceThreshold(200000.0f)
        {
        }
};