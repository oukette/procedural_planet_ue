#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CubeSpherePlanet.generated.h"

class AVoxelChunk;  // Forward declaration to avoid circular includes

// Lightweight struct to manage chunk state without spawning an actor
struct FChunkInfo
{
        FTransform Transform;
        FVector WorldLocation;  // Cached for fast distance checks
        AVoxelChunk *ActiveChunk = nullptr;
        bool bPendingSpawn = false;
        int32 LODLevel = 0;  // Reserved for future resolution scaling
};

UCLASS()
class PROCEDURALPLANET_API ACubeSpherePlanet : public AActor
{
        GENERATED_BODY()

    public:
        ACubeSpherePlanet();
        virtual void BeginPlay() override;
        virtual void Destroyed() override;
        virtual void Tick(float DeltaTime) override;
        virtual bool ShouldTickIfViewportsOnly() const override;

        // Called by a VoxelChunk when its async mesh generation is complete.
        void OnChunkGenerationFinished(AVoxelChunk *Chunk);

        // Calculate automatic chunks per face based on planet parameters
        UFUNCTION(BlueprintCallable, Category = "Planet|Chunking")
        int32 CalculateAutoChunksPerFace() const;

    protected:
        virtual void OnConstruction(const FTransform &Transform) override;

        // Initializes the generation process by populating the spawn queue.
        void PrepareGeneration();

        // Tick sub-functions
        void UpdateLODAndStreaming();
        void ProcessSpawnQueue();
        void ProcessMeshUpdateQueue();

        // Destroys all existing chunks.
        UFUNCTION(CallInEditor, Category = "Planet|Actions", meta = (DisplayName = "Clear All Chunks"))
        void ClearAllChunks();

        // Starts the staggered generation process.
        UFUNCTION(CallInEditor, Category = "Planet|Actions", meta = (DisplayName = "Generate Planet"))
        void GeneratePlanet();

        // Helper to get the camera position in both Editor and Runtime
        FVector GetObserverPosition() const;

    public:
        // --- Generation Control ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Generation")
        bool bGenerateOnBeginPlay = true;

        // --- Planet Shape & Detail ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        int32 Seed;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        int32 ChunksPerFace;  // Grid resolution per cube face (e.g., 4 = 4x4 = 16 chunks)

        // Controls automatic chunk sizing
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Chunking", meta = (DisplayName = "Use Auto Chunk Sizing"))
        bool bAutoChunkSizing = true;

        // When auto-sizing is enabled, this determines chunk density
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Chunking",
                  meta = (EditCondition = "bAutoChunkSizing", ClampMin = "0.1", ClampMax = "10.0", DisplayName = "Chunk Density Factor"))
        float ChunkDensityFactor = 1.0f;

        // Minimum chunks per face (when auto-sizing)
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Chunking",
                  meta = (EditCondition = "bAutoChunkSizing", ClampMin = "1", ClampMax = "64", DisplayName = "Min Chunks Per Face"))
        int32 MinChunksPerFace = 1;

        // Maximum chunks per face (when auto-sizing)
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Chunking",
                  meta = (EditCondition = "bAutoChunkSizing", ClampMin = "1", ClampMax = "256", DisplayName = "Max Chunks Per Face"))
        int32 MaxChunksPerFace = 64;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        float PlanetRadius;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        float NoiseAmplitude;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        float NoiseFrequency;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        int32 VoxelResolution;  // Per-chunk voxel count

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        float VoxelSize;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization")
        bool bEnableCollision;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization")
        bool bCastShadows;

        // --- LOD & Streaming ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization|LOD", meta = (ClampMin = "1000.0"))
        float RenderDistance = 20000.0f;  // Distance at which chunks are spawned

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization|LOD", meta = (ClampMin = "100.0"))
        float CollisionDistance = 6000.0f;  // Distance at which collision is enabled

        // --- Staggered Generation ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization",
                  meta = (DisplayName = "Mesh Updates Per Frame", ClampMin = "1", ClampMax = "100"))
        int32 ChunksToProcessPerFrame;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization",
                  meta = (DisplayName = "Chunks To Spawn Per Frame", ClampMin = "1", ClampMax = "100"))
        int32 ChunksToSpawnPerFrame = 8;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Optimization",
                  meta = (DisplayName = "Max Concurrent Generations", ClampMin = "1", ClampMax = "512"))
        int32 MaxConcurrentChunkGenerations = 32;

    private:
        UPROPERTY(VisibleAnywhere, Category = "Planet")
        USceneComponent *Root;

        // Master list of all potential chunks (lightweight)
        TArray<FChunkInfo> ChunkInfos;

        // Queue of INDICES into ChunkInfos waiting to be spawned
        TArray<int32> ChunkSpawnQueue;

        // Queue for chunks with ready mesh data, waiting for GPU upload.
        TArray<AVoxelChunk *> MeshUpdateQueue;

        // Counter for chunks currently running async generation.
        int32 ActiveGenerationTasks;
};
