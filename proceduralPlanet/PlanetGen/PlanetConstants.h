#pragma once

#include "CoreMinimal.h"


// Global constants for easy tuning and static access
struct FPlanetStatics
{
        // Ratio of RenderDistance where the Far Model takes over.
        // 1.0 = Exactly at RenderDistance.
        // 0.9 = Far model appears slightly before chunks disappear (smoother overlap).
        static constexpr float FarModelDistanceRatio = 1.0f;

        // Ratio of RenderDistance where the Far Model disappears (Getting Closer).
        // 0.8 = Far model stays visible until we are at 80% of render distance.
        static constexpr float FarModelHideRatio = 0.8f;

        // Generation / Grid
        static constexpr float DefaultEngineSphereRadius = 50.0f;
        static constexpr float TargetAutoChunkSize = 8000.0f;
        static constexpr float FarDistanceSafetyMargin = 1.1f;

        // Culling & Visibility
        static constexpr float UndergroundThreshold = -100.0f;
        static constexpr float HorizonCullingDot = -0.5f;
        static constexpr float FrustumCullingDot = -0.5f;
        static constexpr float GridDebugRadiusScale = 1.002f;

        // Debug
        static constexpr int32 DebugSphereSegments = 32;
        static constexpr float DebugSphereLifetime = 60.0f;
        static constexpr float DebugSphereThickness = 20.0f;
        static constexpr float DebugLineLifetime = 30.0f;
        static constexpr float DebugBoxLifetime = 20.0f;
        static constexpr int32 DebugKey_ManagerStats = 10;
        static constexpr int32 DebugKey_DistanceInfo = 101;
        static constexpr int32 DebugKey_PredictionInfo = 102;
        static constexpr int32 DebugKey_LODBreakdown = 103;
        static constexpr int32 DebugKey_LODThreshold = 104;
};


// Automatic Debug Colors for multiple LODs.
const static TArray<FColor> LODColorsDebug = {
    FColor::Green,        // LOD 0
    FColor(128, 255, 0),  // LOD 1 (smooth green)
    FColor::Yellow,       // LOD 2
    FColor(0xF75B00),     // LOD 3 (Orange)
    FColor::Red,          // LOD 4
    FColor::Magenta,      // LOD 5
    FColor(128, 0, 255),  // LOD 6 (Purple)
    FColor(0, 255, 128),  // LOD 7 (Spring Green)
    FColor::Cyan          // LOD 8
};