// Fill out your copyright notice in the Description page of Project Settings.


#include "PlanetDensityGenerator.h"
#include "Math/UnrealMathUtility.h"


PlanetDensityGenerator::PlanetDensityGenerator(const DensityConfig &InConfig) :
    Config(InConfig)
{
    // Validation
    ensure(Config.PlanetRadius > 0.f);
    ensure(Config.VoxelSize > 0.f);
}


float PlanetDensityGenerator::SampleDensity(const FVector &PlanetRelativePosition) const
{
    // 1. Base sphere density
    float SphereDensity = SampleSphereDensity(PlanetRelativePosition);

    // 2. Add noise (currently placeholder, will be implemented next)
    float Noise = SampleNoise(PlanetRelativePosition);

    // 3. Combine
    return SphereDensity + Noise;
}


PlanetDensityGenerator::FGenData PlanetDensityGenerator::GenerateDensityField(int32 Resolution, const FVector &FaceNormal, const FVector &FaceRight,
                                                                              const FVector &FaceUp, const FVector2D &UVMin, const FVector2D &UVMax) const
{
    const int32 SampleCount = Resolution + 1;
    const int32 TotalVoxels = SampleCount * SampleCount * SampleCount;

    FGenData Result;
    Result.Densities.Reserve(TotalVoxels);
    Result.Positions.Reserve(TotalVoxels);

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


FVector PlanetDensityGenerator::GetProjectedPosition(int32 x, int32 y, int32 z, int32 Resolution, const FVector &FaceNormal, const FVector &FaceRight,
                                                     const FVector &FaceUp, const FVector2D &UVMin, const FVector2D &UVMax) const
{
    // 1. Calculate normalized U, V for this voxel (0.0 to 1.0 within the chunk)
    float uPct = x / (float)Resolution;
    float vPct = y / (float)Resolution;

    // 2. Map to Face UV coordinates (-1.0 to 1.0 space of the whole cube face)
    float u = FMath::Lerp(UVMin.X, UVMax.X, uPct);
    float v = FMath::Lerp(UVMin.Y, UVMax.Y, vPct);

    // 3. Point on unit cube face
    FVector PointOnCube = FaceNormal + FaceRight * u + FaceUp * v;

    // 4. Spherify using equal-area distribution
    FVector SphereDir = GetSpherifiedCubePoint(PointOnCube);

    // 5. Calculate altitude (Z is radial height from surface). Z = Resolution/2 represents the planet surface
    float SurfaceLevel = Resolution / 2.0f;
    float Altitude = (z - SurfaceLevel) * Config.VoxelSize;

    // 6. Final position: sphere direction * (radius + altitude)
    return SphereDir * (Config.PlanetRadius + Altitude);
}


float PlanetDensityGenerator::GetDensityAtPos(const FVector &PlanetLocalPos) const
{
    // This is the core logic previously in SampleSphereDensity
    float DistanceToCenter = PlanetLocalPos.Size();
    float SphereDensity = (Config.PlanetRadius - DistanceToCenter) / Config.VoxelSize;

    // Once we add noise, it will be added here:
    // float Noise = SampleNoise(LocalPos);
    // return SphereDensity + Noise;

    return SphereDensity;
}


FVector PlanetDensityGenerator::GetNormalAtPos(const FVector &PlanetLocalPos) const
{
    // Use a small offset to find the slope
    const float eps = 1.0f;

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


float PlanetDensityGenerator::SampleSphereDensity(const FVector &PlanetRelativePosition) const
{
    // Distance from planet center
    float DistanceToCenter = PlanetRelativePosition.Size();

    // Density is positive inside, negative outside
    // Normalized by VoxelSize for smoother marching cubes
    return (Config.PlanetRadius - DistanceToCenter) / Config.VoxelSize;
}


float PlanetDensityGenerator::SampleNoise(const FVector &Position) const
{
    // PLACEHOLDER: Will be implemented with proper 3D noise (Simplex, Perlin, etc.)
    // For now, return 0 to maintain current sphere behavior

    // Future implementation example:
    // FVector NoiseCoord = Position * Config.NoiseFrequency;
    // float NoiseValue = FMath::PerlinNoise3D(NoiseCoord);
    // return NoiseValue * Config.NoiseAmplitude / Config.VoxelSize;

    return 0.0f;
}


FVector PlanetDensityGenerator::GetSpherifiedCubePoint(const FVector &CubePoint)
{
    // Spherified Cube mapping for equal-area distribution
    // Reference: http://mathproofs.blogspot.com/2005/07/mapping-cube-to-sphere.html

    float x2 = CubePoint.X * CubePoint.X;
    float y2 = CubePoint.Y * CubePoint.Y;
    float z2 = CubePoint.Z * CubePoint.Z;

    float x = CubePoint.X * FMath::Sqrt(1.0f - y2 / 2.0f - z2 / 2.0f + y2 * z2 / 3.0f);
    float y = CubePoint.Y * FMath::Sqrt(1.0f - z2 / 2.0f - x2 / 2.0f + z2 * x2 / 3.0f);
    float z = CubePoint.Z * FMath::Sqrt(1.0f - x2 / 2.0f - y2 / 2.0f + x2 * y2 / 3.0f);

    return FVector(x, y, z);
}
