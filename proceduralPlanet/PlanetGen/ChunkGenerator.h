#pragma once

#include "CoreMinimal.h"
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

        // Main update loop to process queue and dispatch threads
        void Update();

        // Set the callback for when a chunk finishes
        void SetOnChunkGeneratedCallback(FOnChunkGenerated InCallback);

        int32 GetPendingCount() const;

    private:
        FPlanetConfig Config;
        const DensityGenerator *DensityGen;  // Owned by Planet/Manager, we just hold ref

        TArray<FChunkRequest> Queue;
        TSet<FChunkId> ActiveTasks;  // Set of IDs currently processing to prevent duplicates

        FOnChunkGenerated OnGeneratedCallback;

        void StartAsyncTask(const FChunkRequest &Request);
};