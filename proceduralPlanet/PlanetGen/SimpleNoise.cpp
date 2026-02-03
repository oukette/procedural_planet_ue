#include "SimpleNoise.h"
#include <cmath>


FSimpleNoise::FSimpleNoise(uint64 Seed, int32 InMaxOctaves) :
    BaseSeed(Seed),
    MaxOctaves(InMaxOctaves)
{
    // Pre-compute seeds for each octave
    FSeedUtils::GenerateNoiseOctaveSeeds(BaseSeed, MaxOctaves, OctaveSeeds);
}


float FSimpleNoise::Sample(const FNoiseContext &Context, float Frequency, int32 Octave) const
{
    if (Octave < 0 || Octave >= MaxOctaves)
    {
        return 0.0f;
    }

    // Scale position by frequency
    FVector ScaledPos = Context.WorldPosition * Frequency;

    // Sample gradient noise with this octave's seed
    return GradientNoise(ScaledPos, OctaveSeeds[Octave]);
}


float FSimpleNoise::SampleFractal(const FNoiseContext &Context, float BaseFrequency, int32 Octaves, float Persistence, float Lacunarity) const
{
    Octaves = FMath::Clamp(Octaves, 1, MaxOctaves);

    float Value = 0.0f;
    float Amplitude = 1.0f;
    float Frequency = BaseFrequency;
    float MaxValue = 0.0f;  // For normalization

    for (int32 i = 0; i < Octaves; i++)
    {
        // Create context for this octave
        FNoiseContext OctaveContext = Context;

        Value += Sample(OctaveContext, Frequency, i) * Amplitude;
        MaxValue += Amplitude;

        Amplitude *= Persistence;
        Frequency *= Lacunarity;
    }

    // Normalize to roughly [-1, 1]
    if (MaxValue > 0.0f)
    {
        Value /= MaxValue;
    }

    return Value;
}


TSharedPtr<IPlanetNoise> FSimpleNoise::Clone() const { return MakeShared<FSimpleNoise>(BaseSeed, MaxOctaves); }


// ==================== Internal Implementation ====================
float FSimpleNoise::GradientNoise(const FVector &Position, uint64 Seed) const
{
    // Simple deterministic gradient noise (Perlin-style)
    // This is a simplified version for testing

    float X = Position.X;
    float Y = Position.Y;
    float Z = Position.Z;

    // Integer coordinates
    int32 IX = FMath::FloorToInt(X);
    int32 IY = FMath::FloorToInt(Y);
    int32 IZ = FMath::FloorToInt(Z);

    // Fractional parts
    float FX = X - IX;
    float FY = Y - IY;
    float FZ = Z - IZ;

    // Fade curves
    float U = Fade(FX);
    float V = Fade(FY);
    float W = Fade(FZ);

    // Hash the 8 cube corners
    float N000 = Grad(FSeedUtils::HashCoordinate(IX, IY, IZ, Seed) & 15, FX, FY, FZ);
    float N100 = Grad(FSeedUtils::HashCoordinate(IX + 1, IY, IZ, Seed) & 15, FX - 1, FY, FZ);
    float N010 = Grad(FSeedUtils::HashCoordinate(IX, IY + 1, IZ, Seed) & 15, FX, FY - 1, FZ);
    float N110 = Grad(FSeedUtils::HashCoordinate(IX + 1, IY + 1, IZ, Seed) & 15, FX - 1, FY - 1, FZ);
    float N001 = Grad(FSeedUtils::HashCoordinate(IX, IY, IZ + 1, Seed) & 15, FX, FY, FZ - 1);
    float N101 = Grad(FSeedUtils::HashCoordinate(IX + 1, IY, IZ + 1, Seed) & 15, FX - 1, FY, FZ - 1);
    float N011 = Grad(FSeedUtils::HashCoordinate(IX, IY + 1, IZ + 1, Seed) & 15, FX, FY - 1, FZ - 1);
    float N111 = Grad(FSeedUtils::HashCoordinate(IX + 1, IY + 1, IZ + 1, Seed) & 15, FX - 1, FY - 1, FZ - 1);

    // Trilinear interpolation
    float X00 = Lerp(N000, N100, U);
    float X10 = Lerp(N010, N110, U);
    float X01 = Lerp(N001, N101, U);
    float X11 = Lerp(N011, N111, U);

    float Y0 = Lerp(X00, X10, V);
    float Y1 = Lerp(X01, X11, V);

    float Result = Lerp(Y0, Y1, W);

    // Ensure result is in reasonable range (Perlin noise is in [-1, 1] theoretically)
    // Clamp to be safe during development
    return FMath::Clamp(Result, -1.0f, 1.0f);
}


float FSimpleNoise::Fade(float T)
{
    // 6t^5 - 15t^4 + 10t^3
    return T * T * T * (T * (T * 6 - 15) + 10);
}


float FSimpleNoise::Lerp(float A, float B, float T) { return A + T * (B - A); }


float FSimpleNoise::Grad(int32 Hash, float X, float Y, float Z)
{
    // Convert hash to gradient vector
    int32 H = Hash & 15;

    // 12 gradient vectors (Perlin's original set)
    float U = H < 8 ? X : Y;
    float V = H < 4 ? Y : (H == 12 || H == 14 ? X : Z);

    return ((H & 1) ? -U : U) + ((H & 2) ? -V : V);
}