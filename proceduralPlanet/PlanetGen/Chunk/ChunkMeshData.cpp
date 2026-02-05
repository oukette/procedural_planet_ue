#include "ChunkMeshData.h"
#include "ProceduralMeshComponent.h"


void FChunkMeshData::Clear()
{
    Vertices.Empty();
    Normals.Empty();
    UVs.Empty();
    Tangents.Empty();
    Triangles.Empty();
    BoundsMin = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
    BoundsMax = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);
}


void FChunkMeshData::CalculateBounds()
{
    if (Vertices.Num() == 0)
    {
        BoundsMin = BoundsMax = FVector::ZeroVector;
        return;
    }

    BoundsMin = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
    BoundsMax = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    for (const FVector &Vertex : Vertices)
    {
        BoundsMin.X = FMath::Min(BoundsMin.X, Vertex.X);
        BoundsMin.Y = FMath::Min(BoundsMin.Y, Vertex.Y);
        BoundsMin.Z = FMath::Min(BoundsMin.Z, Vertex.Z);

        BoundsMax.X = FMath::Max(BoundsMax.X, Vertex.X);
        BoundsMax.Y = FMath::Max(BoundsMax.Y, Vertex.Y);
        BoundsMax.Z = FMath::Max(BoundsMax.Z, Vertex.Z);
    }
}


int32 FChunkMeshData::EstimateMemoryBytes() const
{
    int32 Bytes = 0;
    Bytes += Vertices.GetAllocatedSize();
    Bytes += Normals.GetAllocatedSize();
    Bytes += UVs.GetAllocatedSize();
    Bytes += Tangents.GetAllocatedSize();
    Bytes += Triangles.GetAllocatedSize();
    return Bytes;
}