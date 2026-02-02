#pragma once

#include "CoreMinimal.h"
#include "IPlanetNoise.h"
#include "SeedUtils.h"


/**
 * Simple deterministic Perlin-style noise for initial development.
 * Not production-quality but good for testing the pipeline.
 */
class PROCEDURALPLANET_API FSimpleNoise : public IPlanetNoise
{
    public:
        /**
         * @param Seed Base seed for noise generation
         * @param MaxOctaves Maximum octaves supported
         */
        explicit FSimpleNoise(uint64 Seed, int32 MaxOctaves = 8);

        virtual ~FSimpleNoise() = default;

        // IPlanetNoise interface
        virtual float Sample(const FVector &Position, float Frequency = 1.0f, int32 Octave = 0) const override;

        virtual float SampleFractal(const FVector &Position, float BaseFrequency = 0.001f, int32 Octaves = 4, float Persistence = 0.5f,
                                    float Lacunarity = 2.0f) const override;

        virtual float GetMaxAmplitude() const override { return 1.0f; }

        virtual TSharedPtr<IPlanetNoise> Clone() const override;

    private:
        // Internal Perlin-style gradient noise
        float GradientNoise(const FVector &Position, uint64 Seed) const;

        // Interpolation helpers
        static float Fade(float T);
        static float Lerp(float A, float B, float T);
        static float Grad(int32 Hash, float X, float Y, float Z);

    private:
        uint64 BaseSeed;
        int32 MaxOctaves;
        TArray<uint64> OctaveSeeds;  // Pre-computed seeds for each octave
};