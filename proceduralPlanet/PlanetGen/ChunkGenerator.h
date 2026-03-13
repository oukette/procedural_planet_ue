#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "DataTypes.h"
#include "DensityGenerator.h"


// Callback signature: ChunkId, GenerationId (for validation), MeshData
using FOnChunkGenerated = TFunction<void(const FChunkId &, uint32, TUniquePtr<FChunkMeshData>)>;

struct FChunkRequest
{
        FChunkId Id;
        uint32 GenerationId;
};

class FChunkGenerator
{
    public:
        FChunkGenerator(const FPlanetConfig &InConfig, const DensityGenerator *InDensityGen);
        ~FChunkGenerator();

        // Adds a chunk to the generation queue
        void RequestChunk(const FChunkId &Id, uint32 GenerationId);

        // Cancels a pending or active generation request
        void CancelRequest(const FChunkId &Id);

        // Main update loop to process queue and dispatch threads
        void Update();

        // Set the callback for when a chunk finishes
        void SetOnChunkGeneratedCallback(FOnChunkGenerated InCallback);

        int32 GetPendingCount() const;

        // Stops the generator, preventing new tasks and discarding results from in-flight tasks.
        void Stop();

    private:
        FPlanetConfig Config;
        const DensityGenerator *DensityGen;  // Owned by Planet/Manager, we just hold ref

        TArray<FChunkRequest> RequestsQueue;
        TSet<FChunkId> ActiveTasks;     // Set of IDs currently processing to prevent duplicates
        TSet<FChunkId> CancelledTasks;  // Set of IDs that were cancelled while active

        FOnChunkGenerated OnGeneratedCallback;

        // Flag to signal that the generator is shutting down.
        FThreadSafeBool bIsStopping;

        // Token to track the lifecycle of this instance safely across threads.
        // The bool value is true while the generator is alive, and set to false in the destructor.
        TSharedPtr<bool, ESPMode::ThreadSafe> AliveToken;

        // Shared counter to track how many background threads are currently running.
        // We use this to force the destructor to wait until all workers are done.
        TSharedPtr<FThreadSafeCounter, ESPMode::ThreadSafe> ActiveThreadsCounter;

        void StartAsyncTask(const FChunkRequest &Request);
};