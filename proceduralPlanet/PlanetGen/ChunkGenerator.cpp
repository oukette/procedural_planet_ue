#include "ChunkGenerator.h"
#include "MeshGenerator.h"
#include "../Utils/MathUtils.h"

#include "Async/Async.h"
#include "HAL/PlatformProcess.h"


ChunkGenerator::ChunkGenerator(const FPlanetConfig &InConfig, const DensityGenerator *InDensityGen) :
    m_planetConfig(InConfig),
    m_densityGen(InDensityGen)
{
    m_isStopping = false;
    m_aliveToken = MakeShared<bool, ESPMode::ThreadSafe>(true);
    m_activeThreadsCounter = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>(0);
}


ChunkGenerator::~ChunkGenerator()
{
    // Log a warning if Stop() was not called before destruction.
    // This is a lifecycle hint for debugging.
    UE_LOG(LogTemp,
           Warning,
           TEXT("ChunkGenerator destroyed without Stop() being called. "
                "This is acceptable during normal shutdown but may indicate a lifecycle issue "
                "if seen during gameplay."));

    // Mark the token as false so any pending async tasks know we are dead.
    if (m_aliveToken.IsValid())
    {
        *m_aliveToken = false;
    }

    // Wait for background threads to finish.
    // If we destroy this object (and subsequently the APlanet's NoiseProvider),
    // any running threads accessing the noise provider will crash.
    if (m_activeThreadsCounter.IsValid())
    {
        const double StartWaitTime = FPlatformTime::Seconds();
        while (m_activeThreadsCounter->GetValue() > 0)
        {
            FPlatformProcess::Sleep(0.01f);  // Sleep 10ms to avoid hogging CPU

            if (FPlatformTime::Seconds() - StartWaitTime > 5.0)
            {
                UE_LOG(LogTemp, Error, TEXT("Timed out waiting for chunk generation threads to finish!"));
                break;
            }
        }
    }
}


void ChunkGenerator::RequestChunk(const ChunkId &Id, uint32 GenerationId, float PriorityScore)
{
    if (m_activeTasks.Contains(Id) || m_queuedIds.Contains(Id))
        return;  // Already in queue

    m_queuedIds.Add(Id);
    // Use a Min-Heap (Lowest Score at Top).
    // We use the 'Greater' predicate (>), which causes Heap functions to prioritize smaller values as 'Top'.
    m_requestsQueue.HeapPush({Id, GenerationId, PriorityScore}, [](const ChunkRequest &A, const ChunkRequest &B) { return A.PrioScore > B.PrioScore; });
}


void ChunkGenerator::Stop()
{
    m_isStopping = true;
    m_requestsQueue.Empty();
    m_queuedIds.Empty();

    // Clear active tasks set immediately so no new tasks can be added or processed by logic relying on this set.
    m_activeTasks.Empty();
}


void ChunkGenerator::CancelRequest(const ChunkId &Id)
{
    // Mark as cancelled regardless of whether it's queued or actively running.
    // - If queued: it will be popped and skipped in Update()
    // - If active: it will be caught in the game thread callback
    m_cancelledTasks.Add(Id);
    m_queuedIds.Remove(Id);  // Keep m_queuedIds consistent so RequestChunk can re-queue it later if needed
}


void ChunkGenerator::Update()
{
    // If stopping, don't start any new tasks.
    if (m_isStopping)
    {
        return;
    }

    // Prune any cancelled IDs that are no longer active.
    // This handles the case where a task was cancelled and the chunk was destroyed before the async callback ever fired — meaning the callback never will,
    // and the ID would otherwise leak in m_cancelledTasks indefinitely.
    if (m_cancelledTasks.Num() > 0)
    {
        TArray<ChunkId> StaleCancellations;
        for (const ChunkId &Id : m_cancelledTasks)
        {
            if (!m_activeTasks.Contains(Id))
                StaleCancellations.Add(Id);
        }
        for (const ChunkId &Id : StaleCancellations)
            m_cancelledTasks.Remove(Id);
    }

    // Check limits
    if (m_activeTasks.Num() >= m_planetConfig.MaxConcurrentGenerations)
        return;

    int32 StartedThisTick = 0;

    // Process Queue
    while (m_requestsQueue.Num() > 0 && StartedThisTick < m_planetConfig.ChunkGenerationRate)
    {
        if (m_activeTasks.Num() >= m_planetConfig.MaxConcurrentGenerations)
            break;

        // Efficiently pop the highest priority (lowest score) request from the heap (O(log n))
        ChunkRequest Request;
        m_requestsQueue.HeapPop(Request, [](const ChunkRequest &A, const ChunkRequest &B) { return A.PrioScore > B.PrioScore; });
        m_queuedIds.Remove(Request.Id);

        // Skip if cancelled while sitting in the queue
        if (m_cancelledTasks.Contains(Request.Id))
        {
            m_cancelledTasks.Remove(Request.Id);
            continue;
        }

        // If not already active (double check)
        if (!m_activeTasks.Contains(Request.Id))
        {
            StartAsyncTask(Request);
            StartedThisTick++;
        }
    }
}


void ChunkGenerator::SetOnChunkGeneratedCallback(OnChunkGenerated InCallback) { m_onChunkGeneratedCallback = InCallback; }


int32 ChunkGenerator::GetPendingCount() const { return m_requestsQueue.Num() + m_activeTasks.Num(); }


void ChunkGenerator::StartAsyncTask(const ChunkRequest &Request)
{
    m_activeTasks.Add(Request.Id);

    // Capture data by value for thread safety
    ChunkId Id = Request.Id;
    uint32 GenId = Request.GenerationId;
    int32 Resolution = m_planetConfig.GridResolution;
    int32 LODLevel = Id.LODLevel;
    float PlanetRadius = m_planetConfig.PlanetRadius;

    // Copy DensityGenerator (it's lightweight config + pointer to noise)
    DensityGenerator ThreadGen = *m_densityGen;

    // Calculate Transform (Stateless math via MathUtils)
    ChunkTransform ChunkTransform = FMathUtils::ComputeChunkTransform(Id, PlanetRadius);
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

    // Capture the lifecycle token. This shared pointer keeps the bool alive
    // even if 'this' generator is destroyed.
    TSharedPtr<bool, ESPMode::ThreadSafe> Token = m_aliveToken;

    // Capture the thread counter to keep it alive and modify it safely
    m_activeThreadsCounter->Increment();
    TSharedPtr<FThreadSafeCounter, ESPMode::ThreadSafe> CounterRef = m_activeThreadsCounter;

    // RAII Guard: Ensure the counter is decremented when the lambda is destroyed,
    // whether the task finished naturally or was aborted/destroyed by the thread pool on exit.
    TSharedPtr<void, ESPMode::ThreadSafe> ThreadGuard((void *)nullptr,
                                                      [CounterRef](void *)
                                                      {
                                                          if (CounterRef.IsValid())
                                                          {
                                                              CounterRef->Decrement();
                                                          }
                                                      });

    // Launch Async
    Async(EAsyncExecution::ThreadPool,
          [this, Id, GenId, Resolution, FaceNormal, FaceRight, FaceUp, CubeMin, CubeMax, Transform, LODLevel, ThreadGen, Token, ThreadGuard]()
          {
              // A. Generate Density
              GenData GeneratedData = ThreadGen.GenerateDensityField(Resolution, FaceNormal, FaceRight, FaceUp, CubeMin, CubeMax);

              // B. Generate Mesh
              ChunkMeshData MeshData = MeshGenerator::GenerateMesh(GeneratedData, Resolution, Transform, FTransform::Identity, LODLevel, ThreadGen);

              // C. Return to Game Thread
              AsyncTask(ENamedThreads::GameThread,
                        [this, Id, GenId, MeshData, Token]() mutable
                        {
                            // Safety Check: Is the generator still alive?
                            // If *Token is false, the generator destructor has already run.
                            // accessing 'this' (e.g. m_isStopping) would crash.
                            if (!Token.IsValid() || !(*Token))
                            {
                                return;
                            }

                            // If the generator is stopping, discard the result immediately.
                            // This prevents callbacks to a potentially destroyed ChunkManager.
                            if (m_isStopping)
                            {
                                return;
                            }

                            m_activeTasks.Remove(Id);

                            // Check if this task was cancelled while it was running
                            if (m_cancelledTasks.Contains(Id))
                            {
                                m_cancelledTasks.Remove(Id);  // Clean up the cancellation request
                                return;                       // Do not call the callback
                            }

                            if (m_onChunkGeneratedCallback)
                            {
                                m_onChunkGeneratedCallback(Id, GenId, MakeUnique<ChunkMeshData>(MoveTemp(MeshData)));
                            }
                        });
          });
}