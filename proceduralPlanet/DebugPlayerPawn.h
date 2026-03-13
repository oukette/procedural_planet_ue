#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PlanetGen/Planet.h"
#include "DebugPlayerPawn.generated.h"


class UCapsuleComponent;
class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class USkeletalMeshComponent;


UCLASS()
class PROCEDURALPLANET_API ADebugPlayerPawn : public APawn
{
        GENERATED_BODY()

    public:
        ADebugPlayerPawn();

    protected:
        virtual void BeginPlay() override;

    public:
        virtual void Tick(float DeltaTime) override;
        virtual void SetupPlayerInputComponent(class UInputComponent *PlayerInputComponent) override;
        virtual FVector GetVelocity() const override;

        // --- Components ---
        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
        UCapsuleComponent *CapsuleComponent;

        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
        UStaticMeshComponent *BodyMesh;  // Simple placeholder body

        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
        USkeletalMeshComponent *SkeletalBody;  // Slot for Mannequin (optional)

        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
        USpringArmComponent *SpringArm;

        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
        UCameraComponent *Camera;

        // --- Settings ---
        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
        float WalkSpeed = 600.0f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
        float RunSpeedMultiplier = 2.0f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
        float FlySpeed = 2000.0f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
        float GravityStrength = 980.0f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
        float TurnSpeed = 50.0f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
        float LookUpSpeed = 50.0f;

        UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
        float RollSpeed = 50.0f;

        // --- State ---
        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "State")
        bool bIsWalking = false;  // False = Flying

        UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "State")
        bool bIsFirstPerson = false;

        // --- Internal Physics ---
        FVector CurrentVelocity;
        FVector GravityDirection;

        // Reference to the planet to pull gravity from
        UPROPERTY()
        APlanet *TargetPlanet;

    private:
        // Input State
        FVector MovementInput;
        FVector2D CameraInput;
        bool bIsRunning;

        // Helpers
        void FindPlanet();
        void UpdateGravityDirection();
        void UpdateMovementWalking(float DeltaTime);
        void UpdateMovementFlying(float DeltaTime);
        void AlignToPlanet(float DeltaTime);

        // Input Handlers
        void MoveForward(float Val) { MovementInput.X = Val; }
        void MoveRight(float Val) { MovementInput.Y = Val; }
        void MoveUp(float Val) { MovementInput.Z = Val; }
        void Turn(float Val);
        void LookUp(float Val);

        // New handler for Roll (using the MoveUpDown axis, usually Q/E)
        void InputRoll(float Val);

        void ToggleMovementMode();
        void ToggleCameraMode();
        void AdjustSpeed(float Val);

        void StartRun() { bIsRunning = true; }
        void StopRun() { bIsRunning = false; }
};
