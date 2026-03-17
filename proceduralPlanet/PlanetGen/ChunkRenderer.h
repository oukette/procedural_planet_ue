#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "Chunk.h"
#include "Materials/MaterialInterface.h"


// Handles the visual representation of chunks using a pool of ProceduralMeshComponents.
class ChunkRenderer
{
    private:
        AActor *m_ownerActor;
        UMaterialInterface *m_material;

        // Pool of inactive components ready for reuse
        TArray<TWeakObjectPtr<UProceduralMeshComponent>> m_freeComponentPool;

    public:
        ChunkRenderer(AActor *InOwner, UMaterialInterface *InMaterial);
        ~ChunkRenderer();

        // Upload mesh data to a component and assigns it to the chunk.
        // Component starts hidden. State becomes MeshReady (caller's responsibility).
        void PrepareChunk(Chunk *Chunk, bool bEnableCollision);

        // Make the chunk's component visible.
        // Chunk must be in MeshReady state. State becomes Visible (caller's responsibility).
        void ShowChunk(Chunk *Chunk);

        // Hide the chunk's component. Component stays assigned — no mesh re-upload needed.
        // State becomes MeshReady (caller's responsibility).
        void HideChunk(Chunk *Chunk);

        // Unregister and destroy the given component.
        void DiscardComponent(UProceduralMeshComponent *Comp);

        // Returns the component to the pool and clears the mesh.
        // Called only when a chunk is being permanently destroyed.
        void ReleaseChunk(Chunk *Chunk);

        // Destroys all components currently in the free pool.
        void ReleaseAllComponents();

        AActor *GetOwner() const { return m_ownerActor; }

    private:
        UProceduralMeshComponent *GetFreeComponent();
};