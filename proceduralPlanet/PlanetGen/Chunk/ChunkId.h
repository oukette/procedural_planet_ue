#pragma once

#include "CoreMinimal.h"
#include "Math/IntVector.h"
#include "../MathUtils.h"


/**
 * Unique identifier for a terrain chunk.
 * Immutable, thread-safe, no engine dependencies.
 */
struct PROCEDURALPLANET_API FChunkId
{
        uint8 CubeFace;
        FIntVector ChunkCoords;  // Face-local grid coordinates
        int32 LOD;

        // Default constructor
        FChunkId() :
            CubeFace(0),
            ChunkCoords(0, 0, 0),
            LOD(0)
        {
        }

        // Main constructor
        FChunkId(uint8 InCubeFace, const FIntVector &InChunkCoords, int32 InLOD) :
            CubeFace(InCubeFace),
            ChunkCoords(InChunkCoords),
            LOD(InLOD)
        {
        }

        // Comparison operators for use in TMap/TSet
        bool operator==(const FChunkId &Other) const { return CubeFace == Other.CubeFace && ChunkCoords == Other.ChunkCoords && LOD == Other.LOD; }

        bool operator!=(const FChunkId &Other) const { return !(*this == Other); }

        // For use as key in TMap
        friend uint32 GetTypeHash(const FChunkId &Id);

        // Utility functions
        FString ToString() const;

        bool IsValid() const;

        // Static helper to create chunk ID from world position
        static FChunkId FromWorldPosition(const FVector &WorldPosition, float PlanetRadius, int32 InLOD, float ChunkSize);

        // Get neighbor IDs (for seam handling)
        FChunkId GetNeighbor(int32 DeltaX, int32 DeltaY) const;
};