#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
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

        void StartAsyncTask(const FChunkRequest &Request);
};