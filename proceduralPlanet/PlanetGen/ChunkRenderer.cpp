#include "ChunkRenderer.h"
#include "Engine/Engine.h"  // For GIsRequestingExit


ChunkRenderer::ChunkRenderer(AActor *InOwner, UMaterialInterface *InMaterial) :
    OwnerActor(InOwner),
    Material(InMaterial)
{
}


ChunkRenderer::~ChunkRenderer()
{
    // If the engine is exiting, the UWorld and AActor are likely already tearing down.
    // The components in the pool (Raw Pointers) may have already been freed by the engine.
    // Touching them now would cause a Segfault.
    if (!GIsRequestingExit)
    {
        ReleaseAllComponents();
    }
}


UProceduralMeshComponent *ChunkRenderer::GetFreeComponent()
{
    if (FreeComponentPool.Num() > 0)
    {
        UProceduralMeshComponent *Comp = FreeComponentPool.Pop();
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

    // Configure Collision Settings BEFORE creating the mesh
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

    // Upload Mesh Data
    Comp->CreateMeshSection(0,
                            Chunk->MeshData->Vertices,
                            Chunk->MeshData->Triangles,
                            Chunk->MeshData->Normals,
                            Chunk->MeshData->UV0,
                            Chunk->MeshData->Colors,
                            TArray<FProcMeshTangent>(),
                            false);

    // Apply Material
    Comp->SetMaterial(0, Material);

    // Set Transform (Location and Rotation on the sphere)
    Comp->SetRelativeLocationAndRotation(Chunk->Transform.Location, Chunk->Transform.Rotation);

    // Stay hidden until ShowChunk is called
    Comp->SetVisibility(false);

    // Link
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

void ChunkRenderer::DiscardComponent(UProceduralMeshComponent *Comp)
{
    if (IsValid(Comp) && !GIsRequestingExit)
    {
        // Unregister to stop physics/rendering immediately
        Comp->UnregisterComponent();

        // Destroy the UObject
        if (!Comp->IsBeingDestroyed())
        {
            Comp->DestroyComponent();
        }
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

            // Only clear mesh sections if the game is running.
            // Doing this during exit can crash physics/cooking threads.
            if (!GIsRequestingExit && !Comp->IsPendingKill())
            {
                Comp->ClearAllMeshSections();
            }

            Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

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
        // Vital check: During shutdown, raw pointers to UObjects can be dangling
        // even if 'IsValid' (which checks null/pendingkill) passes on the memory address.
        // We assume that if IsRequestingExit is true, we should stop touching these.
        if (!GIsRequestingExit && IsValid(Comp) && !Comp->IsBeingDestroyed())
        {
            Comp->DestroyComponent();
        }
    }
    FreeComponentPool.Empty();
}