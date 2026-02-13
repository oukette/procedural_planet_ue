#include "SimpleNoise.h"
#include "Math/UnrealMathUtility.h"


int32 SimpleNoise::floor(float x) { return x >= 0 ? (int32)x : (int32)x - 1; }


float SimpleNoise::getNoise(const FVector &Position, int32 Seed) const
{
    // Simplex noise constants
    const float F3 = 1.0f / 3.0f;
    const float G3 = 1.0f / 6.0f;

    // Skew the input space to determine which simplex cell we're in
    float s = (Position.X + Position.Y + Position.Z) * F3;
    int32 i = FMath::FloorToInt(Position.X + s);
    int32 j = FMath::FloorToInt(Position.Y + s);
    int32 k = FMath::FloorToInt(Position.Z + s);

    float t = (i + j + k) * G3;
    float X0 = i - t;  // Unskew the cell origin back to (x,y,z) space
    float Y0 = j - t;
    float Z0 = k - t;
    float x0 = Position.X - X0;  // The x,y,z distances from the cell origin
    float y0 = Position.Y - Y0;
    float z0 = Position.Z - Z0;

    // For the 3D case, the simplex shape is a tetrahedron.
    // Determine which simplex we are in.
    int32 i1, j1, k1;  // Offsets for second corner of simplex in (i,j,k) coords
    int32 i2, j2, k2;  // Offsets for third corner of simplex in (i,j,k) coords

    if (x0 >= y0)
    {
        if (y0 >= z0)
        {
            i1 = 1;
            j1 = 0;
            k1 = 0;
            i2 = 1;
            j2 = 1;
            k2 = 0;
        }  // X Y Z order
        else if (x0 >= z0)
        {
            i1 = 1;
            j1 = 0;
            k1 = 0;
            i2 = 1;
            j2 = 0;
            k2 = 1;
        }  // X Z Y order
        else
        {
            i1 = 0;
            j1 = 0;
            k1 = 1;
            i2 = 1;
            j2 = 0;
            k2 = 1;
        }  // Z X Y order
    }
    else
    {  // x0 < y0
        if (y0 < z0)
        {
            i1 = 0;
            j1 = 0;
            k1 = 1;
            i2 = 0;
            j2 = 1;
            k2 = 1;
        }  // Z Y X order
        else if (x0 < z0)
        {
            i1 = 0;
            j1 = 1;
            k1 = 0;
            i2 = 0;
            j2 = 1;
            k2 = 1;
        }  // Y Z X order
        else
        {
            i1 = 0;
            j1 = 1;
            k1 = 0;
            i2 = 1;
            j2 = 1;
            k2 = 0;
        }  // Y X Z order
    }

    // A step of (1,0,0) in (i,j,k) means a step of (1-c,-c,-c) in (x,y,z),
    // and a step of (0,1,0) in (i,j,k) means a step of (-c,1-c,-c) in (x,y,z), etc.
    // where c = 1/6.
    float x1 = x0 - i1 + G3;
    float y1 = y0 - j1 + G3;
    float z1 = z0 - k1 + G3;
    float x2 = x0 - i2 + 2.0f * G3;
    float y2 = y0 - j2 + 2.0f * G3;
    float z2 = z0 - k2 + 2.0f * G3;
    float x3 = x0 - 1.0f + 3.0f * G3;
    float y3 = y0 - 1.0f + 3.0f * G3;
    float z3 = z0 - 1.0f + 3.0f * G3;

    // Calculate the contribution from the four corners
    float n = 0.0f;
    n += calculateCorner(x0, y0, z0, grad(hash(i, j, k, Seed), x0, y0, z0));
    n += calculateCorner(x1, y1, z1, grad(hash(i + i1, j + j1, k + k1, Seed), x1, y1, z1));
    n += calculateCorner(x2, y2, z2, grad(hash(i + i2, j + j2, k + k2, Seed), x2, y2, z2));
    n += calculateCorner(x3, y3, z3, grad(hash(i + 1, j + 1, k + 1, Seed), x3, y3, z3));

    // The result is scaled to stay just inside [-1,1]
    return 32.0f * n;
}

// Initialize the gradient table
const FVector SimpleNoise::gradTable[16] = {FVector(1, 1, 0),
                                            FVector(-1, 1, 0),
                                            FVector(1, -1, 0),
                                            FVector(-1, -1, 0),
                                            FVector(1, 0, 1),
                                            FVector(-1, 0, 1),
                                            FVector(1, 0, -1),
                                            FVector(-1, 0, -1),
                                            FVector(0, 1, 1),
                                            FVector(0, -1, 1),
                                            FVector(0, 1, -1),
                                            FVector(0, -1, -1),
                                            // Add some more vectors for 3D. These are edges of a cube.
                                            FVector(1, 1, 0),
                                            FVector(-1, 1, 0),
                                            FVector(0, -1, 1),
                                            FVector(0, -1, -1)};