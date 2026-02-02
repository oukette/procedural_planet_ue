#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "Math/UnrealMathUtility.h"


/**
 * Pure deterministic math utilities for cube-sphere projection and planetary coordinate systems.
 * All functions are thread-safe, side-effect free, and use double precision where needed.
 */
class PROCEDURALPLANET_API FPlanetMath
{
    public:
        // ==================== Cube Face Enum & Constants ====================
        enum ECubeFace : uint8
        {
            FaceX_Pos = 0,
            FaceX_Neg,
            FaceY_Pos,
            FaceY_Neg,
            FaceZ_Pos,
            FaceZ_Neg,
            FaceCount
        };

        // Cube face normals in double precision
        static const FVector CubeFaceNormals[FaceCount];

        // Cube face tangent and bitangent vectors
        static const FVector CubeFaceTangents[FaceCount];
        static const FVector CubeFaceBitangents[FaceCount];

        // ==================== Core Projection Functions ====================

        /**
         * Projects a point from cube face coordinates (face UV + face index) to unit sphere surface.
         * Input: face [0..5], u,v in [-1, 1] range
         * Output: normalized direction vector from sphere center
         */
        static FVector CubeFaceToSphere(uint8 Face, float U, float V);

        /**
         * Projects a point from unit sphere surface to cube face coordinates.
         * Input: normalized direction vector
         * Output: face index, and u,v in [-1, 1] range
         */
        static void SphereToCubeFace(const FVector &SphereDir, uint8 &OutFace, float &OutU, float &OutV);

        /**
         * Alternative: direct cube face to sphere using position vector.
         * Input: position on cube face in local cube space (not necessarily normalized)
         * Output: position projected onto sphere (radius = 1)
         */
        static FVector CubePointToSphere(const FVector &CubePoint);

        /**
         * Alternative: sphere to cube point (intersection with cube)
         */
        static FVector SpherePointToCube(const FVector &SphereDir);

        // ==================== Coordinate Transformations ====================

        /**
         * Converts chunk-local grid coordinates to world space coordinates.
         * Input: chunk origin (world), face normal, local offset in meters
         * Output: world position
         */
        static FVector LocalToWorld(const FVector &ChunkOrigin, const FVector &FaceNormal, const FVector &LocalOffset);

        /**
         * Converts world position to chunk-local offset.
         * Inverse of LocalToWorld.
         */
        static FVector WorldToLocal(const FVector &WorldPos, const FVector &ChunkOrigin, const FVector &FaceNormal);

        /**
         * Gets the cube face index for a given direction vector.
         * Returns the face with the largest absolute component.
         */
        static uint8 GetDominantFace(const FVector &Direction);

        /**
         * Computes UV coordinates on a cube face from a direction vector.
         * Assumes the direction is already normalized to the dominant face.
         */
        static void GetFaceUV(const FVector &Direction, uint8 Face, double &OutU, double &OutV);

        // ==================== Precision Helpers ====================

        /**
         * Safe normalize with double precision intermediate.
         */
        static FVector SafeNormalize(const FVector &V, float Tolerance = 1e-6f);

        /**
         * Double-precision dot product (for critical precision paths).
         */
        static double DotProduct64(const FVector &A, const FVector &B);

        /**
         * Double-precision cross product.
         */
        static FVector CrossProduct64(const FVector &A, const FVector &B);

        /**
         * Linear interpolation for double precision
         */
        static float Lerp(float A, float B, float Alpha);

        /**
         * Clamps a value to a range for double precision
         */
        static float Clamp(float Value, float Min, float Max);


        // ==================== Spherical Math ====================

        /**
         * Signed distance to ideal sphere surface.
         * Positive = outside, Negative = inside.
         */
        static float SignedDistanceToSphere(const FVector &Point, float Radius);

        /**
         * Convert spherical coordinates (radius, theta, phi) to Cartesian.
         */
        static FVector SphericalToCartesian(float Radius, float Theta, float Phi);

        /**
         * Convert Cartesian to spherical coordinates.
         */
        static void CartesianToSpherical(const FVector &Point, float &OutRadius, float &OutTheta, float &OutPhi);

        // ==================== Validation & Debug ====================

        /**
         * Returns true if a sphere direction vector is valid (normalized within tolerance)
         */
        static bool IsValidSphereDirection(const FVector &Dir, double Tolerance = 1e-6);

        /**
         * Computes the maximum stretching factor for cube-to-sphere projection
         * at given UV coordinates (useful for LOD adjustment)
         */
        static double ComputeStretchFactor(uint8 Face, double U, double V);

        /**
         * Returns the edge length of a cube face on a sphere of given radius.
         */
        static float GetFaceEdgeLength(float SphereRadius);

        /**
         * Returns the approximate surface area of a cube face on a sphere.
         */
        static float GetFaceSurfaceArea(float SphereRadius);
};