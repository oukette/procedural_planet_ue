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
        UpdateNode(Root.Get(), Context);
    }
}


void FPlanetQuadtree::UpdateNode(FQuadtreeNode *Node, const FPlanetViewContext &Context)
{
    FVector Center = FMathUtils::GetChunkCenter(Node->Id, Config.PlanetRadius);

    // --- 1. Horizon Culling ---
    float ObserverDist = Context.ObserverLocation.Size();
    if (ObserverDist > Config.PlanetRadius)
    {
        float HorizonCos = Config.PlanetRadius / ObserverDist;  // cos(horizon angle) = PlanetRadius / ObserverDist
        float ChunkDot = FVector::DotProduct(Context.ObserverLocation.GetSafeNormal(), Center.GetSafeNormal());
        if (ChunkDot < -HorizonCos)
        {
            DesiredLeaves.Add(Node->Id);
            return;
        }
    }

    // --- 2. Frustum Culling ---
    bool bInFrustum = true;
    if (!Context.ObserverForward.IsNearlyZero())
    {
        float NodeSize = (Config.PlanetRadius * PI * 0.5f) / (float)(1 << Node->Id.LODLevel);
        float SphereRadius = NodeSize * 0.75f;
        bInFrustum = Context.ViewFrustum.IntersectSphere(Center, SphereRadius);
    }

    if (!bInFrustum)
    {
        // Keep the node alive as a leaf only if it's already a leaf.
        // If it has children, recurse one level to let them evaluate split/merge
        // but don't split further. This prevents stale deep trees outside the frustum.
        if (Node->IsLeaf())
        {
            DesiredLeaves.Add(Node->Id);
        }
        else
        {
            // Collapse children — outside frustum, no need to maintain deep structure
            Node->Children.Empty();
            DesiredLeaves.Add(Node->Id);
        }
        return;
    }


    // --- LOD logic ---
    if (ShouldSplit(Node, Context))
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
            UpdateNode(Child.Get(), Context);
    }
    else if (ShouldMerge(Node, Context))
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
                UpdateNode(Child.Get(), Context);
        }
    }
}


bool FPlanetQuadtree::ShouldSplit(const FQuadtreeNode *Node, const FPlanetViewContext &Context) const
{
    if (Node->Id.LODLevel >= Config.MaxLOD)
        return false;

    FVector Center = FMathUtils::GetChunkCenter(Node->Id, Config.PlanetRadius);
    float Dist = FVector::Dist(Center, Context.ObserverLocation);
    float NodeSize = (Config.PlanetRadius * PI * 0.5f) / (float)(1 << Node->Id.LODLevel);

    // Angular size: how large does this chunk appear in radians
    float AngularSize = FMath::Atan2(NodeSize, FMath::Max(Dist, 1.f));

    // Normalize by vertical FOV to get screen-space fraction (0..1)
    // A value of 1.0 means the chunk fills the entire vertical screen height
    float ScreenFraction = AngularSize / FMath::Max(Context.VerticalFOVRadians, KINDA_SMALL_NUMBER);

    return ScreenFraction > Config.LODSplitScreenFraction;
}


bool FPlanetQuadtree::ShouldMerge(const FQuadtreeNode *Node, const FPlanetViewContext &Context) const
{
    if (Node->Id.LODLevel >= Config.MaxLOD)
        return true;

    FVector Center = FMathUtils::GetChunkCenter(Node->Id, Config.PlanetRadius);
    float Dist = FVector::Dist(Center, Context.ObserverLocation);
    float NodeSize = (Config.PlanetRadius * PI * 0.5f) / (float)(1 << Node->Id.LODLevel);

    float AngularSize = FMath::Atan2(NodeSize, FMath::Max(Dist, 1.f));
    float ScreenFraction = AngularSize / FMath::Max(Context.VerticalFOVRadians, KINDA_SMALL_NUMBER);

    // Merge when screen fraction drops below the split threshold scaled by hysteresis.
    // HysteresisRatio < 1.0 means merge triggers at a smaller screen fraction than split,
    // preventing oscillation at the boundary.
    return ScreenFraction < (Config.LODSplitScreenFraction * Config.LODMergeHysteresisRatio);
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