#pragma once

#include "CoreMinimal.h"


// Interface for any noise algorithm used by the planet.
// Must be thread-safe (const methods only).
class IPlanetNoise
{
    public:
        virtual ~IPlanetNoise() = default;

        /// @brief           Sample a noise value at a specific 3D position.
        /// @param _position The 3D coordinate to sample.
        /// @param _seed     A unique seed for this specific sample (or chunk).
        /// @return          float - Typically in range [-1, 1], but depends on algorithm.
        virtual float getNoise(const FVector &_position, int32 _seed) const = 0;
};