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


void FPlanetQuadtree::Update(const FPlanetViewContext &Context, TFunctionRef<bool(const FChunkId &)> IsChunkReady)
{
    DesiredLeaves.Empty();
    PendingChildIds.Empty();

    for (const auto &Root : RootNodes)
    {
        UpdateNode(Root.Get(), Context, IsChunkReady);
    }
}


void FPlanetQuadtree::UpdateNode(FQuadtreeNode *Node, const FPlanetViewContext &Context, TFunctionRef<bool(const FChunkId &)> IsChunkReady)
{
    FVector Center = FMathUtils::GetChunkCenter(Node->Id, Config.PlanetRadius);
    FVector ToChunk = Center - Context.ObserverLocation;

    FVector ChunkNormal = Center.GetSafeNormal();
    FVector ToObserverDir = -ToChunk.GetSafeNormal();

    // --- Culling Logic ---
    float HorizonThreshold = (Node->Id.LODLevel == 0) ? -0.9f : FPlanetStatics::HorizonCullingDot;
    float FrustumThreshold = (Node->Id.LODLevel == 0) ? -0.9f : FPlanetStatics::FrustumCullingDot;

    // 1. Horizon Culling
    if ((ChunkNormal | ToObserverDir) < HorizonThreshold)
    {
        // Culled chunks do not appear in DesiredLeaves.
        // The manager's deferred cleanup will handle releasing them after a delay.
        return;
    }

    // 2. Frustum Culling
    float NodeSize = (Config.PlanetRadius * PI * 0.5f) / (float)(1 << Node->Id.LODLevel);
    bool bIsClose = ToChunk.SizeSquared() < (NodeSize * NodeSize * 2.25f);

    if (!bIsClose && !Context.ObserverForward.IsZero() && (ToChunk.GetSafeNormal() | Context.ObserverForward) < FrustumThreshold)
    {
        // Culled — same as above, no DesiredLeaves entry.
        return;
    }

    // --- LOD Logic ---
    if (ShouldSplit(Node, Context.ObserverLocation))
    {
        if (Node->IsLeaf())
        {
            // Create Children
            int32 NextLOD = Node->Id.LODLevel + 1;
            int32 X = Node->Id.Coords.X;
            int32 Y = Node->Id.Coords.Y;
            uint8 Face = Node->Id.FaceIndex;

            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2, Y * 2, 0), NextLOD), Node));
            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2 + 1, Y * 2, 0), NextLOD), Node));
            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2, Y * 2 + 1, 0), NextLOD), Node));
            Node->Children.Add(MakeUnique<FQuadtreeNode>(FChunkId(Face, FIntVector(X * 2 + 1, Y * 2 + 1, 0), NextLOD), Node));
        }

        // Check if children are ready
        bool bAllChildrenReady = true;
        for (const auto &Child : Node->Children)
        {
            if (!IsChunkReady(Child->Id))
            {
                bAllChildrenReady = false;
                break;
            }
        }

        if (bAllChildrenReady)
        {
            // All children ready — recurse. Parent is no longer a leaf.
            for (auto &Child : Node->Children)
            {
                UpdateNode(Child.Get(), Context, IsChunkReady);
            }
        }
        else
        {
            // Children still loading — hold parent as the desired leaf.
            DesiredLeaves.Add(Node->Id);

            // Tell the manager which children need to be generated.
            for (const auto &Child : Node->Children)
                PendingChildIds.Add(Child->Id);
        }
    }
    else if (ShouldMerge(Node, Context.ObserverLocation))
    {
        // This node should not be split. Prune any children from the tree.
        // The manager's deferred cleanup handles releasing child chunk data gracefully.
        Node->Children.Empty();

        DesiredLeaves.Add(Node->Id);
    }
    else
    {
        // Hysteresis band — hold current state
        if (Node->IsLeaf())
        {
            // Currently a leaf at this LOD — stay visible, no change
            DesiredLeaves.Add(Node->Id);
        }
        else
        {
            // Currently split — keep children alive, recurse into them
            // This preserves the current split without re-committing or destroying it
            bool bAllChildrenReady = true;
            for (const auto &Child : Node->Children)
            {
                if (!IsChunkReady(Child->Id))
                {
                    bAllChildrenReady = false;
                    break;
                }
            }

            if (bAllChildrenReady)
            {
                for (auto &Child : Node->Children)
                    UpdateNode(Child.Get(), Context, IsChunkReady);
            }
            else
            {
                // Children not all ready — hold parent, children will be picked up by the manager
                DesiredLeaves.Add(Node->Id);

                for (const auto &Child : Node->Children)
                    PendingChildIds.Add(Child->Id);  // add this
            }
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
    // it means: split at distance X, but don't merge until distance hysteresisRatio*X.
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