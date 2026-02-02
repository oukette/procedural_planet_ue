#include "MathUtils.h"
#include <cmath>


const FVector FPlanetMath::CubeFaceNormals[FaceCount] = {
    FVector(1, 0, 0),   // FaceX_Pos
    FVector(-1, 0, 0),  // FaceX_Neg
    FVector(0, 1, 0),   // FaceY_Pos
    FVector(0, -1, 0),  // FaceY_Neg
    FVector(0, 0, 1),   // FaceZ_Pos
    FVector(0, 0, -1)   // FaceZ_Neg
};

const FVector FPlanetMath::CubeFaceTangents[FaceCount] = {
    FVector(0, 0, -1),  // FaceX_Pos
    FVector(0, 0, 1),   // FaceX_Neg
    FVector(1, 0, 0),   // FaceY_Pos
    FVector(1, 0, 0),   // FaceY_Neg
    FVector(1, 0, 0),   // FaceZ_Pos
    FVector(-1, 0, 0)   // FaceZ_Neg
};

const FVector FPlanetMath::CubeFaceBitangents[FaceCount] = {
    FVector(0, 1, 0),   // FaceX_Pos
    FVector(0, 1, 0),   // FaceX_Neg
    FVector(0, 0, 1),   // FaceY_Pos
    FVector(0, 0, -1),  // FaceY_Neg
    FVector(0, 1, 0),   // FaceZ_Pos
    FVector(0, 1, 0)    // FaceZ_Neg
};


FVector FPlanetMath::CubeFaceToSphere(uint8 Face, float U, float V)
{
    check(Face < FaceCount);

    // Clamp UV to safe range to avoid numerical issues at edges
    U = Clamp(U, -1.0, 1.0);
    V = Clamp(V, -1.0, 1.0);

    // Start from cube face center and apply UV offsets
    FVector CubePoint = CubeFaceNormals[Face];
    CubePoint += CubeFaceTangents[Face] * U;
    CubePoint += CubeFaceBitangents[Face] * V;

    // Project onto sphere (normalize)
    double Length = std::sqrt(CubePoint.X * CubePoint.X + CubePoint.Y * CubePoint.Y + CubePoint.Z * CubePoint.Z);

    if (Length > 0.0)
    {
        return CubePoint / Length;
    }

    return CubeFaceNormals[Face];  // Fallback to face center
}


void FPlanetMath::SphereToCubeFace(const FVector &SphereDir, uint8 &OutFace, float &OutU, float &OutV)
{
    // Handle non-normalized vectors gracefully
    if (!IsValidSphereDirection(SphereDir))
    {
        // If it's zero or tiny, default to +X face center
        if (SphereDir.SizeSquared() < 1e-12f)
        {
            OutFace = FaceX_Pos;
            OutU = OutV = 0.0f;
            return;
        }

        // Otherwise normalize it
        FVector Normalized = SphereDir.GetSafeNormal();
        return SphereToCubeFace(Normalized, OutFace, OutU, OutV);
    }

    // Find dominant face
    OutFace = GetDominantFace(SphereDir);

    // Compute intersection with cube plane using double precision
    float AbsComponent = FMath::Abs(SphereDir[OutFace / 2]);  // 0:X,1:X,2:Y,3:Y,4:Z,5:Z
    if (AbsComponent < 1e-6f)
    {
        // Near zero, use fallback
        OutU = OutV = 0.0f;
        return;
    }

    float Scale = 1.0f / AbsComponent;
    FVector CubePoint = SphereDir * Scale;

    // Extract UV coordinates
    OutU = FVector::DotProduct(CubePoint, CubeFaceTangents[OutFace]);
    OutV = FVector::DotProduct(CubePoint, CubeFaceBitangents[OutFace]);

    // Clamp
    OutU = Clamp(OutU, -1.0f, 1.0f);
    OutV = Clamp(OutV, -1.0f, 1.0f);
}


FVector FPlanetMath::CubePointToSphere(const FVector &CubePoint) { return CubePoint.GetSafeNormal(); }


FVector FPlanetMath::SpherePointToCube(const FVector &SphereDir)
{
    if (!IsValidSphereDirection(SphereDir))
    {
        // Return a default point on +X face
        return CubeFaceNormals[FaceX_Pos];
    }

    uint8 Face = GetDominantFace(SphereDir);
    float Scale = 1.0 / std::abs(SphereDir[Face / 2]);

    return SphereDir * Scale;
}


FVector FPlanetMath::LocalToWorld(const FVector &ChunkOrigin, const FVector &FaceNormal, const FVector &LocalOffset)
{
    // Convert local offset (in face tangent/bitangent space) to world space
    uint8 Face = GetDominantFace(FaceNormal);

    FVector Tangent = CubeFaceTangents[Face];
    FVector Bitangent = CubeFaceBitangents[Face];

    // LocalOffset is assumed to be in tangent/bitangent/normal space
    FVector WorldOffset = Tangent * LocalOffset.X + Bitangent * LocalOffset.Y + FaceNormal * LocalOffset.Z;

    return ChunkOrigin + WorldOffset;
}


FVector FPlanetMath::WorldToLocal(const FVector &WorldPos, const FVector &ChunkOrigin, const FVector &FaceNormal)
{
    uint8 Face = GetDominantFace(FaceNormal);

    FVector Tangent = CubeFaceTangents[Face];
    FVector Bitangent = CubeFaceBitangents[Face];

    FVector Relative = WorldPos - ChunkOrigin;

    return FVector(FVector::DotProduct(Relative, Tangent), FVector::DotProduct(Relative, Bitangent), FVector::DotProduct(Relative, FaceNormal));
}


uint8 FPlanetMath::GetDominantFace(const FVector &Direction)
{
    double AbsX = std::abs(Direction.X);
    double AbsY = std::abs(Direction.Y);
    double AbsZ = std::abs(Direction.Z);

    if (AbsX >= AbsY && AbsX >= AbsZ)
    {
        return (Direction.X >= 0) ? FaceX_Pos : FaceX_Neg;
    }
    else if (AbsY >= AbsZ)
    {
        return (Direction.Y >= 0) ? FaceY_Pos : FaceY_Neg;
    }
    else
    {
        return (Direction.Z >= 0) ? FaceZ_Pos : FaceZ_Neg;
    }
}


void FPlanetMath::GetFaceUV(const FVector &Direction, uint8 Face, double &OutU, double &OutV)
{
    check(Face < FaceCount);

    double Scale = 1.0 / std::abs(Direction[Face / 2]);
    FVector CubePoint = Direction * Scale;

    OutU = FVector::DotProduct(CubePoint, CubeFaceTangents[Face]);
    OutV = FVector::DotProduct(CubePoint, CubeFaceBitangents[Face]);

    OutU = Clamp(OutU, -1.0, 1.0);
    OutV = Clamp(OutV, -1.0, 1.0);
}


FVector FPlanetMath::SafeNormalize(const FVector &V, float Tolerance)
{
    float SizeSq = V.SizeSquared();
    if (SizeSq > Tolerance)
    {
        return V * (1.0f / FMath::Sqrt(SizeSq));
    }
    return FVector::ZeroVector;
}


double FPlanetMath::DotProduct64(const FVector &A, const FVector &B)
{
    return (double)A.X * (double)B.X + (double)A.Y * (double)B.Y + (double)A.Z * (double)B.Z;
}


FVector FPlanetMath::CrossProduct64(const FVector &A, const FVector &B)
{
    double X = (double)A.Y * (double)B.Z - (double)A.Z * (double)B.Y;
    double Y = (double)A.Z * (double)B.X - (double)A.X * (double)B.Z;
    double Z = (double)A.X * (double)B.Y - (double)A.Y * (double)B.X;

    return FVector((float)X, (float)Y, (float)Z);
}


float FPlanetMath::Lerp(float A, float B, float Alpha) { return A + (B - A) * Alpha; }


float FPlanetMath::Clamp(float Value, float Min, float Max) { return FMath::Clamp(Value, Min, Max); }


float FPlanetMath::SignedDistanceToSphere(const FVector &Point, float Radius) { return Point.Size() - Radius; }


FVector FPlanetMath::SphericalToCartesian(float Radius, float Theta, float Phi)
{
    float SinTheta = FMath::Sin(Theta);
    return FVector(Radius * SinTheta * FMath::Cos(Phi), Radius * SinTheta * FMath::Sin(Phi), Radius * FMath::Cos(Theta));
}


void FPlanetMath::CartesianToSpherical(const FVector &Point, float &OutRadius, float &OutTheta, float &OutPhi)
{
    OutRadius = Point.Size();

    if (OutRadius > 1e-6f)
    {
        OutTheta = FMath::Acos(Point.Z / OutRadius);
        OutPhi = FMath::Atan2(Point.Y, Point.X);
    }
    else
    {
        OutTheta = OutPhi = 0.0f;
    }
}


bool FPlanetMath::IsValidSphereDirection(const FVector &Dir, double Tolerance)
{
    double LengthSq = Dir.X * Dir.X + Dir.Y * Dir.Y + Dir.Z * Dir.Z;
    return std::abs(LengthSq - 1.0) < Tolerance;
}


double FPlanetMath::ComputeStretchFactor(uint8 Face, double U, double V)
{
    // The stretch factor is the ratio of sphere surface area to cube face area
    // at a given UV coordinate. Approximate using the Jacobian of the projection.

    // For a point on cube face (1, u, v), the sphere point is (1, u, v)/sqrt(1+u²+v²)
    // The stretch factor is sqrt(1 + u² + v²) / (1 + u² + v²)

    double USq = U * U;
    double VSq = V * V;
    double Denom = 1.0 + USq + VSq;

    if (Denom > 0.0)
    {
        float Stretch = FMath::Sqrt((1.0f + USq) * (1.0f + VSq)) / Denom;
        return Stretch;
    }

    return 1.0;
}


float FPlanetMath::GetFaceEdgeLength(float SphereRadius)
{
    // Edge length of a cube face when projected onto sphere
    return SphereRadius * PI / 2.0f;  // Approximately
}


float FPlanetMath::GetFaceSurfaceArea(float SphereRadius)
{
    // Surface area of one cube face on sphere
    return 4.0f * PI * SphereRadius * SphereRadius / 6.0f;
}
