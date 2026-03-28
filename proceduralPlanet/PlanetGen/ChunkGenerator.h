#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "Templates/SharedPointer.h"

#include "ChunkId.h"
#include "DensityGenerator.h"


class INoise;


// Callback signature: ChunkId, GenerationId (for validation), MeshData
using OnChunkGenerated = TFunction<void(const ChunkId &, uint32, TUniquePtr<ChunkMeshData>)>;

struct ChunkRequest
{
        ChunkId Id;
        uint32 GenerationId;
        float PrioScore;  // Lower score = Higher priority (e.g. Distance)
};


struct FChunkGeneratorStats
{
    int32 Queued = 0;        // in priority queue, not yet dispatched
    int32 Active = 0;        // running on thread pool
    int32 Cancelled = 0;     // awaiting callback to confirm cancellation
    int32 StartTimesTracked = 0; // sanity: should equal Active
    float AvgGenMs = 0.f;
    float LastGenMs = 0.f;
    int32 ActiveThreads = 0; // raw thread counter from m_activeThreadsCounter
};



class ChunkGenerator
{
    private:
        FPlanetConfig m_planetConfig;
        const DensityGenerator *m_densityGen;  // Owned by Planet/Manager, we just hold ref
        TSharedPtr<INoise, ESPMode::ThreadSafe> m_noiseProviderRef;

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

        TMap<ChunkId, double> m_taskStartTimes;  // wall-clock seconds at task launch
        float m_avgGenerationTimeMs = 0.f;       // average time taken by async generation
        float m_lastGenerationTimeMs = 0.f;      // last sample of async generation time

    public:
        ChunkGenerator(const FPlanetConfig &InConfig, const DensityGenerator *InDensityGen, TSharedPtr<INoise, ESPMode::ThreadSafe> InNoise);
        ~ChunkGenerator();

        // Adds a chunk to the generation queue
        void RequestChunk(const ChunkId &Id, uint32 GenerationId, float PriorityScore);

        // Cancels a pending or active generation request
        void CancelRequest(const ChunkId &Id);

        // Main update loop to process queue and dispatch threads
        void Update();

        // Set the callback for when a chunk finishes
        void SetOnChunkGeneratedCallback(OnChunkGenerated InCallback);

        // Chunks waiting in the priority queue, not yet dispatched to the thread pool
        int32 GetQueuedCount() const { return m_requestsQueue.Num(); }

        // Chunks currently running on the thread pool
        int32 GetActiveCount() const { return m_activeTasks.Num(); }

        // Chunks marked for cancellation, waiting for their callback to fire
        int32 GetCancelledCount() const { return m_cancelledTasks.Num(); }

        // Total in-flight work: queued + active. Useful for throttling decisions.
        int32 GetTotalInFlightCount() const { return m_requestsQueue.Num() + m_activeTasks.Num(); }

        // Stops the generator, preventing new tasks and discarding results from in-flight tasks.
        void Stop();

        // Getter for the current Generator stats
        FChunkGeneratorStats GetDebugStats() const;

    private:
        void StartAsyncTask(const ChunkRequest &Request);
};