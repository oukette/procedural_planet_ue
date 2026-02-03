#pragma once

#include "CoreMinimal.h"
#include "Math/Vector.h"
#include "Math/Vector2D.h"
#include "ProceduralMeshComponent.h"


/**
 * Container for generated mesh data.
 * Thread-safe, plain data, no engine dependencies.
 */
struct PROCEDURALPLANET_API FChunkMeshData
{
        // Vertex data
        TArray<FVector> Vertices;
        TArray<FVector> Normals;
        TArray<FVector2D> UVs;
        TArray<FProcMeshTangent> Tangents;

        // Topology
        TArray<int32> Triangles;

        // Bounds (optional, for culling)
        FVector BoundsMin;
        FVector BoundsMax;

        FChunkMeshData() :
            BoundsMin(FLT_MAX, FLT_MAX, FLT_MAX),
            BoundsMax(-FLT_MAX, -FLT_MAX, -FLT_MAX)
        {
        }

        // Utility functions
        void Clear();
        void CalculateBounds();
        int32 GetVertexCount() const { return Vertices.Num(); }
        int32 GetTriangleCount() const { return Triangles.Num() / 3; }
        bool IsValid() const { return Vertices.Num() > 0 && Triangles.Num() > 0; }

        // Memory usage (for debugging)
        int32 EstimateMemoryBytes() const;
};