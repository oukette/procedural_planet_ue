#include "PlanetQuadtree.h"
#include "MathUtils.h"
#include "DrawDebugHelpers.h"


FPlanetQuadtree::FPlanetQuadtree(const FPlanetConfig &InConfig) :
    Config(InConfig)
{
    // Initialize Roots (LOD 0)
    for (uint8 i = 0; i < 6; ++i)
    {
        FChunkId RootId(i, FIntVector(0, 0, 0), 0);
        RootNodes.Add(MakeUnique<FQuadtreeNode>(RootId, nullptr));
    }

    UE_LOG(LogTemp, Warning, TEXT("FPlanetQuadtree initialized."));
}


FPlanetQuadtree::~FPlanetQuadtree() {}


void FPlanetQuadtree::Update(const FPlanetViewContext &Context)
{
    DesiredLeaves.Empty();

    for (const auto &Root : RootNodes)
    {
        UpdateNode(Root.Get(), Context.ObserverLocation);
    }
}


void FPlanetQuadtree::UpdateNode(FQuadtreeNode *Node, const FVector &ObserverLocal)
{
    // --- Horizon Culling ---
    // If a node is deep over the horizon, we don't need to split it further.
    // We use a dot product check: If the surface normal points in the same direction as the view ray, it's facing away.
    // We only apply this to LOD >= 2 to ensure the curvature is approximated well enough.
    if (Node->Id.LODLevel >= 2)
    {
        FVector Center = FMathUtils::GetChunkCenter(Node->Id, Config.PlanetRadius);
        FVector ViewDir = (Center - ObserverLocal).GetSafeNormal();
        FVector Normal = Center.GetUnsafeNormal();  // For a sphere, Normal is Center normalized

        // 0.0 would be the exact horizon. We use a small positive margin to account for terrain height and chunk extents.
        if (FVector::DotProduct(Normal, ViewDir) > 0.15f)
        {
            DesiredLeaves.Add(Node->Id);
            return;  // Stop recursion
        }
    }

    // --- LOD logic ---
    if (ShouldSplit(Node, ObserverLocal))
    {
        // Expand children if not already split
        if (Node->IsLeaf())
        {
            const int32 NextLOD = Node->Id.LODLevel + 1;
            const int32 X = Node->Id.Coords.X;
            const int32 Y = Node->Id.Coords.Y;
            const uint8 Face = Node->Id.FaceIndex;

            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2, Y * 2, 0), NextLOD), Node));
            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2 + 1, Y * 2, 0), NextLOD), Node));
            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2, Y * 2 + 1, 0), NextLOD), Node));
            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2 + 1, Y * 2 + 1, 0), NextLOD), Node));
        }

        // Recurse regardless — children may themselves split or merge
        for (auto &Child : Node->Children)
            UpdateNode(Child.Get(), ObserverLocal);
    }
    else if (ShouldMerge(Node, ObserverLocal))
    {
        // Collapse children — this node becomes a leaf again
        Node->Children.Empty();  // dont care if those children have loaded chunks — that's the manager's problem.
        DesiredLeaves.Add(Node->Id);
    }
    else
    {
        // Hysteresis band — hold current structure
        if (Node->IsLeaf())
        {
            DesiredLeaves.Add(Node->Id);
        }
        else
        {
            for (auto &Child : Node->Children)
                UpdateNode(Child.Get(), ObserverLocal);
        }
    }
}


bool FPlanetQuadtree::ShouldSplit(const FQuadtreeNode *Node, const FVector &ObserverLocal) const
{
    if (Node->Id.LODLevel >= Config.MaxLOD)
        return false;

    FVector Center = FMathUtils::GetChunkCenter(Node->Id, Config.PlanetRadius);
    float DistSq = FVector::DistSquared(Center, ObserverLocal);
    float NodeSize = (Config.PlanetRadius * PI * 0.5f) / (float)(1 << Node->Id.LODLevel);
    float SplitDist = NodeSize * Config.LODSplitDistanceMultiplier;

    return DistSq < (SplitDist * SplitDist);
}


bool FPlanetQuadtree::ShouldMerge(const FQuadtreeNode *Node, const FVector &ObserverLocal) const
{
    if (Node->Id.LODLevel >= Config.MaxLOD)
        return true;

    FVector Center = FMathUtils::GetChunkCenter(Node->Id, Config.PlanetRadius);
    float DistSq = FVector::DistSquared(Center, ObserverLocal);
    float NodeSize = (Config.PlanetRadius * PI * 0.5f) / (float)(1 << Node->Id.LODLevel);

    // Merge only when observer has moved meaningfully farther than the split threshold.
    // The hysteresis ratio prevents oscillation at the boundary.
    float MergeDist = NodeSize * Config.LODSplitDistanceMultiplier * Config.LODMergeHysteresisRatio;
    return DistSq >= (MergeDist * MergeDist);
}


void FPlanetQuadtree::DrawDebugGrid(const UWorld *World, const FTransform &PlanetTransform) const
{
    if (!World)
        return;

    float Radius = Config.PlanetRadius * FPlanetStatics::GridDebugRadiusScale;

    for (uint8 Face = 0; Face < 6; ++Face)
    {
        FVector Normal = FMathUtils::getFaceNormal(Face);
        FVector Right = FMathUtils::getFaceRight(Face);
        FVector Up = FMathUtils::getFaceUp(Face);

        // Draw simplified grid for root nodes
        FVector P0 = PlanetTransform.TransformPosition(FMathUtils::projectCubeToSphere(Normal - Right - Up) * Radius);
        FVector P1 = PlanetTransform.TransformPosition(FMathUtils::projectCubeToSphere(Normal + Right - Up) * Radius);
        FVector P2 = PlanetTransform.TransformPosition(FMathUtils::projectCubeToSphere(Normal + Right + Up) * Radius);
        FVector P3 = PlanetTransform.TransformPosition(FMathUtils::projectCubeToSphere(Normal - Right + Up) * Radius);

        DrawDebugLine(World, P0, P1, FColor::Cyan, false, -1.0f, 0, FPlanetStatics::DebugLineLifetime);
        DrawDebugLine(World, P1, P2, FColor::Cyan, false, -1.0f, 0, FPlanetStatics::DebugLineLifetime);
        DrawDebugLine(World, P2, P3, FColor::Cyan, false, -1.0f, 0, FPlanetStatics::DebugLineLifetime);
        DrawDebugLine(World, P3, P0, FColor::Cyan, false, -1.0f, 0, FPlanetStatics::DebugLineLifetime);
    }
}