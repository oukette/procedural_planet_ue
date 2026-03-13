#pragma once

#include "CoreMinimal.h"
#include "DataTypes.h"


// Pure math utilities for Cube-to-Sphere projection and spatial transformations.
// These functions are stateless and thread-safe.
class PROCEDURALPLANET_API FMathUtils
{
    public:
        // Projects a point from a unit cube [-1, 1] to a unit sphere.
        // Uses the "Modified Cube-to-Sphere Mapping" to maintain better area distribution.
        static FVector projectCubeToSphere(const FVector &p)
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

        // Calculates the normalized direction vector for a given face index (0-5)
        static FVector getFaceNormal(uint8 FaceIndex)
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

        // Returns the 'Right' vector for a given face to build a local coordinate system
        static FVector getFaceRight(uint8 FaceIndex)
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

        // Returns the 'Up' vector for a given face to build a local coordinate system
        static FVector getFaceUp(uint8 FaceIndex)
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

        // --- Chunk Geometry Helpers ---

        // Calculates the UV bounds (0..1) for a specific chunk ID
        static void GetChunkUVBounds(const FChunkId& Id, FVector2D& OutMin, FVector2D& OutMax)
        {
            float Step = 1.0f / (float)(1 << Id.LODLevel);
            OutMin = FVector2D(Id.Coords.X * Step, Id.Coords.Y * Step);
            OutMax = FVector2D((Id.Coords.X + 1) * Step, (Id.Coords.Y + 1) * Step);
        }

        // Calculates the world center of a chunk (useful for distance checks)
        static FVector GetChunkCenter(const FChunkId& Id, float PlanetRadius)
        {
            FVector2D UVMin, UVMax;
            GetChunkUVBounds(Id, UVMin, UVMax);
            FVector2D CenterUV = (UVMin + UVMax) * 0.5f;

            FVector Normal = getFaceNormal(Id.FaceIndex);
            FVector Right = getFaceRight(Id.FaceIndex);
            FVector Up = getFaceUp(Id.FaceIndex);

            // Map 0..1 UV to -1..1 Cube
            FVector CubePos = Normal + (Right * (CenterUV.X * 2.0f - 1.0f)) + (Up * (CenterUV.Y * 2.0f - 1.0f));
            return projectCubeToSphere(CubePos) * PlanetRadius;
        }

        // Calculates the full transform (Location, Rotation, Scale) for a chunk.
        static FChunkTransform ComputeChunkTransform(const FChunkId& Id, float PlanetRadius)
        {
            FVector Center = GetChunkCenter(Id, PlanetRadius);
            
            // Calculate Rotation (Align Up with Sphere Normal at center)
            FVector ChunkUp = Center.GetSafeNormal();
            FVector FaceUp = getFaceUp(Id.FaceIndex);
            
            // Robust Cross Product to find Right vector
            FVector ChunkRight = FVector::CrossProduct(FaceUp, ChunkUp);
            if (ChunkRight.IsZero()) 
            {
                // Handle pole cases where FaceUp and ChunkUp are parallel
                ChunkRight = FVector::CrossProduct(getFaceRight(Id.FaceIndex), ChunkUp);
            }
            ChunkRight.Normalize();

            FVector ChunkForward = FVector::CrossProduct(ChunkUp, ChunkRight);
            FMatrix RotMatrix = FRotationMatrix::MakeFromXY(ChunkForward, ChunkRight);

            // Scale is purely based on LOD level
            float Scale = 1.0f / (float)(1 << Id.LODLevel);

            return FChunkTransform(Center, Scale, getFaceNormal(Id.FaceIndex), RotMatrix.ToQuat());
        }

        // Inverse lookup to find the Root Chunk (LOD 0) for a local position
        static FChunkId GetRootChunkIdAt(const FVector& LocalPosition)
        {
            // Logic moved from ChunkManager, simplified for LOD 0
            FVector AbsPos = LocalPosition.GetAbs();
            float MaxVal = AbsPos.GetMax();
            uint8 Face = 0;

            if (AbsPos.X == MaxVal) Face = (LocalPosition.X > 0) ? 0 : 1;
            else if (AbsPos.Y == MaxVal) Face = (LocalPosition.Y > 0) ? 2 : 3;
            else Face = (LocalPosition.Z > 0) ? 4 : 5;

            return FChunkId(Face, FIntVector(0, 0, 0), 0);
        }

        // Interpolates between UVMin and UVMax based on local chunk grid coordinates
        // to find the specific cube-surface position before projection.
        static FVector computeCubeSurfacePosition(const FIntVector &GridCoords, int32 Resolution, const FVector &Normal, const FVector &Right,
                                                  const FVector &Up, const FVector2D &UVMin, const FVector2D &UVMax)
        {
            float u = FMath::Lerp(UVMin.X, UVMax.X, (float)GridCoords.X / Resolution);
            float v = FMath::Lerp(UVMin.Y, UVMax.Y, (float)GridCoords.Y / Resolution);

            // Combine into a point on the cube face
            return Normal + (Right * u) + (Up * v);
        }


        static FVector computeVertexInterp(const FVector &P1, const FVector &P2, float D1, float D2)
        {
            const float Epsilon = 1e-6f;
            float Denom = D1 - D2;
            if (FMath::Abs(Denom) < Epsilon)
            {
                return (P1 + P2) * 0.5f;
            }
            float T = D1 / Denom;
            return P1 + T * (P2 - P1);
        }
};