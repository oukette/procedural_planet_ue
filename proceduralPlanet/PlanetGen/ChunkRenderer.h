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

        // Assigns a component to the chunk, uploads mesh data, and makes it visible
        void RenderChunk(FChunk *Chunk);

        // Returns the component to the pool and hides it
        void HideChunk(FChunk *Chunk);

        AActor* GetOwner() const { return OwnerActor; }

    private:
        AActor *OwnerActor;
        UMaterialInterface *Material;

        // Pool of inactive components ready for reuse
        TArray<UProceduralMeshComponent *> ComponentPool;

        UProceduralMeshComponent *GetFreeComponent();
};