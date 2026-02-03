#pragma once

#include "CoreMinimal.h"
#include "ChunkId.h"
#include "ChunkTransform.h"
#include "ChunkMeshData.h"
#include "ChunkState.h"
#include "ProceduralMeshComponent.h"


/**
 * Core terrain chunk as a system entity.
 *
 * Properties:
 * - Engine-agnostic
 * - Deterministic
 * - Thread-safe by design
 * - Not an Actor, Component, or UObject
 * - Pure data container with no behavior
 */
class PROCEDURALPLANET_API FChunk
{
    public:
        // ==================== Identity & Lifecycle ====================
        FChunkId Id;                               /** Unique identifier for this chunk */
        EChunkState State = EChunkState::Unloaded; /** Current lifecycle state */
        uint32 GenerationId = 0;                   /** Generation safety ID - increments each time generation is requested */


        // ==================== Spatial Data ====================
        FChunkTransform Transform; /** Transform data computed once at creation */

        // ==================== Generation Output ====================
        TUniquePtr<FChunkMeshData> MeshData; /** Mesh data generated on worker threads, consumed on game thread */


        // ==================== Rendering (Game Thread Only) ====================
        TWeakObjectPtr<UProceduralMeshComponent> RenderProxy; /** Weak reference to render proxy - accessed only on game thread */


        // ==================== Construction ====================

        /** Default constructor */
        FChunk() = default;

        /**
         * Main constructor.
         * @param InId Unique chunk identifier
         * @param InTransform Precomputed transform data
         */
        FChunk(const FChunkId &InId, const FChunkTransform &InTransform) :
            Id(InId),
            Transform(InTransform),
            MeshData(nullptr)
        {
        }

        /** Destructor */
        ~FChunk() = default;

        // Non-copyable, non-movable (enforce unique ownership)
        FChunk(const FChunk &) = delete;
        FChunk &operator=(const FChunk &) = delete;
        FChunk(FChunk &&) = delete;
        FChunk &operator=(FChunk &&) = delete;


        // ==================== Utility Functions ====================
        /** Check if chunk is valid */
        bool IsValid() const { return Id.IsValid() && Transform.IsValid(); }

        /** Get string representation for debugging */
        FString ToString() const { return FString::Printf(TEXT("Chunk[%s] State=%s GenId=%d"), *Id.ToString(), ::ToString(State), GenerationId); }

        /** Check if chunk is ready for rendering */
        bool IsReadyForRendering() const { return State == EChunkState::Ready && MeshData.IsValid() && MeshData->IsValid(); }

        /** Check if chunk is visible (being rendered) */
        bool IsVisible() const { return State == EChunkState::Visible && RenderProxy.IsValid(); }

        /** Check if chunk is loaded in memory */
        bool IsLoaded() const { return State != EChunkState::Unloaded && State != EChunkState::Unloading; }

        /** Check if chunk is being generated */
        bool IsGenerating() const { return State == EChunkState::Generating; }

        /** Get approximate memory usage */
        int32 EstimateMemoryBytes() const
        {
            int32 Bytes = sizeof(*this);
            if (MeshData.IsValid())
            {
                Bytes += MeshData->EstimateMemoryBytes();
            }
            return Bytes;
        }

        /** Get bounds in world space */
        void GetWorldBounds(FVector &OutMin, FVector &OutMax) const
        {
            if (MeshData.IsValid() && MeshData->GetVertexCount() > 0)
            {
                // Use calculated mesh bounds if available
                OutMin = MeshData->BoundsMin;
                OutMax = MeshData->BoundsMax;
            }
            else
            {
                // Fall back to transform bounds
                Transform.GetWorldBounds(OutMin, OutMax);
            }
        }

        /** Check if world position is within this chunk's area */
        bool ContainsWorldPosition(const FVector &WorldPosition, float Margin = 0.0f) const { return Transform.ContainsWorldPosition(WorldPosition, Margin); }


        // ==================== Mesh Data Management ====================

        /**
         * Set mesh data (should only be called by ChunkManager).
         * @param NewMeshData Mesh data to take ownership of
         */
        void SetMeshData(TUniquePtr<FChunkMeshData> &&NewMeshData)
        {
            check(IsInGameThread());  // Should only be called on game thread
            MeshData = MoveTemp(NewMeshData);

            if (MeshData.IsValid())
            {
                MeshData->CalculateBounds();
            }
        }

        /**
         * Clear mesh data (releases memory).
         */
        void ClearMeshData()
        {
            check(IsInGameThread());  // Should only be called on game thread
            MeshData.Reset();
        }

        /**
         * Get mesh data (read-only).
         */
        const FChunkMeshData *GetMeshData() const { return MeshData.Get(); }


        // ==================== Render Proxy Management ====================

        /**
         * Set render proxy (should only be called by ChunkManager).
         * @param NewProxy Weak reference to render component
         */
        void SetRenderProxy(UProceduralMeshComponent *NewProxy)
        {
            check(IsInGameThread());  // Should only be called on game thread
            RenderProxy = NewProxy;
        }

        /**
         * Clear render proxy reference.
         */
        void ClearRenderProxy()
        {
            check(IsInGameThread());  // Should only be called on game thread
            RenderProxy = nullptr;
        }

        /**
         * Get render proxy (may be null).
         */
        UProceduralMeshComponent *GetRenderProxy() const
        {
            check(IsInGameThread());  // Should only be called on game thread
            return RenderProxy.Get();
        }


        // ==================== State Management ====================

        /**
         * Transition to new state (should only be called by ChunkManager).
         */
        void TransitionToState(EChunkState NewState)
        {
            check(IsInGameThread());  // Should only be called on game thread

            // Validate transition
            if (!::IsValidStateTransition(State, NewState))
            {
                UE_LOG(LogTemp, Error, TEXT("Invalid chunk state transition: %s -> %s"), ::ToString(State), ::ToString(NewState));
                return;
            }

            State = NewState;
        }

        /**
         * Increment generation ID (should only be called by ChunkManager).
         */
        void IncrementGenerationId()
        {
            check(IsInGameThread());  // Should only be called on game thread
            GenerationId++;
        }

        /**
         * Check if generation ID matches (for async safety).
         */
        bool ValidateGenerationId(uint32 ExpectedId) const { return GenerationId == ExpectedId; }


        // ==================== Debug Visualization ====================

        /**
         * Draw debug visualization for this chunk.
         */
        void DrawDebug(UWorld *World) const;
};