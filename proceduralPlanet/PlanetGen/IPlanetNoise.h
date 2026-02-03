#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"


/**
 * Context for noise sampling.
 * Pure data, deterministic inputs only.
 */
struct FNoiseContext
{
        FVector WorldPosition;
        double PlanetRadius;
        uint64 PlanetSeed;

        FNoiseContext() = default;

        FNoiseContext(const FVector &InPosition, double InRadius, uint64 InSeed) :
            WorldPosition(InPosition),
            PlanetRadius(InRadius),
            PlanetSeed(InSeed)
        {
        }
};


/**
 * Pure, stateless noise interface for terrain generation.
 * Thread-safe, deterministic, and side-effect free.
 */
class PROCEDURALPLANET_API IPlanetNoise
{
    public:
        virtual ~IPlanetNoise() = default;

        /**
         * Sample scalar noise at 3D position.
         * @param Position World-space position (relative to planet center)
         * @param Frequency Scale of noise features
         * @param Octave Which octave to sample (0 = base)
         * @return Noise value in [-1, 1] typically
         */
        virtual float Sample(const FNoiseContext &Context, float Frequency = 1.0f, int32 Octave = 0) const = 0;

        /**
         * Sample fractal (multi-octave) noise.
         * @param Position World-space position
         * @param BaseFrequency Frequency of first octave
         * @param Octaves Number of octaves
         * @param Persistence Amplitude multiplier per octave
         * @param Lacunarity Frequency multiplier per octave
         * @return Fractal noise value
         */
        virtual float SampleFractal(const FNoiseContext &Context, float BaseFrequency = 0.001f, int32 Octaves = 4, float Persistence = 0.5f,
                                    float Lacunarity = 2.0f) const = 0;

        /**
         * Get the maximum possible absolute value for this noise.
         * Useful for normalization.
         */
        virtual float GetMaxAmplitude() const = 0;

        /**
         * Clone this noise generator (for thread safety).
         */
        virtual TSharedPtr<IPlanetNoise> Clone() const = 0;
};