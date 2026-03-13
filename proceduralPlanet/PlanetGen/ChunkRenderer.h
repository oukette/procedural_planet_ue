#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "Chunk.h"
#include "Materials/MaterialInterface.h"


// Handles the visual representation of chunks using a pool of ProceduralMeshComponents.
class ChunkRenderer
{
    public:
        ChunkRenderer(AActor *InOwner, UMaterialInterface *InMaterial);
        ~ChunkRenderer();

        // Upload mesh data to a component and assigns it to the chunk.
        // Component starts hidden. State becomes MeshReady (caller's responsibility).
        void PrepareChunk(FChunk *Chunk, bool bEnableCollision);

        // Make the chunk's component visible.
        // Chunk must be in MeshReady state. State becomes Visible (caller's responsibility).
        void ShowChunk(FChunk *Chunk);

        // Hide the chunk's component. Component stays assigned — no mesh re-upload needed.
        // State becomes MeshReady (caller's responsibility).
        void HideChunk(FChunk *Chunk);

        // Unregister and destroy the given component.
        void DiscardComponent(UProceduralMeshComponent* Comp);

        // Returns the component to the pool and clears the mesh.
        // Called only when a chunk is being permanently destroyed.
        void ReleaseChunk(FChunk *Chunk);

        // Destroys all components currently in the free pool.
        void ReleaseAllComponents();

        AActor *GetOwner() const { return OwnerActor; }

    private:
        AActor *OwnerActor;
        UMaterialInterface *Material;

        // Pool of inactive components ready for reuse
        TArray<TWeakObjectPtr<UProceduralMeshComponent>> FreeComponentPool;

        UProceduralMeshComponent *GetFreeComponent();
};