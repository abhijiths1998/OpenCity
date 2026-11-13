// The Marevia world build (formerly the world build (OCBuildWorld)). Layout metres (x east, y north)
// -> Unreal cm (X east, Y south): see FOCRoadGraph::LayoutToWorld.
#include "OCBuildWorldCommandlet.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SplineComponent.h"
#include "Dev/OCPerfCameraPath.h"
#include "Editor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "Landscape.h"
#include "LevelEditorSubsystem.h"
#include "Misc/FileHelper.h"
#include "OCEditorHelpers.h"
#include "OCLog.h"
#include "OCWorldBuilderLibrary.h"
#include "World/OCBlockoutActor.h"
#include "World/OCWorldBuild.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/HLOD/HLODLayerFactory.h"

namespace OCWorldBuild
{
	int32 KitSeed = 1951;   // -kitseed=N: another look for the same layout (facades, colours, roofs, dressing)

	namespace
	{
		// World Partition streaming: 128 m cells; cars reach ~50 m/s, so load 384 m around the player.
		constexpr int32 CellSizeCm = 12800;
		constexpr int32 LoadingRangeCm = 38400;
		constexpr int32 LandscapeGrid = 2;   // components per streaming proxy edge (252 m)
		constexpr float ChunkCm = 25200.0f;

		// HLOD (ARCHITECTURE.md section 7), built by scripts/build_hlod.sh: one instanced layer of the real
		// meshes plus the landscape's own baked HLOD, in 256 m cells shown out to 8 km.
		const TCHAR* HlodPath = TEXT("/Game/World/HLOD");
		const TCHAR* KitPath = TEXT("/Game/Environment/Meshes/Kit");

		FString LayoutPath() { return OCEd::WorldDir() / TEXT("marevia_layout.json"); }
		FString HeightPath() { return OCEd::WorldDir() / TEXT("marevia_height.png"); }

		void SetupStreaming(UWorld* World)
		{
			const int32 N = UOCWorldBuilderLibrary::ConfigureStreaming(World, CellSizeCm, LoadingRangeCm);
			if (N == 0)
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("no streaming partitions configured"));
			}
			OCEd::Log(Tag, FString::Printf(TEXT("streaming: %d partition(s), cell %d cm, loading range %d cm"), N, CellSizeCm, LoadingRangeCm));
		}

		void SetupHlod(UWorld* World)
		{
			// One instanced layer shown out to 8 km. A second, far layer can't help: an instancing layer has no
			// way to represent the landscape (distant terrain vanished and the dam floated in the sky),
			// approximating needs MeshModelingToolsetExp (Experimental) and merging the kit ran the builder out
			// of memory. Landscape proxies get the landscape's own baked HLOD mesh in this layer.
			UHLODLayer* Base = CastChecked<UHLODLayer>(OCEd::GetOrCreate(TEXT("HLOD0_Instanced"), HlodPath, UHLODLayer::StaticClass(), NewObject<UHLODLayerFactory>(), Tag));
			Base->SetLayerType(EHLODLayerType::Instancing);
			Base->SetParentLayer(nullptr);
			Base->MarkPackageDirty();
			OCEd::Save(Base);
			const int32 Levels = UOCWorldBuilderLibrary::ConfigureHLOD(World, Base, { 25600 }, { 800000 });
			if (Levels != 1)
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("HLOD setup failed (%d levels)"), Levels);
			}
			OCEd::Log(Tag, FString::Printf(TEXT("HLOD: %s, cell 25600 cm, range 800000 cm"), *Base->GetName()));
		}

		/** Always loaded. Some classes (the sun, info actors) are never spatially loaded and refuse the change. */
		void Unstream(AActor* A)
		{
			if (A->CanChangeIsSpatiallyLoadedFlag())
			{
				A->SetIsSpatiallyLoaded(false);
			}
		}

		/** Movable sun, sky atmosphere, haze, real-time sky light, clouds (High+) and the post-process look (ART_BIBLE.md). */
		void SetupLighting()
		{
			// Warm afternoon sun, 32 deg up, from the south-west.
			AActor* Sun = OCEd::Spawn(ADirectionalLight::StaticClass(), FVector(0, 0, 1000), FRotator(-32.0, 125.0, 0.0), TEXT("Sun"), TEXT("Lighting"));
			UDirectionalLightComponent* SunC = Sun->FindComponentByClass<UDirectionalLightComponent>();
			OCEd::SetProp(SunC, TEXT("Mobility"), TEXT("Movable"));
			OCEd::SetProp(SunC, TEXT("bAtmosphereSunLight"), TEXT("True"));
			OCEd::SetProp(SunC, TEXT("Intensity"), TEXT("10.0"));
			OCEd::SetProp(SunC, TEXT("LightColor"), TEXT("(R=255,G=240,B=220,A=255)"));
			Unstream(Sun);

			Unstream(OCEd::Spawn(StaticLoadClass(AActor::StaticClass(), nullptr, TEXT("/Script/Engine.SkyAtmosphere")), FVector::ZeroVector, FRotator::ZeroRotator, TEXT("SkyAtmosphere"), TEXT("Lighting")));

			// Light haze for depth; the default density washes distant terrain into the sky.
			AActor* Fog = OCEd::Spawn(AExponentialHeightFog::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, TEXT("HeightFog"), TEXT("Lighting"));
			UExponentialHeightFogComponent* FogC = Fog->FindComponentByClass<UExponentialHeightFogComponent>();
			OCEd::SetProp(FogC, TEXT("FogDensity"), TEXT("0.005"));
			OCEd::SetProp(FogC, TEXT("FogHeightFalloff"), TEXT("0.12"));
			OCEd::SetProp(FogC, TEXT("FogInscatteringLuminance"), TEXT("(R=0.55,G=0.62,B=0.72,A=1.0)"));
			OCEd::SetProp(FogC, TEXT("StartDistance"), TEXT("6000.0"));
			Unstream(Fog);

			AActor* Sky = OCEd::Spawn(ASkyLight::StaticClass(), FVector(0, 0, 500), FRotator::ZeroRotator, TEXT("SkyLight"), TEXT("Lighting"));
			USkyLightComponent* SkyC = Sky->FindComponentByClass<USkyLightComponent>();
			OCEd::SetProp(SkyC, TEXT("Mobility"), TEXT("Movable"));
			OCEd::SetProp(SkyC, TEXT("bRealTimeCapture"), TEXT("True"));
			Unstream(Sky);

			// r.VolumetricCloud is off on Low/Medium.
			Unstream(OCEd::Spawn(StaticLoadClass(AActor::StaticClass(), nullptr, TEXT("/Script/Engine.VolumetricCloud")), FVector::ZeroVector, FRotator::ZeroRotator, TEXT("Clouds"), TEXT("Lighting")));

			APostProcessVolume* Ppv = CastChecked<APostProcessVolume>(OCEd::Spawn(APostProcessVolume::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, TEXT("PostProcess_Marevia"), TEXT("Lighting")));
			Ppv->bUnbound = true;
			Unstream(Ppv);
			ApplyLook(Ppv->Settings);   // shared with the look-only update of an existing map
			OCEd::Log(Tag, TEXT("look: sun, haze, clouds (High+), post-process (exposure range, motion blur 0.15)"));
		}

		void BuildLandscape(UWorld* World, const FJsonPtr& Layout)
		{
			const double SizeY = At(Layout->Get(TEXT("size")), 1);
			const double OriginX = At(Layout->Get(TEXT("origin")), 0), OriginY = At(Layout->Get(TEXT("origin")), 1);
			const double HMin = At(Layout->Get(TEXT("height_range")), 0), HMax = At(Layout->Get(TEXT("height_range")), 1);
			// 1 m quads; heightmap 0..65535 spans hmin..hmax metres:
			//   world Z = Location.Z + Scale.Z * (v - 32768) / 128   must equal   100 * (hmin + v / 65535 * range)
			const double ScaleZ = 12800.0 * (HMax - HMin) / 65535.0;
			const double LocZ = 100.0 * HMin + ScaleZ * 256.0;
			const double NorthEdgeY = OriginY + SizeY;   // PNG row 0 is the north edge = landscape min Y in Unreal
			const FVector Location(OriginX * 100.0, -NorthEdgeY * 100.0, LocZ);
			const FVector Scale(100.0, 100.0, ScaleZ);
			ALandscape* Landscape = UOCWorldBuilderLibrary::CreateLandscapeFromHeightmap(World, HeightPath(), Location, Scale, 2, 63, LandscapeGrid,
				Art(TEXT("/Game/Materials/M_Master_Landscape"), TEXT("/Game/Dev/Materials/M_Dev_Terrain")));
			if (!Landscape)
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("landscape creation failed (see LogOpenCity)"));
			}
			Landscape->SetActorLabel(TEXT("Landscape_Marevia"));
			OCEd::Log(Tag, FString::Printf(TEXT("landscape at %s, scale %s"), *Location.ToString(), *Scale.ToString()));
		}

		void BuildRoads(UWorld* World, const FJsonPtr& Layout)
		{
			const FJsonPtr Roads = Layout->Get(TEXT("roads"));
			// Road surface per paving (oc_art_materials PAVING -> MI_Paving_*).
			TMap<FName, UMaterialInterface*> Paving;
			for (const TCHAR* P : { TEXT("asphalt"), TEXT("sampietrini"), TEXT("porfido"), TEXT("lastricato"), TEXT("stone_slab"), TEXT("gravel_white"), TEXT("concrete") })
			{
				if (UMaterialInterface* Mi = OCEd::Load<UMaterialInterface>(FString(TEXT("/Game/Environment/Materials/Paving/MI_Paving_")) + P))
				{
					Paving.Add(P, Mi);
				}
			}
			TArray<FName> SidewalkKinds;
			for (const FJsonPtr& K : Arr(Roads, TEXT("sidewalk_kinds")))
			{
				SidewalkKinds.Add(*K->Str);
			}
			const int32 N = UOCWorldBuilderLibrary::BuildRoadMeshes(World, LayoutPath(), HeightPath(), TEXT("/Game/World/Roads"),
				Art(TEXT("/Game/Materials/M_Master_Road"), TEXT("/Game/Dev/Materials/M_Dev_Road")),
				Art(TEXT("/Game/Environment/Materials/MI_Sidewalk"), TEXT("/Game/Dev/Materials/M_Dev_Sidewalk")),
				SidewalkKinds, Num(Roads, TEXT("sidewalk_width")) * 100.0f, ChunkCm, Paving);
			if (N == 0)
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("no road chunks built"));
			}
			OCEd::Log(Tag, FString::Printf(TEXT("roads: %d chunks"), N));
		}

		void BuildBuildings(UWorld* World)
		{
			const int32 N = UOCWorldBuilderLibrary::BuildBuildings(World, LayoutPath(), Cast<UStaticMesh>(Asset(Cube)),
				Cast<UMaterialInterface>(Asset(TEXT("/Game/Dev/Materials/M_Dev_Building"))), ChunkCm);
			if (N == 0)
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("no buildings built"));
			}
			OCEd::Log(Tag, FString::Printf(TEXT("buildings: %d"), N));
			if (!OCEd::LoadAsset(FString(KitPath) / TEXT("SM_Kit_Wall_400")))
			{
				OCEd::Log(Tag, TEXT("kit not imported: buildings stay grey-box"));
				return;
			}
			const int32 Pieces = UOCWorldBuilderLibrary::BuildKitBuildings(World, LayoutPath(), HeightPath(), KitPath, ChunkCm, KitSeed);
			if (Pieces == 0)
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("kit buildings failed (see LogOpenCity)"));
			}
			// The blockout boxes stay as each building's collision, unseen.
			int32 Hidden = 0;
			for (TActorIterator<AOCBlockoutActor> It(World); It; ++It)
			{
				TArray<UInstancedStaticMeshComponent*> Comps;
				It->GetComponents(Comps);
				for (UInstancedStaticMeshComponent* C : Comps)
				{
					C->SetVisibility(false);
					C->SetHiddenInGame(true);
					C->SetCastShadow(false);
				}
				++Hidden;
			}
			OCEd::Log(Tag, FString::Printf(TEXT("kit buildings: %d pieces; %d blockout chunks hidden (collision only)"), Pieces, Hidden));
		}

		void BuildDressing(UWorld* World, const FJsonPtr& Layout)
		{
			const float SidewalkCm = Num(Layout->Get(TEXT("roads")), TEXT("sidewalk_width")) * 100.0f;
			if (OCEd::LoadAsset(TEXT("/Game/Environment/Props/SM_Prop_street_lamp_01/SM_Prop_street_lamp_01")))
			{
				OCEd::Log(Tag, FString::Printf(TEXT("street props: %d"), UOCWorldBuilderLibrary::BuildStreetProps(World, LayoutPath(), HeightPath(), SidewalkCm, ChunkCm, 1951)));
			}
			if (OCEd::LoadAsset(TEXT("/Game/Environment/Meshes/Kit/SM_Kit_Prop_WallLantern")))
			{
				OCEd::Log(Tag, FString::Printf(TEXT("street fixtures: %d"), UOCWorldBuilderLibrary::BuildStreetFixtures(World, LayoutPath(), HeightPath(), SidewalkCm, ChunkCm, 2311)));
			}
			if (OCEd::LoadAsset(TEXT("/Game/Environment/Meshes/Kit/SM_Kit_RetainingWall_Stone_400")))
			{
				OCEd::Log(Tag, FString::Printf(TEXT("retaining wall panels: %d"), UOCWorldBuilderLibrary::BuildRetainingWalls(World, LayoutPath(), ChunkCm)));
			}
			if (OCEd::LoadAsset(TEXT("/Game/Environment/Materials/Decals/MI_Decal_ManholeCover001")))
			{
				OCEd::Log(Tag, FString::Printf(TEXT("road decals: %d"), UOCWorldBuilderLibrary::BuildRoadDecals(World, LayoutPath(), HeightPath(), SidewalkCm, ChunkCm, 2207)));
			}
		}

		/** Shortest ungated road path (node indices) between the nodes nearest layout points A and B. */
		TArray<int32> RoadPath(const FJsonPtr& Layout, FVector2D PA, FVector2D PB)
		{
			const FJsonPtr Roads = Layout->Get(TEXT("roads"));
			const TArray<FJsonPtr>& Nodes = Arr(Roads, TEXT("nodes"));
			auto Pos = [&Nodes](int32 I) { return FVector2D(At(Nodes[I], 0), At(Nodes[I], 1)); };
			TMap<int32, TArray<TPair<int32, double>>> Adj;
			for (const FJsonPtr& S : Arr(Roads, TEXT("segments")))
			{
				if (Truthy(S, TEXT("gate")))
				{
					continue;
				}
				const int32 A = Int(S, TEXT("a")), B = Int(S, TEXT("b"));
				const double D = FVector2D::Distance(Pos(A), Pos(B));
				Adj.FindOrAdd(A).Add({ B, D });
				Adj.FindOrAdd(B).Add({ A, D });
			}
			auto Near = [&](FVector2D P)
			{
				int32 Best = 0;
				for (int32 i = 1; i < Nodes.Num(); ++i)
				{
					if (FVector2D::Distance(Pos(i), P) < FVector2D::Distance(Pos(Best), P)) { Best = i; }
				}
				return Best;
			};
			const int32 Start = Near(PA), Goal = Near(PB);
			TMap<int32, double> Dist = { { Start, 0.0 } };
			TMap<int32, int32> Came;
			using FItem = TPair<double, int32>;
			TArray<FItem> Todo = { { 0.0, Start } };
			auto Less = [](const FItem& X, const FItem& Y) { return X.Key < Y.Key || (X.Key == Y.Key && X.Value < Y.Value); };
			while (!Todo.IsEmpty())
			{
				FItem Top;
				Todo.HeapPop(Top, Less);
				if (Top.Value == Goal) { break; }
				if (Top.Key > Dist[Top.Value]) { continue; }
				if (const TArray<TPair<int32, double>>* Out = Adj.Find(Top.Value))
				{
					for (const TPair<int32, double>& E : *Out)
					{
						const double Nd = Top.Key + E.Value;
						const double* Old = Dist.Find(E.Key);
						if (Nd < (Old ? *Old : 1e18))
						{
							Dist.Add(E.Key, Nd);
							Came.Add(E.Key, Top.Value);
							Todo.HeapPush({ Nd, E.Key }, Less);
						}
					}
				}
			}
			if (Goal != Start && !Came.Contains(Goal))
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("perf path: no road path between nodes %d and %d"), Start, Goal);
			}
			TArray<int32> Path = { Goal };
			while (Path.Last() != Start)
			{
				Path.Add(Came[Path.Last()]);
			}
			Algo::Reverse(Path);
			return Path;
		}

		/** Perf camera loop (scripts/perf_run.sh L_Marevia): through the two districts open in a new game,
		 *  4 m over the roads at 25 m/s (city driving pace; ~190 s a lap), following the road graph. */
		void BuildPerfPath(const FJsonPtr& Layout)
		{
			const FVector2D Waypoints[] = { { 780, 1290 }, { 1140, 1000 }, { 2250, 980 }, { 2175, 1150 }, { 2670, 1500 }, { 1725, 1600 }, { 780, 1290 } };
			const TArray<FJsonPtr>& Nodes = Arr(Layout->Get(TEXT("roads")), TEXT("nodes"));
			TArray<int32> Loop;
			for (int32 i = 0; i + 1 < UE_ARRAY_COUNT(Waypoints); ++i)
			{
				const TArray<int32> Leg = RoadPath(Layout, Waypoints[i], Waypoints[i + 1]);
				for (int32 k = Loop.IsEmpty() ? 0 : 1; k < Leg.Num(); ++k)
				{
					Loop.Add(Leg[k]);
				}
			}
			Loop.Pop();   // closed spline: the last node is the first
			auto NodeCm = [&](int32 I, double Up) { return Cm(At(Nodes[I], 0), At(Nodes[I], 1), At(Nodes[I], 2) + Up); };
			AOCPerfCameraPath* Perf = CastChecked<AOCPerfCameraPath>(OCEd::Spawn(AOCPerfCameraPath::StaticClass(), NodeCm(Loop[0], 4.0), FRotator::ZeroRotator,
				TEXT("PerfCameraPath_Marevia"), TEXT("Dev")));
			Perf->SetIsSpatiallyLoaded(false);
			OCEd::SetProp(Perf, TEXT("bGenerateCircle"), TEXT("False"));
			OCEd::SetProp(Perf, TEXT("SpeedCmPerSec"), TEXT("2500.0"));
			OCEd::SetProp(Perf, TEXT("CameraPitch"), TEXT("-6.0"));
			OCEd::SetProp(Perf, TEXT("WarmupSeconds"), TEXT("8.0"));
			USplineComponent* Spline = Perf->FindComponentByClass<USplineComponent>();
			Spline->ClearSplinePoints(false);
			for (int32 I : Loop)
			{
				Spline->AddSplinePoint(NodeCm(I, 4.0), ESplineCoordinateSpace::World, false);
			}
			Spline->SetClosedLoop(true, true);
			OCEd::Log(Tag, FString::Printf(TEXT("perf camera loop: %d road nodes, %.0f m"), Loop.Num(), Spline->GetSplineLength() / 100.0f));
		}

		bool Build()
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *LayoutPath()))
			{
				UE_LOG(LogOpenCity, Error, TEXT("cannot read %s (scripts/make_layout.sh)"), *LayoutPath());
				return false;
			}
			FString Error;
			const FJsonPtr Layout = OCLayout::FJson::Parse(Text, Error);
			if (!Layout.IsValid())
			{
				UE_LOG(LogOpenCity, Error, TEXT("%s: %s"), *LayoutPath(), *Error);
				return false;
			}

			ULevelEditorSubsystem* Levels = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
			if (!Levels->NewLevel(MapPath, /*bIsPartitionedWorld*/ true))
			{
				UE_LOG(LogOpenCity, Error, TEXT("could not create World Partition map %s (delete it first)"), MapPath);
				return false;
			}
			UWorld* World = GEditor->GetEditorWorldContext().World();
			SetupStreaming(World);
			SetupHlod(World);
			BuildLandscape(World, Layout);
			SetupLighting();

			const FJsonPtr Start = Layout->Get(TEXT("start"));
			const FJsonPtr StartPos = Start->Get(TEXT("pos"));
			Unstream(OCEd::Spawn(APlayerStart::StaticClass(), FVector(At(StartPos, 0) * 100.0, -At(StartPos, 1) * 100.0, Num(Start, TEXT("ground_z")) * 100.0 + 150.0),
				FRotator::ZeroRotator, TEXT("PlayerStart_CittaAlta")));

			BuildRoads(World, Layout);
			BuildBuildings(World);
			BuildDressing(World, Layout);
			BuildStory(World, Layout);
			BuildWater(Layout);
			BuildPerfPath(Layout);

			if (!UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, true))
			{
				UE_LOG(LogOpenCity, Error, TEXT("saving %s failed"), MapPath);
				return false;
			}
			OCEd::Log(Tag, FString::Printf(TEXT("saved %s"), MapPath));
			return true;
		}
	}
}

UOCBuildWorldCommandlet::UOCBuildWorldCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UOCBuildWorldCommandlet::Main(const FString& Params)
{
	if (!FParse::Param(*Params, TEXT("skipmaterials")))
	{
		OCWorldBuild::BuildDevMaterials();
	}
	FParse::Value(*Params, TEXT("kitseed="), OCWorldBuild::KitSeed);
	if (FParse::Param(*Params, TEXT("materialsonly")))
	{
		UE_LOG(LogOpenCity, Display, TEXT("OCBuildWorld: materials done"));
		return 0;
	}
	if (!OCWorldBuild::Build())
	{
		UE_LOG(LogOpenCity, Error, TEXT("OCBuildWorld: FAILED"));
		return 1;
	}
	UE_LOG(LogOpenCity, Display, TEXT("OCBuildWorld: done"));
	return 0;
}
