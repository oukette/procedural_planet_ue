#pragma once

#include "CoreMinimal.h"


// The state of a chunk in its lifecycle
enum class ChunkState : uint8
{
    None,        // Initial state
    Pending,     // In the generation queue
    Generating,  // Currently being processed by an async task
    DataReady,   // Mesh data is in RAM but not yet in the GPU
    MeshReady,   // Mesh is assigned to a component
    Visible      // Mesh is visible in the world
};


// Unique identifier for a Chunk on the CubeSphere
struct ChunkId
{
        uint8 FaceIndex = 0;                        // 0-5
        FIntVector Coords = FIntVector::ZeroValue;  // Face-local grid coordinates (X, Y)
        int32 LODLevel = 0;                         // 0 = Root, Higher = Smaller/More Detailed

        ChunkId() = default;

        ChunkId(uint8 InFace, FIntVector InCoords, int32 InLOD) :
            FaceIndex(InFace),
            Coords(InCoords),
            LODLevel(InLOD)
        {
        }

        bool operator==(const ChunkId &Other) const { return FaceIndex == Other.FaceIndex && Coords == Other.Coords && LODLevel == Other.LODLevel; }

        friend uint32 GetTypeHash(const ChunkId &Other)
        {
            return HashCombine(HashCombine(GetTypeHash(Other.FaceIndex), GetTypeHash(Other.Coords)), GetTypeHash(Other.LODLevel));
        }
};


// ---------------------------------------------------------------------------
// Chunk hierarchy helpers — pure functions of ChunkId data
// ---------------------------------------------------------------------------
inline ChunkId GetParentId(const ChunkId &Child) { return ChunkId(Child.FaceIndex, FIntVector(Child.Coords.X / 2, Child.Coords.Y / 2, 0), Child.LODLevel - 1); }


inline TArray<ChunkId> GetChildrenIds(const ChunkId &Parent)
{
    const int32 NextLOD = Parent.LODLevel + 1;
    const int32 X = Parent.Coords.X;
    const int32 Y = Parent.Coords.Y;
    const uint8 Face = Parent.FaceIndex;

    return {
        ChunkId(Face, FIntVector(X * 2, Y * 2, 0), NextLOD),
        ChunkId(Face, FIntVector(X * 2 + 1, Y * 2, 0), NextLOD),
        ChunkId(Face, FIntVector(X * 2, Y * 2 + 1, 0), NextLOD),
        ChunkId(Face, FIntVector(X * 2 + 1, Y * 2 + 1, 0), NextLOD),
    };
}


inline bool IsRootNode(const ChunkId &Id) { return Id.LODLevel == 0; }