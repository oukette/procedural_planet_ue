#pragma once

#include "Math/Vector.h"
#include "Math/Vector2D.h"
#include "Math/Quat.h"
#include "Math/Color.h"
#include "Containers/Array.h"
#include "Templates/UniquePtr.h"
#include "UObject/WeakObjectPtr.h"

#include "GenData.h"
#include "ChunkId.h"


class UProceduralMeshComponent;


// All data required for a single Mesh Section
struct ChunkMeshData
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
struct ChunkTransform
{
        FVector Location = FVector::ZeroVector;  // Center of the chunk in Planet Space
        float Scale = 1.0f;                      // Uniform scale (derived from LOD)
        FVector FaceNormal = FVector::UpVector;  // Which cube face this belongs to
        FQuat Rotation = FQuat::Identity;        // Orientation on the sphere surface

        ChunkTransform() = default;

        ChunkTransform(FVector InLoc, float InScale, FVector InNormal, FQuat InRot = FQuat::Identity) :
            Location(InLoc),
            Scale(InScale),
            FaceNormal(InNormal),
            Rotation(InRot)
        {
        }
};


// A pure C++ representation of a terrain chunk.
// This class is not an Actor. It manages the state and data of a single quadtree node.
class Chunk
{
    public:
        ChunkId m_ID;         // Identity
        ChunkState m_state;   // Lifecycle State
        uint32 m_generationId;  // To handle async cancellations (if m_generationId changes, ignore old task results)

        ChunkTransform m_transform;  // Spatial Info

        GenData m_densityField;                  // This holds the actual density field
        bool m_isDensityDataGenerated = false;  // Is the data ready to be turned into a mesh?

        TUniquePtr<ChunkMeshData> m_meshData;  // The generated mesh data (Valid only when m_state >= DataReady)

        TWeakObjectPtr<UProceduralMeshComponent> m_renderProxy;  // Reference to the actual component rendering this chunk (Valid only when m_state == MeshReady)

        // Constructor
        Chunk(const ChunkId &InId) :
            m_ID(InId),
            m_state(ChunkState::None),
            m_generationId(0)
        {
        }

        // Non-copyable (to prevent accidental deep copies of mesh data)
        Chunk(const Chunk &) = delete;
        Chunk &operator=(const Chunk &) = delete;

        // Move-only
        Chunk(Chunk &&) = default;
        Chunk &operator=(Chunk &&) = default;

        ~Chunk()
        {
            // TUniquePtr automatically cleans up m_meshData
            // WeakObjectPtr handles itself (doesn't destroy the component)
        }
};
