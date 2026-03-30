#pragma once

#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"


// Owns the lifecycle of the auto-created far planet model.
// If the model was user-assigned (not auto-created), this handle leaves it untouched on release.
struct FFarModelHandle
{
    private:
        AStaticMeshActor *m_staticMeshActor = nullptr;
        bool m_isOwned = false;  // true only when we spawned it ourselves

    public:
        // Assign a user-provided actor (not owned — will never be auto-destroyed).
        void SetUserProvided(AActor *Actor)
        {
            m_staticMeshActor = Cast<AStaticMeshActor>(Actor);
            if (Actor && !m_staticMeshActor)
                UE_LOG(LogTemp, Warning, TEXT("FFarModelHandle: FarPlanetModel is not a AStaticMeshActor and will be ignored."));

            m_isOwned = false;
        }

        // Assign an auto-created actor (owned — will be destroyed on Release).
        void SetOwned(AStaticMeshActor *Actor)
        {
            m_staticMeshActor = Actor;
            m_isOwned = true;
        }

        // Destroy the actor if we own it, then reset the handle.
        void Release()
        {
            if (m_isOwned && ::IsValid(m_staticMeshActor))
                m_staticMeshActor->Destroy();

            m_staticMeshActor = nullptr;
            m_isOwned = false;
        }

        // Accessors
        AStaticMeshActor *GetActor() const { return m_staticMeshActor; }
        bool IsOwned() const { return m_isOwned; }
        bool IsValid() const { return ::IsValid(m_staticMeshActor); }
        explicit operator bool() const { return IsValid(); }
};