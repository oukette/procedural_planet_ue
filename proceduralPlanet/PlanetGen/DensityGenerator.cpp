#include "DensityGenerator.h"
#include "SeedUtils.h"
#include <cmath>


FDensityGenerator::FDensityGenerator(const FParameters &InParams, const TSharedPtr<IPlanetNoise> &InTerrainNoise, const TSharedPtr<IPlanetNoise> &InCaveNoise) :
    Params(InParams),
    TerrainNoise(InTerrainNoise),
    CaveNoise(InCaveNoise),
    PlanetPosition(InParams.PlanetPosition)
{
    // Validate parameters
    Params.PlanetRadius = FMath::Max(Params.PlanetRadius, 1.0f);
    Params.CoreRadius = FMath::Clamp(Params.CoreRadius, 0.0f, Params.PlanetRadius * 0.9f);
    Params.TerrainNoiseAmplitude = FMath::Max(Params.TerrainNoiseAmplitude, 0.0f);

    PlanetSeed = FSeedUtils::Hash64(static_cast<uint64>(Params.PlanetRadius * 1000.0f) ^ static_cast<uint64>(Params.TerrainNoiseAmplitude * 100.0f));
}


float FDensityGenerator::SampleDensity(const FVector &PositionRelativeToPlanet) const
{
    // 1. Base sphere
    float Base = SampleBaseSphere(PositionRelativeToPlanet);

    // 2. Terrain displacement
    float Terrain = ComputeTerrainDisplacement(PositionRelativeToPlanet);

    // 3. Combine: negative terrain value carves into sphere
    float Density = Base - Terrain;

    // 4. Caves (optional)
    if (Params.bEnableCaves && CaveNoise.IsValid())
    {
        float Cave = ComputeCaveDensity(PositionRelativeToPlanet);
        Density = FMath::Min(Density, Cave);  // Union operation
    }

    // // 5. Sea level
    // if (Params.SeaLevel > 0.0f)
    // {
    //     // Simple sea level: everything below sea level is solid
    //     float HeightAboveSea = -(Base - Terrain);  // Positive = above sea
    //     if (HeightAboveSea < -Params.SeaLevel)
    //     {
    //         Density = -1.0f;  // Solid below sea
    //     }
    // }

    return Density;
}


float FDensityGenerator::SampleBaseSphere(const FVector &WorldPosition) const
{
    // Signed distance to ideal sphere
    float Distance = WorldPosition.Size();

    // Solid core (optional)
    if (Params.CoreRadius > 0.0f)
    {
        // Sphere with solid core: negative inside core
        return FMath::Max(Distance - Params.PlanetRadius, Params.CoreRadius - Distance);
    }

    return Distance - Params.PlanetRadius;
}


float FDensityGenerator::SampleTerrain(const FVector &WorldPosition) const { return ComputeTerrainDisplacement(WorldPosition); }


float FDensityGenerator::ComputeTerrainDisplacement(const FVector &WorldPosition) const
{
    if (!TerrainNoise.IsValid() || Params.TerrainNoiseAmplitude <= 0.0f)
    {
        return 0.0f;
    }

    FDensityContext Context = CreateContext(WorldPosition);

    // Sample fractal noise
    float Noise = TerrainNoise->SampleFractal(Context,
                                              Params.TerrainNoiseFrequency,
                                              4,     // Octaves
                                              0.5f,  // Persistence
                                              2.0f   // Lacunarity
    );

    return Noise * Params.TerrainNoiseAmplitude;
}


FDensityContext FDensityGenerator::CreateContext(const FVector &WorldPosition) const
{
    FDensityContext Context;
    Context.WorldPosition = WorldPosition;
    Context.PlanetRadius = Params.PlanetRadius;
    Context.PlanetSeed = PlanetSeed;
    Context.TerrainAmplitude = Params.TerrainNoiseAmplitude;
    Context.SeaLevel = Params.SeaLevel;
    return Context;
}


float FDensityGenerator::ComputeCaveDensity(const FVector &WorldPosition) const
{
    if (!CaveNoise.IsValid())
    {
        return 1.0f;  // No caves
    }

    FDensityContext Context = CreateContext(WorldPosition);

    float CaveNoiseValue = CaveNoise->SampleFractal(Context,
                                                    Params.CaveFrequency,
                                                    3,     // Octaves
                                                    0.7f,  // Persistence
                                                    1.8f   // Lacunarity
    );

    // Threshold to create caves
    // CaveNoiseValue > Threshold = empty space (positive density)
    // CaveNoiseValue < Threshold = solid (negative density)
    return (CaveNoiseValue - Params.CaveThreshold) * 10.0f;
}