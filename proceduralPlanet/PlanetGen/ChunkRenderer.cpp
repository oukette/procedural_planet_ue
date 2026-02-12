#include "ChunkRenderer.h"

ChunkRenderer::ChunkRenderer(AActor* InOwner, UMaterialInterface* InMaterial)
    : OwnerActor(InOwner), Material(InMaterial)
{
}

ChunkRenderer::~ChunkRenderer()
{
    // Components attached to the Actor will be destroyed by the Actor.
    // We just need to clear our references.
    ComponentPool.Empty();
}

UProceduralMeshComponent* ChunkRenderer::GetFreeComponent()
{
    if (ComponentPool.Num() > 0)
    {
        UProceduralMeshComponent* Comp = ComponentPool.Pop();
        Comp->SetVisibility(true);
        return Comp;
    }

    // Create new component if pool is empty
    if (OwnerActor)
    {
        UProceduralMeshComponent* NewComp = NewObject<UProceduralMeshComponent>(OwnerActor);
        NewComp->RegisterComponent();
        NewComp->AttachToComponent(OwnerActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        NewComp->bUseAsyncCooking = true; // Important for performance
        return NewComp;
    }

    return nullptr;
}

void ChunkRenderer::RenderChunk(FChunk* Chunk)
{
    if (!Chunk || !Chunk->MeshData)
    {
        return;
    }

    UProceduralMeshComponent* Comp = GetFreeComponent();
    if (!Comp)
    {
        return;
    }

    // 1. Upload Mesh Data
    Comp->CreateMeshSection(0,
        Chunk->MeshData->Vertices,
        Chunk->MeshData->Triangles,
        Chunk->MeshData->Normals,
        Chunk->MeshData->UV0,
        Chunk->MeshData->Colors,
        TArray<FProcMeshTangent>(),
        false // Collision disabled for now, can be enabled via config later
    );

    // 2. Apply Material
    Comp->SetMaterial(0, Material);

    // 3. Set Transform (Location and Rotation on the sphere)
    Comp->SetRelativeLocationAndRotation(Chunk->Transform.Location, Chunk->Transform.Rotation);

    // 4. Link
    Chunk->RenderProxy = Comp;
}

void ChunkRenderer::HideChunk(FChunk* Chunk)
{
    if (UProceduralMeshComponent* Comp = Chunk->RenderProxy.Get())
    {
        Comp->SetVisibility(false);
        Comp->ClearAllMeshSections();
        ComponentPool.Add(Comp);
        Chunk->RenderProxy.Reset();
    }
}