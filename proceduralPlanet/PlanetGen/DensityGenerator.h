#pragma once

#include "CoreMinimal.h"
#include "IPlanetNoise.h"
#include "MathUtils.h"


/**
 * Context for density sampling.
 */
struct FDensityContext : public FNoiseContext
{
        // Additional density-specific parameters can go here
        float TerrainAmplitude;
        float SeaLevel;

        FDensityContext() = default;

        FDensityContext(const FVector &InPosition, double InRadius, uint64 InSeed) :
            FNoiseContext(InPosition, InRadius, InSeed),
            TerrainAmplitude(0.0f),
            SeaLevel(0.0f)
        {
        }

        // Additional constructor for convenience
        FDensityContext(const FVector &InPosition, double InRadius, uint64 InSeed, float InTerrainAmplitude, float InSeaLevel) :
            FNoiseContext(InPosition, InRadius, InSeed),
            TerrainAmplitude(InTerrainAmplitude),
            SeaLevel(InSeaLevel)
        {
        }
};


/**
 * Authoritative terrain density generator.
 * Pure, stateless, deterministic.
 * Combines SDFs, noise, and planet parameters.
 */
class PROCEDURALPLANET_API FDensityGenerator
{
    public:
        struct FParameters
        {
                // Planet geometry
                FVector PlanetPosition;
                float PlanetRadius = 1000.0f;  // Meters
                float SeaLevel = 0.0f;         // Relative to radius

                // Terrain
                float TerrainNoiseAmplitude = 100.0f;  // Max terrain height
                float TerrainNoiseFrequency = 0.001f;  // Base noise frequency

                // Core (optional)
                float CoreRadius = 0.0f;  // Solid core radius (0 = no core)

                // Caves (optional)
                bool bEnableCaves = false;
                float CaveFrequency = 0.01f;
                float CaveThreshold = 0.3f;

                // Default constructor
                FParameters() = default;
        };

    public:
        /**
         * @param Params Planet parameters (copied internally)
         * @param TerrainNoise Noise for terrain displacement
         * @param CaveNoise Optional noise for caves (can be null)
         */
        FDensityGenerator(const FParameters &Params, const TSharedPtr<IPlanetNoise> &TerrainNoise, const TSharedPtr<IPlanetNoise> &CaveNoise = nullptr);

        ~FDensityGenerator() = default;

        /**
         * Compute terrain density at world position.
         * @param PositionRelativeToPlanet Position relative to planet center
         * @return Density value: < 0 = inside terrain, > 0 = outside terrain
         */
        float SampleDensity(const FVector &PositionRelativeToPlanet) const;

        /**
         * Get the ideal sphere SDF (without terrain).
         */
        float SampleBaseSphere(const FVector &WorldPosition) const;

        /**
         * Get terrain-only displacement (without sphere).
         */
        float SampleTerrain(const FVector &WorldPosition) const;

        /**
         * Create a density context for a position.
         */
        FDensityContext CreateContext(const FVector &WorldPosition) const;

        /**
         * Get parameters (read-only).
         */
        const FParameters &GetParameters() const { return Params; }

    private:
        // Internal composition functions
        float ComputeTerrainDisplacement(const FVector &WorldPosition) const;
        float ComputeCaveDensity(const FVector &WorldPosition) const;

    private:
        FParameters Params;
        TSharedPtr<IPlanetNoise> TerrainNoise;
        TSharedPtr<IPlanetNoise> CaveNoise;
        uint64 PlanetSeed = 0;
        FVector PlanetPosition;
};