// Fill out your copyright notice in the Description page of Project Settings.


#include "PlanetDensityGenerator.h"
#include "Math/UnrealMathUtility.h"


/**
 * A self-contained, thread-safe utility class for generating 3D Simplex noise.
 * This is implemented as a static class to keep it decoupled from the PlanetDensityGenerator instance.
 * It uses a seed-based permutation table for deterministic, repeatable noise.
 */
class FNoiseUtils
{
    public:
        // Generates 3D Simplex noise for a given point and seed.
        static float SimplexNoise(const FVector &Position, int32 Seed)
        {
            // Simplex noise constants
            const float F3 = 1.0f / 3.0f;
            const float G3 = 1.0f / 6.0f;

            // Skew the input space to determine which simplex cell we're in
            float s = (Position.X + Position.Y + Position.Z) * F3;
            int32 i = FMath::FloorToInt(Position.X + s);
            int32 j = FMath::FloorToInt(Position.Y + s);
            int32 k = FMath::FloorToInt(Position.Z + s);

            float t = (i + j + k) * G3;
            float X0 = i - t;  // Unskew the cell origin back to (x,y,z) space
            float Y0 = j - t;
            float Z0 = k - t;
            float x0 = Position.X - X0;  // The x,y,z distances from the cell origin
            float y0 = Position.Y - Y0;
            float z0 = Position.Z - Z0;

            // For the 3D case, the simplex shape is a tetrahedron.
            // Determine which simplex we are in.
            int32 i1, j1, k1;  // Offsets for second corner of simplex in (i,j,k) coords
            int32 i2, j2, k2;  // Offsets for third corner of simplex in (i,j,k) coords

            if (x0 >= y0)
            {
                if (y0 >= z0)
                {
                    i1 = 1;
                    j1 = 0;
                    k1 = 0;
                    i2 = 1;
                    j2 = 1;
                    k2 = 0;
                }  // X Y Z order
                else if (x0 >= z0)
                {
                    i1 = 1;
                    j1 = 0;
                    k1 = 0;
                    i2 = 1;
                    j2 = 0;
                    k2 = 1;
                }  // X Z Y order
                else
                {
                    i1 = 0;
                    j1 = 0;
                    k1 = 1;
                    i2 = 1;
                    j2 = 0;
                    k2 = 1;
                }  // Z X Y order
            }
            else
            {  // x0 < y0
                if (y0 < z0)
                {
                    i1 = 0;
                    j1 = 0;
                    k1 = 1;
                    i2 = 0;
                    j2 = 1;
                    k2 = 1;
                }  // Z Y X order
                else if (x0 < z0)
                {
                    i1 = 0;
                    j1 = 1;
                    k1 = 0;
                    i2 = 0;
                    j2 = 1;
                    k2 = 1;
                }  // Y Z X order
                else
                {
                    i1 = 0;
                    j1 = 1;
                    k1 = 0;
                    i2 = 1;
                    j2 = 1;
                    k2 = 0;
                }  // Y X Z order
            }

            // A step of (1,0,0) in (i,j,k) means a step of (1-c,-c,-c) in (x,y,z),
            // and a step of (0,1,0) in (i,j,k) means a step of (-c,1-c,-c) in (x,y,z), etc.
            // where c = 1/6.
            float x1 = x0 - i1 + G3;
            float y1 = y0 - j1 + G3;
            float z1 = z0 - k1 + G3;
            float x2 = x0 - i2 + 2.0f * G3;
            float y2 = y0 - j2 + 2.0f * G3;
            float z2 = z0 - k2 + 2.0f * G3;
            float x3 = x0 - 1.0f + 3.0f * G3;
            float y3 = y0 - 1.0f + 3.0f * G3;
            float z3 = z0 - 1.0f + 3.0f * G3;

            // Calculate the contribution from the four corners
            float n = 0.0f;
            n += CalculateCorner(x0, y0, z0, Grad(Hash(i, j, k, Seed), x0, y0, z0));
            n += CalculateCorner(x1, y1, z1, Grad(Hash(i + i1, j + j1, k + k1, Seed), x1, y1, z1));
            n += CalculateCorner(x2, y2, z2, Grad(Hash(i + i2, j + j2, k + k2, Seed), x2, y2, z2));
            n += CalculateCorner(x3, y3, z3, Grad(Hash(i + 1, j + 1, k + 1, Seed), x3, y3, z3));

            // The result is scaled to stay just inside [-1,1]
            return 32.0f * n;
        }

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

// Initialize the gradient table
const FVector FNoiseUtils::GradTable[16] = {FVector(1, 1, 0),
                                            FVector(-1, 1, 0),
                                            FVector(1, -1, 0),
                                            FVector(-1, -1, 0),
                                            FVector(1, 0, 1),
                                            FVector(-1, 0, 1),
                                            FVector(1, 0, -1),
                                            FVector(-1, 0, -1),
                                            FVector(0, 1, 1),
                                            FVector(0, -1, 1),
                                            FVector(0, 1, -1),
                                            FVector(0, -1, -1),
                                            // Add some more vectors for 3D. These are edges of a cube.
                                            FVector(1, 1, 0),
                                            FVector(-1, 1, 0),
                                            FVector(0, -1, 1),
                                            FVector(0, -1, -1)};


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
    float Noise = SampleNoise(PlanetRelativePosition);  // This will now call the FBM function

    // 3. Combine
    return SphereDensity + Noise;
}


PlanetDensityGenerator::GenData PlanetDensityGenerator::GenerateDensityField(int32 Resolution, const FVector &FaceNormal, const FVector &FaceRight,
                                                                             const FVector &FaceUp, const FVector2D &UVMin, const FVector2D &UVMax) const
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
    // CORRECTED: This function must return the full density value, including noise,
    // so that the gradient calculation for normals is accurate. It should not just
    // return the sphere density.
    return SampleDensity(PlanetLocalPos);
}


FVector PlanetDensityGenerator::GetNormalAtPos(const FVector &PlanetLocalPos) const
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


float PlanetDensityGenerator::SampleSphereDensity(const FVector &PlanetRelativePosition) const
{
    // Distance from planet center
    float DistanceToCenter = PlanetRelativePosition.Size();

    // Density is positive inside, negative outside
    // Normalized by VoxelSize for smoother marching cubes
    return (Config.PlanetRadius - DistanceToCenter) / Config.VoxelSize;
}


float PlanetDensityGenerator::SampleFBM(const FVector &Position) const
{
    float Total = 0.0f;
    float Frequency = Config.NoiseFrequency;
    float Amplitude = 1.0f;
    float MaxValue = 0.0f;  // Used for normalizing the result to [-1, 1]

    for (int32 i = 0; i < Config.NoiseOctaves; i++)
    {
        Total += FNoiseUtils::SimplexNoise(Position * Frequency, Config.Seed + i) * Amplitude;
        MaxValue += Amplitude;
        Amplitude *= Config.NoisePersistence;
        Frequency *= Config.NoiseLacunarity;
    }

    if (MaxValue > 0)
    {
        return Total / MaxValue;
    }
    return 0.0f;
}


float PlanetDensityGenerator::SampleNoise(const FVector &Position) const
{
    // 1. Get the raw noise value from our FBM function. This will be in the range [-1, 1].
    float FbmValue = SampleFBM(Position);

    // 2. The noise value represents a displacement in world units. We scale it by the desired amplitude.
    // 3. We then divide by VoxelSize to convert this world-space displacement into a "density unit"
    //    displacement, which is what Marching Cubes expects to see.
    return FbmValue * Config.NoiseAmplitude / Config.VoxelSize;
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
