// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MathUtils.h"
#include "Planet.generated.h"


UCLASS()
class PROCEDURALPLANET_API APlanet : public AActor
{
        GENERATED_BODY()

    public:
        // Sets default values for this actor's properties
        APlanet();

    protected:
        virtual void BeginPlay() override;

        // Validation functions
        void TestCubeSphereProjection();
        void TestFaceContinuity();
        void TestPrecision();
        void TestEdgeCases();

        // Helper to log test results
        void LogTest(const FString &TestName, bool bPassed, const FString &Details = "");

    private:
        int32 TestsPassed;
        int32 TestsTotal;
};
