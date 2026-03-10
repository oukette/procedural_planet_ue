#include "ChunkRenderer.h"


ChunkRenderer::ChunkRenderer(AActor *InOwner, UMaterialInterface *InMaterial) :
    OwnerActor(InOwner),
    Material(InMaterial)
{
}


ChunkRenderer::~ChunkRenderer()
{
    if (FreeComponentPool.Num() > 0)
    {
        UE_LOG(LogTemp,
               Warning,
               TEXT("ChunkRenderer destroyed with %d components still in pool. "
                    "ReleaseAllComponents() should be called before destruction."),
               FreeComponentPool.Num());
    }
}


UProceduralMeshComponent *ChunkRenderer::GetFreeComponent()
{
    if (FreeComponentPool.Num() > 0)
    {
        UProceduralMeshComponent *Comp = FreeComponentPool.Pop();
        // Comp->SetVisibility(true);
        Comp->SetRelativeTransform(FTransform::Identity);  // clear stale transform from previous owner
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
        NewComp->SetVisibility(false);            // always start hidden

        return NewComp;
    }

    return nullptr;
}


void ChunkRenderer::PrepareChunk(FChunk *Chunk, bool bEnableCollision = false)
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
        Comp->bUseAsyncCooking = true;
        // Comp->UpdateCollision(); // explicit, controlled call
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
                            false);

    // 3. Apply Material
    Comp->SetMaterial(0, Material);

    // 4. Set Transform (Location and Rotation on the sphere)
    Comp->SetRelativeLocationAndRotation(Chunk->Transform.Location, Chunk->Transform.Rotation);

    // 5. Stay hidden until ShowChunk is called
    Comp->SetVisibility(false);

    // 6. Link
    Chunk->RenderProxy = Comp;
}


void ChunkRenderer::ShowChunk(FChunk *Chunk)
{
    if (!Chunk)
        return;

    if (UProceduralMeshComponent *Comp = Chunk->RenderProxy.Get())
    {
        Comp->SetVisibility(true);
    }
}


void ChunkRenderer::HideChunk(FChunk *Chunk)
{
    if (!Chunk)
        return;

    if (UProceduralMeshComponent *Comp = Chunk->RenderProxy.Get())
    {
        Comp->SetVisibility(false);
    }
}


void ChunkRenderer::ReleaseChunk(FChunk *Chunk)
{
    if (!Chunk)
        return;

    if (UProceduralMeshComponent *Comp = Chunk->RenderProxy.Get())
    {
        if (IsValid(Comp))
        {
            Comp->SetVisibility(false);
            Comp->ClearAllMeshSections();
            FreeComponentPool.Add(Comp);
        }
        Chunk->RenderProxy.Reset();
    }
}


void ChunkRenderer::ReleaseAllComponents()
{
    // This function is the central point for destroying all pooled mesh components.
    // It ensures we don't leak components that the renderer created.
    for (UProceduralMeshComponent *Comp : FreeComponentPool)
    {
        if (IsValid(Comp) && !Comp->IsBeingDestroyed())
        {
            Comp->DestroyComponent();
        }
    }
    FreeComponentPool.Empty();
}