#include "ChunkGenerator.h"
#include "Async/Async.h"
#include "MeshGenerator.h"
#include "MathUtils.h"


FChunkGenerator::FChunkGenerator(const FPlanetConfig &InConfig, const DensityGenerator *InDensityGen) :
    Config(InConfig),
    DensityGen(InDensityGen)
{
}

FChunkGenerator::~FChunkGenerator() {}

void FChunkGenerator::RequestChunk(const FChunkId &Id, uint32 GenerationId)
{
    if (ActiveTasks.Contains(Id))
        return;

    // Simple add to queue.
    // Note: In a production scenario, you might want to check if Id is already in Queue to avoid duplicates,
    // but TArray::Add is faster and the Set check above handles the critical "Active" case.
    Queue.Add({Id, GenerationId});
}

void FChunkGenerator::Update()
{
    // 1. Check limits
    if (ActiveTasks.Num() >= Config.MaxConcurrentGenerations)
        return;

    int32 StartedThisTick = 0;

    // 2. Process Queue
    while (Queue.Num() > 0 && StartedThisTick < Config.ChunkGenerationRate)
    {
        if (ActiveTasks.Num() >= Config.MaxConcurrentGenerations)
            break;

        FChunkRequest Request = Queue.Pop();

        // If not already active (double check)
        if (!ActiveTasks.Contains(Request.Id))
        {
            StartAsyncTask(Request);
            StartedThisTick++;
        }
    }
}

void FChunkGenerator::SetOnChunkGeneratedCallback(FOnChunkGenerated InCallback) { OnGeneratedCallback = InCallback; }

int32 FChunkGenerator::GetPendingCount() const { return Queue.Num() + ActiveTasks.Num(); }

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
                            ActiveTasks.Remove(Id);
                            if (OnGeneratedCallback)
                            {
                                OnGeneratedCallback(Id, GenId, MakeUnique<FChunkMeshData>(MoveTemp(MeshData)));
                            }
                        });
          });
}