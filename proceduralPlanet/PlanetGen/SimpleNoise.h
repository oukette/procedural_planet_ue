#pragma once

#include "CoreMinimal.h"
#include "IPlanetNoise.h"



// Concrete implementation using Simplex Noise.
class SimpleNoise : public IPlanetNoise
{
    public:
        virtual float getNoise(const FVector &_position, int32 _seed) const override;

    private:
        // Helper for the Simplex algorithm
        static int32 floor(float x);

        // Simple hashing function for pseudo-random gradient selection
        static int32 hash(int32 i, int32 j, int32 k, int32 seed) { return perm(perm(perm(i, seed) + j, seed) + k, seed); }

        // Seeded permutation function
        static int32 perm(int32 x, int32 seed) { return (((x * x * 15731 + 789221) * x + 1376312589) ^ seed) & 0x7fffffff; }

        static float grad(int32 hash, float x, float y, float z) { return dot(gradTable[hash & 15], x, y, z); }

        static float dot(const FVector &g, float x, float y, float z) { return g.X * x + g.Y * y + g.Z * z; }

        static float calculateCorner(float x, float y, float z, float grad)
        {
            float t = 0.6f - x * x - y * y - z * z;
            if (t < 0)
                return 0.0f;
            t *= t;
            return t * t * grad;
        }

        // Gradient vectors for 3D simplex noise
        static const FVector gradTable[16];
};