#include "World/OCTrafficSim.h"
#include "World/OCLaneGraph.h"

namespace OCTraffic
{
	constexpr double KmhToCms = 100000.0 / 3600.0;
	constexpr int32 CurveSamples = 16;
	constexpr double FarAway = 1.0e9;

	FVector Bezier(const FVector& A, const FVector& C, const FVector& B, double T)
	{
		const double U = 1.0 - T;
		return A * (U * U) + C * (2.0 * U * T) + B * (T * T);
	}
}

void FOCTrafficSim::Build(const FOCLaneGraph& InLanes, const FParams& InParams)
{
	Lanes = &InLanes;
	Params = InParams;
	Reset();
	const TArray<FOCLane>& L = InLanes.GetLanes();
	const TArray<FOCLaneLink>& K = InLanes.GetLinks();
	LinkLengths.SetNum(K.Num());
	LinkMinRadius.SetNum(K.Num());
	LinkControl.SetNum(K.Num());
	LinkNode.SetNum(K.Num());
	Conflicts.Reset();
	Conflicts.SetNum(K.Num());
	TArray<TArray<FVector>> Samples;
	Samples.SetNum(K.Num());
	TMap<int32, TArray<int32>> ByNode;
	for (int32 i = 0; i < K.Num(); ++i)
	{
		const FOCLane& From = L[K[i].FromLane];
		const FOCLane& To = L[K[i].ToLane];
		const FVector A = From.End, B = To.Start;
		// Control point: half the gap out along each lane's heading, averaged. A straight-on link is a line,
		// a turn bends round the corner, and a dead-end U-turn loops forward before coming back.
		const double K2 = 0.5 * FMath::Max(FVector::Dist2D(A, B), 100.0);
		LinkControl[i] = 0.5 * ((A + From.Direction() * K2) + (B - To.Direction() * K2));
		LinkNode[i] = From.ToNode;
		ByNode.FindOrAdd(From.ToNode).Add(i);
		double Len = 0.0, MinR = OCTraffic::FarAway;
		FVector Prev = A;
		Samples[i].Add(A);
		for (int32 s = 1; s <= OCTraffic::CurveSamples; ++s)
		{
			const double T = double(s) / OCTraffic::CurveSamples;
			const FVector P = OCTraffic::Bezier(A, LinkControl[i], B, T);
			Len += FVector::Dist(Prev, P);
			Prev = P;
			Samples[i].Add(P);
		}
		// Curvature of a quadratic Bezier (in plan): |B' x B''| / |B'|^3, B' = 2((1-t)(C-A) + t(B-C)), B'' = 2(B - 2C + A).
		const FVector2D D1a(LinkControl[i] - A), D1b(B - LinkControl[i]), D2(B - 2.0 * LinkControl[i] + A);
		for (int32 s = 0; s <= OCTraffic::CurveSamples; ++s)
		{
			const double T = double(s) / OCTraffic::CurveSamples;
			const FVector2D V = 2.0 * ((1.0 - T) * D1a + T * D1b);
			const double Cross = FMath::Abs(FVector2D::CrossProduct(V, 2.0 * D2));
			const double Sp = V.Size();
			if (Cross > 1e-6 && Sp > 1e-3)
			{
				MinR = FMath::Min(MinR, Sp * Sp * Sp / Cross);
			}
		}
		LinkLengths[i] = FMath::Max(Len, 1.0);
		LinkMinRadius[i] = MinR;
	}
	// Conflicts: links through the same junction whose curves come within a car's width, or that end in the
	// same lane (a merge). Links leaving the same lane never conflict: their cars queue on that lane.
	for (const TPair<int32, TArray<int32>>& N : ByNode)
	{
		for (int32 a = 0; a < N.Value.Num(); ++a)
		{
			for (int32 b = a + 1; b < N.Value.Num(); ++b)
			{
				const int32 I = N.Value[a], J = N.Value[b];
				if (K[I].FromLane == K[J].FromLane)
				{
					continue;
				}
				bool bConflict = K[I].ToLane == K[J].ToLane;
				for (int32 p = 0; p < Samples[I].Num() && !bConflict; ++p)
				{
					for (int32 q = 0; q < Samples[J].Num() && !bConflict; ++q)
					{
						bConflict = FVector::Dist2D(Samples[I][p], Samples[J][q]) < Params.ConflictDistanceCm;
					}
				}
				if (bConflict)
				{
					Conflicts[I].Add(J);
					Conflicts[J].Add(I);
				}
			}
		}
	}
}

void FOCTrafficSim::Reset()
{
	Cars.Reset();
	OnLane.Reset();
	OnLink.Reset();
	NextId = 0;
}

double FOCTrafficSim::LaneLength(int32 Lane) const
{
	return Lanes->GetLanes()[Lane].LengthCm();
}

FVector FOCTrafficSim::LinkPoint(int32 Link, double T) const
{
	const FOCLaneLink& K = Lanes->GetLinks()[Link];
	return OCTraffic::Bezier(Lanes->GetLanes()[K.FromLane].End, LinkControl[Link], Lanes->GetLanes()[K.ToLane].Start, FMath::Clamp(T, 0.0, 1.0));
}

bool FOCTrafficSim::LinksConflict(int32 A, int32 B) const
{
	return Conflicts.IsValidIndex(A) && Conflicts[A].Contains(B);
}

double FOCTrafficSim::LinkMaxSpeed(int32 Link) const
{
	return FMath::Sqrt(Params.LateralAccel * LinkMinRadius[Link]);
}

int32 FOCTrafficSim::ChooseLink(int32 Lane, FRandomStream& Rng) const
{
	TArray<int32> Open;
	for (const int32 L : Lanes->GetOutLinks(Lane))
	{
		const FName Gate = Lanes->GetLanes()[Lanes->GetLinks()[L].ToLane].Gate;
		if (Gate.IsNone() || !ClosedGates.Contains(Gate))
		{
			Open.Add(L);
		}
	}
	return Open.IsEmpty() ? INDEX_NONE : Open[Rng.RandRange(0, Open.Num() - 1)];
}

int32 FOCTrafficSim::Spawn(int32 Lane, double S, double Speed, FRandomStream& Rng)
{
	if (!Lanes || !Lanes->GetLanes().IsValidIndex(Lane))
	{
		return INDEX_NONE;
	}
	FOCTrafficCar& Car = Cars.AddDefaulted_GetRef();
	Car.Id = NextId++;
	Car.Lane = Lane;
	Car.S = FMath::Clamp(S, 0.0, LaneLength(Lane));
	Car.Speed = FMath::Max(0.0, Speed);
	Car.Temper = Rng.FRandRange(0.85f, 1.1f);
	Car.NextLink = ChooseLink(Lane, Rng);
	return Car.Id;
}

void FOCTrafficSim::Remove(int32 Id)
{
	Cars.RemoveAll([Id](const FOCTrafficCar& C) { return C.Id == Id; });
}

const FOCTrafficCar* FOCTrafficSim::FindCar(int32 Id) const
{
	return Cars.FindByPredicate([Id](const FOCTrafficCar& C) { return C.Id == Id; });
}

double FOCTrafficSim::SpeedLimit(const FOCTrafficCar& Car) const
{
	const TArray<FOCLane>& L = Lanes->GetLanes();
	if (Car.Link != INDEX_NONE)
	{
		const FOCLaneLink& K = Lanes->GetLinks()[Car.Link];
		const double Lane = FMath::Min(L[K.FromLane].SpeedKmh, L[K.ToLane].SpeedKmh) * OCTraffic::KmhToCms * Car.Temper;
		return FMath::Max(150.0, FMath::Min(Lane, LinkMaxSpeed(Car.Link)));
	}
	return FMath::Max(150.0, L[Car.Lane].SpeedKmh * OCTraffic::KmhToCms * Car.Temper);
}

bool FOCTrafficSim::CanEnter(int32 Link, int32 CarIndex) const
{
	const int32 Self = Cars[CarIndex].Id;
	for (const int32 C : Conflicts[Link])
	{
		if (OnLink.Contains(C))
		{
			return false;
		}
	}
	int32 Ahead = 0;   // cars already crossing on this link, or holding it, that will need room in the exit lane
	for (const FOCTrafficCar& O : Cars)
	{
		if (O.Id == Self)
		{
			continue;
		}
		if (O.Link == INDEX_NONE && O.bReserved && O.NextLink != INDEX_NONE)
		{
			if (O.NextLink == Link)
			{
				++Ahead;
			}
			else if (Conflicts[Link].Contains(O.NextLink))
			{
				return false;
			}
		}
		else if (O.Link == Link)
		{
			++Ahead;
		}
	}
	// Don't block the box: the exit lane must hold this car and every one ahead of it on the link.
	const int32 To = Lanes->GetLinks()[Link].ToLane;
	const double Need = FMath::Min((Ahead + 1) * (Params.CarLengthCm + Params.MinGapCm), LaneLength(To) - 1.0);
	if (const TArray<int32>* There = OnLane.Find(To))
	{
		if (There->Num() > 0 && Cars[(*There)[0]].S - Params.CarLengthCm < Need)
		{
			return false;
		}
	}
	return true;
}

void FOCTrafficSim::FindLeader(int32 CarIndex, double Horizon, double& OutGap, double& OutLeaderSpeed) const
{
	const FOCTrafficCar& Car = Cars[CarIndex];
	OutGap = OCTraffic::FarAway;
	OutLeaderSpeed = 0.0;
	auto FirstAfter = [this](const TArray<int32>* List, double After, int32 Skip) -> int32
	{
		if (List)
		{
			for (const int32 i : *List)   // sorted by S
			{
				if (i != Skip && Cars[i].S > After)
				{
					return i;
				}
			}
		}
		return INDEX_NONE;
	};
	const TArray<FOCLaneLink>& K = Lanes->GetLinks();
	double Offset = -Car.S;   // distance from this car's front to the start of the stretch being searched
	int32 Lane = Car.Link == INDEX_NONE ? Car.Lane : INDEX_NONE;
	int32 Link = Car.Link;
	double After = Car.S;
	// Walk: own lane or link, then (from a lane) the chosen link, then the lane after it.
	for (int32 Hop = 0; Hop < 3 && Offset < Horizon; ++Hop)
	{
		const int32 Found = FirstAfter(Lane != INDEX_NONE ? OnLane.Find(Lane) : OnLink.Find(Link), After, CarIndex);
		if (Found != INDEX_NONE)
		{
			OutGap = Offset + Cars[Found].S - Params.CarLengthCm;
			OutLeaderSpeed = Cars[Found].Speed;
			return;
		}
		if (Lane != INDEX_NONE)
		{
			const int32 Next = (Lane == Car.Lane && Car.Link == INDEX_NONE) ? Car.NextLink : INDEX_NONE;
			if (Next == INDEX_NONE)
			{
				return;
			}
			Offset += LaneLength(Lane);
			Link = Next;
			Lane = INDEX_NONE;
		}
		else
		{
			Offset += LinkLengths[Link];
			Lane = K[Link].ToLane;
			Link = INDEX_NONE;
		}
		After = -OCTraffic::FarAway;
	}
}

void FOCTrafficSim::Step(double Dt, FRandomStream& Rng)
{
	if (!Lanes || Dt <= 0.0)
	{
		return;
	}
	OnLane.Reset();
	OnLink.Reset();
	for (int32 i = 0; i < Cars.Num(); ++i)
	{
		(Cars[i].Link != INDEX_NONE ? OnLink.FindOrAdd(Cars[i].Link) : OnLane.FindOrAdd(Cars[i].Lane)).Add(i);
	}
	auto BySpeed = [this](TMap<int32, TArray<int32>>& M)
	{
		for (TPair<int32, TArray<int32>>& E : M)
		{
			E.Value.Sort([this](int32 A, int32 B) { return Cars[A].S < Cars[B].S; });
		}
	};
	BySpeed(OnLane);
	BySpeed(OnLink);

	const double SqrtAB = FMath::Sqrt(Params.MaxAccel * Params.ComfortDecel);
	auto Idm = [&](double V, double V0, double Gap, double LeaderV)
	{
		const double SStar = Params.MinGapCm + FMath::Max(0.0, V * Params.HeadwayS + V * (V - LeaderV) / (2.0 * SqrtAB));
		const double Free = 1.0 - FMath::Pow(V / FMath::Max(V0, 1.0), 4.0);
		const double Inter = Gap < OCTraffic::FarAway * 0.5 ? FMath::Square(SStar / FMath::Max(Gap, 1.0)) : 0.0;
		return Params.MaxAccel * (Free - Inter);
	};

	// Accelerations first (everyone sees the same state), then move.
	TArray<double> Accel;
	Accel.SetNumZeroed(Cars.Num());
	for (int32 i = 0; i < Cars.Num(); ++i)
	{
		FOCTrafficCar& Car = Cars[i];
		const double V = Car.Speed;
		const double V0 = SpeedLimit(Car);
		const double Stopping = V * V / (2.0 * Params.ComfortDecel);
		double Gap, LeaderV;
		FindLeader(i, FMath::Max(5000.0, 3.0 * Stopping + 2000.0), Gap, LeaderV);
		double A = Idm(V, V0, Gap, LeaderV);
		Car.bHolding = false;
		if (Car.Link == INDEX_NONE)
		{
			const double ToEnd = LaneLength(Car.Lane) - Car.S;
			if (Car.NextLink == INDEX_NONE)
			{
				A = FMath::Min(A, Idm(V, V0, ToEnd, 0.0));   // no way on: stop at the end of the lane
			}
			else
			{
				// Commit once inside stopping distance plus a margin; until then, hold at the stop line if the
				// junction is not clear.
				if (!Car.bReserved && ToEnd < Stopping + Params.MinGapCm + 100.0 && CanEnter(Car.NextLink, i))
				{
					Car.bReserved = true;
				}
				if (!Car.bReserved && ToEnd < 3.0 * Stopping + 3000.0 && !CanEnter(Car.NextLink, i))
				{
					A = FMath::Min(A, Idm(V, V0, ToEnd, 0.0));
					Car.bHolding = true;
				}
				// Slow for the curve ahead.
				const double Vc = FMath::Max(150.0, LinkMaxSpeed(Car.NextLink));
				if (V > Vc)
				{
					A = FMath::Min(A, -(V * V - Vc * Vc) / (2.0 * FMath::Max(ToEnd, 100.0)));
				}
			}
		}
		Accel[i] = FMath::Clamp(A, -Params.MaxDecel, Params.MaxAccel);
	}

	const TArray<FOCLaneLink>& K = Lanes->GetLinks();
	for (int32 i = 0; i < Cars.Num(); ++i)
	{
		FOCTrafficCar& Car = Cars[i];
		Car.Accel = Accel[i];
		const double V1 = FMath::Max(0.0, Car.Speed + Accel[i] * Dt);
		const double Move = 0.5 * (Car.Speed + V1) * Dt;
		Car.Speed = V1;
		Car.S += Move;
		Car.Driven += Move;
		Car.StoppedFor = V1 < 10.0 ? Car.StoppedFor + Dt : 0.0;
		for (int32 Guard = 0; Guard < 3; ++Guard)
		{
			if (Car.Link == INDEX_NONE)
			{
				const double Len = LaneLength(Car.Lane);
				if (Car.S < Len)
				{
					break;
				}
				if (Car.NextLink != INDEX_NONE && (Car.bReserved || CanEnter(Car.NextLink, i)))
				{
					Car.S -= Len;
					Car.Link = Car.NextLink;
					Car.bReserved = false;
					OnLink.FindOrAdd(Car.Link).Add(i);   // later cars this step see it in the junction
				}
				else
				{
					Car.S = Len;
					Car.Speed = 0.0;
					break;
				}
			}
			else
			{
				const double Len = LinkLengths[Car.Link];
				if (Car.S < Len)
				{
					break;
				}
				Car.S -= Len;
				if (TArray<int32>* There = OnLink.Find(Car.Link))
				{
					There->Remove(i);
					if (There->IsEmpty())
					{
						OnLink.Remove(Car.Link);
					}
				}
				Car.Lane = K[Car.Link].ToLane;
				Car.Link = INDEX_NONE;
				Car.NextLink = ChooseLink(Car.Lane, Rng);
			}
		}
	}
}

FOCTrafficPose FOCTrafficSim::GetPose(const FOCTrafficCar& Car) const
{
	// The car's centre, half a length behind its front bumper.
	FOCTrafficPose Pose;
	const TArray<FOCLane>& L = Lanes->GetLanes();
	const double Back = Car.S - 0.5 * Params.CarLengthCm;
	if (Car.Link != INDEX_NONE)
	{
		const double Len = LinkLengths[Car.Link];
		if (Back >= 0.0)
		{
			const double T = Back / Len;
			const FVector P = LinkPoint(Car.Link, T);
			Pose.Location = P;
			Pose.Forward = (LinkPoint(Car.Link, T + 0.02) - LinkPoint(Car.Link, T - 0.02)).GetSafeNormal();
		}
		else
		{
			const FOCLane& From = L[Lanes->GetLinks()[Car.Link].FromLane];
			Pose.Location = From.End + From.Direction() * Back;
			Pose.Forward = From.Direction();
		}
		return Pose;
	}
	const FOCLane& Lane = L[Car.Lane];
	Pose.Location = Lane.Start + Lane.Direction() * Back;
	Pose.Forward = Lane.Direction();
	return Pose;
}

int32 FOCTrafficSim::CountConflictingCrossings() const
{
	int32 Count = 0;
	for (int32 a = 0; a < Cars.Num(); ++a)
	{
		for (int32 b = a + 1; b < Cars.Num(); ++b)
		{
			if (Cars[a].Link != INDEX_NONE && Cars[b].Link != INDEX_NONE && LinksConflict(Cars[a].Link, Cars[b].Link))
			{
				++Count;
			}
		}
	}
	return Count;
}
