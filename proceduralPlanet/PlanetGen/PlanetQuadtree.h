#pragma once

#include "CoreMinimal.h"
#include "ChunkId.h"
#include "PlanetViewContext.h"
#include "PlanetConfig.h"


// A logical node in the Quadtree.
struct QuadtreeNode
{
        ChunkId Id;
        QuadtreeNode *Parent = nullptr;
        TArray<TUniquePtr<QuadtreeNode>> Children;

        QuadtreeNode(const ChunkId &InId, QuadtreeNode *InParent) :
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
        TSet<ChunkId> m_desiredLeaves;
        int32 m_maxLODOverride = -1;

    public:
        PlanetQuadtree(const FPlanetConfig &InConfig);
        ~PlanetQuadtree();

        // Rebuilds the visibility lists based on the view context.
        // IsChunkReady: A callback to check if a specific chunk ID has mesh data loaded (used for hysteresis).
        void Update(const PlanetViewContext &Context);

        // The ideal set of leaf IDs this frame. Manager diffs this against RenderSet.
        const TSet<ChunkId> &GetDesiredLeaves() const { return m_desiredLeaves; }

        // Debug drawing for the logical grid
        void DrawDebugGrid(const UWorld *World, const FTransform &PlanetTransform) const;

    private:
        PlanetViewContext BuildPredictedContext(const PlanetViewContext &Context) const;
        void RunPass(const PlanetViewContext &Context, int32 MaxLODOverride);
        void UpdateNode(QuadtreeNode *Node, const PlanetViewContext &Context, int32 MaxLODOverride);
        bool ShouldSplit(const QuadtreeNode *Node, const PlanetViewContext &Context, int32 MaxLODOverride) const;
        bool ShouldMerge(const QuadtreeNode *Node, const PlanetViewContext &Context) const;
};