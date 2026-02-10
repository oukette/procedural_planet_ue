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
        int32 LOD;

        UPROPERTY(EditAnywhere, BlueprintReadWrite)
        FIntVector Coords;  // Face-local grid coordinates (X, Y)

        FChunkId() :
            FaceIndex(0),
            LOD(0),
            Coords(FIntVector::ZeroValue)
        {
        }
        FChunkId(uint8 InFace, int32 InLOD, FIntVector InCoords) :
            FaceIndex(InFace),
            LOD(InLOD),
            Coords(InCoords)
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