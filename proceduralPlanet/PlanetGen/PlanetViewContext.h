#pragma once

#include "Math/Vector.h"
#include "Math/MathFwd.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "ConvexVolume.h"


// Context provided to the Manager to evaluate LODs and visibility
struct PlanetViewContext
{
        FVector ObserverLocation;
        FVector ObserverForward;
        FVector ObserverVelocity;
        float ViewDistance;
        float VerticalFOVRadians = FMath::DegreesToRadians(90.f);

        FConvexVolume ViewFrustum;
};
