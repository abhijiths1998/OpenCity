#pragma once

#include "CoreMinimal.h"
#include "WheeledVehiclePawn.h"
#include "Interaction/OCInteractable.h"
#include "Vehicles/OCDrivable.h"
#include "OCVehicle.generated.h"

class UCameraComponent;
class UChaosWheeledVehicleMovementComponent;
class UOCRadioComponent;
class UOCVehicleCameraComponent;
class UOCVehicleDamageComponent;
class UOCVehicleData;
class UOCVehicleInputConfig;
class USpringArmComponent;
class UStaticMeshComponent;
struct FInputActionValue;

/**
 * Drivable vehicle on Chaos Vehicles. All tuning comes from UOCVehicleData (copied into the
 * movement component at spawn). Enter via IOCInteractable; the driver pawn is hidden and
 * attached while driving and placed at a free exit spot on the way out.
 * No Tick: input, hits and a short exit timer drive everything.
 */
UCLASS()
class OPENCITY_API AOCVehicle : public AWheeledVehiclePawn, public IOCInteractable, public IOCDrivable
{
	GENERATED_BODY()

public:
	AOCVehicle(const FObjectInitializer& ObjectInitializer);

	// IOCInteractable
	virtual bool CanInteract(const APawn* Instigator) const override;
	virtual bool Interact(APawn* Instigator) override;
	virtual FText GetInteractionText() const override;

	// IOCDrivable
	virtual APawn* GetDriverPawn() const override { return Driver.Get(); }
	virtual APawn* EjectDriverTo(const FVector& Location) override;
	virtual void Wreck() override;

	UFUNCTION(BlueprintPure, Category = "OpenCity|Vehicle")
	bool HasDriver() const { return Driver.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "OpenCity|Vehicle")
	float GetSpeedKmh() const;

	UFUNCTION(BlueprintPure, Category = "OpenCity|Vehicle")
	bool IsWrecked() const;

	/** Speed over the ground in any direction (forward speed ignores sliding). */
	float GetGroundSpeedKmh() const;

	const UOCVehicleData* GetVehicleData() const { return VehicleData; }
	UOCVehicleDamageComponent* GetDamage() const { return Damage; }
	UOCRadioComponent* GetRadio() const { return Radio; }

	/** Exit as the exit button does: immediately if slow enough, otherwise brake first. */
	void RequestExit();

	/** Put the vehicle back on its wheels, facing the same way (the reset button). */
	void ResetUpright();

	/** Leave the vehicle now if a free exit spot exists (ignores speed). */
	bool ForceExit();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;
	virtual void UnPossessed() override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OpenCity|Vehicle")
	TObjectPtr<UOCVehicleData> VehicleData;

	UPROPERTY(EditDefaultsOnly, Category = "OpenCity|Input")
	TObjectPtr<UOCVehicleInputConfig> InputConfig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> ChaseCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UOCVehicleCameraComponent> CameraRig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenCity|Vehicle")
	TObjectPtr<UOCVehicleDamageComponent> Damage;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenCity|Vehicle")
	TObjectPtr<UOCRadioComponent> Radio;

	/** Copy the data asset into mesh and movement component; optionally rebuild physics. */
	virtual void ApplyVehicleData(bool bRebuildPhysics);

	/** Subclasses (motorbike) hook here for per-vehicle extras. */
	virtual void OnDriverEntered() {}
	virtual void OnDriverExited() {}

	UChaosWheeledVehicleMovementComponent* GetWheeledMovement() const;

	/** (Re)create the static mesh parts listed in VehicleData->VisualParts. */
	void BuildVisualParts();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> VisualPartComponents;

private:
	void HandleThrottle(const FInputActionValue& Value);
	void HandleBrake(const FInputActionValue& Value);
	void HandleSteer(const FInputActionValue& Value);
	void HandleHandbrakeStarted(const FInputActionValue& Value);
	void HandleHandbrakeStopped(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleExit(const FInputActionValue& Value);
	void HandleRadioNext(const FInputActionValue& Value);
	void HandleRadioPrevious(const FInputActionValue& Value);
	void HandleReset(const FInputActionValue& Value);

	UFUNCTION()
	void HandleDriverDestroyed(AActor* DestroyedActor);

	void HandleDamaged(float DamageAmount, float HealthFraction);
	void HandleWrecked();

	void CheckPendingExit();
	void ApplyExitBraking();
	bool FindExitSpot(FVector& OutLocation) const;
	void ExitAt(const FVector& Location);
	void Park();

	TWeakObjectPtr<APawn> Driver;
	FTimerHandle PendingExitTimer;
	float BaseMaxTorque = 0.f;
};
