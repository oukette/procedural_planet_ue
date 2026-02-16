#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SeedUtils.h"
#include "MathUtils.h"
#include "ChunkManager.h"
#include "DataTypes.h"
#include "SimpleNoise.h"
#include "Planet.generated.h"


UCLASS()
class PROCEDURALPLANET_API APlanet : public AActor
{
        GENERATED_BODY()

    private:
        UPROPERTY(VisibleAnywhere, Category = "Planet")
        USceneComponent *Root;

        TUniquePtr<FChunkManager> ChunkManager;
        TUniquePtr<SimpleNoise> NoiseProvider;
        TUniquePtr<DensityGenerator> Generator;

        // Stores the finalized configuration after initPlanet() runs.
        FPlanetConfig RuntimeConfig;

    protected:
        virtual void OnConstruction(const FTransform &Transform) override;

        // Initializes the generation process by populating the spawn queue.
        void initPlanet();

        // Helper to get the camera position in both Editor and Runtime
        FVector GetObserverPosition() const;

        // Creates the planet far model for optimized rendering in far distance.
        void CreateFarModel();

        // Calculates the optimal grid size (ChunksPerFace) and VoxelSize based on Planet Radius.
        void CalculateAutoGrid(int32& OutChunksPerFace, float& OutVoxelSize, int32& OutResolution) const;

        // Calculates LOD distances based on the physical size of a chunk.
        void CalculateAutoLODs(TArray<FLODInfo>& OutLODs, float ChunkArcLength) const;

        // Tick Helpers
        FPlanetViewContext BuildViewContext() const;
        void UpdateChunkManager(const FPlanetViewContext& Context);
        void UpdateFarModelVisibility(const FPlanetViewContext& Context);
        void DrawDebugInfo(const FPlanetViewContext& Context) const;

    public:
        APlanet();
        virtual void BeginPlay() override;
        virtual void Destroyed() override;
        virtual void Tick(float DeltaTime) override;
        virtual bool ShouldTickIfViewportsOnly() const override;

        // --- Generation Control ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Generation")
        bool bGenerateOnBeginPlay = true;

        // --- General Settings ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        FPlanetGenSettings GenSettings;

        // --- Grid & Voxel Settings ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        FPlanetGridSettings GridSettings;

        // --- Noise ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        FNoiseSettings NoiseSettings;

        // --- LOD Settings ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        FPlanetLODSettings LODSettings;

        // --- Performance ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        FPlanetPerformanceSettings PerformanceSettings;

        // Internal State
        bool bIsFarModelAutoCreated = false;
};
