#pragma once

#include "CoreMinimal.h"

#include "../Utils/MathUtils.h"
#include "DensityGenerator.h"


// Stateless utility class for generating mesh data from density fields.
class MeshGenerator
{
    public:
        // Generates mesh data from density data using Marching Cubes.
        // Thread-safe.
        static ChunkMeshData GenerateMesh(const GenData &GenData, int32 Resolution, const FTransform &ChunkTransform, const FTransform &PlanetTransform,
                                          int32 LODLevel, const DensityGenerator &DensityGen);
};