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
    // --- culling will come back later ---
    // FVector Center = FMathUtils::GetChunkCenter(Node->Id, Config.PlanetRadius);
    // FVector ToChunk = Center - Context.ObserverLocation;

    // FVector ChunkNormal = Center.GetSafeNormal();
    // FVector ToObserverDir = -ToChunk.GetSafeNormal();

    // --- LOD Logic ---
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
        Node->Children.Empty(); // dont care if those children have loaded chunks — that's the manager's problem.
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
    float Dist = FVector::Dist(Center, ObserverLocal);
    float NodeSize = (Config.PlanetRadius * PI * 0.5f) / (float)(1 << Node->Id.LODLevel);

    return Dist < (NodeSize * Config.LODSplitDistanceMultiplier);
}


bool FPlanetQuadtree::ShouldMerge(const FQuadtreeNode *Node, const FVector &ObserverLocal) const
{
    if (Node->Id.LODLevel >= Config.MaxLOD)
        return true;

    FVector Center = FMathUtils::GetChunkCenter(Node->Id, Config.PlanetRadius);
    float Dist = FVector::Dist(Center, ObserverLocal);
    float NodeSize = (Config.PlanetRadius * PI * 0.5f) / (float)(1 << Node->Id.LODLevel);

    // Merge only when observer has moved meaningfully farther than the split threshold.
    // The hysteresis ratio prevents oscillation at the boundary.
    return Dist >= (NodeSize * Config.LODSplitDistanceMultiplier * Config.LODMergeHysteresisRatio);
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