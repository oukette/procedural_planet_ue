#include "ChunkRenderer.h"


ChunkRenderer::ChunkRenderer(AActor *InOwner, UMaterialInterface *InMaterial) :
    OwnerActor(InOwner),
    Material(InMaterial)
{
}


ChunkRenderer::~ChunkRenderer()
{
    // Components attached to the Actor will be destroyed by the Actor.
    // We just need to clear our references.
    ComponentPool.Empty();
}


UProceduralMeshComponent *ChunkRenderer::GetFreeComponent()
{
    if (ComponentPool.Num() > 0)
    {
        UProceduralMeshComponent *Comp = ComponentPool.Pop();
        Comp->SetVisibility(true);
        return Comp;
    }

    // Create new component if pool is empty
    if (OwnerActor)
    {
        UProceduralMeshComponent *NewComp = NewObject<UProceduralMeshComponent>(OwnerActor);
        NewComp->RegisterComponent();
        NewComp->AttachToComponent(OwnerActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        NewComp->bUseAsyncCooking = true;         // Important for performance
        NewComp->SetComponentTickEnabled(false);  // Critical: Disable ticking to save performance
        return NewComp;
    }

    return nullptr;
}


void ChunkRenderer::RenderChunk(FChunk *Chunk, bool bEnableCollision)
{
    if (!Chunk || !Chunk->MeshData)
    {
        return;
    }

    UProceduralMeshComponent *Comp = GetFreeComponent();
    if (!Comp)
    {
        return;
    }

    // 1. Configure Collision Settings BEFORE creating the mesh
    // This ensures that when CreateMeshSection triggers collision cooking, it uses the correct flags.
    if (bEnableCollision)
    {
        Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Comp->SetCollisionProfileName(TEXT("BlockAll"));
        Comp->bUseComplexAsSimpleCollision = true;  // Critical for Trimesh collision
        
        // FIX: Disable async cooking for collision chunks to ensure the physics engine 
        // is aware of the ground immediately. Prevents falling through while cooking.
        Comp->bUseAsyncCooking = false; 
    }
    else
    {
        Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }

    // 2. Upload Mesh Data
    Comp->CreateMeshSection(0,
                            Chunk->MeshData->Vertices,
                            Chunk->MeshData->Triangles,
                            Chunk->MeshData->Normals,
                            Chunk->MeshData->UV0,
                            Chunk->MeshData->Colors,
                            TArray<FProcMeshTangent>(),
                            bEnableCollision);

    // 3. Apply Material
    Comp->SetMaterial(0, Material);

    // 4. Set Transform (Location and Rotation on the sphere)
    Comp->SetRelativeLocationAndRotation(Chunk->Transform.Location, Chunk->Transform.Rotation);

    // 5. Link
    Chunk->RenderProxy = Comp;
}


void ChunkRenderer::HideChunk(FChunk *Chunk)
{
    if (UProceduralMeshComponent *Comp = Chunk->RenderProxy.Get())
    {
        Comp->SetVisibility(false);
        Comp->ClearAllMeshSections();
        ComponentPool.Add(Comp);
        Chunk->RenderProxy.Reset();
    }
}