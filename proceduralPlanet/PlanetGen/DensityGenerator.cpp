// Fill out your copyright notice in the Description page of Project Settings.


#include "DensityGenerator.h"


/**
 * A self-contained, thread-safe utility class for generating 3D Simplex noise.
 * This is implemented as a static class to keep it decoupled from the DensityGenerator instance.
 * It uses a seed-based permutation table for deterministic, repeatable noise.
 */
class FNoiseUtils
{
    public:
        // Generates 3D Simplex noise for a given point and seed.

    private:
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


DensityGenerator::DensityGenerator(const DensityConfig &InConfig, const IPlanetNoise *InNoiseProvider) :
    Config(InConfig),
    NoiseProvider(InNoiseProvider)
{
    // Validation
    ensure(Config.PlanetRadius > 0.f);
    ensure(Config.VoxelSize > 0.f);
}


float DensityGenerator::SampleDensity(const FVector &PlanetRelativePosition) const
{
    // 1. Base sphere density
    float SphereDensity = SampleSphereDensity(PlanetRelativePosition);

    // 2. Add noise (currently placeholder, will be implemented next)
    float Noise = SampleNoise(PlanetRelativePosition);  // This will now call the FBM function

    // 3. Combine
    return SphereDensity + Noise;
}


GenData DensityGenerator::GenerateDensityField(int32 Resolution, const FVector &FaceNormal, const FVector &FaceRight, const FVector &FaceUp,
                                                                 const FVector2D &UVMin, const FVector2D &UVMax) const
{
    const int32 SampleCount = Resolution + 1;
    const int32 TotalVoxels = SampleCount * SampleCount * SampleCount;

    GenData Result;
    Result.Densities.Reserve(TotalVoxels);
    Result.Positions.Reserve(TotalVoxels);
    Result.SampleCount = SampleCount;

    // Iterate through all voxel grid points
    for (int32 z = 0; z < SampleCount; z++)
    {
        for (int32 y = 0; y < SampleCount; y++)
        {
            for (int32 x = 0; x < SampleCount; x++)
            {
                // Calculate warped position on sphere
                FVector PlanetRelPos = GetProjectedPosition(x, y, z, Resolution, FaceNormal, FaceRight, FaceUp, UVMin, UVMax);

                // Sample density at this position
                float Density = SampleDensity(PlanetRelPos);

                Result.Densities.Add(Density);
                Result.Positions.Add(PlanetRelPos);
            }
        }
    }

    return Result;
}


FVector DensityGenerator::GetProjectedPosition(int32 x, int32 y, int32 z, int32 Resolution, const FVector &FaceNormal, const FVector &FaceRight,
                                               const FVector &FaceUp, const FVector2D &UVMin, const FVector2D &UVMax) const
{
    // Step 1: Get the position on the Cube surface (-1 to 1 range)
    // We pass z=0 here because z in the voxel grid represents depth/altitude,
    // whereas x and y are the "surface" coordinates.
    FVector CubePos = FMathUtils::GetCubeSurfacePosition(FIntVector(x, y, 0), Resolution, FaceNormal, FaceRight, FaceUp, UVMin, UVMax);

    // Step 2: Project that cube point onto a unit sphere (Radius = 1.0)
    FVector UnitSpherePos = FMathUtils::CubeToSphere(CubePos);

    // 5. Calculate altitude (Z is radial height from surface). Z = Resolution/2 represents the planet surface
    float SurfaceLevel = Resolution / 2.0f;
    float AltitudeOffset = (z - SurfaceLevel) * Config.VoxelSize;

    // 6. Final position: sphere direction * (radius + altitude)
    return UnitSpherePos * (Config.PlanetRadius + AltitudeOffset);
}


float DensityGenerator::GetDensityAtPos(const FVector &PlanetLocalPos) const
{
    // CORRECTED: This function must return the full density value, including noise,
    // so that the gradient calculation for normals is accurate. It should not just
    // return the sphere density.
    return SampleDensity(PlanetLocalPos);
}


FVector DensityGenerator::GetNormalAtPos(const FVector &PlanetLocalPos) const
{
    // Use a small offset to find the slope
    // A smaller epsilon gives a more accurate local gradient. 1.0f can be too large in areas with high-frequency noise.
    const float eps = 0.1f;

    // Gradient is the change in density in each axis
    float nx = GetDensityAtPos(PlanetLocalPos + FVector(eps, 0, 0)) - GetDensityAtPos(PlanetLocalPos - FVector(eps, 0, 0));
    float ny = GetDensityAtPos(PlanetLocalPos + FVector(0, eps, 0)) - GetDensityAtPos(PlanetLocalPos - FVector(0, eps, 0));
    float nz = GetDensityAtPos(PlanetLocalPos + FVector(0, 0, eps)) - GetDensityAtPos(PlanetLocalPos - FVector(0, 0, eps));

    FVector Gradient = FVector(nx, ny, nz);

    // Safety check: if the gradient is zero (dead center), fall back to the radial vector.
    if (Gradient.SizeSquared() < KINDA_SMALL_NUMBER)
    {
        return PlanetLocalPos.GetSafeNormal();
    }

    // Negate because we want the normal to point TOWARDS decreasing density (out of the ground)
    return -Gradient.GetSafeNormal();
}


float DensityGenerator::SampleSphereDensity(const FVector &PlanetRelativePosition) const
{
    // Distance from planet center
    float DistanceToCenter = PlanetRelativePosition.Size();

    // Density is positive inside, negative outside
    // Normalized by VoxelSize for smoother marching cubes
    return (Config.PlanetRadius - DistanceToCenter) / Config.VoxelSize;
}


float DensityGenerator::SampleFBM(const FVector &Position) const
{
    // Safety check: if no provider is set, return 0
    if (!NoiseProvider)
    {
        return 0.0f;
    }

    float Total = 0.0f;
    float Frequency = Config.NoiseFrequency;
    float Amplitude = 1.0f;
    float MaxValue = 0.0f;  // Used for normalizing result to [-1, 1]

    for (int32 i = 0; i < Config.NoiseOctaves; i++)
    {
        // OLD CODE:
        // Total += FNoiseUtils::SimplexNoise(Position * Frequency, Config.Seed + i) * Amplitude;

        // NEW CODE (Interface Call):
        // We pass the modified seed (BaseSeed + OctaveIndex) just like before
        float Signal = NoiseProvider->GetNoise(Position * Frequency, Config.Seed + i);

        Total += Signal * Amplitude;

        MaxValue += Amplitude;
        Amplitude *= Config.NoisePersistence;
        Frequency *= Config.NoiseLacunarity;
    }

    // Normalize result to ensure it stays within expected range [-1, 1]
    if (MaxValue > 0)
    {
        return Total / MaxValue;
    }

    return 0.0f;
}


float DensityGenerator::SampleNoise(const FVector &Position) const
{
    // 1. Get the raw noise value from our FBM function. This will be in the range [-1, 1].
    float FbmValue = SampleFBM(Position);

    // 2. The noise value represents a displacement in world units. We scale it by the desired amplitude.
    // 3. We then divide by VoxelSize to convert this world-space displacement into a "density unit"
    //    displacement, which is what Marching Cubes expects to see.
    return FbmValue * Config.NoiseAmplitude / Config.VoxelSize;
}
