#include "SeedUtils.h"
#include "Math/UnrealMathUtility.h"
#include <cmath>


// ==================== Basic 64-bit Hashing ====================
uint64 FSeedUtils::SplitMix64(uint64 X)
{
    uint64 Z = (X + 0x9e3779b97f4a7c15ull);
    Z = (Z ^ (Z >> 30)) * 0xbf58476d1ce4e5b9ull;
    Z = (Z ^ (Z >> 27)) * 0x94d049bb133111ebull;
    return Z ^ (Z >> 31);
}

uint64 FSeedUtils::PCG_Hash(uint64 X)
{
    uint64 State = X * 6364136223846793005ull + 1442695040888963407ull;
    uint64 Word = ((State >> ((State >> 59ull) + 5ull)) ^ State) * 12605985483714917081ull;
    return (Word >> 43ull) ^ Word;
}

uint64 FSeedUtils::Hash64(uint64 X)
{
    // Use PCG as default - good properties
    return PCG_Hash(X);
}

// ==================== Seed Combination ====================
uint64 FSeedUtils::CombineSeeds(uint64 SeedA, uint64 SeedB)
{
    // Mix seeds using a hash function
    return Hash64(SeedA ^ Hash64(SeedB));
}

uint64 FSeedUtils::CombineSeeds(const TArray<uint64> &Seeds)
{
    if (Seeds.Num() == 0)
        return 0;

    uint64 Result = Seeds[0];
    for (int32 i = 1; i < Seeds.Num(); i++)
    {
        Result = CombineSeeds(Result, Seeds[i]);
    }
    return Result;
}

uint64 FSeedUtils::DeriveSeed(uint64 BaseSeed, uint64 PurposeTag) { return CombineSeeds(BaseSeed, Hash64(PurposeTag)); }

// ==================== Spatial Hashing ====================
uint64 FSeedUtils::HashCoordinate(int32 X, int32 Y, int32 Z, uint64 Seed)
{
    // Mix coordinates using large primes
    uint64 Hash = Seed;
    Hash = Hash64(Hash ^ (static_cast<uint64>(X) * 73856093ull));
    Hash = Hash64(Hash ^ (static_cast<uint64>(Y) * 19349663ull));
    Hash = Hash64(Hash ^ (static_cast<uint64>(Z) * 83492791ull));
    return Hash;
}

uint64 FSeedUtils::HashPosition(float X, float Y, float Z, uint64 Seed)
{
    // Quantize float position to integer grid for hashing
    // Use a fixed resolution that's finer than any noise frequency
    const float GridSize = 0.01f;  // 1cm grid

    int32 IX = FMath::FloorToInt(X / GridSize);
    int32 IY = FMath::FloorToInt(Y / GridSize);
    int32 IZ = FMath::FloorToInt(Z / GridSize);

    return HashCoordinate(IX, IY, IZ, Seed);
}

uint64 FSeedUtils::HashPosition2D(float X, float Y, uint64 Seed) { return HashPosition(X, Y, 0.0f, Seed); }

// ==================== Chunk Seed Derivation ====================
uint64 FSeedUtils::GetChunkSeed(uint64 PlanetSeed, uint8 CubeFace, int32 LOD, int32 ChunkX, int32 ChunkY)
{
    // Combine all chunk identifiers
    uint64 Hash = PlanetSeed;
    Hash = CombineSeeds(Hash, static_cast<uint64>(CubeFace));
    Hash = CombineSeeds(Hash, static_cast<uint64>(LOD));
    Hash = CombineSeeds(Hash, static_cast<uint64>(ChunkX));
    Hash = CombineSeeds(Hash, static_cast<uint64>(ChunkY));

    // Final mixing
    return Hash64(Hash);
}

uint64 FSeedUtils::GetVoxelSeed(uint64 ChunkSeed, int32 VoxelX, int32 VoxelY, int32 VoxelZ) { return HashCoordinate(VoxelX, VoxelY, VoxelZ, ChunkSeed); }

// ==================== Random Number Generation ====================
float FSeedUtils::RandomFloat(uint64 Seed)
{
    // Convert hash to float in [0, 1)
    // Use high 24 bits for good precision
    uint64 Hash = Hash64(Seed);
    float Value = static_cast<float>(Hash >> 40) / static_cast<float>(1 << 24);
    return FMath::Clamp(Value, 0.0f, 1.0f - 1e-7f);  // Ensure < 1.0
}

float FSeedUtils::RandomFloat(uint64 Seed, float Min, float Max)
{
    float T = RandomFloat(Seed);
    return Min + T * (Max - Min);
}

int32 FSeedUtils::RandomInt(uint64 Seed, int32 Min, int32 Max)
{
    if (Min >= Max)
        return Min;

    uint64 Hash = Hash64(Seed);
    uint64 Range = static_cast<uint64>(Max - Min + 1);
    uint64 Value = Hash % Range;

    return Min + static_cast<int32>(Value);
}

FVector FSeedUtils::RandomDirection(uint64 Seed)
{
    // Use three different seeds for X, Y, Z
    float X = RandomFloat(Seed, -1.0f, 1.0f);
    float Y = RandomFloat(CombineSeeds(Seed, 1), -1.0f, 1.0f);
    float Z = RandomFloat(CombineSeeds(Seed, 2), -1.0f, 1.0f);

    FVector Dir(X, Y, Z);
    return Dir.GetSafeNormal();
}

// ==================== Noise Seed Preparation ====================
void FSeedUtils::GenerateNoiseOctaveSeeds(uint64 BaseSeed, int32 NumOctaves, TArray<uint64> &OutOctaveSeeds)
{
    OutOctaveSeeds.Empty(NumOctaves);

    for (int32 i = 0; i < NumOctaves; i++)
    {
        uint64 OctaveSeed = DeriveSeed(BaseSeed, static_cast<uint64>(i * 1234567));
        OutOctaveSeeds.Add(OctaveSeed);
    }
}

uint64 FSeedUtils::GetNoiseLayerSeed(uint64 PlanetSeed, const FString &LayerName)
{
    // Hash the layer name string
    uint64 NameHash = 0;
    for (TCHAR Char : LayerName)
    {
        NameHash = NameHash * 31 + static_cast<uint64>(Char);
    }

    return DeriveSeed(PlanetSeed, NameHash);
}