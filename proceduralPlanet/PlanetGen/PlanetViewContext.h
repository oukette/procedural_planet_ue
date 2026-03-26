#pragma once

#include "Math/Vector.h"
#include "Math/MathFwd.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "ConvexVolume.h"


// Context provided to the Manager to evaluate LODs and visibility
struct PlanetViewContext
{
        FVector ObserverLocation = FVector::ZeroVector;
        FVector ObserverForward = FVector::ZeroVector;
        FVector ObserverVelocity = FVector::ZeroVector;
        float ViewDistance;
        float VerticalFOVRadians = FMath::DegreesToRadians(90.f);
        float AltitudeAboveSurface = 0.f;  // Negative when underground. Only valid when near planet (within FarDistanceThreshold).

        FConvexVolume ViewFrustum;
};
