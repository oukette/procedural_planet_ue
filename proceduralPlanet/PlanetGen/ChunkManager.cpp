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


int32 FChunkManager::GetChunkCount() const { return 6 * Config.ChunksPerFace * Config.ChunksPerFace; }


int32 FChunkManager::GetLoadedChunkCount() const
{
    int32 Count = 0;
    for (const auto &Pair : Chunks)
    {
        // A chunk is considered "Loaded" if its mesh data is generated and ready (Ready)
        // or if it's already being rendered (Visible).
        const EChunkState State = Pair.Value->State;
        if (State == EChunkState::Visible || State == EChunkState::Ready)
        {
            Count++;
        }
    }
    return Count;
}


void FChunkManager::Initialize(AActor *Owner, UMaterialInterface *Material)
{
    Renderer = MakeUnique<ChunkRenderer>(Owner, Material);

    // Chunks are now created on-demand in the Update loop via GetChunk().
    // Pre-allocating them here is wasteful as most would be immediately garbage collected on the first frame.

    // Initialize Quadtree Roots (LOD 0)
    RootNodes.Empty();
    for (uint8 i = 0; i < 6; ++i)
    {
        FChunkId RootId(i, FIntVector(0, 0, 0), 0);
        RootNodes.Add(MakeUnique<FQuadtreeNode>(RootId, nullptr));
    }

    // DEBUG LOG
    UE_LOG(LogTemp, Log, TEXT("FChunkManager initialized. Total grid capacity is %d chunks."), GetChunkCount());
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
    FVector ObserverLocal = Context.ObserverLocation;
    FVector ObserverForwardLocal = Context.ObserverForward;

    if (Renderer && Renderer->GetOwner())
    {
        FTransform OwnerTM = Renderer->GetOwner()->GetActorTransform();
        ObserverLocal = OwnerTM.InverseTransformPosition(Context.ObserverLocation);
        ObserverForwardLocal = OwnerTM.InverseTransformVector(Context.ObserverForward);
    }

    // We use a Set to keep track of what SHOULD exist this frame.
    TSet<FChunkId> RequiredChunks;

    // Pass the LOCAL observer position to UpdateFace
    FPlanetViewContext LocalContext = Context;
    LocalContext.ObserverLocation = ObserverLocal;
    LocalContext.ObserverForward = ObserverForwardLocal;

    for (uint8 Face = 0; Face < 6; ++Face)
    {
        UpdateFace(Face, LocalContext, RequiredChunks);
    }

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
    // Start recursion from root
    UpdateNode(RootNodes[Face].Get(), Context, OutRequired);
}


void FChunkManager::UpdateNode(FQuadtreeNode *Node, const FPlanetViewContext &Context, TSet<FChunkId> &OutRequired)
{
    FVector Center = GetChunkCenter(Node->Id);
    FVector ToChunk = Center - Context.ObserverLocation;

    // 1. Horizon Culling (Backface)
    FVector ChunkNormal = Center.GetSafeNormal();
    FVector ToObserverDir = -ToChunk.GetSafeNormal();

    if ((ChunkNormal | ToObserverDir) < FPlanetStatics::HorizonCullingDot)
        return;

    // 2. Frustum Culling
    if (!Context.ObserverForward.IsZero() && (ToChunk.GetSafeNormal() | Context.ObserverForward) < FPlanetStatics::FrustumCullingDot)
        return;

    // 3. Split Check
    if (ShouldSplit(Node, Context.ObserverLocation))
    {
        if (Node->IsLeaf())
        {
            // Create Children
            int32 NextLOD = Node->Id.LODLevel + 1;
            int32 X = Node->Id.Coords.X;
            int32 Y = Node->Id.Coords.Y;
            uint8 Face = Node->Id.FaceIndex;

            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2, Y * 2, 0), NextLOD), Node));
            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2 + 1, Y * 2, 0), NextLOD), Node));
            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2, Y * 2 + 1, 0), NextLOD), Node));
            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2 + 1, Y * 2 + 1, 0), NextLOD), Node));
        }

        for (auto &Child : Node->Children)
        {
            UpdateNode(Child.Get(), Context, OutRequired);
        }
    }
    else
    {
        // Merge (if it has children, remove them)
        if (!Node->IsLeaf())
        {
            Node->Children.Empty();
        }

        // Add this node as a required chunk
        OutRequired.Add(Node->Id);

        // Update Transform info for the chunk (needed for rendering)
        FChunk *Chunk = GetChunk(Node->Id);
        if (Chunk)
        {
            Chunk->Transform.Location = Center;
            Chunk->Transform.FaceNormal = FMathUtils::getFaceNormal(Node->Id.FaceIndex);
            Chunk->Transform.Scale = 1.0f;
        }
    }
}


bool FChunkManager::ShouldSplit(const FQuadtreeNode *Node, const FVector &ObserverLocal) const
{
    if (Node->Id.LODLevel >= Config.MaxLOD)
        return false;

    FVector Center = GetChunkCenter(Node->Id);
    float Dist = FVector::Dist(Center, ObserverLocal);

    // Calculate physical size of this node (arc length)
    // Base Arc Length (LOD 0) is (PI * R) / 2
    float NodeSize = (Config.PlanetRadius * PI * 0.5f) / (float)(1 << Node->Id.LODLevel);

    return Dist < (NodeSize * Config.LODSplitDistanceMultiplier);
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
    float Radius = Config.PlanetRadius * FPlanetStatics::GridDebugRadiusScale;

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

                DrawDebugLine(World, P0, P1, FColor::Cyan, false, -1.0f, 0, FPlanetStatics::DebugLineLifetime);
                DrawDebugLine(World, P1, P2, FColor::Cyan, false, -1.0f, 0, FPlanetStatics::DebugLineLifetime);
                DrawDebugLine(World, P2, P3, FColor::Cyan, false, -1.0f, 0, FPlanetStatics::DebugLineLifetime);
                DrawDebugLine(World, P3, P0, FColor::Cyan, false, -1.0f, 0, FPlanetStatics::DebugLineLifetime);
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
                DrawDebugBox(World, Box.GetCenter(), Box.GetExtent(), FColor::Orange, false, -1.0f, 0, FPlanetStatics::DebugBoxLifetime);
            }
        }
    }
}


void FChunkManager::GetUVBounds(const FChunkId &Id, FVector2D &OutMin, FVector2D &OutMax) const
{
    // Step size depends on LOD Level.
    // LOD 0 = 1.0, LOD 1 = 0.5, LOD 2 = 0.25, etc.
    float Step = 1.0f / (float)(1 << Id.LODLevel);

    OutMin = FVector2D(Id.Coords.X * Step, Id.Coords.Y * Step);
    OutMax = FVector2D((Id.Coords.X + 1) * Step, (Id.Coords.Y + 1) * Step);
}


FVector FChunkManager::GetChunkCenter(const FChunkId &Id) const
{
    FVector2D UVMin, UVMax;
    GetUVBounds(Id, UVMin, UVMax);
    FVector2D CenterUV = (UVMin + UVMax) * 0.5f;

    FVector Normal = FMathUtils::getFaceNormal(Id.FaceIndex);
    FVector Right = FMathUtils::getFaceRight(Id.FaceIndex);
    FVector Up = FMathUtils::getFaceUp(Id.FaceIndex);

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

    // Map to Grid Coords (Assuming LOD 0 for now)
    int32 X = FMath::FloorToInt(U);  // At LOD 0, range is 0..1, so index is 0
    int32 Y = FMath::FloorToInt(V);

    // Clamp
    X = 0;
    Y = 0;

    return FChunkId(Face, FIntVector(X, Y, 0), 0);
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
    FVector2D UVMin, UVMax;
    GetUVBounds(Id, UVMin, UVMax);

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
    int32 Resolution = Config.GridResolution;
    int32 LODLevel = Id.LODLevel;

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


    // Store Data
    Chunk->MeshData = MoveTemp(MeshData);
    Chunk->State = EChunkState::Ready;

    // Note: We do not spawn the component here.
    // The Update loop or a separate "ProcessMeshQueue" will handle visibility/spawning.
}