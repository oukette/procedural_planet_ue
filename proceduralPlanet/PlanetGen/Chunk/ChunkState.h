#pragma once

#include "CoreMinimal.h"


// Lifecycle state of a chunk.
// State transitions are explicit and monotonic.
enum class EChunkState : uint8
{
    Unloaded,    // Chunk does not exist in memory
    Requested,   // Chunk creation has been requested
    Generating,  // Mesh is being generated asynchronously
    Ready,       // Mesh generation complete, ready for rendering
    Visible,     // Chunk is being rendered
    Unloading    // Chunk is scheduled for removal
};


// Helper functions
inline const TCHAR *ToString(EChunkState State)
{
    switch (State)
    {
        case EChunkState::Unloaded:
            return TEXT("Unloaded");
        case EChunkState::Requested:
            return TEXT("Requested");
        case EChunkState::Generating:
            return TEXT("Generating");
        case EChunkState::Ready:
            return TEXT("Ready");
        case EChunkState::Visible:
            return TEXT("Visible");
        case EChunkState::Unloading:
            return TEXT("Unloading");
        default:
            return TEXT("Unknown");
    }
}


inline bool IsValidStateTransition(EChunkState From, EChunkState To)
{
    // Define allowed transitions
    switch (From)
    {
        case EChunkState::Unloaded:
            return To == EChunkState::Requested;

        case EChunkState::Requested:
            return To == EChunkState::Generating || To == EChunkState::Unloaded;

        case EChunkState::Generating:
            return To == EChunkState::Ready || To == EChunkState::Unloaded;

        case EChunkState::Ready:
            return To == EChunkState::Visible || To == EChunkState::Unloading;

        case EChunkState::Visible:
            return To == EChunkState::Unloading;

        case EChunkState::Unloading:
            return To == EChunkState::Unloaded;

        default:
            return false;
    }
}