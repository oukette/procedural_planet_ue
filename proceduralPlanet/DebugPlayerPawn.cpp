#include "DebugPlayerPawn.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"


ADebugPlayerPawn::ADebugPlayerPawn()
{
    PrimaryActorTick.bCanEverTick = true;

    // 1. Root Capsule (Collision)
    CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
    CapsuleComponent->InitCapsuleSize(34.0f, 88.0f);
    CapsuleComponent->SetCollisionProfileName(TEXT("Pawn"));
    RootComponent = CapsuleComponent;

    // 2. Placeholder Body (Static Mesh)
    BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
    BodyMesh->SetupAttachment(RootComponent);
    // Try to load a basic cylinder so the user sees something immediately
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if (CylinderMesh.Succeeded())
    {
        BodyMesh->SetStaticMesh(CylinderMesh.Object);
        BodyMesh->SetRelativeScale3D(FVector(0.5f, 0.5f, 1.0f));
        BodyMesh->SetRelativeLocation(FVector(0, 0, -88.0f));  // Align feet
    }

    // 3. Skeletal Body (Optional for Mannequin)
    SkeletalBody = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalBody"));
    SkeletalBody->SetupAttachment(RootComponent);
    SkeletalBody->SetRelativeLocation(FVector(0, 0, -88.0f));
    SkeletalBody->SetRelativeRotation(FRotator(0, -90.0f, 0));  // Standard Mannequin alignment

    // 4. Spring Arm
    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArm->SetupAttachment(RootComponent);
    SpringArm->TargetArmLength = 400.0f;
    SpringArm->bUsePawnControlRotation = true;
    SpringArm->bEnableCameraLag = true;
    SpringArm->CameraLagSpeed = 10.0f;

    // 5. Camera
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);

    // Defaults
    bIsWalking = false;  // Start flying
    CurrentVelocity = FVector::ZeroVector;
}


void ADebugPlayerPawn::BeginPlay()
{
    Super::BeginPlay();
    FindPlanet();
}


void ADebugPlayerPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!TargetPlanet)
    {
        FindPlanet();
    }

    UpdateGravityDirection();
    AlignToPlanet(DeltaTime);

    if (bIsWalking)
    {
        UpdateMovementWalking(DeltaTime);
    }
    else
    {
        UpdateMovementFlying(DeltaTime);
    }
}


FVector ADebugPlayerPawn::GetVelocity() const { return CurrentVelocity; }


void ADebugPlayerPawn::FindPlanet()
{
    // Simple logic: Find the first APlanet in the world
    AActor *FoundActor = UGameplayStatics::GetActorOfClass(GetWorld(), APlanet::StaticClass());
    TargetPlanet = Cast<APlanet>(FoundActor);
}


void ADebugPlayerPawn::UpdateGravityDirection()
{
    if (TargetPlanet)
    {
        GravityDirection = TargetPlanet->GetGravityDirection(GetActorLocation());
    }
    else
    {
        GravityDirection = FVector::DownVector;  // Fallback
    }
}


void ADebugPlayerPawn::AlignToPlanet(float DeltaTime)
{
    // We want the Actor's "Up" to match the surface normal (opposite of gravity)
    FVector TargetUp = -GravityDirection;
    FVector CurrentUp = GetActorUpVector();

    // Calculate rotation from CurrentUp to TargetUp
    FQuat DeltaRot = FQuat::FindBetweenNormals(CurrentUp, TargetUp);

    // Apply rotation
    // If walking, we snap instantly or interpolate quickly to avoid jitter on slopes
    // If flying, we might want it smoother, but for a debug pawn, instant alignment prevents disorientation.
    float InterpSpeed = bIsWalking ? 10.0f : 5.0f;
    FQuat NewRot = FMath::QInterpTo(GetActorQuat(), DeltaRot * GetActorQuat(), DeltaTime, InterpSpeed);

    SetActorRotation(NewRot);
}


void ADebugPlayerPawn::UpdateMovementWalking(float DeltaTime)
{
    // 1. Calculate Input Direction relative to Camera View
    FRotator ControlRot = GetControlRotation();
    FVector ForwardDir = FRotationMatrix(ControlRot).GetScaledAxis(EAxis::X);
    FVector RightDir = FRotationMatrix(ControlRot).GetScaledAxis(EAxis::Y);

    // Project onto the surface plane (remove vertical component relative to gravity)
    FVector SurfaceNormal = -GravityDirection;
    ForwardDir = FVector::VectorPlaneProject(ForwardDir, SurfaceNormal).GetSafeNormal();
    RightDir = FVector::VectorPlaneProject(RightDir, SurfaceNormal).GetSafeNormal();

    FVector DesiredMove = (ForwardDir * MovementInput.X) + (RightDir * MovementInput.Y);
    DesiredMove.Normalize();

    // 2. Apply Speed
    float Speed = bIsRunning ? WalkSpeed * RunSpeedMultiplier : WalkSpeed;
    FVector HorizontalVelocity = DesiredMove * Speed;

    // 3. Apply Gravity (Vertical Velocity)
    // We maintain a separate vertical component
    float VerticalSpeed = FVector::DotProduct(CurrentVelocity, GravityDirection);
    VerticalSpeed += GravityStrength * DeltaTime;  // Accelerate down

    // 4. Collision / Ground Snap
    FVector NewLocation = GetActorLocation() + (HorizontalVelocity * DeltaTime) + (GravityDirection * VerticalSpeed * DeltaTime);

    FHitResult Hit;
    // Simple capsule sweep
    FVector TraceStart = GetActorLocation();
    FVector TraceEnd = NewLocation + (GravityDirection * 50.0f);  // Look ahead slightly

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    bool bLanded = false;

    // If we hit something
    if (GetWorld()->SweepSingleByChannel(Hit, TraceStart, TraceEnd, GetActorQuat(), ECC_Visibility, CapsuleComponent->GetCollisionShape(), Params))
    {
        // Snap to floor
        NewLocation = Hit.Location + (SurfaceNormal * CapsuleComponent->GetScaledCapsuleHalfHeight());
        VerticalSpeed = 0.0f;  // Reset gravity accumulation
        bLanded = true;
    }

    FVector OldLocation = GetActorLocation();

    SetActorLocation(NewLocation, true);  // bSweep=true for lateral collision

    // Calculate actual velocity based on how far we really moved (handles collisions/walls correctly)
    if (DeltaTime > KINDA_SMALL_NUMBER)
    {
        CurrentVelocity = (GetActorLocation() - OldLocation) / DeltaTime;

        // Fix for bouncing: If we snapped to the ground, remove the vertical component
        // from the velocity so it doesn't cause an upward launch in the next frame.
        if (bLanded)
        {
            CurrentVelocity = FVector::VectorPlaneProject(CurrentVelocity, GravityDirection);
        }
    }
}


void ADebugPlayerPawn::UpdateMovementFlying(float DeltaTime)
{
    // Standard "Spectator" style movement, but relative to camera orientation
    FRotator ControlRot = GetControlRotation();
    FVector ForwardDir = FRotationMatrix(ControlRot).GetScaledAxis(EAxis::X);
    FVector RightDir = FRotationMatrix(ControlRot).GetScaledAxis(EAxis::Y);
    FVector UpDir = FRotationMatrix(ControlRot).GetScaledAxis(EAxis::Z);

    FVector DesiredMove = (ForwardDir * MovementInput.X) + (RightDir * MovementInput.Y);

    // Add "Up/Down" input (Space/Ctrl usually, mapped to LookUp axis or specific keys)
    // For now, we just fly where we look.

    FVector DesiredOffset = DesiredMove * FlySpeed * DeltaTime;
    FVector OldLocation = GetActorLocation();

    // Apply movement
    AddActorWorldOffset(DesiredOffset, true);

    if (DeltaTime > KINDA_SMALL_NUMBER)
    {
        CurrentVelocity = (GetActorLocation() - OldLocation) / DeltaTime;
    }
}


void ADebugPlayerPawn::SetupPlayerInputComponent(UInputComponent *PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    // Axis
    PlayerInputComponent->BindAxis("MoveForwardBackward", this, &ADebugPlayerPawn::MoveForward);
    PlayerInputComponent->BindAxis("MoveRightLeft", this, &ADebugPlayerPawn::MoveRight);
    PlayerInputComponent->BindAxis("Turn", this, &ADebugPlayerPawn::Turn);
    PlayerInputComponent->BindAxis("LookUp", this, &ADebugPlayerPawn::LookUp);
    PlayerInputComponent->BindAxis("SpeedAdjust", this, &ADebugPlayerPawn::AdjustSpeed);  // Mouse Wheel for Speed

    // Actions
    PlayerInputComponent->BindAction("ToggleWalk", IE_Pressed, this, &ADebugPlayerPawn::ToggleMovementMode);
    PlayerInputComponent->BindAction("ToggleView", IE_Pressed, this, &ADebugPlayerPawn::ToggleCameraMode);
    PlayerInputComponent->BindAction("HoldToRun", IE_Pressed, this, &ADebugPlayerPawn::StartRun);
    PlayerInputComponent->BindAction("HoldToRun", IE_Released, this, &ADebugPlayerPawn::StopRun);
}


void ADebugPlayerPawn::ToggleMovementMode()
{
    bIsWalking = !bIsWalking;

    if (bIsWalking)
    {
        // Reset orientation to upright when entering walk mode
        CurrentVelocity = FVector::ZeroVector;
    }

    UE_LOG(LogPlayerController, Log, TEXT("ToggleMovementMode: %s"), bIsWalking ? TEXT("Walking") : TEXT("Flying"));
}


void ADebugPlayerPawn::ToggleCameraMode()
{
    bIsFirstPerson = !bIsFirstPerson;
    if (bIsFirstPerson)
    {
        SpringArm->TargetArmLength = 0.0f;
        SpringArm->SetRelativeLocation(FVector(0, 0, 60.0f));  // Eye height
        if (BodyMesh) BodyMesh->SetOwnerNoSee(true);
        if (SkeletalBody) SkeletalBody->SetOwnerNoSee(true);
    }
    else
    {
        SpringArm->TargetArmLength = 400.0f;
        SpringArm->SetRelativeLocation(FVector::ZeroVector);
        if (BodyMesh) BodyMesh->SetOwnerNoSee(false);
        if (SkeletalBody) SkeletalBody->SetOwnerNoSee(false);
    }

    UE_LOG(LogPlayerController, Log, TEXT("ToggleCameraMode: %s"), bIsFirstPerson ? TEXT("First Person") : TEXT("Third Person"));
}


void ADebugPlayerPawn::AdjustSpeed(float Val)
{
    if (Val != 0.0f)
    {
        FlySpeed = FMath::Clamp(FlySpeed + (Val * 100.0f), 100.0f, 100000.0f);
    }
}
