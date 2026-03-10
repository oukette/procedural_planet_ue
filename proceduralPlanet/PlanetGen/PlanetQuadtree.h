#pragma once

#include "CoreMinimal.h"
#include "DataTypes.h"


// A logical node in the Quadtree.
struct FQuadtreeNode
{
        FChunkId Id;
        FQuadtreeNode *Parent = nullptr;
        TArray<TUniquePtr<FQuadtreeNode>> Children;

        FQuadtreeNode(const FChunkId &InId, FQuadtreeNode *InParent) :
            Id(InId),
            Parent(InParent)
        {
        }

        bool IsLeaf() const { return Children.Num() == 0; }
};


// The "Brain" of the planet system.
// Decides which chunks should be visible based on camera position and LOD rules.
class FPlanetQuadtree
{
    public:
        FPlanetQuadtree(const FPlanetConfig &InConfig);
        ~FPlanetQuadtree();

        // Rebuilds the visibility lists based on the view context.
        // IsChunkReady: A callback to check if a specific chunk ID has mesh data loaded (used for hysteresis).
        void Update(const FPlanetViewContext &Context, TFunctionRef<bool(const FChunkId &)> IsChunkReady);

        const TSet<FChunkId> &GetDesiredLeaves() const { return DesiredLeaves; }

        // All chunk IDs that are children of a splitting node but not yet ready.
        // The manager uses this to start generating them before they appear in DesiredLeaves.
        const TSet<FChunkId> &GetPendingChildIds() const { return PendingChildIds; }

        // Debug drawing for the logical grid
        void DrawDebugGrid(const UWorld *World, const FTransform &PlanetTransform) const;

    private:
        FPlanetConfig Config;
        TArray<TUniquePtr<FQuadtreeNode>> RootNodes;
        TSet<FChunkId> DesiredLeaves;
        TSet<FChunkId> PendingChildIds;

        void UpdateNode(FQuadtreeNode *Node, const FPlanetViewContext &Context, TFunctionRef<bool(const FChunkId &)> IsChunkReady);
        bool ShouldSplit(const FQuadtreeNode *Node, const FVector &ObserverLocal) const;
        bool ShouldMerge(const FQuadtreeNode *Node, const FVector &ObserverLocal) const;
};