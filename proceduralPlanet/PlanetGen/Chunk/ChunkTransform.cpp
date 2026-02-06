#include "ChunkTransform.h"


FChunkTransform::FChunkTransform(const FVector &PlanetCenter, float PlanetRadius, uint8 Face, const FIntVector &ChunkCoords, int32 InLOD) :
    CubeNormal(FPlanetMath::CubeFaceNormals[Face]),
    LOD(InLOD)
{
    check(Face < FPlanetMath::FaceCount);

    // Calculate chunk size based on LOD
    // At LOD 0, each face is divided into 1 chunk
    // At LOD 1, each face is divided into 2x2 = 4 chunks
    // At LOD 2, each face is divided into 4x4 = 16 chunks, etc.
    int32 ChunksPerFaceEdge = FMath::Pow(2, LOD);  // 1, 2, 4, 8...

    // Each face spans 2 units in UV space (from -1 to +1)
    // So each chunk spans: 2.0 / ChunksPerFaceEdge in UV space
    float ChunkUVSize = 2.0f / ChunksPerFaceEdge;

    // In world space, we need to map this to actual meters
    // The arc length of a quarter sphere (one face) is approximately: PlanetRadius * (PI/2)
    // But since we're using a cube projection, we use the cube face size instead
    // A cube inscribed in a sphere of radius R has face size: 2R/sqrt(3)
    // However, for simplicity and consistency with your marching cubes test,
    // we'll use a direct mapping based on the number of chunks

    // SIMPLIFIED APPROACH (matches your TestMarchingCubesChunk logic):
    // Total face "size" in world units = 2 * PlanetRadius (cube face edge length)
    // ChunkWorldSize = (2 * PlanetRadius) / ChunksPerFaceEdge
    ChunkWorldSize = (2.0f * PlanetRadius) / ChunksPerFaceEdge;

    // Calculate UV coordinates of chunk center
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
    float TerrainMargin = ChunkWorldSize * 0.05f;  // 5% margin
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