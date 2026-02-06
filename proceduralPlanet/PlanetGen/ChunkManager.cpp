#include "ChunkManager.h"
#include "Planet.h"
#include "DensityGenerator.h"
#include "MarchingCubes.h"
#include "SimpleNoise.h"
#include "DrawDebugHelpers.h"
#include "ProceduralMeshComponent.h"


FChunkManager::FChunkManager() :
    OwnerPlanet(nullptr),
    TotalChunksCreated(0),
    TotalChunksDestroyed(0)
{
}

FChunkManager::~FChunkManager() { Shutdown(); }

void FChunkManager::Initialize(APlanet *InOwnerPlanet)
{
    check(InOwnerPlanet);
    OwnerPlanet = InOwnerPlanet;

    UE_LOG(LogTemp, Log, TEXT("ChunkManager initialized for planet: %s"), *OwnerPlanet->GetName());
}

void FChunkManager::Shutdown()
{
    // Destroy all chunks
    for (auto &Pair : Chunks)
    {
        FChunk *Chunk = Pair.Value.Get();
        if (Chunk && Chunk->GetRenderProxy())
        {
            DetachRenderProxy(*Chunk);
        }
    }
    Chunks.Empty();

    // Clear proxy pools
    AvailableProxies.Empty();
    UsedProxies.Empty();

    UE_LOG(LogTemp, Log, TEXT("ChunkManager shutdown. Total created: %d, destroyed: %d"), TotalChunksCreated, TotalChunksDestroyed);
}

void FChunkManager::Update(const FPlanetViewContext &ViewContext)
{
    check(IsInGameThread());

    // This will be implemented in Step 7
    UpdateChunkSet(ViewContext);

    // Process state transitions (Phase 5 will make this async)
    for (auto &Pair : Chunks)
    {
        FChunk *Chunk = Pair.Value.Get();

        switch (Chunk->State)
        {
            case EChunkState::Requested:
                GenerateChunkMesh(*Chunk);
                break;

            case EChunkState::Ready:
                AttachRenderProxy(*Chunk);
                break;

            default:
                break;
        }
    }
}

// === QUERY ===

FChunk *FChunkManager::FindChunk(const FChunkId &Id)
{
    TUniquePtr<FChunk> *Found = Chunks.Find(Id);
    return Found ? Found->Get() : nullptr;
}

const FChunk *FChunkManager::FindChunk(const FChunkId &Id) const
{
    const TUniquePtr<FChunk> *Found = Chunks.Find(Id);
    return Found ? Found->Get() : nullptr;
}

EChunkState FChunkManager::GetChunkState(const FChunkId &Id) const
{
    const FChunk *Chunk = FindChunk(Id);
    return Chunk ? Chunk->State : EChunkState::Unloaded;
}

// === CHUNK OPERATIONS (Stubs - we'll implement these in Steps 3-6) ===

void FChunkManager::CreateChunk(const FChunkId &Id)
{
    // Step 3
}

void FChunkManager::DestroyChunk(const FChunkId &Id)
{
    // Step 6
}

void FChunkManager::GenerateChunkMesh(FChunk &Chunk)
{
    // Step 4
}

void FChunkManager::AttachRenderProxy(FChunk &Chunk)
{
    // Step 5
}

void FChunkManager::DetachRenderProxy(FChunk &Chunk)
{
    // Step 5
}

// === STREAMING (Stub) ===

void FChunkManager::UpdateChunkSet(const FPlanetViewContext &ViewContext)
{
    // Step 7
}

TSet<FChunkId> FChunkManager::DetermineDesiredChunks(const FPlanetViewContext &ViewContext) const
{
    // Step 7
    return TSet<FChunkId>();
}

bool FChunkManager::ShouldChunkBeVisible(const FChunkId &Id, const FPlanetViewContext &ViewContext) const
{
    // Step 7
    return false;
}

// === HELPERS (Stub) ===

FChunkTransform FChunkManager::ComputeChunkTransform(const FChunkId &Id) const
{
    // Step 3
    return FChunkTransform();
}

UProceduralMeshComponent *FChunkManager::AcquireRenderProxy()
{
    // Step 5
    return nullptr;
}

void FChunkManager::ReleaseRenderProxy(UProceduralMeshComponent *Proxy)
{
    // Step 5
}

// === DEBUG ===

void FChunkManager::DrawDebugVisualization(UWorld *World) const
{
    // Step 8
}

void FChunkManager::LogStatistics() const
{
    UE_LOG(LogTemp, Log, TEXT("=== ChunkManager Statistics ==="));
    UE_LOG(LogTemp, Log, TEXT("  Active Chunks: %d"), Chunks.Num());
    UE_LOG(LogTemp, Log, TEXT("  Total Created: %d"), TotalChunksCreated);
    UE_LOG(LogTemp, Log, TEXT("  Total Destroyed: %d"), TotalChunksDestroyed);
    UE_LOG(LogTemp, Log, TEXT("  Available Proxies: %d"), AvailableProxies.Num());
    UE_LOG(LogTemp, Log, TEXT("  Used Proxies: %d"), UsedProxies.Num());
}