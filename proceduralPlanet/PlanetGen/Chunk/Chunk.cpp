#include "Chunk.h"
#include "ChunkId.h"
#include "ChunkTransform.h"
#include "ChunkMeshData.h"
#include "ChunkState.h"
#include "DrawDebugHelpers.h"


void FChunk::DrawDebug(UWorld *World) const
{
    if (!World || !IsValid())
        return;

    FColor StateColor = FColor::White;
    switch (State)
    {
        case EChunkState::Unloaded:
            StateColor = FColor(128, 128, 128);
            break;  // Gray
        case EChunkState::Requested:
            StateColor = FColor::Yellow;
            break;
        case EChunkState::Generating:
            StateColor = FColor::Orange;
            break;
        case EChunkState::Ready:
            StateColor = FColor::Green;
            break;
        case EChunkState::Visible:
            StateColor = FColor::Blue;
            break;
        case EChunkState::Unloading:
            StateColor = FColor::Red;
            break;
    }

    // Draw chunk bounds
    FVector BoundsMin, BoundsMax;
    GetWorldBounds(BoundsMin, BoundsMax);
    FVector BoundsCenter = (BoundsMin + BoundsMax) * 0.5f;
    FVector BoundsExtent = (BoundsMax - BoundsMin) * 0.5f;

    DrawDebugBox(World, BoundsCenter, BoundsExtent, StateColor, true, -1.0f, 0, 2.0f);

    // Draw chunk center
    DrawDebugPoint(World, Transform.WorldOrigin, 5.0f, StateColor, true, -1.0f, 0);

    // Draw face normal
    DrawDebugLine(World, Transform.WorldOrigin, Transform.WorldOrigin + Transform.CubeNormal * 50.0f, FColor::Cyan, true, -1.0f, 0, 1.0f);

    // Draw state text
    if (GEngine)
    {
        FString DebugText = FString::Printf(TEXT("Chunk %s\nState: %s\nGenId: %d"), *Id.ToString(), ::ToString(State), GenerationId);

        if (MeshData.IsValid())
        {
            DebugText += FString::Printf(TEXT("\nVerts: %d Tris: %d"), MeshData->GetVertexCount(), MeshData->GetTriangleCount());
        }

        GEngine->AddOnScreenDebugMessage(-1, 0.0f, StateColor, DebugText);
    }
}