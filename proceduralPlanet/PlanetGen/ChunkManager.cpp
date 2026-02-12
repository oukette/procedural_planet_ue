#include "ChunkManager.h"


FChunkManager::FChunkManager(const FPlanetConfig &InConfig, const DensityGenerator *InGenerator) :
    Config(InConfig),
    Generator(InGenerator)
{
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


void FChunkManager::Initialize()
{
    // Replicates the nested loops from ACubeSpherePlanet::PrepareGeneration
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
            GenerationQueue.Enqueue(Id);
        }
        // (LOD switching logic will go here in Phase 6)
    }
}


void FChunkManager::UpdateFace(uint8 Face, const FPlanetViewContext &Context, TSet<FChunkId> &OutRequired)
{
    int32 GridSize = Config.ChunksPerFace;
    float Step = 1.0f / GridSize;  // Calculate step size once

    // Pre-fetch face vectors to avoid recalculating 256+ times
    FVector Normal = FMathUtils::GetFaceNormal(Face);
    FVector Right = FMathUtils::GetFaceRight(Face);
    FVector Up = FMathUtils::GetFaceUp(Face);

    for (int32 x = 0; x < GridSize; ++x)
    {
        for (int32 y = 0; y < GridSize; ++y)
        {
            // 1. Calculate World Position (Pure Math, no Map Lookup needed)
            // This ensures we can determine LOD even if the chunk doesn't exist yet.
            FVector2D UVMin(x * Step, y * Step);
            FVector2D UVMax((x + 1) * Step, (y + 1) * Step);
            FVector2D CenterUV = (UVMin + UVMax) * 0.5f;

            // Map 0..1 to -1..1
            FVector CubePos = Normal + (Right * (CenterUV.X * 2.0f - 1.0f)) + (Up * (CenterUV.Y * 2.0f - 1.0f));
            FVector WorldPos = FMathUtils::CubeToSphere(CubePos) * Config.PlanetRadius;

            // 2. Distance Check
            float DistSq = FVector::DistSquared(Context.ObserverLocation, WorldPos);

            // 3. Calculate Target LOD
            int32 TargetLOD = CalculateTargetLOD(DistSq);

            // 4. If valid, mark as Required
            if (TargetLOD != -1)
            {
                // Construct the ID with the Correct LOD
                FChunkId Id(Face, TargetLOD, FIntVector(x, y, 0));

                OutRequired.Add(Id);

                // Use your renamed method to ensure the data container exists
                FChunk *Chunk = GetChunk(Id);

                // Store spatial info (useful for the Generator later)
                if (Chunk)
                {
                    Chunk->Transform.Location = WorldPos;
                    Chunk->Transform.FaceNormal = Normal;
                    Chunk->Transform.Scale = 1.0f;  // Could scale based on LOD level here
                }

                // DEBUG LOG
                UE_LOG(LogTemp, Warning, TEXT("ChunkManager: chunk %d (%d,%d) -> LOD%d"), Id.FaceIndex, x, y, TargetLOD);
            }
        }
    }
}


int32 FChunkManager::CalculateTargetLOD(float DistanceSq) const
{
    // Iterate through LOD settings (sorted by distance in Config)
    for (int32 i = 0; i < Config.LODSettings.Num(); ++i)
    {
        float Threshold = Config.LODSettings[i].Distance;

        // Use squared distance for performance (avoid Sqrt)
        if (DistanceSq < (Threshold * Threshold))
        {
            return i;  // Found the correct LOD level
        }
    }

    // If further than the last LOD setting, it's too far.
    return -1;
}