// Map checks and small map updates (formerly oc_m3_verify.py, oc_validate_networks.py, oc_m4b_look.py).
#include "OCWorldCommandlets.h"

#include "Editor.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "OCEditorHelpers.h"
#include "OCLog.h"
#include "Progression/OCAreaUnlockData.h"
#include "Progression/OCProgressionSettings.h"
#include "World/OCInfraNetworkAssets.h"
#include "World/OCRoadGraphAsset.h"
#include "World/OCWorldBuild.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHelpers.h"

using namespace OCWorldBuild;

namespace
{
	void InitCommandlet(UCommandlet* C)
	{
		C->IsClient = false;
		C->IsEditor = true;
		C->IsServer = false;
		C->LogToConsole = true;
	}

	FJsonPtr LoadLayout()
	{
		FString Text, Error;
		const FString Path = OCEd::WorldDir() / TEXT("marevia_layout.json");
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			UE_LOG(LogOpenCity, Error, TEXT("cannot read %s"), *Path);
			return nullptr;
		}
		FJsonPtr Layout = OCLayout::FJson::Parse(Text, Error);
		if (!Layout.IsValid())
		{
			UE_LOG(LogOpenCity, Error, TEXT("%s: %s"), *Path, *Error);
		}
		return Layout;
	}

	struct FChecks
	{
		const TCHAR* Tag;
		TArray<FString> Failures;

		void Check(const FString& Name, bool bOk, const FString& Detail = FString())
		{
			OCEd::Log(Tag, FString::Printf(TEXT("%s %s%s%s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *Name, Detail.IsEmpty() ? TEXT("") : TEXT(": "), *Detail));
			if (!bOk)
			{
				Failures.Add(Name);
			}
		}
	};
}

// ---- OCVerifyMarevia ------------------------------------------------------------------------------

UOCVerifyMareviaCommandlet::UOCVerifyMareviaCommandlet() { InitCommandlet(this); }

int32 UOCVerifyMareviaCommandlet::Main(const FString& Params)
{
	const FJsonPtr Layout = LoadLayout();
	if (!Layout.IsValid())
	{
		return 1;
	}
	FChecks C{ TEXT("oc_m3_verify") };
	if (!UEditorLoadingAndSavingUtils::LoadMap(MapPath))
	{
		UE_LOG(LogOpenCity, Error, TEXT("could not load %s"), MapPath);
		return 1;
	}
	// Every actor of the map from its World Partition descriptor, loaded or not.
	struct FDesc { FString Label; FBox Bounds; FString NativeClass; TArray<FString> DataLayers; };
	TArray<FDesc> Descs;
	UWorldPartition* Wp = GEditor->GetEditorWorldContext().World()->GetWorldPartition();
	if (!Wp)
	{
		UE_LOG(LogOpenCity, Error, TEXT("%s is not a World Partition map"), MapPath);
		return 1;
	}
	FWorldPartitionHelpers::ForEachActorDescInstance(Wp, AActor::StaticClass(), [&Descs](const FWorldPartitionActorDescInstance* D)
	{
		FDesc& Out = Descs.AddDefaulted_GetRef();
		Out.Label = D->GetActorLabel().ToString();
		Out.Bounds = D->GetEditorBounds();
		Out.NativeClass = D->GetActorNativeClass() ? D->GetActorNativeClass()->GetName() : FString();
		for (const FName& L : D->GetDataLayers())
		{
			Out.DataLayers.Add(FSoftObjectPath(L.ToString()).GetAssetName());
		}
		return true;
	});
	TMap<FString, TArray<const FDesc*>> ByLabel;
	for (const FDesc& D : Descs)
	{
		ByLabel.FindOrAdd(D.Label).Add(&D);
	}
	auto CountPrefix = [&ByLabel](std::initializer_list<const TCHAR*> Prefixes)
	{
		int32 N = 0;
		for (const TPair<FString, TArray<const FDesc*>>& P : ByLabel)
		{
			for (const TCHAR* Prefix : Prefixes)
			{
				if (P.Key.StartsWith(Prefix, ESearchCase::CaseSensitive)) { N += P.Value.Num(); break; }
			}
		}
		return N;
	};
	auto Has = [&ByLabel](const FString& L) { return ByLabel.Contains(L); };
	C.Check(TEXT("actor descriptors"), Descs.Num() > 0, FString::FromInt(Descs.Num()));

	// Every story landmark: a named grey-box, centred in its district.
	TMap<FString, FJsonPtr> Rects;
	for (const FJsonPtr& D : Arr(Layout, TEXT("districts")))
	{
		Rects.Add(Str(D, TEXT("name")), D->Get(TEXT("rect")));
	}
	for (const FJsonPtr& Lm : Arr(Layout, TEXT("landmarks")))
	{
		const FString Name = Str(Lm, TEXT("name")), District = Str(Lm, TEXT("district"));
		const TArray<const FDesc*>* Main = ByLabel.Find(TEXT("LM_") + Name);
		if (!Main)
		{
			C.Check(TEXT("landmark ") + Name, false, TEXT("missing"));
			continue;
		}
		const FVector Center = (*Main)[0]->Bounds.GetCenter();
		const double X = Center.X / 100.0, Y = -Center.Y / 100.0;
		const FJsonPtr R = Rects.FindChecked(District);
		bool bInside = At(R, 0) - 30 <= X && X <= At(R, 2) + 30 && At(R, 1) - 30 <= Y && Y <= At(R, 3) + 30;
		if (!bInside && District == TEXT("MoloSalvi"))   // the lighthouse stands on the Molo's breakwater
		{
			const FJsonPtr Bw = Layout->Get(TEXT("water"))->Get(TEXT("breakwater"));
			const double Bx0 = At(Bw->Items[0], 0), By0 = At(Bw->Items[0], 1), Bx1 = At(Bw->Items[1], 0), By1 = At(Bw->Items[1], 1);
			bInside = FMath::Min(Bx0, Bx1) - 20 <= X && X <= FMath::Max(Bx0, Bx1) + 20 && FMath::Min(By0, By1) - 20 <= Y && Y <= FMath::Max(By0, By1) + 20;
		}
		C.Check(FString::Printf(TEXT("landmark %s in %s"), *Name, *District), bInside, FString::Printf(TEXT("(%.0f, %.0f)"), X, Y));
	}

	// Locked areas, water, roads, buildings, bridges.
	for (const FJsonPtr& D : Arr(Layout, TEXT("districts")))
	{
		if (Truthy(D, TEXT("unlock")))
		{
			C.Check(TEXT("locked volume ") + Str(D, TEXT("name")), Has(TEXT("Locked_") + Str(D, TEXT("name"))));
		}
	}
	C.Check(TEXT("sea volume"), Has(TEXT("Water_Sea")));
	C.Check(TEXT("reservoir volume"), Has(TEXT("Water_Reservoir")));
	const int32 Rivers = CountPrefix({ TEXT("Water_River_") });
	C.Check(TEXT("river volumes"), Rivers >= 100, FString::FromInt(Rivers));
	const int32 Roads = CountPrefix({ TEXT("Roads_") });
	C.Check(TEXT("road chunks"), Roads >= 70, FString::FromInt(Roads));
	const int32 Buildings = CountPrefix({ TEXT("Buildings_"), TEXT("BLOCKOUT_Buildings_") });
	C.Check(TEXT("building chunks"), Buildings >= 30, FString::FromInt(Buildings));
	for (const FJsonPtr& S : Arr(Layout->Get(TEXT("roads")), TEXT("segments")))
	{
		if (Truthy(S, TEXT("bridge")))
		{
			const FString Bridge = Str(S, TEXT("bridge"));
			C.Check(TEXT("bridge ") + Bridge, CountPrefix({ *(TEXT("Bridge_") + Bridge) }) > 0);
		}
	}

	// Water surfaces and scenery, and the perf camera loop.
	C.Check(TEXT("sea surface"), Has(TEXT("Water_SeaSurface")));
	C.Check(TEXT("reservoir surface"), Has(TEXT("Water_ReservoirSurface")));
	const int32 RiverSurfaces = CountPrefix({ TEXT("Water_RiverSurface_") });
	C.Check(TEXT("river surfaces"), RiverSurfaces >= 95, FString::FromInt(RiverSurfaces));   // ~100: cascades steeper than 17 deg get none
	C.Check(TEXT("distant peaks"), CountPrefix({ TEXT("Scenery_Peak_") }) >= 10);
	C.Check(TEXT("Isola Ventura"), CountPrefix({ TEXT("Scenery_IsolaVentura_Fortress") }) > 0);
	C.Check(TEXT("perf camera loop"), Has(TEXT("PerfCameraPath_Marevia")));

	// HLOD: the layer exists, and HLOD actors exist once built.
	C.Check(TEXT("HLOD layer"), OCEd::LoadAsset(TEXT("/Game/World/HLOD/HLOD0_Instanced")) != nullptr);
	int32 HlodActors = 0;
	for (const FDesc& D : Descs)
	{
		if (D.NativeClass.Contains(TEXT("WorldPartitionHLOD")))
		{
			++HlodActors;
		}
	}
	if (FParse::Param(*Params, TEXT("requirehlod")) || FPlatformMisc::GetEnvironmentVariable(TEXT("OC_VERIFY_HLOD")) == TEXT("1"))
	{
		C.Check(TEXT("HLOD actors built (scripts/build_hlod.sh)"), HlodActors > 0, FString::FromInt(HlodActors));
	}
	else
	{
		OCEd::Log(C.Tag, FString::Printf(TEXT("INFO HLOD actors: %d (pass -requirehlod to require them)"), HlodActors));
	}

	// Gates: each gate's locked props are in its Data Layer; moving bridges also have an open state.
	TMap<FString, int32> PerLayer;
	for (const FDesc& D : Descs)
	{
		for (const FString& L : D.DataLayers)
		{
			PerLayer.FindOrAdd(L)++;
		}
	}
	for (const FJsonPtr& Gate : Arr(Layout, TEXT("gates")))
	{
		FString Left, Name;
		Str(Gate, TEXT("tag")).Split(TEXT("."), &Left, &Name);
		const int32 Locked = PerLayer.FindRef(FString::Printf(TEXT("DL_Gate_%s_Locked"), *Name));
		C.Check(FString::Printf(TEXT("gate %s locked props"), *Name), Locked > 0, FString::FromInt(Locked));
		if (Name == TEXT("PonteMobile") || Name == TEXT("Molo"))
		{
			const int32 Open = PerLayer.FindRef(FString::Printf(TEXT("DL_Gate_%s_Open"), *Name));
			C.Check(FString::Printf(TEXT("gate %s open state"), *Name), Open > 0, FString::FromInt(Open));
		}
	}

	// Progression data and settings.
	const UOCAreaUnlockData* Unlocks = OCEd::Load<UOCAreaUnlockData>(TEXT("/Game/Core/Progression/DA_AreaUnlocks"));
	const int32 NumAreas = Unlocks ? Unlocks->Areas.Num() : 0;
	C.Check(TEXT("unlock data"), NumAreas == Arr(Layout, TEXT("districts")).Num() + Arr(Layout, TEXT("gates")).Num(), FString::FromInt(NumAreas));
	TArray<FString> OpenAtStart;
	if (Unlocks)
	{
		for (const FOCAreaUnlock& E : Unlocks->Areas)
		{
			const FString AreaTag = E.Area.GetTagName().ToString();
			if (AreaTag.StartsWith(TEXT("Gate.")))
			{
				C.Check(FString::Printf(TEXT("unlock %s has layers"), *AreaTag), E.LayersWhileLocked.Num() > 0);
			}
			if (!E.RequiredMilestone.IsValid())
			{
				OpenAtStart.Add(AreaTag);
			}
		}
	}
	OpenAtStart.Sort();
	C.Check(TEXT("new game opens only Città Alta and Le Fornaci"), OpenAtStart == TArray<FString>{ TEXT("Area.CittaAlta"), TEXT("Area.LeFornaci") },
		FString::Join(OpenAtStart, TEXT(", ")));
	int32 GraphNodes = -1;
	if (const UOCRoadGraphAsset* RoadsAsset = OCEd::Load<UOCRoadGraphAsset>(TEXT("/Game/World/DA_MareviaRoads")))
	{
		FString Error;
		const FJsonPtr Graph = OCLayout::FJson::Parse(RoadsAsset->LayoutJson, Error);
		const FJsonPtr R = Graph.IsValid() ? Graph->Get(TEXT("roads")) : nullptr;
		const FJsonPtr N = R.IsValid() ? R->Get(TEXT("nodes")) : nullptr;
		GraphNodes = N.IsValid() ? N->Items.Num() : -1;
	}
	C.Check(TEXT("road graph asset"), GraphNodes == Arr(Layout->Get(TEXT("roads")), TEXT("nodes")).Num(), FString::FromInt(GraphNodes));
	const UOCProgressionSettings* Settings = GetDefault<UOCProgressionSettings>();
	C.Check(TEXT("settings: unlock data"), Settings->UnlockData.ToString().Contains(TEXT("DA_AreaUnlocks")));
	C.Check(TEXT("settings: road graph"), Settings->RoadGraph.ToString().Contains(TEXT("DA_MareviaRoads")));

	if (!C.Failures.IsEmpty())
	{
		UE_LOG(LogOpenCity, Error, TEXT("%d check(s) failed: %s"), C.Failures.Num(), *FString::Join(C.Failures, TEXT(", ")));
		return 1;
	}
	OCEd::Log(C.Tag, TEXT("all checks passed"));
	return 0;
}

// ---- OCValidateNetworks ---------------------------------------------------------------------------

UOCValidateNetworksCommandlet::UOCValidateNetworksCommandlet() { InitCommandlet(this); }

int32 UOCValidateNetworksCommandlet::Main(const FString& Params)
{
	const TCHAR* ValTag = TEXT("oc_validate");
	if (FParse::Param(*Params, TEXT("import")))
	{
		const FJsonPtr Layout = LoadLayout();
		if (!Layout.IsValid())
		{
			return 1;
		}
		WriteNetworkAssets(Layout);
	}
	const UOCRoadGraphAsset* Roads = OCEd::Load<UOCRoadGraphAsset>(TEXT("/Game/World/DA_MareviaRoads"));
	const UOCRailNetworkAsset* Rail = OCEd::Load<UOCRailNetworkAsset>(TEXT("/Game/World/DA_MareviaRail"));
	const UOCWaterNetworkAsset* Water = OCEd::Load<UOCWaterNetworkAsset>(TEXT("/Game/World/DA_MareviaWater"));
	int32 Errors = 0, Issues = 0;
	const FString Report = UOCNetworkValidationLibrary::ValidateNetworks(Roads, Rail, Water, 8, Errors, Issues);
	const FString Out = OCEd::ProjectDir() / TEXT("Saved/Validation/networks.md");
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("# Network validation %s\n\n```\n%s```\n"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M")), *Report),
		*Out, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	TArray<FString> Lines;
	Report.ParseIntoArrayLines(Lines, false);
	for (const FString& L : Lines)
	{
		OCEd::Log(ValTag, L);
	}
	if (Errors > 0)
	{
		UE_LOG(LogOpenCity, Error, TEXT("%d network error(s) of %d issue(s); see Saved/Validation/networks.md"), Errors, Issues);
		return 1;
	}
	return 0;
}

// ---- OCApplyLook ----------------------------------------------------------------------------------

UOCApplyLookCommandlet::UOCApplyLookCommandlet() { InitCommandlet(this); }

int32 UOCApplyLookCommandlet::Main(const FString& Params)
{
	if (!UEditorLoadingAndSavingUtils::LoadMap(MapPath))
	{
		UE_LOG(LogOpenCity, Error, TEXT("could not load %s"), MapPath);
		return 1;
	}
	TArray<APostProcessVolume*> Volumes;
	for (TActorIterator<APostProcessVolume> It(GEditor->GetEditorWorldContext().World()); It; ++It)
	{
		if (It->bUnbound)
		{
			Volumes.Add(*It);
		}
	}
	if (Volumes.Num() != 1)
	{
		UE_LOG(LogOpenCity, Error, TEXT("expected one unbound post-process volume in %s, found %d"), MapPath, Volumes.Num());
		return 1;
	}
	Volumes[0]->Modify();
	ApplyLook(Volumes[0]->Settings);
	Volumes[0]->MarkPackageDirty();
	UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, true);
	OCEd::Log(TEXT("oc_look"), Volumes[0]->GetActorLabel() + TEXT(": post-process look applied"));
	return 0;
}
