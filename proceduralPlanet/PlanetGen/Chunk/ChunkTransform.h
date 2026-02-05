#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "../MathUtils.h"


// Spatial transform data for a chunk.
// Computed once at chunk creation, immutable thereafter.
// Contains no engine references, thread-safe.
struct PROCEDURALPLANET_API FChunkTransform
{
        FVector WorldOrigin;   // World position of chunk center
        FVector CubeNormal;    // Normal of the cube face this chunk belongs to
        float ChunkWorldSize;  // Size of chunk in world units (meters)
        int32 LOD;             // Level of detail

        // Default constructor
        FChunkTransform() :
            WorldOrigin(FVector::ZeroVector),
            CubeNormal(FVector::ZeroVector),
            ChunkWorldSize(0.0f),
            LOD(0)
        {
        }

        // Main constructor.
        FChunkTransform(const FVector &PlanetCenter, float PlanetRadius, uint8 Face, const FIntVector &ChunkCoords, int32 InLOD);

        // Utility functions
        bool IsValid() const;

        FString ToString() const;

        // Convert local position (relative to chunk center) to world position.
        // Local coordinates are in chunk tangent/bitangent/normal space.
        FVector LocalToWorld(const FVector &LocalPosition) const;

        // Convert world position to local position (relative to chunk center).
        FVector WorldToLocal(const FVector &WorldPosition) const;

        // Get the bounds of this chunk in world space.
        void GetWorldBounds(FVector &OutMin, FVector &OutMax) const;

        // Check if a world position is within this chunk's bounds.
        bool ContainsWorldPosition(const FVector &WorldPosition, float Margin) const;

        // Get the transform for debug visualization.
        FTransform GetDebugTransform() const;
};