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
        void Update(const FPlanetViewContext &Context);

        // The ideal set of leaf IDs this frame. Manager diffs this against RenderSet.
        const TSet<FChunkId> &GetDesiredLeaves() const { return DesiredLeaves; }

        // Debug drawing for the logical grid
        void DrawDebugGrid(const UWorld *World, const FTransform &PlanetTransform) const;

    private:
        FPlanetConfig Config;
        TArray<TUniquePtr<FQuadtreeNode>> RootNodes;
        TSet<FChunkId> DesiredLeaves;

        void UpdateNode(FQuadtreeNode *Node, const FVector &ObserverLocal);
        bool ShouldSplit(const FQuadtreeNode *Node, const FVector &ObserverLocal) const;
        bool ShouldMerge(const FQuadtreeNode *Node, const FVector &ObserverLocal) const;
};