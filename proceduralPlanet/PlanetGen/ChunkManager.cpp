#include "ChunkManager.h"
#include "Async/Async.h"
#include "SimpleNoise.h"
#include "MeshGenerator.h"
#include "DrawDebugHelpers.h"


FChunkManager::FChunkManager(const FPlanetConfig &planetConfig, const DensityGenerator *densityGen) :
    Config(planetConfig),
    Generator(densityGen)
{
    MaxConcurrentGenerations = Config.MaxConcurrentGenerations;
    GenerationRate = Config.ChunkGenerationRate;
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
        // A chunk is "Visible" if the Update loop gave it a valid LOD OR if its state implies it's currently doing something.
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
                FVector Normal = FMathUtils::getFaceNormal(Face);
                FVector Right = FMathUtils::getFaceRight(Face);
                FVector Up = FMathUtils::getFaceUp(Face);

                // Calculate Center Position (LOD 0)
                FVector2D CenterUV = (UVMin + UVMax) * 0.5f;
                // Remap 0..1 to -1..1
                FVector CubePos = Normal + (Right * (CenterUV.X * 2.0f - 1.0f)) + (Up * (CenterUV.Y * 2.0f - 1.0f));
                FVector SpherePos = FMathUtils::projectCubeToSphere(CubePos) * Config.PlanetRadius;

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
    FVector ObserverLocal = Context.ObserverLocation;
    FVector ObserverForwardLocal = Context.ObserverForward;

    if (Renderer && Renderer->GetOwner())
    {
        FTransform OwnerTM = Renderer->GetOwner()->GetActorTransform();
        ObserverLocal = OwnerTM.InverseTransformPosition(Context.ObserverLocation);
        ObserverForwardLocal = OwnerTM.InverseTransformVector(Context.ObserverForward);
    }

    float DistToCenter = ObserverLocal.Size();
    float DistToSurface = DistToCenter - Config.PlanetRadius;

    // Use RenderDistance as the threshold for switching to Far Model
    bool bUseFarModel = DistToSurface > Config.FarDistanceThreshold;

    // We use a Set to keep track of what SHOULD exist this frame.
    TSet<FChunkId> RequiredChunks;

    // 2. Determine Required Chunks
    if (!bUseFarModel)
    {
        // UNDERGROUND CHECK
        // If we are significantly below the surface, standard LOD distance logic fails (distance to surface chunks becomes large).
        // We switch to "Cave Mode": Only load the chunk we are inside.
        const float UndergroundThreshold = -100.0f;  // 1 meter below "sea level"

        if (DistToSurface < UndergroundThreshold)
        {
            // Find the chunk we are currently inside
            FChunkId CurrentChunk = GetChunkIdAt(ObserverLocal);
            // Force it to be required at LOD 0
            RequiredChunks.Add(CurrentChunk);

            // Optional: Add neighbors if we want to see slightly further underground
        }
        else
        {
            // SURFACE LOGIC
            // Pass the LOCAL observer position to UpdateFace
            FPlanetViewContext LocalContext = Context;
            LocalContext.ObserverLocation = ObserverLocal;
            LocalContext.ObserverForward = ObserverForwardLocal;

            for (uint8 Face = 0; Face < 6; ++Face)
            {
                UpdateFace(Face, LocalContext, RequiredChunks);
            }
        }
    }
    // If bUseFarModel is true, RequiredChunks remains empty, causing the loop below to unload everything.

    // 3. Process State Changes
    TArray<FChunkId> ChunksToRemove;

    // A. Remove/Unload chunks that are no longer required
    for (auto &Pair : Chunks)
    {
        FChunkId Id = Pair.Key;
        FChunk *Chunk = Pair.Value.Get();

        if (!RequiredChunks.Contains(Id))
        {
            // Immediately mark as Unloaded if we are hiding it.
            if (Chunk->State == EChunkState::Visible || Chunk->State == EChunkState::Ready)
            {
                Renderer->HideChunk(Chunk);  // Return component to pool
                Chunk->State = EChunkState::Unloaded;
            }

            // Garbage Collection: If it's unloaded and not required, we can remove it to save memory.
            // Only remove if not currently generating (safety)
            if (Chunk->State == EChunkState::Unloaded && !CurrentlyGenerating.Contains(Id))
            {
                ChunksToRemove.Add(Id);
            }
        }
    }

    // Execute removal
    for (const FChunkId &Id : ChunksToRemove)
    {
        Chunks.Remove(Id);
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
    }

    ProcessQueues();
}


void FChunkManager::UpdateFace(uint8 Face, const FPlanetViewContext &Context, TSet<FChunkId> &OutRequired)
{
    int32 GridSize = Config.ChunksPerFace;

    for (int32 x = 0; x < GridSize; ++x)
    {
        for (int32 y = 0; y < GridSize; ++y)
        {
            FIntVector Coords(x, y, 0);

            // WorldPos here is actually Planet-Relative Position (0,0,0 is center)
            FVector PlanetLocalPos = GetChunkCenter(Face, x, y);
            FVector ToChunk = PlanetLocalPos - Context.ObserverLocation;
            float DistSq = ToChunk.SizeSquared();

            // 1. Horizon Culling (Backface)
            // Simple approximation: Dot product of Chunk Normal (Pos normalized) and Vector to Observer
            // If the chunk is facing away from the observer, cull it.
            // We use a small tolerance because chunks have height.
            FVector ChunkNormal = PlanetLocalPos.GetSafeNormal();
            FVector ToObserverDir = -ToChunk.GetSafeNormal();

            // If Dot < -0.2, it's well over the horizon.
            if ((ChunkNormal | ToObserverDir) < -0.2f)
                continue;

            // 2. Frustum Culling
            // Is the chunk roughly in front of the camera?
            // Allow 90+ degrees FOV (Dot > 0.0) or even wider to prevent popping at edges.
            if ((ToChunk.GetSafeNormal() | Context.ObserverForward) < -0.2f)
                continue;

            // 2. Find current LOD for this grid cell, if any chunk exists
            int32 CurrentLOD = -1;
            for (int32 lod = 0; lod < Config.LODLayers.Num(); ++lod)
            {
                FChunkId testId(Face, lod, Coords);
                // Only consider chunks that are actually valid/visible as the "Current" LOD.
                // This prevents the system from thinking an "Unloading" chunk is still active, which was blocking the target LOD calculation.
                if (TUniquePtr<FChunk> *FoundChunk = Chunks.Find(testId))
                {
                    if (FoundChunk->Get()->State == EChunkState::Visible || FoundChunk->Get()->State == EChunkState::Ready)
                    {
                        CurrentLOD = lod;
                        break;
                    }
                }
            }

            // 3. Calculate Target LOD using hysteresis
            int32 TargetLOD = CalculateTargetLOD(DistSq, CurrentLOD);

            // 4. If a chunk is required (TargetLOD is valid), add its ID to the set.
            if (TargetLOD != -1)
            {
                FChunkId RequiredId(Face, TargetLOD, Coords);
                OutRequired.Add(RequiredId);

                // Ensure the chunk data container exists and has its transform info updated. This is important for new chunks.
                FChunk *Chunk = GetChunk(RequiredId);
                if (Chunk)
                {
                    Chunk->Transform.Location = PlanetLocalPos;
                    Chunk->Transform.FaceNormal = FMathUtils::getFaceNormal(Face);
                    Chunk->Transform.Scale = 1.0f;
                }
            }
        }
    }
}


void FChunkManager::DrawDebugGrid(const UWorld *World) const
{
    if (!World)
        return;

    // Get Planet Transform to draw grid in correct World Space
    FTransform PlanetTransform = FTransform::Identity;
    if (Renderer && Renderer->GetOwner())
    {
        PlanetTransform = Renderer->GetOwner()->GetActorTransform();
    }

    int32 GridSize = Config.ChunksPerFace;
    float Step = 1.0f / GridSize;
    // Draw slightly offset to avoid z-fighting with the mesh
    float Radius = Config.PlanetRadius * 1.002f;

    for (uint8 Face = 0; Face < 6; ++Face)
    {
        FVector Normal = FMathUtils::getFaceNormal(Face);
        FVector Right = FMathUtils::getFaceRight(Face);
        FVector Up = FMathUtils::getFaceUp(Face);

        for (int32 x = 0; x < GridSize; ++x)
        {
            for (int32 y = 0; y < GridSize; ++y)
            {
                float UMin = x * Step;
                float UMax = (x + 1) * Step;
                float VMin = y * Step;
                float VMax = (y + 1) * Step;

                auto ToWorld = [&](float U, float V)
                {
                    FVector CubePos = Normal + (Right * (U * 2.0f - 1.0f)) + (Up * (V * 2.0f - 1.0f));
                    FVector SpherePos = FMathUtils::projectCubeToSphere(CubePos) * Radius;
                    // Apply Planet Transform
                    return PlanetTransform.TransformPosition(SpherePos);
                };

                FVector P0 = ToWorld(UMin, VMin);
                FVector P1 = ToWorld(UMax, VMin);
                FVector P2 = ToWorld(UMax, VMax);
                FVector P3 = ToWorld(UMin, VMax);

                DrawDebugLine(World, P0, P1, FColor::Cyan, false, -1.0f, 0, 30.0f);
                DrawDebugLine(World, P1, P2, FColor::Cyan, false, -1.0f, 0, 30.0f);
                DrawDebugLine(World, P2, P3, FColor::Cyan, false, -1.0f, 0, 30.0f);
                DrawDebugLine(World, P3, P0, FColor::Cyan, false, -1.0f, 0, 30.0f);
            }
        }
    }
}


void FChunkManager::DrawDebugChunkBounds(const UWorld *World) const
{
    if (!World)
        return;

    for (const auto &Pair : Chunks)
    {
        const FChunk *Chunk = Pair.Value.Get();
        // Only draw bounds for chunks that have a visible mesh component
        if (Chunk && Chunk->State == EChunkState::Visible && Chunk->RenderProxy.IsValid())
        {
            if (UProceduralMeshComponent *Comp = Chunk->RenderProxy.Get())
            {
                FBox Box = Comp->Bounds.GetBox();
                DrawDebugBox(World, Box.GetCenter(), Box.GetExtent(), FColor::Orange, false, -1.0f, 0, 20.0f);
            }
        }
    }
}


FVector FChunkManager::GetChunkCenter(uint8 Face, int32 X, int32 Y) const
{
    float Step = 1.0f / Config.ChunksPerFace;
    FVector2D UVMin(X * Step, Y * Step);
    FVector2D UVMax((X + 1) * Step, (Y + 1) * Step);
    FVector2D CenterUV = (UVMin + UVMax) * 0.5f;

    FVector Normal = FMathUtils::getFaceNormal(Face);
    FVector Right = FMathUtils::getFaceRight(Face);
    FVector Up = FMathUtils::getFaceUp(Face);

    FVector CubePos = Normal + (Right * (CenterUV.X * 2.0f - 1.0f)) + (Up * (CenterUV.Y * 2.0f - 1.0f));

    return FMathUtils::projectCubeToSphere(CubePos) * Config.PlanetRadius;
}


FChunkId FChunkManager::GetChunkIdAt(const FVector &LocalPosition) const
{
    // Inverse of GetChunkCenter logic
    FVector AbsPos = LocalPosition.GetAbs();
    float MaxVal = AbsPos.GetMax();
    uint8 Face = 0;

    // Determine Face
    if (AbsPos.X == MaxVal)
        Face = (LocalPosition.X > 0) ? 0 : 1;
    else if (AbsPos.Y == MaxVal)
        Face = (LocalPosition.Y > 0) ? 2 : 3;
    else
        Face = (LocalPosition.Z > 0) ? 4 : 5;

    // Project to Cube (-1..1)
    FVector CubePos = LocalPosition / MaxVal;

    // Get Basis Vectors
    FVector Right = FMathUtils::getFaceRight(Face);
    FVector Up = FMathUtils::getFaceUp(Face);

    // Project to UV (0..1)
    // CubePos = Normal + Right*(2u-1) + Up*(2v-1)
    // Dot(CubePos, Right) = 2u - 1  =>  u = (Dot + 1) / 2
    float U = (FVector::DotProduct(CubePos, Right) + 1.0f) * 0.5f;
    float V = (FVector::DotProduct(CubePos, Up) + 1.0f) * 0.5f;

    // Map to Grid Coords
    int32 X = FMath::FloorToInt(U * Config.ChunksPerFace);
    int32 Y = FMath::FloorToInt(V * Config.ChunksPerFace);

    // Clamp
    X = FMath::Clamp(X, 0, Config.ChunksPerFace - 1);
    Y = FMath::Clamp(Y, 0, Config.ChunksPerFace - 1);

    // Return ID at LOD 0
    return FChunkId(Face, 0, FIntVector(X, Y, 0));
}


int32 FChunkManager::CalculateTargetLOD(float DistanceSq, int32 CurrentLOD) const
{
    // If a chunk is already active, use hysteresis to prevent rapid switching
    if (CurrentLOD != -1)
    {
        // Check for UPGRADE to a higher-detail LOD (smaller index)
        if (CurrentLOD > 0)
        {
            const float UpgradeDistSq = FMath::Square(Config.LODLayers[CurrentLOD - 1].Distance);
            if (DistanceSq < UpgradeDistSq)
            {
                return CurrentLOD - 1;
            }
        }

        // Check for DOWNGRADE to a lower-detail LOD (larger index)
        if (CurrentLOD < Config.LODLayers.Num() - 1)
        {
            const float DowngradeDistSq = FMath::Square(Config.LODLayers[CurrentLOD].Distance * Config.LODHysteresis);
            if (DistanceSq > DowngradeDistSq)
            {
                return CurrentLOD + 1;
            }
        }

        // Check for DESPAWN (distance is beyond the largest LOD's range)
        const float LastLODDist = Config.LODLayers.Last().Distance;
        const float DespawnDistSq = FMath::Square(LastLODDist * Config.LODDespawnHysteresis);
        if (DistanceSq > DespawnDistSq)
        {
            return -1;  // Despawn
        }

        return CurrentLOD;  // No change
    }

    // No active chunk, determine if we need to spawn one
    for (int32 LodIndex = 0; LodIndex < Config.LODLayers.Num(); ++LodIndex)
    {
        float Threshold = Config.LODLayers[LodIndex].Distance;

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
    ProcessGenerationQueue();

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

    // Remap UVs (0..1) to Cube Coordinates (-1..1)
    FVector2D CubeMin = UVMin * 2.0f - 1.0f;
    FVector2D CubeMax = UVMax * 2.0f - 1.0f;

    uint8 FaceIdx = Id.FaceIndex;
    FVector FaceNormal = FMathUtils::getFaceNormal(FaceIdx);
    FVector FaceRight = FMathUtils::getFaceRight(FaceIdx);
    FVector FaceUp = FMathUtils::getFaceUp(FaceIdx);

    // Calculate Transform (Relative to Planet Center)
    FVector2D CenterUV = (UVMin + UVMax) * 0.5f;
    FVector CubePos = FaceNormal + (FaceRight * (CenterUV.X * 2.0f - 1.0f)) + (FaceUp * (CenterUV.Y * 2.0f - 1.0f));
    FVector SphereDir = FMathUtils::projectCubeToSphere(CubePos);
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
    int32 Resolution = Config.LODLayers[Id.LOD].VoxelResolution;
    int32 LODLevel = Id.LOD;

    // Copy Generator Config (Thread Safety)
    DensityGenerator ThreadGen = *Generator;

    // 3. Launch Async Task
    Async(EAsyncExecution::ThreadPool,
          [this, Id, GenId, Resolution, FaceNormal, FaceRight, FaceUp, CubeMin, CubeMax, ChunkTransform, LODLevel, ThreadGen]()
          {
              // A. Generate Density
              GenData GeneratedData = ThreadGen.GenerateDensityField(Resolution, FaceNormal, FaceRight, FaceUp, CubeMin, CubeMax);

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

    // Apply Debug Colors based on LOD
    if (MeshData && MeshData->Vertices.Num() > 0)
    {
        FColor DebugColor = (Id.LOD < LODColorsDebug.Num()) ? LODColorsDebug[Id.LOD] : FColor::White;
        // Ensure Colors array is sized correctly
        MeshData->Colors.Init(DebugColor, MeshData->Vertices.Num());
    }

    // Store Data
    Chunk->MeshData = MoveTemp(MeshData);
    Chunk->State = EChunkState::Ready;

    // Note: We do not spawn the component here.
    // The Update loop or a separate "ProcessMeshQueue" will handle visibility/spawning.
}