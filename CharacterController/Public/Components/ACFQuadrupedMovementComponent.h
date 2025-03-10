// Copyright (C) Developed by Pask, Published by Dark Tower Interactive SRL 2024. All Rights Reserved.

#pragma once

#include "ACFCCTypes.h"
#include "ACFCharacterMovementComponent.h"
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include <Engine/DataTable.h>

#include "ACFQuadrupedMovementComponent.generated.h"


UCLASS(Blueprintable, ClassGroup = (ACF), meta = (BlueprintSpawnableComponent))
class CHARACTERCONTROLLER_API UACFQuadrupedMovementComponent : public UACFCharacterMovementComponent {
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = ACF)
    void Turn(float Value);

    UFUNCTION(BlueprintCallable, Category = ACF)
    void MoveForwardLocal(float Value);
        
    UFUNCTION(BlueprintPure, Category = ACF)
    bool GetForceSpeedToForward() const
    { return bForceSpeedToForward; }

    UFUNCTION(BlueprintCallable, Category = ACF)
    void SetForceSpeedToForward(bool val) { bForceSpeedToForward = val; }

    UFUNCTION(BlueprintPure, Category = ACF)
    float GetSpeedToForwardInterpRate() const 
    { return SpeedToForwardInterpRate; }

    UFUNCTION(BlueprintCallable, Category = ACF)
    void SetSpeedToForwardInterpRate(float val) { SpeedToForwardInterpRate = val; }

protected:
    virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /*Forces the movement to follow the forward vector of the actor. Usefull for horses or animals that can't strafe*/
    UPROPERTY(EditDefaultsOnly, Category = " ACF | Quadruped")
    bool bForceSpeedToForward = false;

    /*Strength of the force forward*/
    UPROPERTY(EditDefaultsOnly, meta = (EditCondition = "bForceSpeedToForward"), Category = " ACF | Quadruped")
    float SpeedToForwardInterpRate = 20.f;

    /*Aligns the body of the animal with the ground */
    UPROPERTY(EditDefaultsOnly, Category = " ACF | Quadruped")
    bool bAlignBodyWithGround = false;

    /*Speed of the lerp of the alignment*/
    UPROPERTY(EditDefaultsOnly, meta = (EditCondition = "bAlignBodyWithGround"), Category = " ACF | Quadruped")
    float AlignmentSpeed = 15.f;

    /*Limit in degrees of the alignment*/
    UPROPERTY(EditDefaultsOnly, meta = (EditCondition = "bAlignBodyWithGround"), Category = " ACF | Quadruped")
    float AlignmentLimit = 35.f;

private:
    FRotator rotationInput;

    FVector movementInput;
};
