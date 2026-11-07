#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "OCInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * Input actions and mapping contexts for the on-foot player.
 * Asset: /Game/Core/Input/DA_OCInputConfig (built by Content/Python/oc_m1_setup.py).
 */
UCLASS(BlueprintType)
class OPENCITY_API UOCInputConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Always active while on foot. */
	UPROPERTY(EditDefaultsOnly, Category = "Contexts")
	TObjectPtr<UInputMappingContext> OnFootContext;

	/** Added on top of OnFootContext only while aiming. */
	UPROPERTY(EditDefaultsOnly, Category = "Contexts")
	TObjectPtr<UInputMappingContext> AimingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Contexts")
	int32 OnFootPriority = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Contexts")
	int32 AimingPriority = 1;

	/** Axis2D: X = right, Y = forward. */
	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> Move;

	/** Axis2D: X = yaw, Y = pitch. */
	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> Look;

	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> Jump;

	/** Held (keyboard). */
	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> Sprint;

	/** Pressed to toggle (gamepad stick click); clears itself when the player stops. */
	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> SprintToggle;

	/** Pressed to toggle. */
	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> Crouch;

	/** Held. */
	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> Aim;

	/** Use the nearest interactable (enter vehicles). */
	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> Interact;

	/** Only mapped in AimingContext. */
	UPROPERTY(EditDefaultsOnly, Category = "Actions")
	TObjectPtr<UInputAction> SwapShoulder;
};
