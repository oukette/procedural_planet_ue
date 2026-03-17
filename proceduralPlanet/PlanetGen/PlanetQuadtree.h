#pragma once

#include "CoreMinimal.h"
#include "ChunkId.h"
#include "DataTypes.h"


// A logical node in the Quadtree.
struct QuadtreeNode
{
        FChunkId Id;
        QuadtreeNode *Parent = nullptr;
        TArray<TUniquePtr<QuadtreeNode>> Children;

        QuadtreeNode(const FChunkId &InId, QuadtreeNode *InParent) :
            Id(InId),
            Parent(InParent)
        {
        }

        bool IsLeaf() const { return Children.Num() == 0; }
};


// The "Brain" of the planet system.
// Decides which chunks should be visible based on camera position and LOD rules.
class PlanetQuadtree
{
    private:
        FPlanetConfig m_planetConfig;
        TArray<TUniquePtr<QuadtreeNode>> m_rootNodes;
        TSet<FChunkId> m_desiredLeaves;

    public:
        PlanetQuadtree(const FPlanetConfig &InConfig);
        ~PlanetQuadtree();

        // Rebuilds the visibility lists based on the view context.
        // IsChunkReady: A callback to check if a specific chunk ID has mesh data loaded (used for hysteresis).
        void Update(const FPlanetViewContext &Context);

        // The ideal set of leaf IDs this frame. Manager diffs this against RenderSet.
        const TSet<FChunkId> &GetDesiredLeaves() const { return m_desiredLeaves; }

        // Debug drawing for the logical grid
        void DrawDebugGrid(const UWorld *World, const FTransform &PlanetTransform) const;

    private:
        void UpdateNode(QuadtreeNode *Node, const FPlanetViewContext &Context);
        bool ShouldSplit(const QuadtreeNode *Node, const FPlanetViewContext &Context) const;
        bool ShouldMerge(const QuadtreeNode *Node, const FPlanetViewContext &Context) const;
};