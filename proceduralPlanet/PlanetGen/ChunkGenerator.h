#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "ChunkId.h"
#include "DensityGenerator.h"


// Callback signature: ChunkId, GenerationId (for validation), MeshData
using OnChunkGenerated = TFunction<void(const ChunkId &, uint32, TUniquePtr<ChunkMeshData>)>;

struct ChunkRequest
{
        ChunkId Id;
        uint32 GenerationId;
        float PrioScore;  // Lower score = Higher priority (e.g. Distance)
};


class ChunkGenerator
{
    private:
        FPlanetConfig m_planetConfig;
        const DensityGenerator *m_densityGen;  // Owned by Planet/Manager, we just hold ref

        TArray<ChunkRequest> m_requestsQueue;
        TSet<ChunkId> m_activeTasks;     // Set of IDs currently processing to prevent duplicates
        TSet<ChunkId> m_queuedIds;       // mirrors heap contents for O(1) duplicate detection
        TSet<ChunkId> m_cancelledTasks;  // Set of IDs that were cancelled while active

        OnChunkGenerated m_onChunkGeneratedCallback;

        FThreadSafeBool m_isStopping;  // Flag to signal that the generator is shutting down.

        // Token to track the lifecycle of this instance safely across threads.
        // The bool value is true while the generator is alive, and set to false in the destructor.
        TSharedPtr<bool, ESPMode::ThreadSafe> m_aliveToken;

        // Shared counter to track how many background threads are currently running.
        // We use this to force the destructor to wait until all workers are done.
        TSharedPtr<FThreadSafeCounter, ESPMode::ThreadSafe> m_activeThreadsCounter;

    public:
        ChunkGenerator(const FPlanetConfig &InConfig, const DensityGenerator *InDensityGen);
        ~ChunkGenerator();

        // Adds a chunk to the generation queue
        void RequestChunk(const ChunkId &Id, uint32 GenerationId, float PriorityScore);

        // Cancels a pending or active generation request
        void CancelRequest(const ChunkId &Id);

        // Main update loop to process queue and dispatch threads
        void Update();

        // Set the callback for when a chunk finishes
        void SetOnChunkGeneratedCallback(OnChunkGenerated InCallback);

        int32 GetPendingCount() const;

        // Stops the generator, preventing new tasks and discarding results from in-flight tasks.
        void Stop();

    private:
        void StartAsyncTask(const ChunkRequest &Request);
};