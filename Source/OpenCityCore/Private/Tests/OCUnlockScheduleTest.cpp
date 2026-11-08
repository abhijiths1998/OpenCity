#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Progression/OCUnlockSchedule.h"
#include "World/OCRoadGraph.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace OCUnlockScheduleTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	FGameplayTag Tag(const TCHAR* Name) { return FGameplayTag::RequestGameplayTag(FName(Name), false); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCUnlockScheduleRulesTest, "OpenCity.Core.Progression.UnlockSchedule", OCUnlockScheduleTest::Flags)

bool FOCUnlockScheduleRulesTest::RunTest(const FString& Parameters)
{
	using namespace OCUnlockScheduleTest;
	const FGameplayTag Funerale = Tag(TEXT("Story.Mission.Funerale")), Fornace = Tag(TEXT("Story.Mission.LaFornace"));
	if (!TestTrue(TEXT("Story tags are registered (Config/DefaultGameplayTags.ini)"), Funerale.IsValid() && Fornace.IsValid()))
	{
		return false;
	}
	FOCUnlockSchedule S;
	S.AddRule(Tag(TEXT("Area.PortoVecchio")), Funerale);
	S.AddRule(Tag(TEXT("Gate.PonteMobile")), Fornace);
	S.AddRule(Tag(TEXT("Gate.RailCrossing")), Fornace);
	S.AddRule(Tag(TEXT("Area.CittaAlta")), FGameplayTag()); // explicit "open from the start"

	FGameplayTagContainer Done;
	TestTrue(TEXT("No rule: open"), S.IsOpen(Tag(TEXT("Area.LeFornaci")), Done));
	TestTrue(TEXT("Empty requirement: open"), S.IsOpen(Tag(TEXT("Area.CittaAlta")), Done));
	TestFalse(TEXT("Port locked at start"), S.IsOpen(Tag(TEXT("Area.PortoVecchio")), Done));
	TestEqual(TEXT("Both gates closed at start"), S.ClosedGates(Done).Num(), 2);
	TestTrue(TEXT("Closed gate uses the road graph name"), S.ClosedGates(Done).Contains(FName(TEXT("Gate.PonteMobile"))));
	TestFalse(TEXT("Areas are not gates"), S.ClosedGates(Done).Contains(FName(TEXT("Area.PortoVecchio"))));

	Done.AddTag(Fornace);
	TestTrue(TEXT("La Fornace opens the lifting bridge"), S.IsOpen(Tag(TEXT("Gate.PonteMobile")), Done));
	TestEqual(TEXT("...and the rail crossing"), S.ClosedGates(Done).Num(), 0);
	TestFalse(TEXT("but not the port"), S.IsOpen(Tag(TEXT("Area.PortoVecchio")), Done));
	TestEqual(TEXT("Required milestone"), S.RequiredFor(Tag(TEXT("Gate.RailCrossing"))), Fornace);
	TestEqual(TEXT("Distinct milestones"), S.AllMilestones().Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOCUnlockScheduleMareviaTest, "OpenCity.Core.Progression.MareviaSchedule", OCUnlockScheduleTest::Flags)

bool FOCUnlockScheduleMareviaTest::RunTest(const FString& Parameters)
{
	using namespace OCUnlockScheduleTest;
	// The real layout: every district and gate milestone is a registered tag, and story order
	// opens the road network step by step until everything is reachable.
	const FString File = FPaths::ProjectDir() / TEXT("SourceArt/World/marevia_layout.json");
	FString Text;
	TSharedPtr<FJsonObject> Root;
	FOCRoadGraph G;
	FString Error;
	if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)
		|| !G.LoadFromLayoutJson(Text, Error))
	{
		AddError(TEXT("Cannot load the Marevia layout"));
		return false;
	}
	FOCUnlockSchedule S;
	for (const TSharedPtr<FJsonValue>& D : Root->GetArrayField(TEXT("districts")))
	{
		const FString Name = D->AsObject()->GetStringField(TEXT("name")), Unlock = D->AsObject()->GetStringField(TEXT("unlock"));
		const FGameplayTag Area = Tag(*(TEXT("Area.") + Name));
		TestTrue(*FString::Printf(TEXT("Area.%s registered"), *Name), Area.IsValid());
		TestTrue(*FString::Printf(TEXT("%s milestone registered"), *Unlock), Unlock.IsEmpty() || Tag(*Unlock).IsValid());
		S.AddRule(Area, Unlock.IsEmpty() ? FGameplayTag() : Tag(*Unlock));
	}
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("gates")))
	{
		const FString Gate = V->AsObject()->GetStringField(TEXT("tag")), Opens = V->AsObject()->GetStringField(TEXT("opens_with"));
		TestTrue(*FString::Printf(TEXT("%s registered"), *Gate), Tag(*Gate).IsValid());
		S.AddRule(Tag(*Gate), Tag(*Opens));
	}

	const int32 Start = G.FindNearestNode(FOCRoadGraph::LayoutToWorld(520.0, 1290.0, 0.0));
	FGameplayTagContainer Done;
	int32 Reach = G.CountReachable(Start, S.ClosedGates(Done));
	TestEqual(TEXT("All six gates closed in a new game"), S.ClosedGates(Done).Num(), 6);
	TestFalse(TEXT("Porto Vecchio locked in a new game"), S.IsOpen(Tag(TEXT("Area.PortoVecchio")), Done));
	TestTrue(TEXT("Città Alta open in a new game"), S.IsOpen(Tag(TEXT("Area.CittaAlta")), Done));
	TestTrue(TEXT("Le Fornaci open in a new game"), S.IsOpen(Tag(TEXT("Area.LeFornaci")), Done));
	for (const TCHAR* M : { TEXT("Story.Mission.Funerale"), TEXT("Story.Mission.StradaCostiera"), TEXT("Story.Mission.LaCava"),
		TEXT("Story.Mission.LaFornace"), TEXT("Story.Act2.Start") })
	{
		Done.AddTag(Tag(M));
		const int32 Now = G.CountReachable(Start, S.ClosedGates(Done));
		TestTrue(*FString::Printf(TEXT("%s opens more road (%d -> %d)"), M, Reach, Now), Now > Reach);
		Reach = Now;
	}
	TestEqual(TEXT("Every gate open at the end"), S.ClosedGates(Done).Num(), 0);
	TestEqual(TEXT("Whole network reachable at the end"), Reach, G.NumNodes());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
