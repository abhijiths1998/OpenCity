#include "Population/OCCrowdSim.h"

#include "Math/UnrealMathUtility.h"

namespace
{
	constexpr double DegSToRadPerCmS = 180.0 / PI;
}

int32 FOCCrowdSim::Spawn(const FVector2D& Pos, FRandomStream& Rng)
{
	FOCPedestrian P;
	P.Id = NextId++;
	P.Pos = Pos;
	P.Dir = FVector2D(Rng.FRand(), Rng.FRand()).GetSafeNormal();
	P.Speed = Params.WalkSpeedCm * (0.7 + Rng.FRand() * 0.6);
	P.State = EOCPedState::Idle;
	P.StateTimer = Rng.FRandRange((float)Params.IdleMinS, (float)Params.IdleMaxS);
	P.Health = 1.f;
	int32 Id = Peds.Add(P);
	return Id;
}

void FOCCrowdSim::Remove(int32 Id)
{
	for (int32 i = Peds.Num() - 1; i >= 0; --i)
	{
		if (Peds[i].Id == Id)
		{
			Peds.RemoveAtSwap(i);
			return;
		}
	}
}

void FOCCrowdSim::Reset()
{
	Peds.Reset();
	Threats.Reset();
	NextId = 0;
}

void FOCCrowdSim::PickWanderTarget(FOCPedestrian& P, FRandomStream& Rng) const
{
	const double Angle = Rng.FRandRange(0.f, (float)(2.0 * PI));
	const double Dist = Rng.FRandRange(0.f, (float)Params.WanderRadiusCm);	P.WanderTarget = P.Pos + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * (float)Dist;
}

void FOCCrowdSim::SteerToward(FOCPedestrian& P, const FVector2D& To, double Dt) const
{
	const FVector2D Want = (To - P.Pos).GetSafeNormal();
	if (Want.IsNearlyZero())
	{
		return;
	}
	const double MaxTurn = Params.TurnRateDegS * (PI / 180.0) * Dt;
	const double Cur = FMath::Atan2(P.Dir.Y, P.Dir.X);
	const double Trg = FMath::Atan2(Want.Y, Want.X);
	double Delta = Trg - Cur;
	while (Delta > PI) Delta -= 2.0 * PI;
	while (Delta < -PI) Delta += 2.0 * PI;
	Delta = FMath::Clamp(Delta, -MaxTurn, MaxTurn);
	const double NewAngle = Cur + Delta;
	P.Dir = FVector2D(FMath::Cos(NewAngle), FMath::Sin(NewAngle));
}

int32 FOCCrowdSim::NearestThreat(const FOCPedestrian& P) const
{
	int32 Best = INDEX_NONE;
	double BestDistSq = TNumericLimits<double>::Max();
	for (int32 i = 0; i < Threats.Num(); ++i)
	{
		const FVector2D D = Threats[i].Pos - P.Pos;
		const double DistSq = D.SizeSquared();
		if (DistSq < FMath::Square(Params.ThreatRadiusCm) && DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = i;
		}
	}
	return Best;
}

void FOCCrowdSim::Step(double Dt, double Now, FRandomStream& Rng)
{
	// Reset per-step threat list once consumed; keep it for neighbour separation too below.
	TArray<FVector2D> Separation;
	Separation.SetNum(Peds.Num());
	for (int32 i = 0; i < Peds.Num(); ++i)
	{
		FOCPedestrian& P = Peds[i];
		if (P.Health <= 0.f)
		{
			P.State = EOCPedState::Dead;
			P.Speed = 0.0;
			continue;
		}

		const int32 Th = NearestThreat(P);
		if (Th != INDEX_NONE)
		{
			// Remember how long to keep fleeing after this threat passes.
			P.FleeUntil = Now + Params.FleeDurationS;
		}
		const bool bFleeing = Now < P.FleeUntil;

		if (bFleeing && Th != INDEX_NONE)
		{
			const FVector2D Away = (P.Pos - Threats[Th].Pos).GetSafeNormal();
			SteerToward(P, P.Pos + Away * 1000.0, Dt);
			P.Speed = Params.FleeSpeedCm;
			P.State = EOCPedState::Flee;
		}
		else if (P.State == EOCPedState::Flee && Now >= P.FleeUntil)
		{
			P.State = EOCPedState::Wander;
			P.Speed = Params.WalkSpeedCm * (0.7 + Rng.FRand() * 0.6);
			P.StateTimer = Rng.FRandRange((float)Params.IdleMinS, (float)Params.IdleMaxS);
		}
		else if (P.State == EOCPedState::Idle)
		{
			P.StateTimer -= Dt;
			P.Speed = 0.0;
			if (P.StateTimer <= 0.0)
			{
				PickWanderTarget(P, Rng);
				P.State = EOCPedState::Wander;
				P.Speed = Params.WalkSpeedCm;
			}
		}
		else if (P.State == EOCPedState::Wander)
		{
			SteerToward(P, P.WanderTarget, Dt);
			if (FVector2D::DistSquared(P.Pos, P.WanderTarget) < FMath::Square(150.0))
			{
				P.State = EOCPedState::Idle;
				P.StateTimer = Rng.FRandRange((float)Params.IdleMinS, (float)Params.IdleMaxS);
				P.Speed = 0.0;
			}
		}

		// Neighbour separation: push apart from peds too close.
		FVector2D Push = FVector2D::ZeroVector;
		for (int32 j = 0; j < Peds.Num(); ++j)
		{
			if (j == i || Peds[j].Health <= 0.f) continue;
			const FVector2D D = P.Pos - Peds[j].Pos;
			const double DistSq = D.SizeSquared();
			const double WantSq = FMath::Square(Params.SeparationCm);
			if (DistSq > 0.0 && DistSq < WantSq)
			{
				Push += D.GetSafeNormal() * (float)(1.0 - FMath::Sqrt(DistSq / WantSq));
			}
		}
		if (!Push.IsNearlyZero())
		{
			const FVector2D NewDir = (P.Dir + Push * 2.0).GetSafeNormal();
			P.Dir = NewDir;
		}

		if (P.Speed > 0.0 && P.Dir.SizeSquared() > 0.0)
		{
			P.Pos += P.Dir * (float)(P.Speed * Dt);
		}
	}

	Threats.Reset();
}

bool FOCCrowdSim::Damage(int32 Id, float Amount)
{
	for (FOCPedestrian& P : Peds)
	{
		if (P.Id == Id)
		{
			if (P.Health <= 0.f) return false;
			P.Health = FMath::Max(0.f, P.Health - Amount);
			if (P.Health <= 0.f)
			{
				P.State = EOCPedState::Dead;
				P.Speed = 0.0;
			}
			return true;
		}
	}
	return false;
}

const FOCPedestrian* FOCCrowdSim::FindPed(int32 Id) const
{
	for (const FOCPedestrian& P : Peds)
	{
		if (P.Id == Id) return &P;
	}
	return nullptr;
}
