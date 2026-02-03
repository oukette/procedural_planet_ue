#include "ChunkId.h"
#include <cstdint>
#include <string>


uint32 GetTypeHash(const FChunkId &Id)
{
    // Combine all components into a single hash
    uint32 Hash = FCrc::MemCrc32(&Id.CubeFace, sizeof(uint8));
    Hash = FCrc::MemCrc32(&Id.ChunkCoords, sizeof(FIntVector), Hash);
    Hash = FCrc::MemCrc32(&Id.LOD, sizeof(int32), Hash);
    return Hash;
}


FString FChunkId::ToString() const
{
    const TCHAR *FaceNames[6] = {TEXT("+X"), TEXT("-X"), TEXT("+Y"), TEXT("-Y"), TEXT("+Z"), TEXT("-Z")};
    return FString::Printf(TEXT("Face=%s Coords=(%d,%d,%d) LOD=%d"), FaceNames[CubeFace], ChunkCoords.X, ChunkCoords.Y, ChunkCoords.Z, LOD);
}


bool FChunkId::IsValid() const
{
    return CubeFace < FPlanetMath::FaceCount && ChunkCoords.X >= 0 && ChunkCoords.Y >= 0 && ChunkCoords.Z == 0 &&  // Z should always be 0 for 2D face grid
           LOD >= 0;
}


FChunkId FChunkId::FromWorldPosition(const FVector &WorldPosition, float PlanetRadius, int32 InLOD, float ChunkSize)
{
    // Convert world position to sphere direction
    FVector SphereDir = WorldPosition.GetSafeNormal();

    // Get cube face
    uint8 Face;
    float U, V;
    FPlanetMath::SphereToCubeFace(SphereDir, Face, U, V);

    // Convert UV to chunk coordinates
    // UV range is [-1, 1], we need to map to chunk grid
    float FaceSize = 2.0f;  // UV space from -1 to 1
    int32 ChunksPerFace = FMath::Pow(2, InLOD);
    float ChunkUVSize = FaceSize / ChunksPerFace;

    // Convert UV to grid coordinates
    int32 GridX = FMath::FloorToInt((U + 1.0f) / ChunkUVSize);
    int32 GridY = FMath::FloorToInt((V + 1.0f) / ChunkUVSize);

    // Clamp to valid range
    GridX = FMath::Clamp(GridX, 0, ChunksPerFace - 1);
    GridY = FMath::Clamp(GridY, 0, ChunksPerFace - 1);

    return FChunkId(Face, FIntVector(GridX, GridY, 0), InLOD);
}


// Get neighbor IDs (for seam handling)
FChunkId FChunkId::GetNeighbor(int32 DeltaX, int32 DeltaY) const
{
    int32 ChunksPerFace = FMath::Pow(2, LOD);
    int32 NewX = ChunkCoords.X + DeltaX;
    int32 NewY = ChunkCoords.Y + DeltaY;

    // Handle face wrapping (simplified - we'll implement full neighbor logic later)
    uint8 NewFace = CubeFace;

    // Simple clamping for now
    if (NewX < 0 || NewX >= ChunksPerFace || NewY < 0 || NewY >= ChunksPerFace)
    {
        // Return invalid ID for out-of-bounds (will be handled by neighbor system)
        return FChunkId();
    }

    return FChunkId(NewFace, FIntVector(NewX, NewY, 0), LOD);
}