#pragma once

#include "CoreMinimal.h"


/**
 * Pure math utilities for Cube-to-Sphere projection and spatial transformations.
 * These functions are stateless and thread-safe.
 */
class PROCEDURALPLANET_API FMathUtils
{
    public:
        /**
         *  Projects a point from a unit cube [-1, 1] to a unit sphere.
         *  Uses the "Modified Cube-to-Sphere Mapping" to maintain better area distribution.
         */
        static FVector CubeToSphere(const FVector &p)
        {
            // Spherified Cube mapping for equal-area distribution
            // Reference: http://mathproofs.blogspot.com/2005/07/mapping-cube-to-sphere.html

            float x2 = p.X * p.X;
            float y2 = p.Y * p.Y;
            float z2 = p.Z * p.Z;

            return FVector(p.X * FMath::Sqrt(1.0f - (y2 / 2.0f) - (z2 / 2.0f) + (y2 * z2 / 3.0f)),
                           p.Y * FMath::Sqrt(1.0f - (z2 / 2.0f) - (x2 / 2.0f) + (z2 * x2 / 3.0f)),
                           p.Z * FMath::Sqrt(1.0f - (x2 / 2.0f) - (y2 / 2.0f) + (x2 * y2 / 3.0f)));
        }

        /** Calculates the normalized direction vector for a given face index (0-5) */
        static FVector GetFaceNormal(uint8 FaceIndex)
        {
            switch (FaceIndex)
            {
                case 0:
                    return FVector::ForwardVector;  // +X
                case 1:
                    return FVector::BackwardVector;  // -X
                case 2:
                    return FVector::RightVector;  // +Y
                case 3:
                    return FVector::LeftVector;  // -Y
                case 4:
                    return FVector::UpVector;  // +Z
                case 5:
                    return FVector::DownVector;  // -Z
                default:
                    return FVector::UpVector;
            }
        }

        /** Returns the 'Right' vector for a given face to build a local coordinate system */
        static FVector GetFaceRight(uint8 FaceIndex)
        {
            switch (FaceIndex)
            {
                case 0:
                    return FVector::RightVector;  // +X
                case 1:
                    return FVector::LeftVector;  // -X
                case 2:
                    return FVector::BackwardVector;  // +Y
                case 3:
                    return FVector::ForwardVector;  // -Y
                case 4:
                    return FVector::RightVector;  // +Z
                case 5:
                    return FVector::RightVector;  // -Z
                default:
                    return FVector::RightVector;
            }
        }

        /** Returns the 'Up' vector for a given face to build a local coordinate system */
        static FVector GetFaceUp(uint8 FaceIndex)
        {
            switch (FaceIndex)
            {
                case 0:
                    return FVector::UpVector;  // +X
                case 1:
                    return FVector::UpVector;  // -X
                case 2:
                    return FVector::UpVector;  // +Y
                case 3:
                    return FVector::UpVector;  // -Y
                case 4:
                    return FVector::BackwardVector;  // +Z
                case 5:
                    return FVector::ForwardVector;  // -Z
                default:
                    return FVector::UpVector;
            }
        }

        /**
         ** Interpolates between UVMin and UVMax based on local chunk grid coordinates
         *  to find the specific cube-surface position before projection.
         */
        static FVector GetCubeSurfacePosition(const FIntVector &GridCoords, int32 Resolution, const FVector &Normal, const FVector &Right, const FVector &Up,
                                              const FVector2D &UVMin, const FVector2D &UVMax)
        {
            float u = FMath::Lerp(UVMin.X, UVMax.X, (float)GridCoords.X / Resolution);
            float v = FMath::Lerp(UVMin.Y, UVMax.Y, (float)GridCoords.Y / Resolution);
            
            // Map 0..1 to the -1..1 range expected by the CubeSphere projection
            float uScaled = u * 2.0f - 1.0f;
            float vScaled = v * 2.0f - 1.0f;
            
            // Combine into a point on the cube face
            return Normal + (Right * u) + (Up * v);
        }
};