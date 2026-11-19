#include "Missions/OCObjective.h"

#include "Events/OCEventSubsystem.h"

void UOCObjective::Activate()
{
	if (State == EOCObjectiveState::Inactive)
	{
		State = EOCObjectiveState::Active;
		ActiveSeconds = 0.f;
		FailReason.Reset();
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
	ActiveSeconds = 0.f;
	FailReason.Reset();
	ResetProgress();
}

EOCObjectiveResult UOCObjective::Advance(float DeltaSeconds, const FOCObjectiveContext& Ctx)
{
	if (!IsActive())
	{
		return EOCObjectiveResult::None;
	}
	ActiveSeconds += FMath::Max(DeltaSeconds, 0.f);

	EOCObjectiveResult Result = Evaluate(Ctx);
	if (Result != EOCObjectiveResult::Succeed)
	{
		if (FailAtHeatLevel > 0 && Ctx.HeatLevel >= FailAtHeatLevel)
		{
			Result = EOCObjectiveResult::Fail;
			FailReason = FString::Printf(TEXT("drew police attention (heat %d)"), Ctx.HeatLevel);
		}
		else if (TimeLimitSeconds > 0.f && ActiveSeconds > TimeLimitSeconds)
		{
			Result = EOCObjectiveResult::Fail;
			FailReason = FString::Printf(TEXT("out of time (%.0f s)"), TimeLimitSeconds);
		}
	}
	if (Result == EOCObjectiveResult::Succeed) Succeed();
	else if (Result == EOCObjectiveResult::Fail) Fail();
	return Result;
}

EOCObjectiveResult UOCObjectiveReach::Evaluate(const FOCObjectiveContext& Ctx)
{
	if (!Ctx.bHasPlayer)
	{
		return EOCObjectiveResult::None;
	}
	const bool bNear = FVector::DistSquared2D(Ctx.PlayerLocation, Location) <= FMath::Square(RadiusCm)
		&& FMath::Abs(Ctx.PlayerLocation.Z - Location.Z) <= HeightToleranceCm;
	return bNear ? EOCObjectiveResult::Succeed : EOCObjectiveResult::None;
}

void UOCObjectiveEliminate::OnEvent(const FOCEvent& Ev)
{
	if (!IsActive() || Ev.Tag != OCEventTags::Murder() || !UOCEventSubsystem::IsPlayerInstigated(Ev.Instigator.Get()))
	{
		return;
	}
	if (RadiusCm > 0.f && FVector::DistSquared2D(Ev.Location, Location) > FMath::Square(RadiusCm))
	{
		return;
	}
	++Kills;
}

EOCObjectiveResult UOCObjectiveEliminate::Evaluate(const FOCObjectiveContext& Ctx)
{
	return Kills >= KillCount ? EOCObjectiveResult::Succeed : EOCObjectiveResult::None;
}

EOCObjectiveResult UOCObjectiveSurvive::Evaluate(const FOCObjectiveContext& Ctx)
{
	return ActiveSeconds >= DurationSeconds ? EOCObjectiveResult::Succeed : EOCObjectiveResult::None;
}

EOCObjectiveResult UOCObjectiveLoseHeat::Evaluate(const FOCObjectiveContext& Ctx)
{
	return Ctx.HeatLevel == 0 ? EOCObjectiveResult::Succeed : EOCObjectiveResult::None;
}
