#pragma once

#include "CoreMinimal.h"
#include "OCWantedTypes.generated.h"

/** Heat / wanted level. 0 = clean, 1-5 = escalating police response. */
UENUM(BlueprintType)
enum class EOCHeatLevel : uint8
{
	None      = 0,
	Level1    = 1,   // patrol cars watch, no pursuit
	Level2    = 2,   // police pursue on sight
	Level3    = 3,   // roadblocks, spike strips
	Level4    = 4,   // helicopters, aggressive pursuit
	Level5    = 5,   // maximum response, city-wide search
};

/** A crime that can raise heat if witnessed. */
UENUM(BlueprintType)
enum class EOCrime : uint8
{
	None,
	Gunshot,
	Assault,
	Murder,
	VehicleTheft,
	RecklessDriving,
};

/** One search zone: where police are looking for the player. */
USTRUCT(BlueprintType)
struct OPENCITY_API FOCSearchZone
{
	GENERATED_BODY()

	UPROPERTY()
	FVector Center = FVector::ZeroVector; // zone centre (world cm)

	UPROPERTY()
	float RadiusCm = 0.f;     // zone radius

	UPROPERTY()
	float ExpiresAt = 0.f;    // world time when the zone lapses
};
