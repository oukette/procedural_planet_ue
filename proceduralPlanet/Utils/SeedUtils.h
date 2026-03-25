#pragma once

#include "CoreMinimal.h"
#include "../PlanetGen/ChunkId.h"


// Pure math utilities for generating seeds and hashes.
class PROCEDURALPLANET_API FSeedUtils
{
    public:
        // Generates a deterministic seed for a specific chunk. Uses a simple but effective bit-mixing hash.
        static int32 GetChunkSeed(int32 PlanetSeed, const ChunkId &Id)
        {
            uint32 Hash = HashCombine(GetTypeHash(PlanetSeed), GetTypeHash(Id));

            // Final bit-mix to ensure the bits are well distributed for noise consumption
            Hash = ((Hash >> 16) ^ Hash) * 0x45d9f3b;
            Hash = ((Hash >> 16) ^ Hash) * 0x45d9f3b;
            Hash = (Hash >> 16) ^ Hash;

            return (int32)Hash;
        }
};