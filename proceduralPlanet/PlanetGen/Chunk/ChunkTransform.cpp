#include "ChunkTransform.h"


FChunkTransform::FChunkTransform(const FVector &PlanetCenter, float PlanetRadius, uint8 Face, const FIntVector &ChunkCoords, int32 InLOD) :
    CubeNormal(FPlanetMath::CubeFaceNormals[Face]),
    LOD(InLOD)
{
    check(Face < FPlanetMath::FaceCount);

    // Calculate chunk size based on LOD
    // At LOD 0, the face is divided into 1 chunk
    // Each LOD increase quadruples the number of chunks
    int32 ChunksPerFace = FMath::Pow(2, LOD);
    ChunkWorldSize = (PlanetRadius * PI) / ChunksPerFace;  // Approximate

    // Calculate UV coordinates of chunk center
    // UV range is [-1, 1] across the entire face
    float FaceSize = 2.0f;  // UV space from -1 to 1
    float ChunkUVSize = FaceSize / ChunksPerFace;

    float U = (ChunkCoords.X + 0.5f) * ChunkUVSize - 1.0f;  // Center of chunk in UV space
    float V = (ChunkCoords.Y + 0.5f) * ChunkUVSize - 1.0f;

    // Convert UV to sphere direction using spherified mapping
    FVector SphereDir = FPlanetMath::CubeFaceToSphere(Face, U, V);

    // Scale to planet surface and offset from planet center
    WorldOrigin = PlanetCenter + SphereDir * PlanetRadius;
}


bool FChunkTransform::IsValid() const
{
    return CubeNormal.SizeSquared() > 0.9f &&  // Should be normalized
           ChunkWorldSize > 0.0f && LOD >= 0;
}


FString FChunkTransform::ToString() const
{
    return FString::Printf(TEXT("Origin=%s Normal=%s Size=%.1fm LOD=%d"), *WorldOrigin.ToString(), *CubeNormal.ToString(), ChunkWorldSize, LOD);
}


FVector FChunkTransform::LocalToWorld(const FVector &LocalPosition) const
{
    // Get tangent and bitangent for this face
    uint8 Face = FPlanetMath::GetDominantFace(CubeNormal);
    FVector Tangent = FPlanetMath::CubeFaceTangents[Face];
    FVector Bitangent = FPlanetMath::CubeFaceBitangents[Face];

    // Transform from local to world
    return WorldOrigin + Tangent * LocalPosition.X + Bitangent * LocalPosition.Y + CubeNormal * LocalPosition.Z;
}


FVector FChunkTransform::WorldToLocal(const FVector &WorldPosition) const
{
    uint8 Face = FPlanetMath::GetDominantFace(CubeNormal);
    FVector Tangent = FPlanetMath::CubeFaceTangents[Face];
    FVector Bitangent = FPlanetMath::CubeFaceBitangents[Face];

    FVector Relative = WorldPosition - WorldOrigin;

    return FVector(FVector::DotProduct(Relative, Tangent), FVector::DotProduct(Relative, Bitangent), FVector::DotProduct(Relative, CubeNormal));
}


void FChunkTransform::GetWorldBounds(FVector &OutMin, FVector &OutMax) const
{
    // Chunk extends half size in tangent/bitangent directions
    float HalfSize = ChunkWorldSize * 0.5f;
    uint8 Face = FPlanetMath::GetDominantFace(CubeNormal);
    FVector Tangent = FPlanetMath::CubeFaceTangents[Face];
    FVector Bitangent = FPlanetMath::CubeFaceBitangents[Face];

    // Calculate corners
    FVector Corners[4] = {WorldOrigin + Tangent * HalfSize + Bitangent * HalfSize,
                          WorldOrigin + Tangent * HalfSize - Bitangent * HalfSize,
                          WorldOrigin - Tangent * HalfSize + Bitangent * HalfSize,
                          WorldOrigin - Tangent * HalfSize - Bitangent * HalfSize};

    // Initialize bounds
    OutMin = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
    OutMax = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    // Expand bounds to include all corners
    for (const FVector &Corner : Corners)
    {
        OutMin.X = FMath::Min(OutMin.X, Corner.X);
        OutMin.Y = FMath::Min(OutMin.Y, Corner.Y);
        OutMin.Z = FMath::Min(OutMin.Z, Corner.Z);

        OutMax.X = FMath::Max(OutMax.X, Corner.X);
        OutMax.Y = FMath::Max(OutMax.Y, Corner.Y);
        OutMax.Z = FMath::Max(OutMax.Z, Corner.Z);
    }

    // Account for terrain displacement (add some margin)
    float TerrainMargin = ChunkWorldSize * 0.2f;  // 20% margin
    OutMin -= FVector(TerrainMargin, TerrainMargin, TerrainMargin);
    OutMax += FVector(TerrainMargin, TerrainMargin, TerrainMargin);
}


bool FChunkTransform::ContainsWorldPosition(const FVector &WorldPosition, float Margin = 0.0f) const
{
    FVector LocalPos = WorldToLocal(WorldPosition);
    float HalfSizeWithMargin = ChunkWorldSize * 0.5f + Margin;

    return FMath::Abs(LocalPos.X) <= HalfSizeWithMargin && FMath::Abs(LocalPos.Y) <= HalfSizeWithMargin &&
           FMath::Abs(LocalPos.Z) <= ChunkWorldSize;  // Z can extend further for terrain
}


FTransform FChunkTransform::GetDebugTransform() const
{
    // Create rotation from normal
    FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, CubeNormal);
    return FTransform(Rotation, WorldOrigin, FVector::OneVector);
}