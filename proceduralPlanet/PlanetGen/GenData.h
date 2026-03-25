#pragma once

#include "Math/Vector.h"
#include "Containers/Array.h"


// Container for generated field data to avoid re-calculating positions
struct GenData
{
        TArray<float> Densities;
        TArray<FVector> Positions;
        int32 SampleCount = 0;
};