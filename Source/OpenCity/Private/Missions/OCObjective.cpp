#include "Missions/OCObjective.h"

#include "Damage/OCDamageable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

void UOCObjective::Activate()
{
	if (State == EOCObjectiveState::Inactive)
	{
		State = EOCObjectiveState::Active;
	}
}

void UOCObjective::Succeed()
{
	if (State == EOCObjectiveState::Active)
	{
		State = EOCObjectiveState::Succeeded;
	}
}

void UOCObjective::Fail()
{
	if (State == EOCObjectiveState::Active)
	{
		State = EOCObjectiveState::Failed;
	}
}

void UOCObjective::Reset()
{
	State = EOCObjectiveState::Inactive;
}

EOCObjectiveResult UOCObjectiveReach::Evaluate_Implementation()
{
	if (State != EOCObjectiveState::Active)
	{
		return EOCObjectiveResult::None;
	}
	// The trigger volume reports entry via the mission subsystem; here we just
	// return None and let the subsystem drive the transition. Subclasses can
	// override with direct checks.
	return EOCObjectiveResult::None;
}

EOCObjectiveResult UOCObjectiveEliminate::Evaluate_Implementation()
{
	if (State != EOCObjectiveState::Active)
	{
		return EOCObjectiveResult::None;
	}
	if (!Target || !Target->IsValidLowLevel())
	{
		return EOCObjectiveResult::Succeed;
	}
	if (IOCDamageable* Damageable = Cast<IOCDamageable>(Target))
	{
		if (Damageable->IsDead())
		{
			return EOCObjectiveResult::Succeed;
		}
	}
	return EOCObjectiveResult::None;
}

EOCObjectiveResult UOCObjectiveSurvive::Evaluate_Implementation()
{
	if (State != EOCObjectiveState::Active)
	{
		return EOCObjectiveResult::None;
	}
	return ElapsedSeconds >= DurationSeconds ? EOCObjectiveResult::Succeed : EOCObjectiveResult::None;
}
