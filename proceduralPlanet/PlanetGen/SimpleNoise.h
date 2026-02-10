#pragma once

#include "CoreMinimal.h"
#include "IPlanetNoise.h"


/**
 * Concrete implementation using Simplex Noise (formerly FNoiseUtils).
 */
class SimpleNoise : public IPlanetNoise
{
    public:
        virtual float GetNoise(const FVector &Position, int32 Seed) const override;

    private:
        // Helper for the Simplex algorithm
        static int32 Floor(float x);

        // Simple hashing function for pseudo-random gradient selection
        static int32 Hash(int32 i, int32 j, int32 k, int32 seed) { return Perm(Perm(Perm(i, seed) + j, seed) + k, seed); }

        // Seeded permutation function
        static int32 Perm(int32 x, int32 seed) { return (((x * x * 15731 + 789221) * x + 1376312589) ^ seed) & 0x7fffffff; }

        static float Grad(int32 hash, float x, float y, float z) { return Dot(GradTable[hash & 15], x, y, z); }

        static float Dot(const FVector &g, float x, float y, float z) { return g.X * x + g.Y * y + g.Z * z; }

        static float CalculateCorner(float x, float y, float z, float grad)
        {
            float t = 0.6f - x * x - y * y - z * z;
            if (t < 0)
                return 0.0f;
            t *= t;
            return t * t * grad;
        }

        // Gradient vectors for 3D simplex noise
        static const FVector GradTable[16];
};