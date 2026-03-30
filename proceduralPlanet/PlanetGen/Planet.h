#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "../Utils/SeedUtils.h"
#include "../Utils/MathUtils.h"
#include "ChunkManager.h"
#include "PlanetConfig.h"
#include "PlanetConstants.h"
#include "SimpleNoise.h"
#include "Planet.generated.h"


UCLASS()
class PROCEDURALPLANET_API APlanet : public AActor
{
        GENERATED_BODY()

    private:
        UPROPERTY(VisibleAnywhere, Category = "Planet")
        USceneComponent *Root;

        TUniquePtr<ChunkManager> m_chunkManager;
        TSharedPtr<INoise, ESPMode::ThreadSafe> m_noiseProvider;
        TUniquePtr<DensityGenerator> m_densityGen;

        FPlanetConfig m_planetConfig;   // Store the finalized configuration after initPlanet() runs.

    public:
        // Generation Control
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Generation")
        bool bGenerateOnBeginPlay = true;

        // General Settings
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        FPlanetGenSettings GenSettings;

        // Grid & Voxel Settings
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        FPlanetGridSettings GridSettings;

        // Noise
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        FNoiseSettings NoiseSettings;

        // Performance
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        FPlanetPerformanceSettings PerformanceSettings;

        // Debug
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
        FPlanetDebugSettings DebugSettings;

        // Internal State
        bool bIsFarModelAutoCreated = false;

    protected:
        virtual void OnConstruction(const FTransform &Transform) override;

        // Initialize the generation process by populating the spawn queue.
        void initPlanet();

        // Create the planet far model for optimized rendering in far distance.
        void CreateFarModel();

        // Compute the optimal VoxelSize based on Planet Radius.
        void ComputeAutoVoxelSize(float &OutVoxelSize) const;

        // Build the configurations.
        FPlanetConfig BuildPlanetConfig(float VoxelSize) const;
        DensityConfig BuildDensityConfig(float VoxelSize) const;

        // Helper to update the chunk manager.
        void UpdateChunkManager(const PlanetViewContext &Context);

        // Helper to update the far model visibility.
        void UpdateFarModelVisibility(const PlanetViewContext &Context);

        // Debug methods
        void DrawDebugInfo(const PlanetViewContext &Context) const;
        void DrawPredictiveDebug(const PlanetViewContext &LocalContext) const;

    public:
        APlanet();
        virtual void BeginPlay() override;
        virtual void Destroyed() override;
        virtual void Tick(float DeltaTime) override;
        virtual bool ShouldTickIfViewportsOnly() const override;

        // Editor Tools
        UFUNCTION(CallInEditor, Category = "Planet|Generation") void GeneratePlanet();
        UFUNCTION(CallInEditor, Category = "Planet|Generation") void ClearPlanet();

        // Returns the normalized direction of gravity (pointing towards planet center) at a specific location.
        UFUNCTION(BlueprintCallable, Category = "Planet|Physics")
        FVector GetGravityDirection(const FVector &WorldLocation) const;
};
