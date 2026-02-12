#include "ChunkManager.h"
#include "Async/Async.h"
#include "SimpleNoise.h"
#include "MeshGenerator.h"


FChunkManager::FChunkManager(const FPlanetConfig &InConfig, const DensityGenerator *InGenerator) :
    Config(InConfig),
    Generator(InGenerator)
{
    MaxConcurrentGenerations = Config.MaxConcurrentGenerations;
    GenerationRate = Config.GenerationRate;
    // DEBUG LOG
    UE_LOG(LogTemp, Warning, TEXT("FChunkManager created."));
}


FChunkManager::~FChunkManager()
{
    // UniquePtrs in the TMap will automatically destroy all FChunks
    Chunks.Empty();
}


int32 FChunkManager::GetVisibleChunkCount() const
{
    int32 Count = 0;
    for (const auto &Pair : Chunks)
    {
        // A chunk is "Visible" if the Update loop gave it a valid LOD
        // OR if its state implies it's currently doing something.
        // For the current Step 4.2 logic, checking the ID's LOD is the best indicator.
        if (Pair.Key.LOD != -1 && Pair.Value->State != EChunkState::Unloaded)
        {
            Count++;
        }
    }
    return Count;
}


void FChunkManager::Initialize(AActor *Owner, UMaterialInterface *Material)
{
    Renderer = MakeUnique<ChunkRenderer>(Owner, Material);

    // Replicates the nested loops from APlanet::PrepareGeneration
    int32 ChunksPerFace = Config.ChunksPerFace;

    for (uint8 Face = 0; Face < 6; Face++)
    {
        for (int32 x = 0; x < ChunksPerFace; x++)
        {
            for (int32 y = 0; y < ChunksPerFace; y++)
            {
                // 1. Create Identity
                FChunkId Id(Face, 0, FIntVector(x, y, 0));  // Start at LOD 0

                // 2. Create Data Entry
                FChunk *Chunk = CreateChunk(Id);

                // 3. Pre-calculate Spatial Info (Optimization)
                // We do this once here so we don't recalculate it every frame in Tick

                // Calculate UVs
                float Step = 1.0f / ChunksPerFace;
                FVector2D UVMin(x * Step, y * Step);
                FVector2D UVMax((x + 1) * Step, (y + 1) * Step);

                // Get Face Vectors
                FVector Normal = FMathUtils::GetFaceNormal(Face);
                FVector Right = FMathUtils::GetFaceRight(Face);
                FVector Up = FMathUtils::GetFaceUp(Face);

                // Calculate Center Position (LOD 0)
                FVector2D CenterUV = (UVMin + UVMax) * 0.5f;
                // Remap 0..1 to -1..1
                FVector CubePos = Normal + (Right * (CenterUV.X * 2.0f - 1.0f)) + (Up * (CenterUV.Y * 2.0f - 1.0f));
                FVector SpherePos = FMathUtils::CubeToSphere(CubePos) * Config.PlanetRadius;

                // Store in Transform
                Chunk->Transform.Location = SpherePos;
                Chunk->Transform.FaceNormal = Normal;
                Chunk->Transform.Scale = 1.0f;  // Base scale
            }
        }
    }

    // DEBUG LOG
    UE_LOG(LogTemp, Warning, TEXT("FChunkManager initialized with %d chunks."), Chunks.Num());
}


FChunk *FChunkManager::CreateChunk(const FChunkId &Id)
{
    TUniquePtr<FChunk> NewChunk = MakeUnique<FChunk>(Id);
    FChunk *Ptr = NewChunk.Get();
    Chunks.Add(Id, MoveTemp(NewChunk));
    return Ptr;
}


FChunk *FChunkManager::GetChunk(const FChunkId &Id)
{
    // If it exists, return it
    if (TUniquePtr<FChunk> *Found = Chunks.Find(Id))
    {
        return Found->Get();
    }

    // Otherwise, create a new one
    auto Ptr = CreateChunk(Id);

    return Ptr;
}


void FChunkManager::Update(const FPlanetViewContext &Context)
{
    // 1. Check Far Model Condition
    // If we are very far away, we don't want ANY chunks.
    float DistToPlanetSq = Context.ObserverLocation.SizeSquared();
    float FarThresholdSq = FMath::Square(Config.FarDistanceThreshold);

    if (DistToPlanetSq > FarThresholdSq)
    {
        // We are too far!
        // We should signal the Actor to show the Far Model (we'll do this via a return value or flag later).
        // For now, let's just ensure no chunks are marked as 'Required'.
        // (The cleanup loop below will handle unloading them).
        return;
    }

    // 2. Iterate Faces to find Required Chunks
    // We use a Set to keep track of what SHOULD exist this frame.
    TSet<FChunkId> RequiredChunks;

    for (uint8 Face = 0; Face < 6; ++Face)
    {
        UpdateFace(Face, Context, RequiredChunks);
    }

    // 3. Process State Changes

    // A. Remove/Unload chunks that are no longer required
    for (auto &Pair : Chunks)
    {
        FChunkId Id = Pair.Key;
        FChunk *Chunk = Pair.Value.Get();

        if (!RequiredChunks.Contains(Id))
        {
            // If it was active, mark it for unloading
            if (Chunk->State != EChunkState::Unloaded)
            {
                Renderer->HideChunk(Chunk);  // Return component to pool
                Chunk->State = EChunkState::Unloading;
                // In Phase 5, this will trigger the Actor to destroy the mesh
            }
        }
    }

    // B. Request Generation for new required chunks
    for (const FChunkId &Id : RequiredChunks)
    {
        FChunk *Chunk = GetChunk(Id);

        // If it's new (Unloaded) or needs an LOD update
        if (Chunk->State == EChunkState::Unloaded)
        {
            Chunk->State = EChunkState::Requested;
            PendingGenerationQueue.Add(Id);
        }
        // If data is ready but not yet visible, render it!
        else if (Chunk->State == EChunkState::Ready)
        {
            Renderer->RenderChunk(Chunk);
            Chunk->State = EChunkState::Visible;
        }

        // (LOD switching logic will go here in Phase 6)
    }

    ProcessQueues();
}


void FChunkManager::UpdateFace(uint8 Face, const FPlanetViewContext &Context, TSet<FChunkId> &OutRequired)
{
    int32 GridSize = Config.ChunksPerFace;
    float Step = 1.0f / GridSize;

    FVector Normal = FMathUtils::GetFaceNormal(Face);
    FVector Right = FMathUtils::GetFaceRight(Face);
    FVector Up = FMathUtils::GetFaceUp(Face);

    for (int32 x = 0; x < GridSize; ++x)
    {
        for (int32 y = 0; y < GridSize; ++y)
        {
            FIntVector Coords(x, y, 0);

            // 1. Calculate World Position
            FVector2D UVMin(x * Step, y * Step);
            FVector2D UVMax((x + 1) * Step, (y + 1) * Step);
            FVector2D CenterUV = (UVMin + UVMax) * 0.5f;
            FVector CubePos = Normal + (Right * (CenterUV.X * 2.0f - 1.0f)) + (Up * (CenterUV.Y * 2.0f - 1.0f));
            FVector WorldPos = FMathUtils::CubeToSphere(CubePos) * Config.PlanetRadius;
            float DistSq = FVector::DistSquared(Context.ObserverLocation, WorldPos);

            // 2. Find current LOD for this grid cell, if any chunk exists
            int32 CurrentLOD = -1;
            for (int32 lod = 0; lod < Config.LODSettings.Num(); ++lod)
            {
                FChunkId testId(Face, lod, Coords);
                if (Chunks.Contains(testId))
                {
                    CurrentLOD = lod;
                    break;
                }
            }

            // 3. Calculate Target LOD using hysteresis
            int32 TargetLOD = CalculateTargetLOD(DistSq, CurrentLOD);

            // 4. If a chunk is required (TargetLOD is valid), add its ID to the set.
            if (TargetLOD != -1)
            {
                FChunkId RequiredId(Face, TargetLOD, Coords);
                OutRequired.Add(RequiredId);

                // Ensure the chunk data container exists and has its transform info updated.
                // This is important for new chunks.
                FChunk *Chunk = GetChunk(RequiredId);
                if (Chunk)
                {
                    Chunk->Transform.Location = WorldPos;
                    Chunk->Transform.FaceNormal = Normal;
                    Chunk->Transform.Scale = 1.0f;
                }
            }
        }
    }
}


int32 FChunkManager::CalculateTargetLOD(float DistanceSq, int32 CurrentLOD) const
{
    // If a chunk is already active, use hysteresis to prevent rapid switching
    if (CurrentLOD != -1)
    {
        // Check for UPGRADE to a higher-detail LOD (smaller index)
        if (CurrentLOD > 0)
        {
            const float UpgradeDistSq = FMath::Square(Config.LODSettings[CurrentLOD - 1].Distance);
            if (DistanceSq < UpgradeDistSq)
            {
                return CurrentLOD - 1;
            }
        }

        // Check for DOWNGRADE to a lower-detail LOD (larger index)
        if (CurrentLOD < Config.LODSettings.Num() - 1)
        {
            const float DowngradeDistSq = FMath::Square(Config.LODSettings[CurrentLOD].Distance * Config.LODHysteresis);
            if (DistanceSq > DowngradeDistSq)
            {
                return CurrentLOD + 1;
            }
        }

        // Check for DESPAWN (distance is beyond the largest LOD's range)
        const float LastLODDist = Config.LODSettings.Last().Distance;
        const float DespawnDistSq = FMath::Square(LastLODDist * Config.LODDespawnHysteresis);
        if (DistanceSq > DespawnDistSq)
        {
            return -1;  // Despawn
        }

        return CurrentLOD;  // No change
    }

    // No active chunk, determine if we need to spawn one
    for (int32 LodIndex = 0; LodIndex < Config.LODSettings.Num(); ++LodIndex)
    {
        float Threshold = Config.LODSettings[LodIndex].Distance;

        // Use squared distance for performance (avoid Sqrt)
        if (DistanceSq < (Threshold * Threshold))
        {
            return LodIndex;  // Spawn at this LOD
        }
    }

    // If further than the last LOD setting, it's too far.
    return -1;
}


void FChunkManager::ProcessQueues()
{
    // Phase 5: Handling the Data/Density generation
    ProcessGenerationQueue();

    // Phase 6: (Future) Handling the Mesh/Actor spawning
    // ProcessMeshQueue();
}


void FChunkManager::ProcessGenerationQueue()
{
    // 1. Don't overload the CPU threads
    if (CurrentlyGenerating.Num() >= MaxConcurrentGenerations)
        return;

    int32 StartedThisTick = 0;

    // 2. Start new tasks up to the GenerationRate
    while (PendingGenerationQueue.Num() > 0 && StartedThisTick < GenerationRate)
    {
        // Ensure we haven't hit the global thread limit mid-loop
        if (CurrentlyGenerating.Num() >= MaxConcurrentGenerations)
            break;

        FChunkId Id = PendingGenerationQueue.Pop();

        // Final safety check before threading
        if (!CurrentlyGenerating.Contains(Id))
        {
            StartAsyncGeneration(Id);  // This is where the magic happens
            StartedThisTick++;
        }
    }
}


void FChunkManager::StartAsyncGeneration(const FChunkId &Id)
{
    FChunk *Chunk = GetChunk(Id);
    if (!Chunk)
        return;

    // 1. Update State
    Chunk->State = EChunkState::Generating;
    Chunk->GenerationId++;
    CurrentlyGenerating.Add(Id);

    // 2. Prepare Data for Thread (Capture by Value)
    uint32 GenId = Chunk->GenerationId;

    // Reconstruct Spatial Data
    int32 ChunksPerFace = Config.ChunksPerFace;
    float Step = 1.0f / ChunksPerFace;
    FVector2D UVMin(Id.Coords.X * Step, Id.Coords.Y * Step);
    FVector2D UVMax((Id.Coords.X + 1) * Step, (Id.Coords.Y + 1) * Step);

    uint8 FaceIdx = Id.FaceIndex;
    FVector FaceNormal = FMathUtils::GetFaceNormal(FaceIdx);
    FVector FaceRight = FMathUtils::GetFaceRight(FaceIdx);
    FVector FaceUp = FMathUtils::GetFaceUp(FaceIdx);

    // Calculate Transform (Relative to Planet Center)
    FVector2D CenterUV = (UVMin + UVMax) * 0.5f;
    FVector CubePos = FaceNormal + (FaceRight * (CenterUV.X * 2.0f - 1.0f)) + (FaceUp * (CenterUV.Y * 2.0f - 1.0f));
    FVector SphereDir = FMathUtils::CubeToSphere(CubePos);
    FVector ChunkLocation = SphereDir * Config.PlanetRadius;

    // Calculate Rotation (Align Up with Sphere Normal)
    FVector ChunkUp = SphereDir;
    FVector ChunkRight = FVector::CrossProduct(FaceUp, ChunkUp).GetSafeNormal();
    FVector ChunkForward = FVector::CrossProduct(ChunkUp, ChunkRight);
    FMatrix RotMatrix = FRotationMatrix::MakeFromXY(ChunkForward, ChunkRight);
    FTransform ChunkTransform(RotMatrix.ToQuat(), ChunkLocation);

    // Store rotation in chunk data for the renderer to use later
    if (Chunk)
    {
        Chunk->Transform.Rotation = RotMatrix.ToQuat();
    }

    // LOD Config
    int32 Resolution = Config.LODSettings[Id.LOD].VoxelResolution;
    int32 LODLevel = Id.LOD;

    // Copy Generator Config (Thread Safety)
    DensityGenerator ThreadGen = *Generator;

    // 3. Launch Async Task
    Async(EAsyncExecution::ThreadPool,
          [this, Id, GenId, Resolution, FaceNormal, FaceRight, FaceUp, UVMin, UVMax, ChunkTransform, LODLevel, ThreadGen]()
          {
              // A. Generate Density
              GenData GeneratedData = ThreadGen.GenerateDensityField(Resolution, FaceNormal, FaceRight, FaceUp, UVMin, UVMax);

              // B. Generate Mesh
              // PlanetTransform is Identity because we want vertices relative to Planet Center (0,0,0)
              FChunkMeshData MeshData = MeshGenerator::GenerateMesh(GeneratedData, Resolution, ChunkTransform, FTransform::Identity, LODLevel, ThreadGen);

              // C. Return to Game Thread
              AsyncTask(ENamedThreads::GameThread,
                        [this, Id, GenId, MeshData]() mutable { OnGenerationComplete(Id, GenId, MakeUnique<FChunkMeshData>(MoveTemp(MeshData))); });
          });
}


void FChunkManager::OnGenerationComplete(const FChunkId &Id, uint32 GenId, TUniquePtr<FChunkMeshData> MeshData)
{
    CurrentlyGenerating.Remove(Id);

    FChunk *Chunk = GetChunk(Id);
    if (!Chunk)
        return;  // Chunk was unloaded while generating

    if (Chunk->GenerationId != GenId)
        return;  // Stale task (Chunk was reset/regenerated)

    // Store Data
    Chunk->MeshData = MoveTemp(MeshData);
    Chunk->State = EChunkState::Ready;

    // Note: We do not spawn the component here.
    // The Update loop or a separate "ProcessMeshQueue" will handle visibility/spawning.
}