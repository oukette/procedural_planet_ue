// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MathUtils.h"
#include "IPlanetNoise.h"
#include "PlanetConfig.h"


// Encapsulates all density field generation logic for procedural planets.
// Thread-safe and stateless - can be used from async tasks.
// Density Convention:
//   Positive = Solid (inside terrain)
//   Negative = Air (outside terrain)
//   Zero = Surface
class PROCEDURALPLANET_API DensityGenerator
{
    private:
        DensityConfig m_densityConfig;
        const IPlanetNoise *m_noiseProvider;

    public:
        // Constructor
        explicit DensityGenerator(const DensityConfig &InConfig, const IPlanetNoise *InNoiseProvider = nullptr);

        // Sample density at a world position (relative to planet center)
        float SampleDensity(const FVector &PlanetRelativePosition) const;

        // Generate entire density field for a chunk (optimized batch operation)
        GenData GenerateDensityField(int32 Resolution, const FVector &FaceNormal, const FVector &FaceRight, const FVector &FaceUp, const FVector2D &UVMin,
                                     const FVector2D &UVMax) const;

        // Accessors for validation/debugging
        const DensityConfig &GetConfig() const { return m_densityConfig; }

        // Calculate warped position on sphere surface (cube-to-sphere projection)
        FVector GetProjectedPosition(int32 x, int32 y, int32 z, int32 Resolution, const FVector &FaceNormal, const FVector &FaceRight, const FVector &FaceUp,
                                     const FVector2D &UVMin, const FVector2D &UVMax) const;

        float GetDensityAtPos(const FVector &LocalPos) const;

        FVector GetNormalAtPos(const FVector &LocalPos) const;

    private:
        // Base sphere density (distance to center)
        float SampleSphereDensity(const FVector &PlanetRelativePosition) const;

        // Fractal Brownian Motion sampling
        float SampleFBM(const FVector &Position) const;

        // Noise sampling (to be implemented with your noise system)
        float SampleNoise(const FVector &Position) const;

        // Helper: Apply spherified cube mapping
        static FVector GetSpherifiedCubePoint(const FVector &CubePoint);
};
