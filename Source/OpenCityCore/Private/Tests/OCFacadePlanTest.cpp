#include "Misc/AutomationTest.h"
#include "World/OCFacadePlan.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OCFacadePlanTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCFacadePlanLayoutTest, "OpenCity.World.FacadePlan.Layout", OCFacadePlanTest::Flags)
bool FOCFacadePlanLayoutTest::RunTest(const FString& Parameters)
{
	// Every length and seed: bays of 2 or 4 m filling the facade exactly, columns in order, one door on a
	// street facade (none elsewhere), balconies only in 4 m columns, never on the top storey of 3 or more.
	FOCFacadeStyle Style;
	Style.Balcony = 0.5f;
	int32 Bad = 0, Plans = 0;
	for (int32 Units = 2; Units <= 25; ++Units)
	{
		for (int32 Storeys = 1; Storeys <= 9; Storeys += 2)
		{
			for (uint32 Seed = 1; Seed <= 40; ++Seed)
			{
				for (const bool bStreet : { true, false })
				{
					FOCFacadeIn In;
					In.LengthCm = Units * 200.0;
					In.Storeys = Storeys;
					In.bStreet = bStreet;
					const FOCFacadePlan P = OCPlanFacade(In, Style, Seed);
					++Plans;
					double Sum = 0.0;
					int32 Doors = 0;
					bool bOk = true;
					for (const FOCBayColumn& C : P.Columns)
					{
						bOk &= (C.WidthCm == 200.0 || C.WidthCm == 400.0) && FMath::IsNearlyEqual(C.OffsetCm, Sum);
						Sum += C.WidthCm;
						Doors += C.Ground == EOCBay::Door ? 1 : 0;
						bOk &= C.IsFour() || (C.Ground != EOCBay::Shop && C.Ground != EOCBay::GroundWindow && C.BalconyTo < C.BalconyFrom);
						bOk &= C.BalconyTo < C.BalconyFrom || C.BalconyFrom >= 1;
						bOk &= Storeys < 3 || C.At(Storeys - 1) != EOCBay::Balcony;
					}
					bOk &= FMath::IsNearlyEqual(Sum, In.LengthCm) && Doors == (bStreet ? 1 : 0);
					if (!bOk && Bad++ < 5)
					{
						AddError(FString::Printf(TEXT("bad plan: %d m, %d storeys, seed %u, street %d"), Units * 2, Storeys, Seed, bStreet));
					}
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d plans checked"), Plans));
	return Bad == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCFacadePlanSymmetryTest, "OpenCity.World.FacadePlan.Symmetry", OCFacadePlanTest::Flags)
bool FOCFacadePlanSymmetryTest::RunTest(const FString& Parameters)
{
	// Asked for symmetry, a street facade mirrors about its middle: widths, ground uses (but the door) and
	// balcony runs. The door is on the axis whenever the facade has an odd number of 2 m units.
	FOCFacadeStyle Style;
	Style.Symmetric = 1.f;
	Style.Balcony = 0.5f;
	int32 Bad = 0, OnAxis = 0, Odd = 0;
	for (int32 Units = 3; Units <= 25; ++Units)
	{
		for (uint32 Seed = 1; Seed <= 30; ++Seed)
		{
			FOCFacadeIn In;
			In.LengthCm = Units * 200.0;
			In.Storeys = 4;
			const FOCFacadePlan P = OCPlanFacade(In, Style, Seed);
			const int32 N = P.Columns.Num();
			bool bOk = P.bSymmetric;
			for (int32 i = 0; i < N; ++i)
			{
				const FOCBayColumn& A = P.Columns[i];
				const FOCBayColumn& B = P.Columns[N - 1 - i];
				bOk &= A.WidthCm == B.WidthCm && A.Upper == B.Upper && A.BalconyFrom == B.BalconyFrom && A.BalconyTo == B.BalconyTo;
				if (i != P.DoorColumn && N - 1 - i != P.DoorColumn)
				{
					bOk &= A.Ground == B.Ground;
				}
			}
			if (Units % 2 == 1)
			{
				++Odd;
				OnAxis += (N % 2 == 1 && P.DoorColumn == N / 2) ? 1 : 0;
			}
			if (!bOk && Bad++ < 5)
			{
				AddError(FString::Printf(TEXT("not symmetric: %d m, seed %u"), Units * 2, Seed));
			}
		}
	}
	TestEqual(TEXT("door on the axis of every odd facade"), OnAxis, Odd);
	return Bad == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCFacadePlanSeedTest, "OpenCity.World.FacadePlan.Seeds", OCFacadePlanTest::Flags)
bool FOCFacadePlanSeedTest::RunTest(const FString& Parameters)
{
	// The same seed gives the same plan; sub-seeds differ by purpose and part; a side facade with no window
	// chance is blank; a landmark gets no shops and a central door.
	FOCFacadeStyle Style;
	FOCFacadeIn In;
	In.LengthCm = 2000.0;
	In.Storeys = 5;
	const FOCFacadePlan A = OCPlanFacade(In, Style, 77), B = OCPlanFacade(In, Style, 77);
	bool bSame = A.Columns.Num() == B.Columns.Num() && A.DoorColumn == B.DoorColumn;
	for (int32 i = 0; bSame && i < A.Columns.Num(); ++i)
	{
		bSame = A.Columns[i].Ground == B.Columns[i].Ground && A.Columns[i].BalconyTo == B.Columns[i].BalconyTo && A.Columns[i].WidthCm == B.Columns[i].WidthCm;
	}
	TestTrue(TEXT("same seed, same plan"), bSame);
	TestNotEqual(TEXT("purposes differ"), OCSubSeed(5, 1, OCSeedPurpose::Bays), OCSubSeed(5, 1, OCSeedPurpose::Roof));
	TestNotEqual(TEXT("parts differ"), OCSubSeed(5, 1, OCSeedPurpose::Bays), OCSubSeed(5, 2, OCSeedPurpose::Bays));
	TestEqual(TEXT("sub-seed is stable"), OCSubSeed(5, 1, OCSeedPurpose::Bays), OCSubSeed(5, 1, OCSeedPurpose::Bays));

	FOCFacadeStyle Blind;
	Blind.SideWindows = 0.f;
	FOCFacadeIn Side = In;
	Side.bStreet = false;
	const FOCFacadePlan S = OCPlanFacade(Side, Blind, 3);
	TestTrue(TEXT("blind side facade has no openings"), S.Columns.FindByPredicate([](const FOCBayColumn& C) { return C.Ground != EOCBay::Blank || C.Upper != EOCBay::Blank; }) == nullptr);

	FOCFacadeIn Lm = In;
	Lm.LengthCm = 2200.0;
	Lm.bLandmark = true;
	FOCFacadeStyle Shops;
	Shops.Shop = 1.f;
	const FOCFacadePlan L = OCPlanFacade(Lm, Shops, 9);
	TestTrue(TEXT("landmark has no shops"), L.Columns.FindByPredicate([](const FOCBayColumn& C) { return C.Ground == EOCBay::Shop; }) == nullptr);
	TestEqual(TEXT("landmark door on the axis"), L.DoorColumn, L.Columns.Num() / 2);
	return true;
}

#endif
