#pragma once

#include "CoreMinimal.h"
#include "DataTypes.h"


/**
 * A pure C++ representation of a terrain chunk.
 * This class is not an Actor. It manages the state and data of a single quadtree node.
 */
class FChunk
{
    public:
        FChunkId Id;  // Identity
        EChunkState State;  // Lifecycle State
        uint32 GenerationId;  // To handle async cancellations (if GenerationId changes, ignore old task results)

        FChunkTransform Transform;  // Spatial Info

        GenData DensityField;  // This holds the actual density field
        bool bIsDensityDataGenerated = false;  // Is the data ready to be turned into a mesh?
        
        TUniquePtr<FChunkMeshData> MeshData; // The generated mesh data (Valid only when State >= Ready)
        
        TWeakObjectPtr<UProceduralMeshComponent> RenderProxy; // Reference to the actual component rendering this chunk (Valid only when State == Visible)

        // Constructor
        FChunk(const FChunkId &InId) :
            Id(InId),
            State(EChunkState::Unloaded),
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