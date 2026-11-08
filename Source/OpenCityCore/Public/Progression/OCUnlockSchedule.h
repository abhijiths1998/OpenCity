#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

/**
 * Which areas and gates are open for a set of completed story milestones. Pure logic, unit-tested;
 * the rules come from data (UOCAreaUnlockData), never code. An area with no rule is always open.
 * Areas are tags such as Area.PortoVecchio; gates are Gate.* tags matching the road graph's gates.
 */
class OPENCITYCORE_API FOCUnlockSchedule
{
public:
	void Reset() { Rules.Reset(); }

	/** Area (or gate) opens once Required is among the completed milestones. */
	void AddRule(const FGameplayTag& Area, const FGameplayTag& Required) { Rules.Add(Area, Required); }

	bool IsOpen(const FGameplayTag& Area, const FGameplayTagContainer& Completed) const;

	/** Gate.* areas still closed, as road graph gate names (for FOCRoadGraph pathing). */
	TSet<FName> ClosedGates(const FGameplayTagContainer& Completed) const;

	/** The milestone an area waits for (invalid if none). */
	FGameplayTag RequiredFor(const FGameplayTag& Area) const;

	/** Every milestone any rule waits for. */
	TArray<FGameplayTag> AllMilestones() const;

	int32 Num() const { return Rules.Num(); }

private:
	TMap<FGameplayTag, FGameplayTag> Rules;
};
