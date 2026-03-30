#pragma once

#include "Math/Vector.h"
#include "Math/MathFwd.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "ConvexVolume.h"

#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"


class APlanet;
class APlayerCameraManager;


// Context provided to the Manager to evaluate LODs and visibility
struct PlanetViewContext
{
    public:
        FVector ObserverLocation = FVector::ZeroVector;
        FVector ObserverForward = FVector::ZeroVector;
        FVector ObserverVelocity = FVector::ZeroVector;

        float ViewDistance;
        float AltitudeAboveSurface = 0.f;  // Negative when underground. Only valid when near planet (within FarDistanceThreshold).

        float VerticalFOVRadians = FMath::DegreesToRadians(90.f);
        FConvexVolume ViewFrustum;

        // Factory methods
        static PlanetViewContext BuildWorldContext(const AActor *PlanetActor);

        // Transform a world-space context into planet-local space, and compute altitude.
        // FarDistanceThreshold and PlanetRadius are taken from the caller's config to avoid a circular dependency on FPlanetConfig.
        static PlanetViewContext BuildLocalContext(const PlanetViewContext &World, const AActor *PlanetActor, float PlanetRadius, float FarDistanceThreshold,
                                                   float FarDistanceSafetyMargin);

    private:
        // Compute the view frustrum in planet-local space.
        static void ApplyViewFrustum(APlayerCameraManager *PCM, const AActor *PlanetActor, PlanetViewContext &Ctx);

        // Compute vertical FOV from the horizontal FOV and viewport aspect ratio.
        static void ApplyVerticalFOV(APlayerCameraManager *PCM, PlanetViewContext &Ctx);

        // Compute the aspect ratio of the viewport.
        static float GetViewportAspectRatio(FIntPoint ViewportSize = FIntPoint(0, 0));
};


inline PlanetViewContext PlanetViewContext::BuildWorldContext(const AActor *PlanetActor)
{
    PlanetViewContext Ctx;

    if (!PlanetActor || !PlanetActor->GetWorld())
        return Ctx;

    UWorld *World = PlanetActor->GetWorld();

    // Observer location — works in both Editor viewports and Runtime
    if (World->ViewLocationsRenderedLastFrame.Num() > 0)
        Ctx.ObserverLocation = World->ViewLocationsRenderedLastFrame[0];

    if (APlayerCameraManager *PCM = UGameplayStatics::GetPlayerCameraManager(World, 0))
    {
        Ctx.ObserverForward = PCM->GetCameraRotation().Vector();
        ApplyVerticalFOV(PCM, Ctx);
        ApplyViewFrustum(PCM, PlanetActor, Ctx);
    }

    if (APawn *Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
    {
        if (IsValid(Pawn))
            Ctx.ObserverVelocity = Pawn->GetVelocity();
    }

    return Ctx;
}


inline PlanetViewContext PlanetViewContext::BuildLocalContext(const PlanetViewContext &World, const AActor *PlanetActor, float PlanetRadius,
                                                              float FarDistanceThreshold, float FarDistanceSafetyMargin)
{
    PlanetViewContext Local;

    if (!PlanetActor)
        return Local;

    const FTransform T = PlanetActor->GetActorTransform();

    Local.ObserverLocation = T.InverseTransformPosition(World.ObserverLocation);
    Local.ObserverForward = T.InverseTransformVector(World.ObserverForward);
    Local.ObserverVelocity = T.InverseTransformVector(World.ObserverVelocity);
    Local.ViewDistance = World.ViewDistance;
    Local.VerticalFOVRadians = World.VerticalFOVRadians;
    Local.ViewFrustum = World.ViewFrustum;

    const float DistToCenter = Local.ObserverLocation.Size();
    const bool bNearPlanet = DistToCenter < (FarDistanceThreshold * FarDistanceSafetyMargin);
    Local.AltitudeAboveSurface = bNearPlanet ? (DistToCenter - PlanetRadius) : 0.f;

    return Local;
}


inline void PlanetViewContext::ApplyVerticalFOV(APlayerCameraManager *PCM, PlanetViewContext &Ctx)
{
    if (!PCM)
        return;

    const float AspectRatio = GetViewportAspectRatio();
    const float HFOVRad = FMath::DegreesToRadians(PCM->GetFOVAngle());
    Ctx.VerticalFOVRadians = 2.f * FMath::Atan(FMath::Tan(HFOVRad * 0.5f) / AspectRatio);
}


inline void PlanetViewContext::ApplyViewFrustum(APlayerCameraManager *PCM, const AActor *PlanetActor, PlanetViewContext &Ctx)
{
    if (!PCM || !PlanetActor)
        return;

    if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->Viewport)
        return;

    const FIntPoint ViewportSize = GEngine->GameViewport->Viewport->GetSizeXY();

    // Degenerate viewport on first frame — skip rather than produce planes that cull everything
    if (ViewportSize.X == 0 || ViewportSize.Y == 0)
        return;

    FMinimalViewInfo CameraView;
    CameraView.Location = PCM->GetCameraLocation();
    CameraView.Rotation = PCM->GetCameraRotation();
    CameraView.FOV = PCM->GetFOVAngle();
    CameraView.ProjectionMode = ECameraProjectionMode::Perspective;
    CameraView.AspectRatio = GetViewportAspectRatio(ViewportSize);

    FMatrix ViewMatrix, ProjectionMatrix, ViewProjectionMatrix;
    UGameplayStatics::GetViewProjectionMatrix(CameraView, ViewMatrix, ProjectionMatrix, ViewProjectionMatrix);

    FConvexVolume WorldFrustum;
    GetViewFrustumBounds(WorldFrustum, ViewProjectionMatrix, false);

    // Transform frustum planes from world space into planet-local space.
    // Chunk centers are computed in local space, so the frustum must match.
    const FMatrix LocalMatrix = PlanetActor->GetActorTransform().ToMatrixWithScale().Inverse();
    Ctx.ViewFrustum.Planes.Empty(WorldFrustum.Planes.Num());
    for (const FPlane &WorldPlane : WorldFrustum.Planes)
        Ctx.ViewFrustum.Planes.Add(WorldPlane.TransformBy(LocalMatrix));

    Ctx.ViewFrustum.Init();
}


inline float PlanetViewContext::GetViewportAspectRatio(FIntPoint ViewportSize)
{
    if (ViewportSize.X == 0 || ViewportSize.Y == 0)
    {
        if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
            ViewportSize = GEngine->GameViewport->Viewport->GetSizeXY();
    }

    return (ViewportSize.X > 0 && ViewportSize.Y > 0) ? (float)ViewportSize.X / (float)ViewportSize.Y : 1.777f;  // fallback 16:9
}