#include "ChunkGenerator.h"
#include "Async/Async.h"
#include "MeshGenerator.h"
#include "MathUtils.h"


FChunkGenerator::FChunkGenerator(const FPlanetConfig &InConfig, const DensityGenerator *InDensityGen) :
    Config(InConfig),
    DensityGen(InDensityGen)
{
    bIsStopping = false;
}

FChunkGenerator::~FChunkGenerator()
{
    // Log a warning if Stop() was not called before destruction.
    // This is a lifecycle hint for debugging, not a hard assertion — normal shutdown sequences may destroy the generator without an
    // explicit Stop() and that is acceptable.
    UE_LOG(LogTemp,
           Warning,
           TEXT("FChunkGenerator destroyed without Stop() being called. "
                "This is acceptable during normal shutdown but may indicate a lifecycle issue "
                "if seen during gameplay."));
}

void FChunkGenerator::RequestChunk(const FChunkId &Id, uint32 GenerationId)
{
    if (ActiveTasks.Contains(Id))
        return;  // Already in queue

    // Add the request to queue
    RequestsQueue.Add({Id, GenerationId});
}

void FChunkGenerator::Stop()
{
    bIsStopping = true;
    RequestsQueue.Empty();
    // Any tasks currently running will complete, but their results will be
    // discarded on the game thread because bIsStopping is true.
}

void FChunkGenerator::CancelRequest(const FChunkId &Id)
{
    // 1. If it's in the queue, remove it. This is O(n), but the queue is not expected to be huge.
    RequestsQueue.RemoveAll([&Id](const FChunkRequest &Request) { return Request.Id == Id; });

    // 2. If it's an active task, we can't stop it.
    // Add it to a "cancelled" set. The task will complete, but we'll check this set before firing the callback.
    if (ActiveTasks.Contains(Id))
    {
        CancelledTasks.Add(Id);
    }
}

void FChunkGenerator::Update()
{
    // If stopping, don't start any new tasks.
    if (bIsStopping)
    {
        return;
    }

    // Prune any cancelled IDs that are no longer active.
    // This handles the case where a task was cancelled and the chunk was destroyed before the async callback ever fired — meaning the callback never will,
    // and the ID would otherwise leak in CancelledTasks indefinitely.
    if (CancelledTasks.Num() > 0)
    {
        TArray<FChunkId> StaleCancellations;
        for (const FChunkId &Id : CancelledTasks)
        {
            if (!ActiveTasks.Contains(Id))
            {
                StaleCancellations.Add(Id);
            }
        }
        for (const FChunkId &Id : StaleCancellations)
        {
            CancelledTasks.Remove(Id);
        }
    }

    // Check limits
    if (ActiveTasks.Num() >= Config.MaxConcurrentGenerations)
        return;

    int32 StartedThisTick = 0;

    // Process Queue
    while (RequestsQueue.Num() > 0 && StartedThisTick < Config.ChunkGenerationRate)
    {
        if (ActiveTasks.Num() >= Config.MaxConcurrentGenerations)
            break;

        // Pop from front (index 0) for FIFO ordering — oldest request processed first.
        FChunkRequest Request = RequestsQueue[0];
        RequestsQueue.RemoveAt(0, 1, false);  // false = don't shrink allocation each removal

        // If not already active (double check)
        if (!ActiveTasks.Contains(Request.Id))
        {
            StartAsyncTask(Request);
            StartedThisTick++;
        }
    }
}

void FChunkGenerator::SetOnChunkGeneratedCallback(FOnChunkGenerated InCallback) { OnGeneratedCallback = InCallback; }

int32 FChunkGenerator::GetPendingCount() const { return RequestsQueue.Num() + ActiveTasks.Num(); }

void FChunkGenerator::StartAsyncTask(const FChunkRequest &Request)
{
    ActiveTasks.Add(Request.Id);

    // Capture data by value for thread safety
    FChunkId Id = Request.Id;
    uint32 GenId = Request.GenerationId;
    int32 Resolution = Config.GridResolution;
    int32 LODLevel = Id.LODLevel;
    float PlanetRadius = Config.PlanetRadius;

    // Copy DensityGenerator (it's lightweight config + pointer to noise)
    DensityGenerator ThreadGen = *DensityGen;

    // Calculate Transform (Stateless math via MathUtils)
    FChunkTransform ChunkTransform = FMathUtils::ComputeChunkTransform(Id, PlanetRadius);
    FTransform Transform(ChunkTransform.Rotation, ChunkTransform.Location);

    // Calculate Geometry
    FVector2D UVMin, UVMax;
    FMathUtils::GetChunkUVBounds(Id, UVMin, UVMax);
    FVector2D CubeMin = UVMin * 2.0f - 1.0f;
    FVector2D CubeMax = UVMax * 2.0f - 1.0f;

    uint8 FaceIdx = Id.FaceIndex;
    FVector FaceNormal = FMathUtils::getFaceNormal(FaceIdx);
    FVector FaceRight = FMathUtils::getFaceRight(FaceIdx);
    FVector FaceUp = FMathUtils::getFaceUp(FaceIdx);

    // Launch Async
    Async(EAsyncExecution::ThreadPool,
          [this, Id, GenId, Resolution, FaceNormal, FaceRight, FaceUp, CubeMin, CubeMax, Transform, LODLevel, ThreadGen]()
          {
              // A. Generate Density
              GenData GeneratedData = ThreadGen.GenerateDensityField(Resolution, FaceNormal, FaceRight, FaceUp, CubeMin, CubeMax);

              // B. Generate Mesh
              FChunkMeshData MeshData = MeshGenerator::GenerateMesh(GeneratedData, Resolution, Transform, FTransform::Identity, LODLevel, ThreadGen);

              // C. Return to Game Thread
              AsyncTask(ENamedThreads::GameThread,
                        [this, Id, GenId, MeshData]() mutable
                        {
                            // If the generator is stopping, discard the result immediately.
                            // This prevents callbacks to a potentially destroyed ChunkManager.
                            if (bIsStopping)
                            {
                                return;
                            }

                            ActiveTasks.Remove(Id);

                            // Check if this task was cancelled while it was running
                            if (CancelledTasks.Contains(Id))
                            {
                                CancelledTasks.Remove(Id);  // Clean up the cancellation request
                                return;                     // Do not call the callback
                            }

                            if (OnGeneratedCallback)
                            {
                                OnGeneratedCallback(Id, GenId, MakeUnique<FChunkMeshData>(MoveTemp(MeshData)));
                            }
                        });
          });
}