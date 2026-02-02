#pragma once

#include "CoreMinimal.h"
#include <cstdint>


/**
 * Deterministic hashing utilities for procedural generation.
 * All functions are thread-safe, pure, and side-effect free.
 */
class PROCEDURALPLANET_API FSeedUtils
{
    public:
        // ==================== Basic 64-bit Hashing ====================

        /**
         * SplitMix64 hash function - fast, good distribution.
         */
        static uint64 SplitMix64(uint64 X);

        /**
         * PCG-style hash - excellent statistical properties.
         */
        static uint64 PCG_Hash(uint64 X);

        /**
         * Fast 64-bit hash for integers.
         */
        static uint64 Hash64(uint64 X);

        // ==================== Seed Combination ====================

        /**
         * Combine two seeds into a new deterministic seed.
         */
        static uint64 CombineSeeds(uint64 SeedA, uint64 SeedB);

        /**
         * Combine multiple seeds.
         */
        static uint64 CombineSeeds(const TArray<uint64> &Seeds);

        /**
         * Derive a seed for a specific purpose from a base seed.
         */
        static uint64 DeriveSeed(uint64 BaseSeed, uint64 PurposeTag);

        // ==================== Spatial Hashing ====================

        /**
         * Hash a 3D integer coordinate (for chunk/voxel indexing).
         */
        static uint64 HashCoordinate(int32 X, int32 Y, int32 Z, uint64 Seed = 0);

        /**
         * Hash a 3D floating point position (for noise sampling).
         */
        static uint64 HashPosition(float X, float Y, float Z, uint64 Seed = 0);

        /**
         * Hash a 2D floating point position (for biomes/maps).
         */
        static uint64 HashPosition2D(float X, float Y, uint64 Seed = 0);

        // ==================== Chunk Seed Derivation ====================

        /**
         * Generate a deterministic seed for a chunk.
         */
        static uint64 GetChunkSeed(uint64 PlanetSeed, uint8 CubeFace, int32 LOD, int32 ChunkX, int32 ChunkY);

        /**
         * Generate a deterministic seed for a voxel within a chunk.
         */
        static uint64 GetVoxelSeed(uint64 ChunkSeed, int32 VoxelX, int32 VoxelY, int32 VoxelZ);

        // ==================== Random Number Generation ====================

        /**
         * Get a deterministic float in [0, 1] from a seed.
         */
        static float RandomFloat(uint64 Seed);

        /**
         * Get a deterministic float in [Min, Max] from a seed.
         */
        static float RandomFloat(uint64 Seed, float Min, float Max);

        /**
         * Get a deterministic integer in [Min, Max] from a seed.
         */
        static int32 RandomInt(uint64 Seed, int32 Min, int32 Max);

        /**
         * Get a deterministic normalized vector (random direction).
         */
        static FVector RandomDirection(uint64 Seed);

        // ==================== Noise Seed Preparation ====================

        /**
         * Generate seeds for noise octaves from a base seed.
         */
        static void GenerateNoiseOctaveSeeds(uint64 BaseSeed, int32 NumOctaves, TArray<uint64> &OutOctaveSeeds);

        /**
         * Generate a seed for a specific noise layer (terrain, caves, etc.).
         */
        static uint64 GetNoiseLayerSeed(uint64 PlanetSeed, const FString &LayerName);
};