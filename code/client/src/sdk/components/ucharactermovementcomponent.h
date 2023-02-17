#pragma once

#include <cstdint>

#include "../containers/frotator.h"
#include "../entities/acharacter.h"

namespace SDK {
    enum class EMovementMode : uint8_t { MOVE_None = 0, MOVE_Walking = 1, MOVE_NavWalking = 2, MOVE_Falling = 3, MOVE_Swimming = 4, MOVE_Flying = 5, MOVE_Custom = 6, MOVE_MAX = 7 };

    struct UCharacterMovementComponent {
        unsigned char inheritedSizePadding[0x150];
        char pad0[0x10];
        ACharacter *CharacterOwner;
        float GravityScale;
        float MaxStepHeight;
        float JumpZVelocity;
        float JumpOffJumpZFactor;
        float WalkableFloorAngle;
        float WalkableFloorZ;
        char bMovementModeIsManaged : 1;
        char bMovementModeCalledFromManager : 1;
        char pad_180_2 : 6;
        EMovementMode MovementMode;
        char CustomMovementMode;
        char NetworkSmoothingMode[0x1];
        float GroundFriction;
        char pad1[0x24];
        float MaxWalkSpeed;
        float MaxWalkSpeedCrouched;
        float MaxSwimSpeed;
        float MaxFlySpeed;
        float MaxCustomMovementSpeed;
        float MaxAcceleration;
        float MinAnalogWalkSpeed;
        float BrakingFrictionFactor;
        float BrakingFriction;
        float BrakingSubStepTime;
        float BrakingDecelerationWalking;
        float BrakingDecelerationFalling;
        float BrakingDecelerationSwimming;
        float BrakingDecelerationFlying;
        float AirControl;
        float AirControlBoostMultiplier;
        float AirControlBoostVelocityThreshold;
        float FallingLateralFriction;
        float CrouchedHalfHeight;
        float Buoyancy;
        float PerchRadiusThreshold;
        float PerchAdditionalHeight;
        FRotator RotationRate;
        char bUseSeparateBrakingFriction : 1;
        char bApplyGravityWhileJumping : 1;
        char bUseControllerDesiredRotation : 1;
        char bOrientRotationToMovement : 1;
        char bSweepWhileNavWalking : 1;
        char pad2 : 1;
        char bMovementInProgress : 1;
        char bEnableScopedMovementUpdates : 1;
        char bEnableServerDualMoveScopedMovementUpdates : 1;
        char bForceMaxAccel : 1;
        char bRunPhysicsWithNoController : 1;
        char bForceNextFloorCheck : 1;
        char bShrinkProxyCapsule : 1;
        char bCanWalkOffLedges : 1;
        char bCanWalkOffLedgesWhenCrouching : 1;
        char pad3 : 1;
        char pad4 : 1;
        char bNetworkSkipProxyPredictionOnNetUpdate : 1;
        char bNetworkAlwaysReplicateTransformUpdateTimestamp : 1;
        char bDeferUpdateMoveComponent : 1;
    };
}
