#pragma once

#include "CoreMinimal.h"
#include "DataTypes.h"
#include "ChunkId.h"


// All data required for a single Mesh Section
struct FChunkMeshData
{
        TArray<FVector> Vertices;
        TArray<int32> Triangles;
        TArray<FVector> Normals;
        TArray<FVector2D> UV0;
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
struct FChunkTransform
{
        FVector Location = FVector::ZeroVector;  // Center of the chunk in Planet Space
        float Scale = 1.0f;                      // Uniform scale (derived from LOD)
        FVector FaceNormal = FVector::UpVector;  // Which cube face this belongs to
        FQuat Rotation = FQuat::Identity;        // Orientation on the sphere surface

        FChunkTransform() = default;

        FChunkTransform(FVector InLoc, float InScale, FVector InNormal, FQuat InRot = FQuat::Identity) :
            Location(InLoc),
            Scale(InScale),
            FaceNormal(InNormal),
            Rotation(InRot)
        {
        }
};


// A pure C++ representation of a terrain chunk.
// This class is not an Actor. It manages the state and data of a single quadtree node.
class FChunk
{
    public:
        FChunkId Id;          // Identity
        EChunkState State;    // Lifecycle State
        uint32 GenerationId;  // To handle async cancellations (if GenerationId changes, ignore old task results)

        FChunkTransform Transform;  // Spatial Info

        GenData DensityField;                  // This holds the actual density field
        bool bIsDensityDataGenerated = false;  // Is the data ready to be turned into a mesh?

        TUniquePtr<FChunkMeshData> MeshData;  // The generated mesh data (Valid only when State >= DataReady)

        TWeakObjectPtr<UProceduralMeshComponent> RenderProxy;  // Reference to the actual component rendering this chunk (Valid only when State == MeshReady)

        // Constructor
        FChunk(const FChunkId &InId) :
            Id(InId),
            State(EChunkState::None),
            GenerationId(0)
        {
        }

        // Non-copyable (to prevent accidental deep copies of mesh data)
        FChunk(const FChunk &) = delete;
        FChunk &operator=(const FChunk &) = delete;

        // Move-only
        FChunk(FChunk &&) = default;
        FChunk &operator=(FChunk &&) = default;

        ~FChunk()
        {
            // TUniquePtr automatically cleans up MeshData
            // WeakObjectPtr handles itself (doesn't destroy the component)
        }
};


