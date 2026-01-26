// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

/**
 * Encapsulates all density field generation logic for procedural planets.
 * Thread-safe and stateless - can be used from async tasks.
 *
 * Density Convention:
 *   Positive = Solid (inside terrain)
 *   Negative = Air (outside terrain)
 *   Zero = Surface
 */
class PROCEDURALPLANET_API PlanetDensityGenerator
{
    public:
        // Configuration structure to keep parameters organized
        struct DensityConfig
        {
                float PlanetRadius;
                float NoiseAmplitude;
                float NoiseFrequency;
                int32 Seed;
                float VoxelSize;  // For normalization

                // Future expansion: biomes, caves, etc.
                // bool bEnableCaves = false;
                // float CaveFrequency = 0.01f;

                DensityConfig() :
                    PlanetRadius(50000.f),
                    NoiseAmplitude(500.f),
                    NoiseFrequency(0.0003f),
                    Seed(1337),
                    VoxelSize(100.f)
                {
                }
        };

        // Container for generated field data to avoid re-calculating positions
        struct FGenData
        {
                TArray<float> Densities;
                TArray<FVector> Positions;
        };

        // Constructor
        explicit PlanetDensityGenerator(const DensityConfig &InConfig);

        // Sample density at a world position (relative to planet center)
        float SampleDensity(const FVector &PlanetRelativePosition) const;

        // Generate entire density field for a chunk (optimized batch operation)
        FGenData GenerateDensityField(int32 Resolution, const FVector &FaceNormal, const FVector &FaceRight, const FVector &FaceUp, const FVector2D &UVMin,
                                      const FVector2D &UVMax) const;

        // Accessors for validation/debugging
        const DensityConfig &GetConfig() const { return Config; }

        // Calculate warped position on sphere surface (cube-to-sphere projection)
        FVector GetProjectedPosition(int32 x, int32 y, int32 z, int32 Resolution, const FVector &FaceNormal, const FVector &FaceRight, const FVector &FaceUp,
                                     const FVector2D &UVMin, const FVector2D &UVMax) const;

        float GetDensityAtPos(const FVector &LocalPos) const;

        FVector GetNormalAtPos(const FVector &LocalPos) const;

    private:
        DensityConfig Config;

        // Base sphere density (distance to center)
        float SampleSphereDensity(const FVector &PlanetRelativePosition) const;

        // Noise sampling (to be implemented with your noise system)
        float SampleNoise(const FVector &Position) const;

        // Helper: Apply spherified cube mapping
        static FVector GetSpherifiedCubePoint(const FVector &CubePoint);
};
