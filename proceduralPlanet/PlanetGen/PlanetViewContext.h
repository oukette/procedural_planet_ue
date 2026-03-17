#pragma once

#include "CoreMinimal.h"


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
