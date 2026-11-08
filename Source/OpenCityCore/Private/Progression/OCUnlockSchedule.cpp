#include "Progression/OCUnlockSchedule.h"

bool FOCUnlockSchedule::IsOpen(const FGameplayTag& Area, const FGameplayTagContainer& Completed) const
{
	const FGameplayTag* Required = Rules.Find(Area);
	return !Required || !Required->IsValid() || Completed.HasTagExact(*Required);
}

TSet<FName> FOCUnlockSchedule::ClosedGates(const FGameplayTagContainer& Completed) const
{
	TSet<FName> Closed;
	for (const TPair<FGameplayTag, FGameplayTag>& Rule : Rules)
	{
		const FName Name = Rule.Key.GetTagName();
		if (Name.ToString().StartsWith(TEXT("Gate.")) && !IsOpen(Rule.Key, Completed))
		{
			Closed.Add(Name);
		}
	}
	return Closed;
}

FGameplayTag FOCUnlockSchedule::RequiredFor(const FGameplayTag& Area) const
{
	const FGameplayTag* Required = Rules.Find(Area);
	return Required ? *Required : FGameplayTag();
}

TArray<FGameplayTag> FOCUnlockSchedule::AllMilestones() const
{
	TArray<FGameplayTag> Out;
	for (const TPair<FGameplayTag, FGameplayTag>& Rule : Rules)
	{
		if (Rule.Value.IsValid())
		{
			Out.AddUnique(Rule.Value);
		}
	}
	return Out;
}
