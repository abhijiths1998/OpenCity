#include "World/OCFacadePlan.h"

#include "Math/RandomStream.h"

EOCBay FOCBayColumn::At(int32 Storey) const
{
	if (Storey <= 0)
	{
		return Ground;
	}
	return (Storey >= BalconyFrom && Storey <= BalconyTo) ? EOCBay::Balcony : Upper;
}

uint32 OCSubSeed(uint32 Seed, uint32 Part, uint32 Purpose)
{
	// SplitMix-style mixing of the three words.
	uint64 H = (uint64(Seed) << 32) ^ (uint64(Part) * 0x9E3779B97F4A7C15ull) ^ (uint64(Purpose) * 0xC2B2AE3D27D4EB4Full);
	H ^= H >> 30;
	H *= 0xBF58476D1CE4E5B9ull;
	H ^= H >> 27;
	H *= 0x94D049BB133111EBull;
	H ^= H >> 31;
	return uint32(H ^ (H >> 32));
}

TArray<double> OCFacadeBays(double LengthCm, bool bSymmetric, int32 TwoAt, bool bCentreFour)
{
	const int32 Units = FMath::Max(2, FMath::RoundToInt32(LengthCm / 200.0));   // 2 m units
	TArray<double> Out;
	if (bSymmetric && Units > 2)
	{
		// Mirror halves round an optional centre bay (2 m, or 4 m when asked and the count is even); an odd
		// half ends in a 2 m bay at the corner.
		const bool bFourCentre = bCentreFour && Units % 2 == 0 && Units >= 4;
		const bool bCentre = Units % 2 == 1 || bFourCentre;
		const int32 Half = (Units - (bFourCentre ? 2 : (bCentre ? 1 : 0))) / 2;
		TArray<double> HalfSeq;
		for (int32 i = 0; i < Half / 2; ++i)
		{
			HalfSeq.Add(400.0);
		}
		if (Half % 2 == 1)
		{
			HalfSeq.Add(200.0);   // outer end
		}
		for (int32 i = HalfSeq.Num() - 1; i >= 0; --i)
		{
			Out.Add(HalfSeq[i]);
		}
		if (bCentre)
		{
			Out.Add(bFourCentre ? 400.0 : 200.0);
		}
		Out.Append(HalfSeq);
		return Out;
	}
	for (int32 i = 0; i < Units / 2; ++i)
	{
		Out.Add(400.0);
	}
	if (Units % 2 == 1)
	{
		Out.Insert(200.0, FMath::Clamp(TwoAt, 0, Out.Num()));
	}
	return Out;
}

FOCFacadePlan OCPlanFacade(const FOCFacadeIn& In, const FOCFacadeStyle& Style, uint32 Seed)
{
	FRandomStream R(int32(Seed & 0x7fffffff));
	FOCFacadePlan Plan;
	const int32 Units = FMath::Max(2, FMath::RoundToInt32(In.LengthCm / 200.0));
	Plan.bSymmetric = In.bStreet && (In.bLandmark || R.FRand() < Style.Symmetric);
	Plan.bPlainTop = In.Storeys >= 3 && R.FRand() < 0.4f;
	// Asymmetric: a lone 2 m bay goes to an end (a townhouse's door bay) more often than the middle.
	const int32 Fours = Units / 2;
	const int32 TwoAt = R.FRand() < 0.7f ? (R.FRand() < 0.5f ? 0 : Fours) : R.RandRange(0, Fours);
	const TArray<double> Widths = OCFacadeBays(Units * 200.0, Plan.bSymmetric, TwoAt, In.bWideDoor && In.bDoor);
	double Offset = 0.0;
	for (const double W : Widths)
	{
		FOCBayColumn& C = Plan.Columns.AddDefaulted_GetRef();
		C.OffsetCm = Offset;
		C.WidthCm = W;
		Offset += W;
	}
	const int32 N = Plan.Columns.Num();
	auto Mirror = [N](int32 i) { return N - 1 - i; };
	// Choices are made for the first half (and the centre) and copied to the mirror column when symmetric.
	const int32 Decide = Plan.bSymmetric ? (N + 1) / 2 : N;
	auto Copy = [&](int32 i)
	{
		if (Plan.bSymmetric && Mirror(i) != i)
		{
			const FOCBayColumn& Src = Plan.Columns[i];
			FOCBayColumn& Dst = Plan.Columns[Mirror(i)];
			Dst.Ground = Src.Ground;
			Dst.Upper = Src.Upper;
			Dst.BalconyFrom = Src.BalconyFrom;
			Dst.BalconyTo = Src.BalconyTo;
		}
	};

	if (!In.bStreet)
	{
		// Side and back: each column is windows all the way up or blank all the way up.
		for (int32 i = 0; i < N; ++i)
		{
			FOCBayColumn& C = Plan.Columns[i];
			const bool bWindows = R.FRand() < Style.SideWindows;
			C.Upper = bWindows ? EOCBay::Window : EOCBay::Blank;
			C.Ground = (bWindows && C.IsFour() && R.FRand() < 0.6f) ? EOCBay::GroundWindow : EOCBay::Blank;
		}
		return Plan;
	}

	// The door: on the axis if there is a centre bay, else in a 2 m corner bay, else beside the axis.
	if (!In.bDoor)
	{
	}
	else if (Plan.bSymmetric)
	{
		if (N % 2 == 1 && (!Plan.Columns[N / 2].IsFour() || In.bWideDoor))
		{
			Plan.DoorColumn = N / 2;   // the centre bay: 2 m, or the 4 m one a wide door asked for
		}
		else if (!Plan.Columns[0].IsFour())
		{
			Plan.DoorColumn = R.FRand() < 0.5f ? 0 : N - 1;
		}
		else
		{
			Plan.DoorColumn = N / 2;   // N even: the column right of the axis; door in its near half
			Plan.Columns[Plan.DoorColumn].bDoorAtEnd = false;
			if (N % 2 == 1)
			{
				Plan.Columns[Plan.DoorColumn].bDoorAtEnd = R.FRand() < 0.5f;   // a 4 m centre column
			}
		}
	}
	else
	{
		Plan.DoorColumn = Plan.Columns.IndexOfByPredicate([](const FOCBayColumn& C) { return !C.IsFour(); });
		if (Plan.DoorColumn == INDEX_NONE)
		{
			Plan.DoorColumn = R.FRand() < 0.7f ? (R.FRand() < 0.5f ? 0 : N - 1) : R.RandRange(0, N - 1);
			Plan.Columns[Plan.DoorColumn].bDoorAtEnd = Plan.DoorColumn == N - 1 ? true : (Plan.DoorColumn == 0 ? false : R.FRand() < 0.5f);
		}
	}

	// Ground floor: shops and barred windows in the 4 m columns, plain wall in the 2 m ones.
	const float ShopChance = In.bLandmark ? 0.f : Style.Shop;
	for (int32 i = 0; i < Decide; ++i)
	{
		FOCBayColumn& C = Plan.Columns[i];
		C.Ground = !C.IsFour() ? EOCBay::Blank : (R.FRand() < ShopChance ? EOCBay::Shop : EOCBay::GroundWindow);
		C.Upper = EOCBay::Window;
		Copy(i);
	}
	if (Plan.DoorColumn != INDEX_NONE)
	{
		Plan.Columns[Plan.DoorColumn].Ground = EOCBay::Door;
	}

	// Balconies: one scheme per facade, on 4 m columns, from the first floor up to below the top storey
	// (a piano nobile has only the first floor).
	const float Scheme = R.FRand();
	if (!In.bLandmark && In.Storeys >= 2 && Scheme < FMath::Min(1.f, Style.Balcony * 2.f))
	{
		const int32 TopBalcony = In.Storeys >= 3 ? In.Storeys - 2 : 1;
		const int32 To = R.FRand() < 0.35f ? 1 : TopBalcony;
		TArray<int32> FourCols;
		for (int32 i = 0; i < N; ++i)
		{
			if (Plan.Columns[i].IsFour())
			{
				FourCols.Add(i);
			}
		}
		// Pattern over the 4 m columns: 0 centre, 1 ends, 2 alternate (odd count only when symmetric), 3 all.
		const int32 K = FourCols.Num();
		int32 Pattern = R.RandRange(0, 3);
		if (Pattern == 2 && (K < 3 || (Plan.bSymmetric && K % 2 == 0)))
		{
			Pattern = 0;
		}
		if (Pattern == 1 && K < 3)
		{
			Pattern = 3;
		}
		for (int32 k = 0; k < K; ++k)
		{
			bool bOn = false;
			switch (Pattern)
			{
			case 0: bOn = (K % 2 == 1) ? k == K / 2 : (k == K / 2 || k == K / 2 - 1); break;
			case 1: bOn = k == 0 || k == K - 1; break;
			case 2: bOn = k % 2 == 1; break;
			default: bOn = true; break;
			}
			if (bOn)
			{
				Plan.Columns[FourCols[k]].BalconyFrom = 1;
				Plan.Columns[FourCols[k]].BalconyTo = To;
			}
		}
	}
	return Plan;
}
