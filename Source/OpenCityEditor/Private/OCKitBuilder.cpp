// Assembles Marevia's buildings from the modular kit (Tools/kit/KIT_SPEC.md, BUILDINGS_PLAN.md):
// UOCWorldBuilderLibrary::BuildKitBuildings.
//
// Each building record in the layout (footprint on the 2 m grid, storeys of 3.2 m, archetype, city fields,
// street character) is dressed by its archetype in DA_BuildingStyles: every facade is planned first
// (OCFacadePlan: stacked columns, symmetric fronts, balcony patterns), then emitted as kit instances.
// Walls behind neighbours are not built; roofs are hips (end caps + a stretched ridge), gables or flat
// roofs with clutter; details (closed shutters, signs, AC units, laundry, flower boxes) follow upkeep and
// wealth. Choices use hashed sub-seeds per purpose, so every rebuild is identical.

#include "OCWorldBuilderLibrary.h"

#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "OCEditorHelpers.h"
#include "OCHeightField.h"
#include "OCLog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/OCBuildingStyles.h"
#include "World/OCFacadePlan.h"
#include "World/OCKitBuildingChunk.h"
#include "World/OCRoadGraph.h"

namespace OCKit
{
	constexpr double StoreyCm = 320.0;
	constexpr double StreetProbeCm = 500.0;   // a facade faces a street if a road edge is this close
	constexpr double StreetGapCm = 700.0;

	struct FPieces
	{
		TMap<FName, UStaticMesh*> Meshes;
		TSet<FName> Missing;

		UStaticMesh* Get(const TCHAR* Name)
		{
			const FName Key(Name);
			if (UStaticMesh** Found = Meshes.Find(Key))
			{
				return *Found;
			}
			Missing.Add(Key);
			return nullptr;
		}
	};

	struct FInstance
	{
		UStaticMesh* Mesh = nullptr;
		FTransform Transform;
		float Data[AOCKitBuildingChunk::NumCustomData] = {};
	};

	/** Road segments in a 50 m grid, for "does this facade face a street?". */
	struct FRoadIndex
	{
		static constexpr double CellCm = 5000.0;
		TArray<FVector2D> A, B;
		TArray<double> HalfWidth;
		TMap<FIntPoint, TArray<int32>> Cells;

		void Build(const FOCRoadGraph& G)
		{
			for (const FOCRoadSegment& S : G.GetSegments())
			{
				const FVector2D P0(G.GetNodes()[S.A].Location), P1(G.GetNodes()[S.B].Location);
				const int32 I = A.Add(P0);
				B.Add(P1);
				HalfWidth.Add(S.WidthCm * 0.5);
				const double Pad = S.WidthCm * 0.5 + StreetGapCm;
				const FIntPoint Lo(FMath::FloorToInt32((FMath::Min(P0.X, P1.X) - Pad) / CellCm), FMath::FloorToInt32((FMath::Min(P0.Y, P1.Y) - Pad) / CellCm));
				const FIntPoint Hi(FMath::FloorToInt32((FMath::Max(P0.X, P1.X) + Pad) / CellCm), FMath::FloorToInt32((FMath::Max(P0.Y, P1.Y) + Pad) / CellCm));
				for (int32 X = Lo.X; X <= Hi.X; ++X)
				{
					for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
					{
						Cells.FindOrAdd(FIntPoint(X, Y)).Add(I);
					}
				}
			}
		}

		/** Distance (cm) from P to the nearest road edge (negative inside a road). */
		double EdgeDistance(const FVector2D& P) const
		{
			double Best = TNumericLimits<double>::Max();
			if (const TArray<int32>* List = Cells.Find(FIntPoint(FMath::FloorToInt32(P.X / CellCm), FMath::FloorToInt32(P.Y / CellCm))))
			{
				for (const int32 I : *List)
				{
					const FVector2D AB = B[I] - A[I];
					const double T = FMath::Clamp(FVector2D::DotProduct(P - A[I], AB) / FMath::Max(AB.SizeSquared(), 1.0), 0.0, 1.0);
					Best = FMath::Min(Best, FVector2D::Distance(P, A[I] + AB * T) - HalfWidth[I]);
				}
			}
			return Best;
		}
	};

}

namespace OCKit
{
	constexpr double CoverProbeCm = 50.0;     // a wall point is behind a neighbour if this far out is inside it
	constexpr double PlinthBelowCm = 300.0;   // how far the plinth (and door/shop stubs) reach below the floor
	constexpr double ChamferCm = 200.0;       // corner buildings: the corner cut (a 2.83 m facade piece)
	const TCHAR* const StylesAsset = TEXT("/Game/Environment/Buildings/DA_BuildingStyles");

	/** The look of one building, drawn once (and shared by every volume of a lot). */
	struct FLook
	{
		int32 Colour = 0;
		FLinearColor Wall, Wood;
		float Wear = 0.f;
		int32 Trim = 0;
		bool bShutters = true;
		FName Crown, Upper, UpperFirst, Top, Balcony, Shop, GroundOther;
	};

	/** One building record in world space, before anything is placed. */
	struct FBld
	{
		FVector C, U, Vw;          // centre on the ground plane, unit width and depth axes
		double W = 0.0, D = 0.0;   // cm, on the 2 m grid
		int32 Storeys = 1;
		FString StyleName, LmKind;
		const FOCBuildingArchetype* Arch = nullptr;
		bool bLandmark = false;
		double Age = 0.5, Wealth = 0.5, Upkeep = 0.5;
		int32 Street = 0, Lot = -1, On = -1, Chamfer = 0;
		double StoreyH = StoreyCm, GroundH = StoreyCm;   // cm (BUILDINGS_PLAN.md 4.6: 2.9-3.5 m storeys, 3.2-4.2 m ground floors)
		FString Feature;                                 // torretta, leanto, recess

		/** Floor level of storey S (0 = ground) and that storey's height. */
		double ZAt(int32 S) const { return S <= 0 ? Z0 : Z0 + GroundH + (S - 1) * StoreyH; }
		double HAt(int32 S) const { return S <= 0 ? GroundH : StoreyH; }
		bool bLotMain = true;
		double Z0 = 0.0, TopZ = 0.0;
		struct FFacade { FVector Out; double Length; double Half; bool bStreet = false; double RoadDist = 0.0; };
		FFacade F[4];
		int32 Front = INDEX_NONE;  // the street facade with the door
		FLook Look;
		uint32 Signature = 0;

		bool Contains(const FVector& P) const
		{
			const FVector L = P - C;
			return FMath::Abs(FVector::DotProduct(L, U)) <= W * 0.5 && FMath::Abs(FVector::DotProduct(L, Vw)) <= D * 0.5;
		}
	};

	/** Building footprints in a 50 m grid: who stands at a point, and how high. */
	struct FBldIndex
	{
		static constexpr double CellCm = 5000.0;
		const TArray<FBld>* All = nullptr;
		TMap<FIntPoint, TArray<int32>> Cells;

		void Build(const TArray<FBld>& Blds)
		{
			All = &Blds;
			for (int32 i = 0; i < Blds.Num(); ++i)
			{
				const double R = 0.5 * FMath::Sqrt(Blds[i].W * Blds[i].W + Blds[i].D * Blds[i].D);
				for (int32 X = FMath::FloorToInt32((Blds[i].C.X - R) / CellCm); X <= FMath::FloorToInt32((Blds[i].C.X + R) / CellCm); ++X)
				{
					for (int32 Y = FMath::FloorToInt32((Blds[i].C.Y - R) / CellCm); Y <= FMath::FloorToInt32((Blds[i].C.Y + R) / CellCm); ++Y)
					{
						Cells.FindOrAdd(FIntPoint(X, Y)).Add(i);
					}
				}
			}
		}

		/** The highest wall top (TopZ) of another building standing on P (lowest double if none); Who = which. */
		double CoverTop(const FVector& P, int32 Self, int32* Who = nullptr) const
		{
			double Top = TNumericLimits<double>::Lowest();
			if (const TArray<int32>* List = Cells.Find(FIntPoint(FMath::FloorToInt32(P.X / CellCm), FMath::FloorToInt32(P.Y / CellCm))))
			{
				for (const int32 i : *List)
				{
					if (i != Self && (*All)[i].Contains(P) && (*All)[i].TopZ > Top)
					{
						Top = (*All)[i].TopZ;
						if (Who) { *Who = i; }
					}
				}
			}
			return Top;
		}
	};

	FName PickWeighted(const TArray<FOCPieceWeight>& List, FRandomStream& R, FName Fallback)
	{
		float Sum = 0.f;
		for (const FOCPieceWeight& P : List) { Sum += P.Weight; }
		if (Sum <= 0.f)
		{
			return Fallback;
		}
		float X = R.FRand() * Sum;
		for (const FOCPieceWeight& P : List)
		{
			X -= P.Weight;
			if (X <= 0.f) { return P.Piece; }
		}
		return List.Last().Piece;
	}

	/** A default archetype per district style, for records written before archetypes existed. */
	FName DefaultArchetype(const FString& Style)
	{
		static const TMap<FString, FName> M = { { TEXT("oldtown"), TEXT("townhouse") }, { TEXT("grid90"), TEXT("tenement") }, { TEXT("grid120"), TEXT("shed") },
			{ TEXT("beach"), TEXT("slabhotel") }, { TEXT("village"), TEXT("cottage") }, { TEXT("terminal"), TEXT("shed") }, { TEXT("hills"), TEXT("villa") } };
		const FName* Found = M.Find(Style);
		return Found ? *Found : FName(TEXT("townhouse"));
	}

	/** Height (cm above the roof base) of a pitched roof's surface at (Along, Across) cm from its centre. */
	double PitchedHeight(double Along, double Across, double Len, double Wid, double Rise, bool bHip)
	{
		constexpr double Fascia = 0.04;
		const double Side = 1.0 - FMath::Abs(Across) / (Wid * 0.5);
		const double End = bHip ? (Len * 0.5 - FMath::Abs(Along)) / (Wid * 0.5) : 1.0;
		return Rise * (Fascia + (1.0 - Fascia) * FMath::Clamp(FMath::Min(Side, End), 0.0, 1.0));
	}
}

int32 UOCWorldBuilderLibrary::BuildKitBuildings(UWorld* World, const FString& LayoutJsonPath, const FString& HeightmapPng,
	const FString& KitFolder, float ChunkSizeCm, int32 Seed)
{
	using namespace OCKit;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!World || !FFileHelper::LoadFileToString(Text, *LayoutJsonPath)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildKitBuildings: cannot read %s"), *LayoutJsonPath);
		return 0;
	}
	OCWorld::FHeightField Height;
	if (!Height.Load(HeightmapPng, Root))
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildKitBuildings: cannot read heightmap %s"), *HeightmapPng);
		return 0;
	}
	FOCRoadGraph Roads;
	FString Error;
	if (!Roads.LoadFromLayoutJson(Text, Error))
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildKitBuildings: road graph: %s"), *Error);
		return 0;
	}
	FRoadIndex RoadIndex;
	RoadIndex.Build(Roads);

	// The styles asset: made with the starting values the first time, the tuning source after that.
	UOCBuildingStyleSet* Styles = OCEd::Load<UOCBuildingStyleSet>(StylesAsset);
	if (!Styles)
	{
		Styles = CastChecked<UOCBuildingStyleSet>(OCEd::GetOrCreateDataAsset(TEXT("DA_BuildingStyles"), TEXT("/Game/Environment/Buildings"),
			UOCBuildingStyleSet::StaticClass(), TEXT("oc_world")));
		Styles->FillDefaults();
		OCEd::Save(Styles);
	}

	// Every kit mesh in the folder, by piece name (SM_Kit_<Name>).
	FPieces Pieces;
	{
		TArray<FAssetData> Assets;
		IAssetRegistry::Get()->GetAssetsByPath(FName(*KitFolder), Assets, false);
		for (const FAssetData& A : Assets)
		{
			const FString Name = A.AssetName.ToString();
			if (Name.StartsWith(TEXT("SM_Kit_")))
			{
				if (UStaticMesh* Mesh = Cast<UStaticMesh>(A.GetAsset()))
				{
					Pieces.Meshes.Add(FName(*Name.Mid(7)), Mesh);
				}
			}
		}
	}
	if (!Pieces.Meshes.Contains(FName(TEXT("Wall_400"))))
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildKitBuildings: no kit in %s (run scripts/run_commandlet.sh OCBuildArt -steps=kit)"), *KitFolder);
		return 0;
	}
	auto Has = [&Pieces](const FString& Name) { return Pieces.Meshes.Contains(FName(*Name)); };

	// Optional doorstep and shopfront clutter (OCArtImport.cpp (props)).
	auto LoadProp = [](const TCHAR* Name) { return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Environment/Props/%s/%s.%s"), Name, Name, Name)); };
	UStaticMesh* Pot = LoadProp(TEXT("SM_Prop_ceramic_pot"));
	UStaticMesh* Fern = LoadProp(TEXT("SM_Prop_fern_02"));
	UStaticMesh* Crate = LoadProp(TEXT("SM_Prop_plastic_crate_01"));
	UStaticMesh* WoodCrate = LoadProp(TEXT("SM_Prop_wooden_crate_01"));
	UStaticMesh* Cafe = LoadProp(TEXT("SM_Prop_outdoor_table_chair_set_01"));

	TArray<FString> StyleNames;
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("building_styles")))
	{
		StyleNames.Add(V->AsString());
	}

	// ---- Pass 1: every building's frame, archetype, storeys and top, so each facade can see its neighbours.
	const TArray<TSharedPtr<FJsonValue>>& List = Root->GetArrayField(TEXT("buildings"));
	TArray<FBld> Blds;
	Blds.SetNum(List.Num());
	TSet<int32> LotsSeen;
	int32 MissingArch = 0;
	for (int32 i = 0; i < List.Num(); ++i)
	{
		const TSharedPtr<FJsonObject>& Rec = List[i]->AsObject();
		const TArray<TSharedPtr<FJsonValue>>& Pos = Rec->GetArrayField(TEXT("pos"));
		const TArray<TSharedPtr<FJsonValue>>& Size = Rec->GetArrayField(TEXT("size"));
		FBld& B = Blds[i];
		const int32 StyleIndex = static_cast<int32>(Rec->GetNumberField(TEXT("style")));
		B.StyleName = StyleNames.IsValidIndex(StyleIndex) ? StyleNames[StyleIndex] : FString();
		B.bLandmark = Rec->HasField(TEXT("landmark"));
		Rec->TryGetStringField(TEXT("lmkind"), B.LmKind);
		FString ArchName;
		const FName Want = Rec->TryGetStringField(TEXT("archetype"), ArchName) ? FName(*ArchName) : DefaultArchetype(B.StyleName);
		B.Arch = Styles->Find(Want);
		if (!B.Arch)
		{
			++MissingArch;
			B.Arch = Styles->Find(DefaultArchetype(B.StyleName));
		}
		if (!B.Arch && Styles->Archetypes.Num() > 0)
		{
			B.Arch = &Styles->Archetypes[0];
		}
		Rec->TryGetNumberField(TEXT("age"), B.Age);
		Rec->TryGetNumberField(TEXT("wealth"), B.Wealth);
		Rec->TryGetNumberField(TEXT("upkeep"), B.Upkeep);
		Rec->TryGetNumberField(TEXT("street"), B.Street);
		Rec->TryGetNumberField(TEXT("lot"), B.Lot);
		Rec->TryGetNumberField(TEXT("on"), B.On);
		Rec->TryGetNumberField(TEXT("chamfer"), B.Chamfer);
		double Sh = 0.0, Gh = 0.0;
		if (Rec->TryGetNumberField(TEXT("storey_h"), Sh) && Sh > 2.0) { B.StoreyH = Sh * 100.0; }
		B.GroundH = (Rec->TryGetNumberField(TEXT("ground_h"), Gh) && Gh > 2.0) ? Gh * 100.0 : B.StoreyH;
		Rec->TryGetStringField(TEXT("feature"), B.Feature);
		if (B.Lot >= 0)
		{
			bool bSeen = false;
			LotsSeen.Add(B.Lot, &bSeen);
			B.bLotMain = !bSeen;
		}
		double StoreysD = 0.0;
		B.Storeys = Rec->TryGetNumberField(TEXT("storeys"), StoreysD) ? FMath::Max(1, static_cast<int32>(StoreysD))
			: FMath::Max(1, FMath::RoundToInt32((Rec->GetNumberField(TEXT("top")) - Rec->GetNumberField(TEXT("base")) - 1.0) / 3.2));
		// Building frame in world space (layout: x east, y north, yaw counter-clockwise; Unreal flips y).
		const double YawRad = FMath::DegreesToRadians(Rec->GetNumberField(TEXT("yaw")));
		B.U = FVector(FMath::Cos(YawRad), -FMath::Sin(YawRad), 0.0);   // along the width
		B.Vw = FVector(-FMath::Sin(YawRad), -FMath::Cos(YawRad), 0.0); // along the depth
		B.C = FOCRoadGraph::LayoutToWorld(Pos[0]->AsNumber(), Pos[1]->AsNumber(), 0.0);
		B.W = FMath::Max(400.0, FMath::RoundToDouble(Size[0]->AsNumber() / 2.0) * 200.0);
		B.D = FMath::Max(400.0, FMath::RoundToDouble(Size[1]->AsNumber() / 2.0) * 200.0);
		B.F[0] = { -B.Vw, B.W, B.D * 0.5 };
		B.F[1] = { B.U, B.D, B.W * 0.5 };
		B.F[2] = { B.Vw, B.W, B.D * 0.5 };
		B.F[3] = { -B.U, B.D, B.W * 0.5 };
	}
	FBldIndex Index;
	Index.Build(Blds);
	if (!Blds.IsEmpty() && !Blds[0].Arch)
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildKitBuildings: %s has no archetypes"), StylesAsset);
		return 0;
	}

	// Which facades face a street (probed at a quarter, half and three quarters of their length, never
	// through a neighbour), the front door's facade, and the ground floor level.
	int32 StreetFacades = 0;
	for (int32 i = 0; i < Blds.Num(); ++i)
	{
		FBld& B = Blds[i];
		if (B.On >= 0 && Blds.IsValidIndex(B.On))
		{
			// A recessed top storey: on its building's roof, no street front.
			B.Z0 = Blds[B.On].TopZ;
			B.GroundH = B.StoreyH;
			B.TopZ = B.ZAt(B.Storeys);
			continue;
		}
		double BestRoad = TNumericLimits<double>::Max();
		int32 Nearest = 0;
		for (int32 f = 0; f < 4; ++f)
		{
			FBld::FFacade& Fa = B.F[f];
			const FVector Along = FVector::CrossProduct(-Fa.Out, FVector::UpVector).GetSafeNormal();
			Fa.RoadDist = TNumericLimits<double>::Max();
			for (const double T : { -0.25, 0.0, 0.25 })
			{
				const FVector P = B.C + Fa.Out * Fa.Half + Along * (T * Fa.Length);
				if (Index.CoverTop(P + Fa.Out * CoverProbeCm, i) > TNumericLimits<double>::Lowest())
				{
					continue;   // a neighbour stands against this part of the wall
				}
				Fa.RoadDist = FMath::Min(Fa.RoadDist, RoadIndex.EdgeDistance(FVector2D(P + Fa.Out * StreetProbeCm)));
			}
			Fa.bStreet = Fa.RoadDist < StreetGapCm;
			StreetFacades += Fa.bStreet ? 1 : 0;
			if (Fa.RoadDist < BestRoad)
			{
				BestRoad = Fa.RoadDist;
				Nearest = f;
			}
		}
		if (B.bLotMain)
		{
			B.F[Nearest].bStreet = true;   // not near any road (rare): the facade closest to one is the front
			B.Front = Nearest;
		}
		else if (B.F[Nearest].bStreet)
		{
			B.Front = Nearest;   // a wing on a back street has its own door
		}

		// Ground floor level: the street side's ground, never burying the uphill side by > 1.2 m.
		double Z0 = TNumericLimits<double>::Max(), MaxCorner = -TNumericLimits<double>::Max();
		for (const FBld::FFacade& Fa : B.F)
		{
			if (Fa.bStreet)
			{
				const FVector Mid = B.C + Fa.Out * Fa.Half;
				Z0 = FMath::Min(Z0, Height.HeightCm(Mid.X, Mid.Y));
			}
		}
		for (int32 sx = -1; sx <= 1; sx += 2)
		{
			for (int32 sy = -1; sy <= 1; sy += 2)
			{
				const FVector Corner = B.C + B.U * (sx * B.W * 0.5) + B.Vw * (sy * B.D * 0.5);
				MaxCorner = FMath::Max(MaxCorner, Height.HeightCm(Corner.X, Corner.Y));
				if (Z0 == TNumericLimits<double>::Max())
				{
					Z0 = Height.HeightCm(Corner.X, Corner.Y);
				}
			}
		}
		B.Z0 = FMath::Max(Z0, MaxCorner - 120.0) + 8.0;
		B.TopZ = B.ZAt(B.Storeys);
	}

	// ---- Pass 2: per building, its look, facade plans, pieces, roof and dressing.
	TMap<FIntPoint, TArray<FInstance>> Chunks;
	TMap<UStaticMesh*, int32> PieceUse;
	TMap<FString, TMap<FName, int32>> CellsPerStyle;   // facade cells per district style and piece
	TMap<int32, FLook> LotLooks;
	TMap<FIntPoint, TArray<int32>> SigCells;           // 60 m cells: buildings with a street-front signature
	int32 Buildings = 0, HiddenCells = 0, PartyBlank = 0, Basements = 0, FloatingDoors = 0, Symmetric = 0, Facades = 0;
	int32 SigRerolls = 0, SigRepeats = 0, ColourRerolls = 0, Chamfers = 0, Dressed = 0;
	int32 Hips = 0, Gables = 0, Flats = 0;
	const double Lowest = TNumericLimits<double>::Lowest();
	for (int32 BuildingIndex = 0; BuildingIndex < Blds.Num(); ++BuildingIndex)
	{
		FBld& B = Blds[BuildingIndex];
		const FOCBuildingArchetype& Arch = *B.Arch;
		const FVector& C = B.C;
		const FVector& U = B.U;
		const FVector& Vw = B.Vw;
		const double W = B.W, D = B.D, Z0 = B.Z0, TopZ = B.TopZ;
		const int32 Storeys = B.Storeys;
		const uint32 BSeed = OCSubSeed(uint32(Seed), uint32(BuildingIndex), OCSeedPurpose::Building);
		auto Stream = [BSeed](uint32 Purpose, uint32 Part = 0) { return FRandomStream(int32(OCSubSeed(BSeed, Part, Purpose) & 0x7fffffff)); };
		FRandomStream Rr = Stream(OCSeedPurpose::Roof), Rp = Stream(OCSeedPurpose::Props), Rd = Stream(OCSeedPurpose::LitWindows);
		const bool bPlaster = Arch.Material == TEXT("Plaster"), bBrick = Arch.Material == TEXT("Brick"), bShed = Arch.Material == TEXT("Shed");

		// Who stands against this building (for colour and window contrast): probe each facade's middle.
		TSet<int32> Touching;
		for (const FBld::FFacade& Fa : B.F)
		{
			int32 Who = INDEX_NONE;
			if (Index.CoverTop(C + Fa.Out * (Fa.Half + CoverProbeCm), BuildingIndex, &Who) > Lowest && Who != INDEX_NONE && Blds[Who].Lot != B.Lot)
			{
				Touching.Add(Who);
			}
		}

		// The look: one per lot. Wall colour from the street's two or three colours most of the time, never
		// the colour or window family of a building it touches that is already dressed.
		if (const FLook* Shared = B.Lot >= 0 ? LotLooks.Find(B.Lot) : nullptr)
		{
			B.Look = *Shared;
		}
		else
		{
			FRandomStream Rc = Stream(OCSeedPurpose::Colours);
			FLook& L = B.Look;
			const TArray<FLinearColor>& Pal = Styles->WallPalettes.IsValidIndex(Arch.Palette) && Styles->WallPalettes[Arch.Palette].Colours.Num() > 0
				? Styles->WallPalettes[Arch.Palette].Colours : Styles->WallPalettes[0].Colours;
			const int32 N = Pal.Num();
			auto Taken = [&](int32 Colour, FName Window)
			{
				for (const int32 T : Touching)
				{
					if (T < BuildingIndex && ((Colour >= 0 && Blds[T].Look.Colour == Colour) || (!Window.IsNone() && Blds[T].Look.Upper == Window)))
					{
						return true;
					}
				}
				return false;
			};
			for (int32 Try = 0; Try < 6; ++Try)
			{
				const bool bStreetColour = Rc.FRand() < 0.75f;
				L.Colour = bStreetColour ? (B.Street + Rc.RandRange(0, 2) * 3) % N : Rc.RandRange(0, N - 1);
				if (!Taken(L.Colour, NAME_None))
				{
					break;
				}
				++ColourRerolls;
			}
			L.Wall = Pal[L.Colour] * Rc.FRandRange(0.93f, 1.05f);
			const TArray<FLinearColor>& Woods = Styles->WoodColours.Colours;
			// Shutters: the street's colour half the time, otherwise weighted (dark green dominates).
			int32 WoodIndex = (B.Street / 7) % FMath::Max(1, Woods.Num());
			if (Rc.FRand() < 0.5f && Styles->WoodWeights.Num() == Woods.Num())
			{
				float Sum = 0.f;
				for (const float Wt : Styles->WoodWeights) { Sum += Wt; }
				float X = Rc.FRand() * Sum;
				for (int32 k = 0; k < Woods.Num(); ++k) { X -= Styles->WoodWeights[k]; if (X <= 0.f) { WoodIndex = k; break; } }
			}
			L.Wood = Woods.Num() > 0 ? Woods[WoodIndex] : FLinearColor::Gray;
			L.Wear = FMath::Clamp(float(1.0 - B.Upkeep) * 0.8f + Rc.FRand() * 0.3f, 0.f, 1.f);
			L.Trim = (Arch.Trim == 0 && B.Wealth < 0.3 && Rc.FRand() < 0.5f) ? 1 : Arch.Trim;
			if (Arch.Material == TEXT("Plaster") && Rc.FRand() < Arch.PaintedTrim)
			{
				L.Trim = Rc.FRand() < 0.5f ? 4 : 5;   // painted architecture: cream, or a darker shade of the wall
			}
			L.bShutters = Rc.FRand() < Arch.Shutters;
			L.Crown = PickWeighted(Arch.Crowns, Rc, TEXT("Cornice"));
			for (int32 Try = 0; Try < 3; ++Try)
			{
				L.Upper = PickWeighted(Arch.Upper, Rc, TEXT("Window"));
				if (!Taken(-1, L.Upper)) { break; }
			}
			L.UpperFirst = Arch.UpperFirst.IsEmpty() ? L.Upper : PickWeighted(Arch.UpperFirst, Rc, L.Upper);
			L.Top = Arch.Top.IsEmpty() ? L.Upper : PickWeighted(Arch.Top, Rc, L.Upper);
			L.Balcony = PickWeighted(Arch.Balconies, Rc, TEXT("Balcony"));
			L.Shop = PickWeighted(Arch.Shops, Rc, TEXT("Shopfront"));
			L.GroundOther = PickWeighted(Arch.GroundOther, Rc, TEXT("GroundWindow"));
			if (B.Lot >= 0)
			{
				LotLooks.Add(B.Lot, L);
			}
		}
		const FLook& Look = B.Look;
		float Data[AOCKitBuildingChunk::NumCustomData] = {};
		Data[0] = Look.Wall.R; Data[1] = Look.Wall.G; Data[2] = Look.Wall.B;
		Data[3] = Look.Wood.R; Data[4] = Look.Wood.G; Data[5] = Look.Wood.B;
		Data[6] = Look.Wear;
		Data[9] = float(Look.Trim);
		FString CrownName = Look.Crown.ToString();
		bool bCrown = CrownName != TEXT("None") && Has(CrownName + TEXT("_400"));
		double CrownH = !bCrown ? 0.0 : (CrownName == TEXT("Brick_Corbel") ? 27.0 : (CrownName == TEXT("CorniceBracket") ? 32.0 : 35.0));

		TArray<FInstance>& Out = Chunks.FindOrAdd(FIntPoint(FMath::FloorToInt32(C.X / ChunkSizeCm), FMath::FloorToInt32(C.Y / ChunkSizeCm)));
		FString CellStyle = B.StyleName;
		double CurScale = 1.0;   // vertical scale of storey pieces: this storey's height / 3.2 m
		auto Add = [&](const FString& Name, const FVector& Loc, const FRotator& Rot, const FVector& Scale = FVector::OneVector, bool bGround = false)
		{
			UStaticMesh* Mesh = Pieces.Get(*Name);
			if (!Mesh)
			{
				return;
			}
			FInstance I;
			I.Mesh = Mesh;
			I.Transform = FTransform(Rot, Loc, Scale * FVector(1.0, 1.0, CurScale));
			FMemory::Memcpy(I.Data, Data, sizeof(Data));
			I.Data[8] = bGround ? 1.f : 0.f;   // ground-floor walls: base band and rain splash (SURFACE_HLSL)
			Out.Add(I);
			++PieceUse.FindOrAdd(Mesh);
		};
		auto Cell = [&](const FString& Name, const FVector& Loc, const FRotator& Rot, bool bGround)
		{
			Add(Name, Loc, Rot, FVector::OneVector, bGround);
			++CellsPerStyle.FindOrAdd(CellStyle).FindOrAdd(FName(*Name));
		};
		auto AddProp = [&](UStaticMesh* Mesh, const FVector& Loc, double YawDeg, double Scale = 1.0, bool bOnGround = true)
		{
			if (Mesh)
			{
				FInstance I;
				I.Mesh = Mesh;
				I.Transform = FTransform(FRotator(0.0, YawDeg, 0.0), bOnGround ? FVector(Loc.X, Loc.Y, Height.HeightCm(Loc.X, Loc.Y) + 20.0) : Loc, FVector(Scale));
				for (int32 k = 0; k < 6; ++k) { I.Data[k] = 1.f; }
				Out.Add(I);
			}
		};
		const bool bSeafront = Arch.Palette == 1;
		auto Sized = [&](const FString& Family, bool bFour) { return Family + (bFour ? TEXT("_400") : TEXT("_200")); };
		// The plain wall of this building's material.
		auto Blank = [&](bool bFour) { return Sized(bBrick ? TEXT("Brick_Wall") : (bShed ? TEXT("Shed_Wall") : TEXT("Wall")), bFour); };
		// A window family in a bay, with fallbacks when a family has no piece of that width.
		auto WindowPiece = [&](FName Family, bool bFour) -> FString
		{
			if (bBrick) { return Sized(TEXT("Brick_WindowArch"), bFour); }
			if (bShed) { return bFour ? FString(TEXT("Shed_WindowStrip_400")) : Blank(false); }
			FString F = Family.ToString();
			if (F == TEXT("Window") && !Look.bShutters) { F = TEXT("WindowPlain"); }
			const FString Name = Sized(F, bFour);
			return Has(Name) ? Name : Sized(Look.bShutters ? TEXT("Window") : TEXT("WindowPlain"), bFour);
		};

		// ---- Roof choice first: gable ends change what the facades carry. Pitched by default with deep eaves
		// (BUILDINGS_PLAN.md 4.2); rendered vaults on the Campanian coast; lean-tos against a taller wall; hips on
		// torrette; flat roofs with parapets under recessed tops and on corner buildings.
		const bool bTorretta = B.Feature == TEXT("torretta"), bLeanTo = B.Feature == TEXT("leanto");
		bool bHasTop = false;
		for (int32 k = BuildingIndex + 1; k < FMath::Min(Blds.Num(), BuildingIndex + 6); ++k)
		{
			bHasTop |= Blds[k].On == BuildingIndex && Blds[k].Feature != TEXT("torretta");
		}
		const bool bPitched = (B.On < 0 || bTorretta) && !bLeanTo && B.Chamfer == 0 && !bHasTop && (bTorretta || Rr.FRand() < Arch.PitchedRoof);
		const bool bVault = !bPitched && !bLeanTo && !bHasTop && B.On < 0 && B.Chamfer == 0 && Rr.FRand() < Arch.Vault;
		const bool bSlate = bPitched && bPlaster && Rr.FRand() < Arch.Slate;
		// Terraces (both side walls against neighbours) take a gable along the street; churches a gable front.
		auto SideCovered = [&](int32 f)
		{
			const FBld::FFacade& Fa = B.F[f];
			return Index.CoverTop(C + Fa.Out * (Fa.Half + CoverProbeCm), BuildingIndex) > Lowest;
		};
		const bool bTerrace = SideCovered(1) && SideCovered(3);
		const bool bChurch = B.LmKind == TEXT("church");
		const bool bGable = bPitched && !bTorretta && (bTerrace || bChurch || !bPlaster || Rr.FRand() < Arch.Gable);
		// Ridge axis: along U (width) or Vw (depth).
		bool bRidgeU = W >= D;
		if (bGable && bTerrace) { bRidgeU = true; }
		if (bChurch && B.Front >= 0) { bRidgeU = (B.Front == 1 || B.Front == 3); }   // ridge runs back from the front
		const bool bHip = bPitched && !bGable;
		if (bHip) { bRidgeU = W >= D; }
		// Deep eaves (0.75 m) on rafters or a plastered cove replace the cornice under pitched plaster roofs.
		const bool bEaves = bPitched && bPlaster && Arch.Eaves != TEXT("None");
		if (bEaves)
		{
			CrownName = Arch.Eaves == TEXT("Rafters") ? TEXT("EavesRafters") : TEXT("EavesCoved");
			bCrown = Has(CrownName + TEXT("_400"));
			CrownH = bCrown ? 16.0 : 0.0;
		}
		if (bVault || bLeanTo)
		{
			bCrown = false;
			CrownH = 0.0;
		}
		const double Overhang = bPitched ? (bEaves ? 75.0 : 40.0) : 0.0;
		const double RoofZ = TopZ + CrownH;
		const double RLen = (bRidgeU ? W : D) + 2.0 * Overhang, RWid = (bRidgeU ? D : W) + 2.0 * Overhang;
		const double Rise = FMath::Clamp(RWid * 0.5 * Arch.Pitch, 60.0, 650.0);
		auto IsGableFacade = [&](int32 f) { return bGable && (bRidgeU ? (f == 1 || f == 3) : (f == 0 || f == 2)); };

		// ---- Facades: plan all four, check the street front's signature, then emit.
		FOCFacadePlan Plans[4];
		double Lengths[4];
		FVector Starts[4], Alongs[4];
		bool bChamferStart[4] = {}, bChamferEnd[4] = {};
		const FVector ChamferCorner = C - U * (W * 0.5) - Vw * (D * 0.5);
		for (int32 f = 0; f < 4; ++f)
		{
			const FBld::FFacade& Fa = B.F[f];
			Alongs[f] = FVector::CrossProduct(-Fa.Out, FVector::UpVector).GetSafeNormal();
			Starts[f] = C + Fa.Out * Fa.Half - Alongs[f] * (Fa.Length * 0.5);
			Lengths[f] = Fa.Length;
			if (B.Chamfer)
			{
				if (FVector::Dist2D(Starts[f], ChamferCorner) < 5.0)
				{
					bChamferStart[f] = true;
					Starts[f] += Alongs[f] * ChamferCm;
					Lengths[f] -= ChamferCm;
				}
				else if (FVector::Dist2D(Starts[f] + Alongs[f] * Fa.Length, ChamferCorner) < 5.0)
				{
					bChamferEnd[f] = true;
					Lengths[f] -= ChamferCm;
				}
			}
		}
		auto PlanAll = [&](uint32 Variant)
		{
			for (int32 f = 0; f < 4; ++f)
			{
				const FBld::FFacade& Fa = B.F[f];
				FOCFacadeIn In;
				In.LengthCm = Lengths[f];
				In.Storeys = Storeys;
				In.bStreet = Fa.bStreet && B.On < 0;
				In.bLandmark = B.bLandmark;
				In.bDoor = f == B.Front;
				In.bWideDoor = Arch.bWideDoor;
				FOCFacadeStyle FS;
				FS.Shop = Arch.Shop * float(0.6 + 0.8 * ((B.Street >> 4) % 100) / 100.0);   // street shop density
				FS.Balcony = Arch.Balcony * float(0.6 + 0.8 * B.Wealth);
				FS.SideWindows = Arch.SideWindows;
				FS.Symmetric = Arch.Symmetric;
				Plans[f] = OCPlanFacade(In, FS, OCSubSeed(BSeed, uint32(f) + 16 * Variant, OCSeedPurpose::Bays));
			}
			// Signature of the street front: archetype, bays, ground and balcony layout, windows, height, colour.
			FString Sig = FString::Printf(TEXT("%s|%s|%d|%d|"), *Arch.Name.ToString(), *Look.Upper.ToString(), Storeys, Look.Colour);
			if (B.Front >= 0)
			{
				for (const FOCBayColumn& Col : Plans[B.Front].Columns)
				{
					Sig += FString::Printf(TEXT("%d%d%d,"), int32(Col.WidthCm / 200), int32(Col.Ground), Col.BalconyTo - Col.BalconyFrom);
				}
			}
			return FCrc::StrCrc32(*Sig);
		};
		auto SigClash = [&](uint32 Sig)
		{
			const FIntPoint Cell0(FMath::FloorToInt32(C.X / 6000.0), FMath::FloorToInt32(C.Y / 6000.0));
			for (int32 dx = -1; dx <= 1; ++dx)
			{
				for (int32 dy = -1; dy <= 1; ++dy)
				{
					if (const TArray<int32>* L = SigCells.Find(Cell0 + FIntPoint(dx, dy)))
					{
						for (const int32 Other : *L)
						{
							if (Blds[Other].Signature == Sig && FVector::Dist2D(Blds[Other].C, C) < 6000.0 && Blds[Other].Lot != B.Lot)
							{
								return true;
							}
						}
					}
				}
			}
			return false;
		};
		B.Signature = PlanAll(0);
		if (B.Front >= 0 && SigClash(B.Signature))
		{
			++SigRerolls;
			B.Signature = PlanAll(1);
			SigRepeats += SigClash(B.Signature) ? 1 : 0;
		}
		if (B.Front >= 0)
		{
			SigCells.FindOrAdd(FIntPoint(FMath::FloorToInt32(C.X / 6000.0), FMath::FloorToInt32(C.Y / 6000.0))).Add(BuildingIndex);
		}

		FRandomStream Rw = Stream(OCSeedPurpose::Building, 99);   // per-window detail: shutter states, blind windows
		const float ClosedChance = Styles->ClosedShutters * float(0.2 + 0.8 * (1.0 - B.Upkeep));
		const float Dress = Styles->Dressing;
		// An external stair to the first floor on one open side wall (village, hills, some old-town houses).
		int32 StairFacade = INDEX_NONE;
		if (bPlaster && B.On < 0 && Storeys >= 2 && Rw.FRand() < Arch.ExtStair)
		{
			for (int32 f = 0; f < 4 && StairFacade == INDEX_NONE; ++f)
			{
				const FBld::FFacade& Fa = B.F[f];
				const FVector Probe = Starts[f] + Alongs[f] * 200.0 + Fa.Out * 150.0;
				if (!Fa.bStreet && Lengths[f] >= 800.0 && Plans[f].Columns.Num() > 0 && Plans[f].Columns[0].IsFour()
					&& Index.CoverTop(Probe, BuildingIndex) <= Lowest && Index.CoverTop(Starts[f] + Alongs[f] * 200.0 + Fa.Out * CoverProbeCm, BuildingIndex) <= Lowest)
				{
					StairFacade = f;
				}
			}
		}
		for (int32 f = 0; f < 4; ++f)
		{
			const FBld::FFacade& Fa = B.F[f];
			const FOCFacadePlan& Plan = Plans[f];
			const FVector Along = Alongs[f];
			const FRotator Rot = FRotationMatrix::MakeFromXY(Along, -Fa.Out).Rotator();
			const FVector Start = Starts[f];
			const bool bGableFacade = IsGableFacade(f);
			++Facades;
			Symmetric += Plan.bSymmetric ? 1 : 0;
			const bool bLanternDoor = B.StyleName == TEXT("oldtown") && Rp.FRand() < 0.4f * Dress;

			for (const FOCBayColumn& Col : Plan.Columns)
			{
				const int32 ColIndex = int32(&Col - Plan.Columns.GetData());
				const bool bStairCol = f == StairFacade && ColIndex == 0;
				const bool bArcadeFront = bPlaster && Arch.bArcade && Fa.bStreet && B.On < 0;
				const bool bFour = Col.IsFour();
				const double Bw = Col.WidthCm;
				const FVector BayStart = Start + Along * Col.OffsetCm;
				const FString Wall = Blank(bFour);
				const FString Plinth = bPlaster ? Sized(TEXT("Plinth"), bFour) : FString();

				// A neighbour against this bay: over its whole width (Full) the wall is hidden up to the
				// neighbour's top; over part of it the wall stays, but blank, up to that height.
				const double T1 = Index.CoverTop(BayStart + Along * (Bw * 0.25) + Fa.Out * CoverProbeCm, BuildingIndex);
				const double T2 = Index.CoverTop(BayStart + Along * (Bw * 0.75) + Fa.Out * CoverProbeCm, BuildingIndex);
				const bool bFull = T1 > Lowest && T2 > Lowest;
				const double CoverZ = bFull ? FMath::Min(T1, T2) : FMath::Max(T1, T2);

				// Downhill: the ground under this bay may fall further than the plinth reaches. Fill the drop
				// with plain lower storeys and put the plinth under the lowest one.
				const FVector Out10 = Fa.Out * 10.0;
				const double Ground = FMath::Min(Height.HeightCm((BayStart + Out10).X, (BayStart + Out10).Y),
					Height.HeightCm((BayStart + Along * Bw + Out10).X, (BayStart + Along * Bw + Out10).Y));
				const double Drop = B.On >= 0 ? 0.0 : Z0 - Ground;
				if (CoverZ <= Lowest && Drop > PlinthBelowCm - 20.0)
				{
					const int32 Lower = FMath::CeilToInt32((Drop - (PlinthBelowCm - 20.0)) / StoreyCm);
					for (int32 j = 1; j <= Lower; ++j)
					{
						Add(Wall, BayStart + FVector(0, 0, Z0 - j * StoreyCm), Rot, FVector::OneVector, true);
					}
					if (!Plinth.IsEmpty())
					{
						Add(Plinth, BayStart + FVector(0, 0, Z0 - Lower * StoreyCm), Rot);
					}
					++Basements;
				}

				bool bSkipNext = false;
				for (int32 s = 0; s < Storeys; ++s)
				{
					const double Zs = B.ZAt(s);
					CurScale = B.HAt(s) / StoreyCm;
					const FVector P = BayStart + FVector(0, 0, Zs);
					const bool bGround = s == 0 && B.On < 0;
					if (bSkipNext)
					{
						bSkipNext = false;
						continue;
					}
					if (CoverZ > Lowest && Zs < CoverZ)
					{
						if (bFull && Zs + B.HAt(s) <= CoverZ + 5.0)
						{
							++HiddenCells;
							continue;
						}
						Cell(Wall, P, Rot, bGround);   // straddles the neighbour's roof line, or half behind it
						if (bGround && !Plinth.IsEmpty())
						{
							Add(Plinth, P, Rot);
						}
						++PartyBlank;
						continue;
					}
					// Section 4.3: sill bands (marcadavanzale) and pilaster strips (lesene) on street fronts.
					if (s >= 1 && bPlaster && Fa.bStreet)
					{
						if (Arch.bSillBand) { Add(Sized(TEXT("SillBand"), bFour), P, Rot); }
						if (Arch.bLesene && ColIndex > 0 && ColIndex % 2 == 0) { Add(TEXT("Pilaster_320"), P, Rot); }
					}
					if (bStairCol && s == 1)
					{
						Cell(TEXT("Juliet_400"), P, Rot, false);   // the door at the head of the external stair
						continue;
					}
					if (bGround && bArcadeFront)
					{
						Cell(bFour ? FString(TEXT("Arcade_400")) : Sized(TEXT("RusticWall"), false), P, Rot, true);   // portico
						continue;
					}
					EOCBay Type = Col.At(s);
					if (bShed && Type == EOCBay::Window && OCSubSeed(BSeed, uint32(Col.OffsetCm), OCSeedPurpose::Bays) % 100 < 45)
					{
						Type = EOCBay::Blank;   // sheds: glazing strips on about half the columns
					}
					if (bGround && (Type == EOCBay::Door || Type == EOCBay::Shop) && Drop > 120.0)
					{
						// A door or shop would hang above the street: windows over a plinth instead.
						Type = bFour ? EOCBay::GroundWindow : EOCBay::Blank;
						++FloatingDoors;
					}
					if (bGround)
					{
						switch (Type)
						{
						case EOCBay::Door:
						{
							const FString Wide = Arch.Door.ToString() + TEXT("_400");
							if (bBrick || bShed)
							{
								Cell(bFour ? FString(bBrick ? TEXT("Brick_GoodsDoor_400") : TEXT("Shed_RollerDoor_400")) : Wall, P, Rot, true);
							}
							else if (bFour && Arch.Door != TEXT("Door") && Has(Wide))
							{
								Cell(Wide, P, Rot, true);
								bSkipNext = Arch.Door == TEXT("Church_Portal");   // the portal is two storeys tall
							}
							else
							{
								const FVector DoorAt = BayStart + Along * ((bFour && Col.bDoorAtEnd) ? 200.0 : 0.0);
								Cell(TEXT("Door_200"), DoorAt + FVector(0, 0, Z0), Rot, true);
								if (bFour)
								{
									const FVector WallAt = BayStart + Along * (Col.bDoorAtEnd ? 0.0 : 200.0);
									Add(TEXT("Wall_200"), WallAt + FVector(0, 0, Z0), Rot, FVector::OneVector, true);
									Add(TEXT("Plinth_200"), WallAt + FVector(0, 0, Z0), Rot);
								}
								if (Rp.FRand() < 0.55f)  // potted plants either side of the step
								{
									const FVector Step = DoorAt + Fa.Out * 60.0;
									AddProp(Rp.FRand() < 0.5f ? Pot : Fern, Step + Along * 20.0, Rot.Yaw + Rp.FRandRange(0.f, 360.f));
									AddProp(Pot, Step + Along * 180.0, Rot.Yaw + Rp.FRandRange(0.f, 360.f));
								}
								if (bLanternDoor)
								{
									Add(TEXT("Prop_WallLantern"), DoorAt + Along * 185.0 + FVector(0, 0, Z0 + 240.0), Rot);
								}
							}
							break;
						}
						case EOCBay::Shop:
						{
							if (bBrick || bShed)
							{
								Cell(bBrick ? FString(TEXT("Brick_GoodsDoor_400")) : FString(TEXT("Shed_RollerDoor_400")), P, Rot, true);
								break;
							}
							// Per shop: closed more often where upkeep is low, an awning more often where it is wealthy.
							FString Shop = Look.Shop.ToString() + TEXT("_400");
							if (Rw.FRand() < 0.35f * float(1.0 - B.Upkeep)) { Shop = TEXT("ShopClosed_400"); }
							else if (Rw.FRand() < 0.25f * float(B.Wealth)) { Shop = TEXT("ShopAwning_400"); }
							if (!Has(Shop)) { Shop = TEXT("Shopfront_400"); }
							Cell(Shop, P, Rot, true);
							if (Rp.FRand() < 0.8f)
							{
								Add(FString::Printf(TEXT("ShopSign_%02d"), Rp.RandRange(0, 11)), P, Rot);
							}
							if (Rp.FRand() < 0.15f * Dress)
							{
								Add(TEXT("Prop_ShopBlade"), BayStart + Along * 15.0 + FVector(0, 0, Z0 + 300.0), Rot);
							}
							const FVector Front = BayStart + Fa.Out * 90.0;
							const float Pick = Rp.FRand();
							if (bSeafront && Pick < 0.45f)
							{
								AddProp(Cafe, Front + Fa.Out * 60.0 + Along * 200.0, Rot.Yaw + 90.0);
							}
							else if (Pick < 0.35f)
							{
								AddProp(Rp.FRand() < 0.6f ? Crate : WoodCrate, Front + Along * Rp.FRandRange(30.f, 90.f), Rot.Yaw + Rp.FRandRange(-20.f, 20.f));
								AddProp(Crate, Front + Along * Rp.FRandRange(300.f, 370.f), Rot.Yaw + Rp.FRandRange(-20.f, 20.f));
							}
							break;
						}
						case EOCBay::GroundWindow:
						{
							if (bBrick || bShed)
							{
								Cell(WindowPiece(Look.Upper, bFour), P, Rot, true);
								break;
							}
							FString G = Look.GroundOther.ToString();
							G = (G == TEXT("GroundWindow") || G == TEXT("Garage") || G == TEXT("Portone")) ? G + TEXT("_400") : Sized(G, bFour);
							if (!Has(G)) { G = TEXT("GroundWindow_400"); }
							if (Arch.bRustic && G == TEXT("GroundWindow_400") && bPlaster) { G = TEXT("RusticWindow_400"); }
							Cell(G, P, Rot, true);
							if (G != TEXT("Garage_400") && G != TEXT("Portone_400") && G != TEXT("RusticWindow_400"))
							{
								Add(TEXT("Plinth_400"), P, Rot);
							}
							break;
						}
						default:
							if (Arch.bRustic && bPlaster && Fa.bStreet)
							{
								Cell(Sized(TEXT("RusticWall"), bFour), P, Rot, true);   // bugnato
								break;
							}
							Cell(Wall, P, Rot, true);
							if (!Plinth.IsEmpty()) { Add(Plinth, P, Rot); }
							break;
						}
						continue;
					}
					const bool bTop = Storeys >= 3 && s == Storeys - 1;
					if (Type == EOCBay::Blank)
					{
						Cell(Wall, P, Rot, false);
						// Wall dressing on blank bays: an AC unit, now and then.
						if (bPlaster && Rd.FRand() < 0.07f * Dress)
						{
							Add(TEXT("Prop_WallAC"), BayStart + Along * (Bw * 0.5) + FVector(0, 0, Zs + 120.0), Rot);
							++Dressed;
						}
						continue;
					}
					if (Type == EOCBay::Balcony && bFour && bPlaster)
					{
						FString Bal = Look.Balcony.ToString() + TEXT("_400");
						Cell(Has(Bal) ? Bal : FString(TEXT("Balcony_400")), P, Rot, false);
						continue;
					}
					if (bChurch && f == B.Front && bFour && int32(&Col - Plan.Columns.GetData()) == Plan.DoorColumn && s == 2)
					{
						Cell(TEXT("Church_Rose_400"), P, Rot, false);   // over the two-storey portal
						continue;
					}
					FName Family = s == 1 ? Look.UpperFirst : (bTop ? Look.Top : Look.Upper);
					if (bTop && Plan.bPlainTop && Family == Look.Upper && Family == TEXT("Window")) { Family = TEXT("WindowPlain"); }
					FString Win = WindowPiece(Family, bFour);
					if (bPlaster && Win.StartsWith(TEXT("Window_")))
					{
						// Upkeep: closed and half-closed shutters, window by window.
						const float X = Rw.FRand();
						if (X < ClosedChance * 0.6f) { Win = Sized(TEXT("WindowClosed"), bFour); }
						else if (X < ClosedChance) { Win = Sized(TEXT("WindowHalf"), bFour); }
					}
					if (bPlaster && !Fa.bStreet && Rw.FRand() < 0.10f * float(B.Age))
					{
						Win = Sized(TEXT("WindowBlind"), bFour);   // old houses: windows walled up on the side
					}
					Cell(Win, P, Rot, false);
					// Window dressing: flower boxes in wealthier streets (two windows in a 4 m bay).
					if (bPlaster && Win.StartsWith(TEXT("Window")) && !Win.StartsWith(TEXT("WindowBlind")) && Rd.FRand() < 0.10f * float(B.Wealth) * Dress)
					{
						const double Cx = bFour ? (Rd.FRand() < 0.5f ? 100.0 : 300.0) : Bw * 0.5;
						const FVector Sill = BayStart + Along * Cx + FVector(0, 0, Zs + 92.0 * CurScale);
						Add(TEXT("Prop_FlowerBox"), Sill, Rot);
						AddProp(Fern, Sill - Fa.Out * 13.0 + FVector(0, 0, 10.0), Rot.Yaw + Rd.FRandRange(0.f, 360.f), 0.35, false);
						++Dressed;
					}
					// Laundry on side and back walls of the old town and the village.
					if (bPlaster && !Fa.bStreet && bFour && (B.StyleName == TEXT("oldtown") || B.StyleName == TEXT("village")) && Rd.FRand() < 0.06f * Dress)
					{
						Add(TEXT("Prop_Laundry"), BayStart + Along * 50.0 + FVector(0, 0, Zs + 255.0), Rot);
						++Dressed;
					}
				}
				if (bStairCol)
				{
					CurScale = 1.0;
					Add(TEXT("ExtStair_400"), BayStart + FVector(0, 0, Z0), Rot, FVector(1.0, 1.0, B.GroundH / StoreyCm));
					++Dressed;
				}
				// A battered base (scarpa) where the street falls away, and on towers.
				if (bPlaster && Arch.bScarpa && Fa.bStreet && B.On < 0 && CoverZ <= Lowest && (Drop > 80.0 || Arch.Name == TEXT("tower")))
				{
					CurScale = 1.0;
					Add(Sized(TEXT("Scarpa"), bFour), BayStart + FVector(0, 0, Z0), Rot);
				}
				CurScale = 1.0;
				// Under the roof: the crown (where not behind a taller neighbour), a parapet on flat roofs,
				// a gutter on pitched eaves; a string course over the ground floor of street fronts.
				const bool bRoofHidden = bFull && CoverZ >= TopZ - 5.0;
				if (bCrown && !bRoofHidden && !bGableFacade)
				{
					Add(Sized(CrownName, bFour), BayStart + FVector(0, 0, TopZ), Rot);
				}
				if (!bPitched && !bVault && !bLeanTo && !(bFull && CoverZ >= TopZ + 85.0))
				{
					Add(Sized(TEXT("Parapet"), bFour), BayStart + FVector(0, 0, TopZ + (bCrown ? CrownH : 0.0)), Rot);
				}
				if (bPitched && !bGableFacade && !bRoofHidden)
				{
					// A row of pan tiles along the eave (clay roofs), a gutter under it on street fronts.
					if (!bSlate && !bShed && bEaves)
					{
						Add(Sized(TEXT("EavesRow"), bFour), BayStart + FVector(0, 0, RoofZ + 0.04 * Rise - 18.0), Rot);
					}
					if (Fa.bStreet)
					{
						Add(Sized(TEXT("Gutter"), bFour), BayStart - Fa.Out * (Overhang - 40.0) + FVector(0, 0, RoofZ), Rot);
					}
				}
				if (bPlaster && Arch.bStringCourse && Fa.bStreet && Storeys >= 2 && !(CoverZ > Lowest && CoverZ > B.ZAt(1)))
				{
					Add(Sized(TEXT("StringCourse"), bFour), BayStart + FVector(0, 0, B.ZAt(1)), Rot);
				}
				if (Arch.bCanopy && f == B.Front && bFour && CoverZ <= Lowest)
				{
					Add(TEXT("Canopy_400"), BayStart + FVector(0, 0, Z0), Rot);
				}
			}

			// A cable along the side walls at the first-floor line, now and then.
			if (bPlaster && !Fa.bStreet && Storeys >= 2 && Rd.FRand() < 0.25f * Dress)
			{
				Add(TEXT("Prop_Cable"), Start - Fa.Out * 3.0 + FVector(0, 0, B.ZAt(1) - 20.0), Rot, FVector(Lengths[f] / 100.0, 1.0, 1.0));
			}
			// The corner this facade starts at: quoins or pilasters, a downpipe on street fronts and the cornice
			// mitre, each only above whatever neighbour stands against either side of the corner.
			if (bChamferStart[f])
			{
				continue;   // the chamfer pieces close this corner
			}
			const double CornerZ = FMath::Max(Index.CoverTop(Start + Fa.Out * CoverProbeCm + Along * 30.0, BuildingIndex),
				Index.CoverTop(Start - Along * CoverProbeCm - Fa.Out * 30.0, BuildingIndex));
			const bool bEdgeQuoins = Arch.bQuoins && bPlaster && !(B.StyleName == TEXT("grid90") && Storeys > 5);
			for (int32 s = 0; s < Storeys; ++s)
			{
				const double Zs = B.ZAt(s);
				CurScale = B.HAt(s) / StoreyCm;
				if (Zs < CornerZ)
				{
					continue;
				}
				if (bEdgeQuoins)
				{
					Add(TEXT("Corner_320"), Start + FVector(0, 0, Zs), Rot);
				}
				else if (Arch.bPilasters)
				{
					Add(TEXT("Pilaster_320"), Start + FVector(0, 0, Zs), Rot);
				}
				if (Fa.bStreet && f % 2 == 0 && bPlaster)
				{
					// The pipe's brackets reach Y = 0, so its pivot sits on the wall face.
					Add(TEXT("Drainpipe_320"), Start + Along * 45.0 + FVector(0, 0, Zs), Rot);
				}
			}
			CurScale = 1.0;
			if (bCrown && CrownName == TEXT("Cornice") && CornerZ < TopZ - 5.0 && !bChamferEnd[(f + 3) % 4])
			{
				Add(TEXT("CorniceCorner"), Start + FVector(0, 0, TopZ), Rot);  // mitre block: closes the cornice at the corner
			}
		}

		// ---- Section 4.1 and 4.5: what joins this building to its neighbours and its ground.
		if (bPlaster && B.On < 0)
		{
			// A wall shrine at a street corner, at first-floor height, where the corner bay is blank there.
			if (B.Front >= 0 && Storeys >= 2 && Rp.FRand() < Arch.Edicola && !bChamferStart[B.Front])
			{
				const FOCFacadePlan& Pl = Plans[B.Front];
				if (Pl.Columns.Num() > 0 && Pl.Columns[0].At(1) == EOCBay::Blank)
				{
					const FBld::FFacade& Fa = B.F[B.Front];
					Add(TEXT("Edicola"), Starts[B.Front] + Alongs[B.Front] * (Pl.Columns[0].WidthCm * 0.5) + FVector(0, 0, B.ZAt(1) + 60.0),
						FRotationMatrix::MakeFromXY(Alongs[B.Front], -Fa.Out).Rotator());
					++Dressed;
				}
			}
			// Across a narrow gap to the facing house: brace arches (archetti) high up, or a bridge room
			// (sottoportico) on the first-floor line when both houses front the same street.
			for (int32 f = 0; f < 4; ++f)
			{
				const FBld::FFacade& Fa = B.F[f];
				const FVector Mid = C + Fa.Out * Fa.Half;
				int32 Who = INDEX_NONE;
				double Gap = 0.0;
				for (double Dd = 60.0; Dd <= (Fa.bStreet ? 950.0 : 460.0); Dd += 10.0)
				{
					int32 W2 = INDEX_NONE;
					if (Index.CoverTop(Mid + Fa.Out * Dd, BuildingIndex, &W2) > Lowest && W2 != INDEX_NONE)
					{
						Who = W2;
						Gap = Dd;
						break;
					}
				}
				if (Who == INDEX_NONE || Who < BuildingIndex || Blds[Who].Lot == B.Lot || Gap < 240.0 || Blds[Who].On >= 0)
				{
					continue;
				}
				const FRotator Rot = FRotationMatrix::MakeFromXY(Alongs[f], -Fa.Out).Rotator();
				const double Under = FMath::Min(TopZ, Blds[Who].TopZ) - 250.0;
				const double Draw = Rd.FRand();
				if (Draw < Arch.BraceArches * (Fa.bStreet ? 0.25 : 1.0) && Under > Z0 + (Fa.bStreet ? 600.0 : 450.0))
				{
					Add(TEXT("Archetto"), Mid + FVector(0, 0, Under), Rot, FVector(1.0, Gap / 100.0, 1.0));
					if (Gap < 500.0 && Rd.FRand() < 0.5f)
					{
						Add(TEXT("Archetto"), Mid + Alongs[f] * FMath::Min(300.0, Fa.Length * 0.3) + FVector(0, 0, Under - 280.0), Rot, FVector(1.0, Gap / 100.0, 1.0));
					}
					++Dressed;
				}
				else if (!Fa.bStreet && B.Front >= 0 && Blds[Who].Front >= 0 && Rd.FRand() < Arch.Bridges
					&& Gap <= 460.0 && FVector::DotProduct(B.F[B.Front].Out, Blds[Who].F[Blds[Who].Front].Out) > 0.95
					&& FMath::Abs(FVector::DotProduct(B.F[B.Front].Out, Fa.Out)) < 0.1 && Storeys >= 2 && Blds[Who].Storeys >= 2)
				{
					// The room spans the gap at the street front, 4 m deep, on this house's first-floor line.
					const FVector FrontOut = B.F[B.Front].Out;
					const FVector Front0 = C + Fa.Out * Fa.Half + FrontOut * B.F[B.Front].Half;   // this side wall, at the street front
					FMatrix M = FRotationMatrix::MakeFromXY(Fa.Out, -FrontOut);
					FVector Origin = Front0;
					if (M.GetScaledAxis(EAxis::Z).Z < 0.0)
					{
						M = FRotationMatrix::MakeFromXY(-Fa.Out, -FrontOut);
						Origin = Front0 + Fa.Out * Gap;
					}
					Add(TEXT("Sottoportico"), Origin + FVector(0, 0, B.ZAt(1)), M.Rotator(), FVector(Gap / 100.0, 1.0, B.StoreyH / StoreyCm));
					++Dressed;
				}
			}
			// Villas and farms: a walled garden (tiled coping) 5 m out on the back and sides, gate piers in front.
			if (Arch.bGardenWalls && B.bLotMain)
			{
				for (int32 f = 0; f < 4; ++f)
				{
					const FBld::FFacade& Fa = B.F[f];
					const FRotator Rot = FRotationMatrix::MakeFromXY(Alongs[f], -Fa.Out).Rotator();
					if (f == B.Front)
					{
						const double Reach = FMath::Min(500.0, Fa.RoadDist + StreetProbeCm - 250.0);
						if (Reach > 250.0)
						{
							for (const double Side : { -1.0, 1.0 })
							{
								const FVector At = C + Fa.Out * (Fa.Half + Reach) + Alongs[f] * (Side * 220.0);
								Add(TEXT("GatePier"), FVector(At.X, At.Y, Height.HeightCm(At.X, At.Y)), Rot);
							}
						}
						continue;
					}
					const double Len = Fa.Length + 1000.0;
					const FVector Line0 = C + Fa.Out * (Fa.Half + 500.0) - Alongs[f] * (Len * 0.5);
					for (double X = 0.0; X < Len - 10.0; X += 400.0)
					{
						const double Seg = FMath::Min(400.0, Len - X);
						const FVector At = Line0 + Alongs[f] * X;
						const FVector Centre = At + Alongs[f] * (Seg * 0.5);
						if (Index.CoverTop(Centre, -1) > Lowest || RoadIndex.EdgeDistance(FVector2D(Centre)) < 150.0)
						{
							continue;
						}
						Add(TEXT("GardenWall_400"), FVector(At.X, At.Y, Height.HeightCm(Centre.X, Centre.Y)), Rot, FVector(Seg / 400.0, 1.0, 1.0));
					}
				}
				++Dressed;
			}
		}

		// The chamfered corner: one 2.83 m bay column from the end of one facade to the start of the next.
		if (B.Chamfer)
		{
			int32 Fe = INDEX_NONE, Fs = INDEX_NONE;
			for (int32 f = 0; f < 4; ++f)
			{
				if (bChamferEnd[f]) { Fe = f; }
				if (bChamferStart[f]) { Fs = f; }
			}
			if (Fe != INDEX_NONE && Fs != INDEX_NONE)
			{
				FVector Pa = Starts[Fe] + Alongs[Fe] * Lengths[Fe], Pb = Starts[Fs];
				FVector Along = (Pb - Pa).GetSafeNormal2D();
				FVector OutDir = FVector::CrossProduct(Along, FVector::UpVector);
				if (FVector::DotProduct(OutDir, (Pa + Pb) * 0.5 - C) < 0.0)
				{
					Swap(Pa, Pb);
					Along = -Along;
					OutDir = -OutDir;
				}
				const FRotator Rot = FRotationMatrix::MakeFromXY(Along, -OutDir).Rotator();
				for (int32 s = 0; s < Storeys; ++s)
				{
					const FVector P = Pa + FVector(0, 0, B.ZAt(s));
					CurScale = B.HAt(s) / StoreyCm;
					Cell(s == 0 ? FString(TEXT("Wall_283")) : FString(Look.bShutters ? TEXT("Window_283") : TEXT("WindowPlain_283")), P, Rot, s == 0);
					if (s == 0) { Add(TEXT("Plinth_283"), P, Rot); }
				}
				CurScale = 1.0;
				if (bCrown) { Add(TEXT("Cornice_283"), Pa + FVector(0, 0, TopZ), Rot); }
				Add(TEXT("Parapet_283"), Pa + FVector(0, 0, TopZ + CrownH), Rot);
				++Chamfers;
			}
		}

		// ---- Roof (BUILDINGS_PLAN.md phase 4 and section 4.2).
		const FRotator Yaw(0.0, FMath::RadiansToDegrees(FMath::Atan2(U.Y, U.X)), 0.0); // yaw only: roofs stay upright
		const FVector RAlong = bRidgeU ? U : Vw, RAcross = bRidgeU ? Vw : U;
		const FRotator RidgeRot(0.0, FMath::RadiansToDegrees(FMath::Atan2(RAlong.Y, RAlong.X)), 0.0);
		const FString HipEnd = bSlate ? TEXT("RoofHipEndSlate") : TEXT("RoofHipEnd");
		const FString Ridge = bShed ? TEXT("RoofRidgeMetal") : (bSlate ? TEXT("RoofRidgeSlate") : TEXT("RoofRidge"));
		const bool bItalian = B.StyleName == TEXT("oldtown") || B.StyleName == TEXT("village") || B.StyleName == TEXT("hills") || B.StyleName == TEXT("grid90");
		if (bHip)
		{
			// Two hip ends scaled uniformly by the roof's width and a ridge stretched between them: every
			// slope has the same pitch whatever the building's proportions.
			const double Half = FMath::Max(0.0, (RLen - RWid) * 0.5);
			const FVector Base = C + FVector(0, 0, RoofZ);
			Add(HipEnd, Base - RAlong * Half, RidgeRot, FVector(RWid / 100.0, RWid / 100.0, Rise / 100.0));
			Add(HipEnd, Base + RAlong * Half, RidgeRot + FRotator(0.0, 180.0, 0.0), FVector(RWid / 100.0, RWid / 100.0, Rise / 100.0));
			if (Half > 1.0)
			{
				Add(Ridge, Base - RAlong * Half, RidgeRot, FVector(2.0 * Half / 100.0, RWid / 100.0, Rise / 100.0));
			}
			++Hips;
		}
		else if (bGable)
		{
			Add(Ridge, C - RAlong * (RLen * 0.5) + FVector(0, 0, RoofZ), RidgeRot, FVector(RLen / 100.0, RWid / 100.0, Rise / 100.0));
			// Gable end walls in the building's material, from the wall top up under the roof; where a neighbour
			// as tall stands against it, the party wall rises 15 cm through the tiles as a coping line.
			// A church front gets its pediment and a bell gable instead.
			const FString Gable = bBrick ? TEXT("GableEndBrick") : (bShed ? TEXT("GableEndCorrugated") : TEXT("GableEnd"));
			for (int32 f = 0; f < 4; ++f)
			{
				if (!IsGableFacade(f))
				{
					continue;
				}
				const FRotator Rot = FRotationMatrix::MakeFromXY(Alongs[f], -B.F[f].Out).Rotator();
				const double GableH = (RoofZ - TopZ) + Rise * (RWid - 2.0 * Overhang) / RWid;
				const FVector At = Starts[f] + FVector(0, 0, TopZ);
				if (bChurch && f == B.Front)
				{
					Add(TEXT("Pediment"), At, Rot, FVector(Lengths[f] / 100.0, 1.0, (GableH + 40.0) / 100.0));
					Add(TEXT("BellGable"), At + Alongs[f] * (Lengths[f] * 0.5) + FVector(0, 0, GableH + 20.0), Rot);
					continue;
				}
				Add(Gable, At, Rot, FVector(Lengths[f] / 100.0, 1.0, GableH / 100.0));
				const FVector Mid = C + B.F[f].Out * (B.F[f].Half + CoverProbeCm);
				if (bPlaster && Index.CoverTop(Mid, BuildingIndex) >= TopZ - 60.0)
				{
					Add(Gable, At + FVector(0, 0, 15.0), Rot, FVector(Lengths[f] / 100.0, 0.5, GableH / 100.0));
				}
			}
			++Gables;
		}
		else if (bLeanTo)
		{
			// Lean-to: a single pitch falling away from the taller wall it stands against (the lot's main volume).
			for (int32 f = 0; f < 4; ++f)
			{
				int32 Who = INDEX_NONE;
				const FBld::FFacade& Fa = B.F[f];
				if (Index.CoverTop(C + Fa.Out * (Fa.Half + CoverProbeCm), BuildingIndex, &Who) > Lowest && Who != INDEX_NONE && Blds[Who].Lot == B.Lot)
				{
					const FRotator Rot = FRotationMatrix::MakeFromXY(-Alongs[f], Fa.Out).Rotator();
					Add(TEXT("RoofMono"), Starts[f] + Alongs[f] * Lengths[f] + FVector(0, 0, TopZ), Rot, FVector(Lengths[f] / 100.0, (2.0 * Fa.Half + 40.0) / 100.0, 1.1));
					break;
				}
			}
			++Gables;
		}
		else if (bVault)
		{
			// Campanian barrel vault along the long side, rendered like the walls.
			const bool bAlongU = W >= D;
			Add(TEXT("RoofVault"), C + FVector(0, 0, TopZ), FRotator(0.0, Yaw.Yaw + (bAlongU ? 0.0 : 90.0), 0.0),
				FVector((bAlongU ? W : D) / 100.0, (bAlongU ? D : W) / 100.0, FMath::Min(W, D) * 0.16 / 100.0));
			++Flats;
		}
		if (bPitched && bPlaster)
		{
			// Chimneys on the slope (Italian pots in the old styles): base at the lowest roof point under each,
			// stretched to stand 0.9 m clear of the highest.
			const int32 Chimneys = bTorretta ? 0 : FMath::Clamp(Arch.Chimneys + (Rr.FRand() < 0.5f ? 0 : -1), 0, 3);
			for (int32 k = 0; k < Chimneys; ++k)
			{
				const double La = Rr.FRandRange(-0.35f, 0.35f) * RLen, Lc = Rr.FRandRange(-0.25f, 0.25f) * RWid;
				double Lo = TNumericLimits<double>::Max(), Hi = -TNumericLimits<double>::Max();
				for (const double Dx : { -36.0, 36.0 })
				{
					for (const double Dy : { -36.0, 36.0 })
					{
						const double H = PitchedHeight(La + Dx, Lc + Dy, RLen, RWid, Rise, bHip);
						Lo = FMath::Min(Lo, H);
						Hi = FMath::Max(Hi, H);
					}
				}
				Add(bItalian ? TEXT("Prop_ChimneyPot") : TEXT("Prop_Chimney"), C + RAlong * La + RAcross * Lc + FVector(0, 0, RoofZ + Lo - 10.0), Yaw,
					FVector(1.0, 1.0, (Hi - Lo + 100.0) / 100.0));
			}
			// Dormers on street-facing slopes, a TV aerial on the ridge, now and then an altana over it.
			if (!bTorretta && Rr.FRand() < Arch.Dormers)
			{
				for (int32 f = 0; f < 4; ++f)
				{
					if (IsGableFacade(f) || !B.F[f].bStreet || Lengths[f] < 600.0)
					{
						continue;
					}
					const bool bAlongRidge = FMath::Abs(FVector::DotProduct(Alongs[f], RAlong)) > 0.7;
					const double Room = bAlongRidge ? (bHip ? RLen - RWid - 200.0 : RLen - 400.0) : RWid - 600.0;
					const int32 Count = Room > 600.0 ? 2 : (Room > 150.0 ? 1 : 0);
					for (int32 k = 0; k < Count; ++k)
					{
						const double Off = Count == 1 ? 0.0 : (k == 0 ? -0.25 : 0.25) * Room;
						const double In = Overhang + 90.0;   // front wall 0.9 m inside the wall line
						const double Across = (bAlongRidge ? RWid : RLen) * 0.5 - In;
						const double H = bAlongRidge ? PitchedHeight(Off, Across, RLen, RWid, Rise, bHip) : PitchedHeight(Across, Off, RLen, RWid, Rise, bHip);
						const FVector Mid = C + B.F[f].Out * (B.F[f].Half - In + Overhang) + Alongs[f] * Off;
						Add(TEXT("Dormer"), FVector(Mid.X, Mid.Y, RoofZ + H - 20.0), FRotationMatrix::MakeFromXY(Alongs[f], -B.F[f].Out).Rotator());
					}
					break;
				}
			}
			if (Rr.FRand() < 0.35f)
			{
				const double La = Rr.FRandRange(-0.25f, 0.25f) * FMath::Max(0.0, RLen - RWid);
				Add(TEXT("Prop_Aerial"), C + RAlong * La + FVector(0, 0, RoofZ + Rise - 30.0), Yaw);
			}
			if (!bTorretta && Rr.FRand() < Arch.Altana && RLen - (bHip ? RWid : 0.0) > 350.0)
			{
				Add(TEXT("Altana"), C + FVector(0, 0, RoofZ + Rise - 60.0), RidgeRot);
			}
		}
		if (!bPitched && !bLeanTo && !bVault)
		{
			if (B.Chamfer)
			{
				// Two slabs that leave the cut corner open.
				Add(TEXT("RoofFlat"), C + U * (ChamferCm * 0.5) + FVector(0, 0, TopZ), Yaw, FVector((W - ChamferCm) / 100.0, D / 100.0, 1.0));
				Add(TEXT("RoofFlat"), C - U * ((W - ChamferCm) * 0.5) + Vw * (ChamferCm * 0.5) + FVector(0, 0, TopZ), Yaw, FVector(ChamferCm / 100.0, (D - ChamferCm) / 100.0, 1.0));
			}
			else
			{
				Add(TEXT("RoofFlat"), C + FVector(0, 0, TopZ), Yaw, FVector(W / 100.0, D / 100.0, 1.0));
			}
			++Flats;
			// Clutter on a 3 m grid kept 1 m inside the parapet, so pieces never overlap; a pergola or the hut first.
			const int32 Nx = bHasTop ? 0 : FMath::FloorToInt32((W - 200.0 - (B.Chamfer ? ChamferCm : 0.0)) / 300.0), Ny = FMath::FloorToInt32((D - 200.0) / 300.0);
			if (Nx > 0 && Ny > 0)
			{
				TArray<int32> CellsXY;
				for (int32 k = 0; k < Nx * Ny; ++k) { CellsXY.Add(k); }
				for (int32 k = CellsXY.Num() - 1; k > 0; --k) { CellsXY.Swap(k, Rp.RandRange(0, k)); }
				const int32 Props = FMath::Min(CellsXY.Num(), FMath::Clamp(FMath::RoundToInt32(W * D / 1.0e4 / 70.0), 1, 5));
				static const TCHAR* RoofProps[] = { TEXT("Prop_WaterTank"), TEXT("Prop_ACUnit"), TEXT("Prop_Aerial"), TEXT("Prop_Dish") };
				const bool bPergola = Rp.FRand() < Arch.Pergola;
				const bool bHut = !bPergola && Rp.FRand() < 0.5f;
				const double Shift = B.Chamfer ? ChamferCm * 0.5 : 0.0;
				for (int32 p = 0; p < Props; ++p)
				{
					const int32 Cx = CellsXY[p] % Nx, Cy = CellsXY[p] / Nx;
					const double Lu = Shift - 0.5 * (Nx * 300.0) + (Cx + 0.5) * 300.0 + Rp.FRandRange(-30.f, 30.f);
					const double Lv = -0.5 * (Ny * 300.0) + (Cy + 0.5) * 300.0 + Rp.FRandRange(-30.f, 30.f);
					const TCHAR* What = p == 0 && bPergola ? TEXT("Pergola") : ((p == 0 && bHut) ? TEXT("Prop_RoofHut") : RoofProps[Rp.RandRange(0, 3)]);
					Add(What, C + U * Lu + Vw * Lv + FVector(0, 0, TopZ + 25.0), FRotator(0.0, Yaw.Yaw + 90.0 * Rp.RandRange(0, 3), 0.0));
				}
			}
		}
		++Buildings;
	}

	int32 Count = 0;
	for (const TPair<FIntPoint, TArray<FInstance>>& Pair : Chunks)
	{
		FBox Bounds(ForceInit);
		for (const FInstance& I : Pair.Value)
		{
			Bounds += I.Transform.GetLocation();
		}
		AOCKitBuildingChunk* Actor = World->SpawnActor<AOCKitBuildingChunk>(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z), FRotator::ZeroRotator);
		if (!Actor)
		{
			continue;
		}
		for (const FInstance& I : Pair.Value)
		{
			UInstancedStaticMeshComponent* ISM = Actor->GetOrAddPiece(I.Mesh);
			const int32 InstanceIndex = ISM->AddInstance(I.Transform, /*bWorldSpace*/ true);
			ISM->SetCustomData(InstanceIndex, TArrayView<const float>(I.Data, AOCKitBuildingChunk::NumCustomData));
			++Count;
		}
		Actor->SetActorLabel(FString::Printf(TEXT("Kit_Buildings_%d_%d"), Pair.Key.X, Pair.Key.Y));
		Actor->SetFolderPath(TEXT("Buildings/Kit"));
	}

	// ---- Reports (BUILDINGS_PLAN.md 3.5): what was built, and how varied it is.
	TArray<FString> Missing;
	for (const FName& N : Pieces.Missing) { Missing.Add(N.ToString()); }
	UE_LOG(LogOpenCity, Display, TEXT("BuildKitBuildings: %d buildings, %d piece instances in %d chunks%s"), Buildings, Count, Chunks.Num(),
		Missing.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("; missing pieces skipped: %s"), *FString::Join(Missing, TEXT(", "))));
	UE_LOG(LogOpenCity, Display, TEXT("BuildKitBuildings: %d street facades of %d, %d symmetric; %d wall cells hidden by neighbours, %d blanked; %d bays on a lower storey (slope); %d doors/shops kept off a drop; %d chamfered corners; %d wall details"),
		StreetFacades, Facades, Symmetric, HiddenCells, PartyBlank, Basements, FloatingDoors, Chamfers, Dressed);
	UE_LOG(LogOpenCity, Display, TEXT("BuildKitBuildings: roofs %d hip, %d gable, %d flat; %d records with an unknown archetype"), Hips, Gables, Flats, MissingArch);
	// Neighbour contrast: touching buildings of different lots should differ in at least two of storeys,
	// wall colour, window family and crown.
	int32 Pairs = 0, Contrasted = 0;
	for (int32 i = 0; i < Blds.Num(); ++i)
	{
		for (const FBld::FFacade& Fa : Blds[i].F)
		{
			int32 Who = INDEX_NONE;
			if (Index.CoverTop(Blds[i].C + Fa.Out * (Fa.Half + CoverProbeCm), i, &Who) > Lowest && Who > i && Blds[Who].Lot != Blds[i].Lot)
			{
				const FLook& A = Blds[i].Look;
				const FLook& Bk = Blds[Who].Look;
				const int32 Diff = (Blds[i].Storeys != Blds[Who].Storeys) + (A.Colour != Bk.Colour || Blds[i].Arch->Palette != Blds[Who].Arch->Palette) + (A.Upper != Bk.Upper) + (A.Crown != Bk.Crown);
				++Pairs;
				Contrasted += Diff >= 2 ? 1 : 0;
			}
		}
	}
	UE_LOG(LogOpenCity, Display, TEXT("BuildKitBuildings: variety: %d street fronts re-planned to avoid a repeat within 60 m (%d still repeat); neighbour contrast %d of %d touching pairs (%.0f%%) differ in 2+ of storeys, colour, windows, crown; %d colour re-draws"),
		SigRerolls, SigRepeats, Contrasted, Pairs, 100.0 * Contrasted / FMath::Max(1, Pairs), ColourRerolls);
	// Piece share of facade cells per district style (warn over 25%).
	for (const TPair<FString, TMap<FName, int32>>& Style : CellsPerStyle)
	{
		int32 Total = 0;
		TPair<FName, int32> Top(NAME_None, 0);
		for (const TPair<FName, int32>& P : Style.Value)
		{
			Total += P.Value;
			if (P.Value > Top.Value) { Top = P; }
		}
		const double Share = 100.0 * Top.Value / FMath::Max(1, Total);
		UE_LOG(LogOpenCity, Display, TEXT("BuildKitBuildings: %s: %d facade cells, %d pieces, most used %s %.1f%%%s"), *Style.Key, Total, Style.Value.Num(),
			*Top.Key.ToString(), Share, Share > 25.0 ? TEXT("  (over 25%)") : TEXT(""));
	}
	// Street contrast: a street's storey and colour mix should not just be its district's average.
	{
		TMap<FString, TMap<int32, TArray<int32>>> ByStyleStreet;
		for (int32 i = 0; i < Blds.Num(); ++i)
		{
			if (Blds[i].On < 0 && Blds[i].bLotMain) { ByStyleStreet.FindOrAdd(Blds[i].StyleName).FindOrAdd(Blds[i].Street).Add(i); }
		}
		int32 Streets = 0, Flat = 0;
		for (const TPair<FString, TMap<int32, TArray<int32>>>& S : ByStyleStreet)
		{
			TMap<int32, double> DistSt, DistCol;
			int32 All = 0;
			for (const TPair<int32, TArray<int32>>& St : S.Value) { for (const int32 i : St.Value) { DistSt.FindOrAdd(Blds[i].Storeys) += 1; DistCol.FindOrAdd(Blds[i].Look.Colour) += 1; ++All; } }
			for (const TPair<int32, TArray<int32>>& St : S.Value)
			{
				if (St.Value.Num() < 5) { continue; }
				TMap<int32, double> Hs, Hc;
				for (const int32 i : St.Value) { Hs.FindOrAdd(Blds[i].Storeys) += 1; Hc.FindOrAdd(Blds[i].Look.Colour) += 1; }
				double L1 = 0.0;
				for (const TPair<int32, double>& P : DistSt) { L1 += FMath::Abs(P.Value / All - Hs.FindRef(P.Key) / St.Value.Num()); }
				for (const TPair<int32, double>& P : DistCol) { L1 += FMath::Abs(P.Value / All - Hc.FindRef(P.Key) / St.Value.Num()); }
				++Streets;
				Flat += L1 * 0.25 < 0.2 ? 1 : 0;
			}
		}
		UE_LOG(LogOpenCity, Display, TEXT("BuildKitBuildings: street contrast: %d of %d streets (5+ buildings) are within 0.2 of their district's storey and colour mix"), Flat, Streets);
	}
	// Overall piece share.
	PieceUse.ValueSort([](int32 A, int32 B) { return A > B; });
	int32 PieceTotal = 0;
	for (const TPair<UStaticMesh*, int32>& P : PieceUse) { PieceTotal += P.Value; }
	FString Share;
	int32 Listed = 0;
	for (const TPair<UStaticMesh*, int32>& P : PieceUse)
	{
		if (Listed++ >= 25) { break; }
		Share += FString::Printf(TEXT(" %s %.1f%%"), *P.Key->GetName().Replace(TEXT("SM_Kit_"), TEXT("")), 100.0 * P.Value / FMath::Max(1, PieceTotal));
	}
	UE_LOG(LogOpenCity, Display, TEXT("BuildKitBuildings: %d piece kinds; top 25:%s"), PieceUse.Num(), *Share);
	return Count;
}



int32 UOCWorldBuilderLibrary::BuildStreetProps(UWorld* World, const FString& LayoutJsonPath, const FString& HeightmapPng,
	float SidewalkWidthCm, float ChunkSizeCm, int32 Seed)
{
	using namespace OCKit;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	OCWorld::FHeightField Height;
	FOCRoadGraph Roads;
	FString Error;
	if (!World || !FFileHelper::LoadFileToString(Text, *LayoutJsonPath)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()
		|| !Height.Load(HeightmapPng, Root) || !Roads.LoadFromLayoutJson(Text, Error))
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildStreetProps: cannot read the layout or heightmap"));
		return 0;
	}
	auto LoadProp = [](const TCHAR* Name) { return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Environment/Props/%s/%s.%s"), Name, Name, Name)); };
	UStaticMesh* Lamp = LoadProp(TEXT("SM_Prop_street_lamp_01"));
	UStaticMesh* Trees[2] = { LoadProp(TEXT("SM_Prop_island_tree_01")), LoadProp(TEXT("SM_Prop_island_tree_02")) };
	UStaticMesh* Bin = LoadProp(TEXT("SM_Prop_metal_trash_can"));
	if (!Lamp)
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildStreetProps: props not imported (OCArtImport.cpp (props))"));
		return 0;
	}

	// Junction clearance per node: half the widest road there plus a margin (as FOCLaneGraph).
	const TArray<FOCRoadNode>& Nodes = Roads.GetNodes();
	TArray<double> Clear;
	Clear.Init(0.0, Nodes.Num());
	TArray<int32> Degree;
	Degree.Init(0, Nodes.Num());
	for (const FOCRoadSegment& S : Roads.GetSegments())
	{
		Clear[S.A] = FMath::Max(Clear[S.A], S.WidthCm * 0.5 + SidewalkWidthCm + 300.0);
		Clear[S.B] = FMath::Max(Clear[S.B], S.WidthCm * 0.5 + SidewalkWidthCm + 300.0);
		++Degree[S.A];
		++Degree[S.B];
	}

	TMap<FIntPoint, TArray<FInstance>> Chunks;
	FRandomStream R(Seed);
	int32 Lamps = 0, TreeCount = 0;
	auto Put = [&](UStaticMesh* Mesh, const FVector& At, double YawDeg, double Scale)
	{
		if (!Mesh)
		{
			return;
		}
		FInstance I;
		I.Mesh = Mesh;
		I.Transform = FTransform(FRotator(0.0, YawDeg, 0.0), FVector(At.X, At.Y, Height.HeightCm(At.X, At.Y) + 27.0), FVector(Scale));
		for (int32 k = 0; k < 6; ++k) { I.Data[k] = 1.f; }
		Chunks.FindOrAdd(FIntPoint(FMath::FloorToInt32(At.X / ChunkSizeCm), FMath::FloorToInt32(At.Y / ChunkSizeCm))).Add(I);
	};

	for (const FOCRoadSegment& S : Roads.GetSegments())
	{
		const bool bSidewalk = S.Kind == TEXT("avenue") || S.Kind == TEXT("street") || S.Kind == TEXT("coast");
		if (!bSidewalk || !S.Bridge.IsNone())
		{
			continue;
		}
		const FVector A = Nodes[S.A].Location, B = Nodes[S.B].Location;
		const FVector Dir = FVector(B - A).GetSafeNormal2D();
		const FVector Right(-Dir.Y, Dir.X, 0.0);
		const double Len = FVector::Dist2D(A, B);
		const double From = Clear[S.A], To = Len - Clear[S.B];
		if (To - From < 400.0)
		{
			continue;
		}
		const bool bBoulevard = S.Kind == TEXT("avenue") || S.Kind == TEXT("coast");
		const double Curb = S.WidthCm * 0.5 + 55.0;                  // lamps and bins at the kerb
		const double Mid = S.WidthCm * 0.5 + SidewalkWidthCm * 0.5;  // trees in the middle of the pavement
		auto At = [&](double D, double Side, double Off) { return A + Dir * D + Right * (Side * Off); };
		const double SegYaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));

		// Street lights every 28 m, staggered across the two sides; both sides on boulevards. Centro
		// storico (wall lanterns), post-war streets and the coast road (LED poles) are lit by
		// BuildStreetFixtures instead.
		const bool bFixtureLit = S.Profile == TEXT("centro_storico") || S.Profile == TEXT("costiera")
			|| (S.Era == TEXT("postwar") && (S.Profile == TEXT("urbana_quartiere") || S.Profile == TEXT("urbana_locale")));
		int32 k = 0;
		for (double D = From + 6.0 * 100.0; D <= (bFixtureLit ? -1.0 : To); D += 2800.0, ++k)
		{
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				if (!bBoulevard && (k + (Side > 0 ? 1 : 0)) % 2 == 0)
				{
					continue;
				}
				// The lamp's arm reaches over the road (+X of the model toward the road centre).
				Put(Lamp, At(D, Side, Curb), SegYaw + (Side > 0 ? 180.0 : 0.0) + 90.0, 1.25);
				++Lamps;
			}
		}
		// Trees every 10 m on boulevards, kept 3 m clear of the lamps.
		if (bBoulevard)
		{
			for (double D = From + 1100.0; D <= To; D += 1000.0)
			{
				if (FMath::Fmod(D - (From + 600.0), 2800.0) < 300.0 || FMath::Fmod(D - (From + 600.0), 2800.0) > 2500.0)
				{
					continue;
				}
				for (int32 Side = -1; Side <= 1; Side += 2)
				{
					Put(Trees[R.RandRange(0, 1)], At(D + R.FRandRange(-60.f, 60.f), Side, Mid), R.FRandRange(0.f, 360.f), R.FRandRange(0.85f, 1.15f));
					++TreeCount;
				}
			}
		}
		// A bin near each junction end. No standalone hydrants: Italian ones are underground or
		// wall-mounted (INFRA_DESIGN.md section 1).
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			if (R.FRand() < 0.6f) { Put(Bin, At(From + 150.0, Side, Curb + 30.0), SegYaw + 90.0, 1.0); }
			if (R.FRand() < 0.4f) { Put(Bin, At(To - 150.0, Side, Curb + 30.0), SegYaw + 90.0, 1.0); }
		}
	}

	int32 Count = 0;
	for (const TPair<FIntPoint, TArray<FInstance>>& Pair : Chunks)
	{
		FBox Bounds(ForceInit);
		for (const FInstance& I : Pair.Value)
		{
			Bounds += I.Transform.GetLocation();
		}
		AOCKitBuildingChunk* Actor = World->SpawnActor<AOCKitBuildingChunk>(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z), FRotator::ZeroRotator);
		if (!Actor)
		{
			continue;
		}
		for (const FInstance& I : Pair.Value)
		{
			UInstancedStaticMeshComponent* ISM = Actor->GetOrAddPiece(I.Mesh);
			// Lamps, trunks and bins are solid; leaves, ferns and pots are not.
			if (I.Mesh == Lamp || I.Mesh == Bin)
			{
				ISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
			const int32 Index = ISM->AddInstance(I.Transform, /*bWorldSpace*/ true);
			ISM->SetCustomData(Index, TArrayView<const float>(I.Data, AOCKitBuildingChunk::NumCustomData));
			++Count;
		}
		Actor->SetActorLabel(FString::Printf(TEXT("Street_Props_%d_%d"), Pair.Key.X, Pair.Key.Y));
		Actor->SetFolderPath(TEXT("Props"));
	}
	UE_LOG(LogOpenCity, Display, TEXT("BuildStreetProps: %d props (%d lamps, %d trees) in %d chunks"), Count, Lamps, TreeCount, Chunks.Num());
	return Count;
}


int32 UOCWorldBuilderLibrary::BuildStreetFixtures(UWorld* World, const FString& LayoutJsonPath, const FString& HeightmapPng,
	float SidewalkWidthCm, float ChunkSizeCm, int32 Seed)
{
	using namespace OCKit;
	FString Text, Error;
	TSharedPtr<FJsonObject> Root;
	OCWorld::FHeightField Height;
	FOCRoadGraph Roads;
	if (!World || !FFileHelper::LoadFileToString(Text, *LayoutJsonPath)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()
		|| !Height.Load(HeightmapPng, Root) || !Roads.LoadFromLayoutJson(Text, Error))
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildStreetFixtures: cannot read the layout or heightmap"));
		return 0;
	}
	auto Kit = [](const TCHAR* Name) { return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Environment/Meshes/Kit/SM_Kit_%s.SM_Kit_%s"), Name, Name)); };
	UStaticMesh* Lantern = Kit(TEXT("Prop_WallLantern"));
	UStaticMesh* LedPole = Kit(TEXT("Prop_LedPole"));
	UStaticMesh* SignZTL = Kit(TEXT("Sign_ZTL"));
	UStaticMesh* Sign30 = Kit(TEXT("Sign_Speed30"));
	UStaticMesh* Sign50 = Kit(TEXT("Sign_Speed50"));
	UStaticMesh* SignStop = Kit(TEXT("Sign_Stop"));
	UStaticMesh* SignGive = Kit(TEXT("Sign_GiveWay"));
	UStaticMesh* Mirror = Kit(TEXT("Prop_ConvexMirror"));
	UStaticMesh* Bin = Kit(TEXT("Prop_RecyclingBin"));
	UStaticMesh* Fountain = Kit(TEXT("Prop_Fountain"));
	UStaticMesh* BollardIron = Kit(TEXT("Prop_BollardIron"));
	UStaticMesh* Signal = Kit(TEXT("Prop_TrafficLight"));   // optional: older kits lack the Phase 5 pieces
	UStaticMesh* CableLamp = Kit(TEXT("Prop_CableLamp"));
	UStaticMesh* Cable = Kit(TEXT("Prop_Cable"));
	if (!Lantern || !LedPole || !SignZTL || !Bin)
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildStreetFixtures: fixtures missing (run scripts/run_commandlet.sh OCBuildArt -steps=kit)"));
		return 0;
	}
	const TArray<FOCRoadNode>& Nodes = Roads.GetNodes();
	const TArray<FOCRoadSegment>& Segs = Roads.GetSegments();
	TArray<double> Clear;
	Clear.Init(0.0, Nodes.Num());
	for (const FOCRoadSegment& S : Segs)
	{
		for (const int32 N : { S.A, S.B })
		{
			Clear[N] = FMath::Max(Clear[N], S.WidthCm * 0.5 + SidewalkWidthCm);
		}
	}
	auto HasWalk = [](const FOCRoadSegment& S) { return S.Kind == TEXT("avenue") || S.Kind == TEXT("street"); };
	// Fixtures reach (or face) along their local -Y: yaw that turns local +Y onto Dir.
	auto YawForLocalY = [](const FVector& Dir) { return FMath::RadiansToDegrees(FMath::Atan2(-Dir.X, Dir.Y)); };

	// Nothing may stand on a carriageway: a sign placed off its own road's edge can land on the
	// road it joins at a junction (STOP signs stood on the viale's zebra crossings), so every
	// fixture is checked against all roads around it, with 0.3 m of margin past the edge.
	TMap<FIntPoint, TArray<int32>> RoadCells;  // 50 m cells
	const double RoadCellCm = 5000.0;
	for (int32 s = 0; s < Segs.Num(); ++s)
	{
		const FVector& PA = Nodes[Segs[s].A].Location;
		const FVector& PB = Nodes[Segs[s].B].Location;
		for (int32 x = FMath::FloorToInt32(FMath::Min(PA.X, PB.X) / RoadCellCm) - 1; x <= FMath::FloorToInt32(FMath::Max(PA.X, PB.X) / RoadCellCm) + 1; ++x)
		{
			for (int32 y = FMath::FloorToInt32(FMath::Min(PA.Y, PB.Y) / RoadCellCm) - 1; y <= FMath::FloorToInt32(FMath::Max(PA.Y, PB.Y) / RoadCellCm) + 1; ++y)
			{
				RoadCells.FindOrAdd(FIntPoint(x, y)).Add(s);
			}
		}
	}
	auto OnCarriageway = [&](const FVector& At)
	{
		const TArray<int32>* Cell = RoadCells.Find(FIntPoint(FMath::FloorToInt32(At.X / RoadCellCm), FMath::FloorToInt32(At.Y / RoadCellCm)));
		for (const int32 s : Cell ? *Cell : TArray<int32>())
		{
			const FVector& PA = Nodes[Segs[s].A].Location;
			const FVector& PB = Nodes[Segs[s].B].Location;
			const FVector2D Foot = FMath::ClosestPointOnSegment2D(FVector2D(At), FVector2D(PA), FVector2D(PB));
			if (FVector2D::Distance(FVector2D(At), Foot) < Segs[s].WidthCm * 0.5 + 30.0)
			{
				return true;
			}
		}
		return false;
	};

	TMap<FIntPoint, TArray<FInstance>> Chunks;
	TSet<UStaticMesh*> Solid;
	FRandomStream R(Seed);
	int32 Count = 0, Skipped = 0;
	auto Put = [&](UStaticMesh* Mesh, const FVector& At, double YawDeg, double Lift = 0.0, const FLinearColor& Colour = FLinearColor::White, bool bOnGround = true)
	{
		if (!Mesh)
		{
			return;
		}
		if (bOnGround && OnCarriageway(At))
		{
			++Skipped;
			return;
		}
		FInstance I;
		I.Mesh = Mesh;
		const double Z = bOnGround ? Height.HeightCm(At.X, At.Y) + 27.0 + Lift : At.Z;
		I.Transform = FTransform(FRotator(0.0, YawDeg, 0.0), FVector(At.X, At.Y, Z));
		I.Data[0] = Colour.R;
		I.Data[1] = Colour.G;
		I.Data[2] = Colour.B;
		for (int32 k = 3; k < 6; ++k) { I.Data[k] = 1.f; }
		I.Data[7] = Mesh == Signal ? 0.f : 1.f;  // lamp glass lit at night (M_Master_KitGlass); signal lenses never
		if (Mesh == Signal)
		{
			I.Data[0] = 5.f;  // yellow push-button box: paint palette entry 5 (M_Master_Paint)
		}
		Chunks.FindOrAdd(FIntPoint(FMath::FloorToInt32(At.X / ChunkSizeCm), FMath::FloorToInt32(At.Y / ChunkSizeCm))).Add(I);
		++Count;
	};

	// A placed transform as is (cables hang over the road, so no ground or carriageway check).
	auto PutXf = [&](UStaticMesh* Mesh, const FTransform& Xf, float Lit = 0.f)
	{
		if (!Mesh)
		{
			return;
		}
		FInstance I;
		I.Mesh = Mesh;
		I.Transform = Xf;
		for (int32 k = 0; k < 6; ++k) { I.Data[k] = 1.f; }
		I.Data[7] = Lit;
		const FVector L = Xf.GetLocation();
		Chunks.FindOrAdd(FIntPoint(FMath::FloorToInt32(L.X / ChunkSizeCm), FMath::FloorToInt32(L.Y / ChunkSizeCm))).Add(I);
		++Count;
	};
	// A lamp hung on a cable across the street between facade points L and R (cm, on the facades):
	// the cable is anchored 6.5 m above the road and sags 0.3 m to the lamp in the middle.
	int32 CableLamps = 0;
	auto HangLamp = [&](const FVector& L, const FVector& R)
	{
		if (!CableLamp || !Cable)
		{
			return false;
		}
		const FVector Mid2 = (L + R) * 0.5;
		const double Road = Height.HeightCm(Mid2.X, Mid2.Y) + 27.0;
		const FVector A(L.X, L.Y, Road + 650.0), B(R.X, R.Y, Road + 650.0);
		const FVector M(Mid2.X, Mid2.Y, Road + 620.0);
		for (const FVector& From : { A, B })
		{
			const FVector D = M - From;
			const FRotator Rot = D.Rotation();  // pitch and yaw along the half span
			PutXf(Cable, FTransform(Rot, From, FVector(D.Size() / 100.0, 1.0, 1.0)));
		}
		PutXf(CableLamp, FTransform(FRotator::ZeroRotator, M - FVector(0, 0, 62.0)), 1.f);  // bowl lit at night
		++CableLamps;
		return true;
	};

	// Signs where traffic approaches node N along segment S: on the approaching driver's right,
	// Offset cm before the junction edge, facing the traffic. Without a pavement the post stands 1 m off the carriageway:
	// at 60 cm the plate (it faces the traffic, so it reaches back towards the road) stopped cars riding
	// a little right of their lane on 5.5 m alleys.
	auto SignAt = [&](UStaticMesh* Sign, const FOCRoadSegment& S0, int32 N0, double Offset)
	{
		// Clear[N0] + Offset back along the approach, on through plain bends where the segment is shorter:
		// capped at 45% of an 11 m link, a STOP sign stood on the inside corner of the T it guards and the
		// cars turning right hit it. Only a segment between two junctions still caps it.
		const FOCRoadSegment* S = &S0;
		int32 N = N0;
		double Back = Clear[N0] + Offset;
		for (int32 Hop = 0; Hop < 4; ++Hop)
		{
			const double L = FVector::Dist2D(Nodes[S->A].Location, Nodes[S->B].Location);
			const int32 Far = S->A == N ? S->B : S->A;
			const TArray<int32>& There = Roads.SegmentsAt(Far);
			if (There.Num() != 2)
			{
				Back = FMath::Min(Back, L * 0.45);   // another junction at the far end: keep off its corner too
				break;
			}
			if (Back <= L * 0.95)
			{
				break;
			}
			const FOCRoadSegment& Next = &Segs[There[0]] == S ? Segs[There[1]] : Segs[There[0]];
			Back = FMath::Max(Back - L, 100.0);
			N = Far;
			S = &Next;
		}
		const FVector A = Nodes[S->A].Location, B = Nodes[S->B].Location;
		const FVector Dir = (B - A).GetSafeNormal2D();
		const FVector Side(-Dir.Y, Dir.X, 0.0);
		const bool bAtB = N == S->B;
		const FVector Travel = bAtB ? Dir : -Dir;
		const FVector Right = bAtB ? Side : -Side;
		const FVector P = Nodes[N].Location - Travel * Back + Right * (S->WidthCm * 0.5 + (HasWalk(*S) ? SidewalkWidthCm * 0.6 : 100.0));
		Put(Sign, P, YawForLocalY(Travel), HasWalk(*S) ? 15.0 : 0.0);
	};

	for (const FOCRoadSegment& S : Segs)
	{
		if (!S.Bridge.IsNone() || S.bPedestrian)
		{
			continue;
		}
		const FVector A = Nodes[S.A].Location, B = Nodes[S.B].Location;
		const FVector Dir = (B - A).GetSafeNormal2D();
		const FVector Side(-Dir.Y, Dir.X, 0.0);
		const double Len = FVector::Dist2D(A, B);
		const double From = Clear[S.A] + 300.0, To = Len - Clear[S.B] - 300.0;
		if (To - From < 300.0)
		{
			continue;
		}
		const double Half = S.WidthCm * 0.5;
		const double Walk = HasWalk(S) ? SidewalkWidthCm : 0.0;
		auto At = [&](double D, double Sign, double Off) { return A + Dir * D + Side * (Sign * Off); };

		// Centro storico: lanterns on the facades, alternating sides every 18 m, 4.2 m up; every
		// third one is a lamp hung on a cable across the street instead.
		if (S.Profile == TEXT("centro_storico"))
		{
			int32 k = 0;
			for (double D = From; D <= To; D += 1800.0, ++k)
			{
				if (k % 3 == 1 && HangLamp(At(D, -1.0, Half + Walk + 30.0), At(D, 1.0, Half + Walk + 30.0)))
				{
					continue;
				}
				const double SideSign = (k % 2) ? 1.0 : -1.0;
				const FVector Wall = At(D, SideSign, Half + Walk + 30.0);
				Put(Lantern, Wall, YawForLocalY(Side * SideSign), 420.0);
			}
		}
		// Post-war streets and the coast road: galvanised LED poles at the kerb, arm over the road.
		const bool bPostwar = S.Era == TEXT("postwar") && (S.Profile == TEXT("urbana_quartiere") || S.Profile == TEXT("urbana_locale"));
		if (bPostwar || S.Profile == TEXT("costiera"))
		{
			int32 k = 0;
			for (double D = From + 500.0; D <= To; D += (bPostwar ? 3200.0 : 4500.0), ++k)
			{
				const double SideSign = (k % 2) ? 1.0 : -1.0;
				Put(LedPole, At(D, SideSign, Half + (Walk > 0.0 ? 45.0 : 80.0)), YawForLocalY(Side * SideSign), Walk > 0.0 ? 15.0 : 0.0);
			}
		}
		// Recycling bins: groups of five (glass, plastic, paper, organic, unsorted) every ~150 m on
		// post-war pavements, side by side along the kerb.
		if (bPostwar && Walk > 0.0)
		{
			for (double D = From + R.FRandRange(600.f, 4000.f); D + 800.0 <= To; D += 15000.0)
			{
				const double SideSign = R.FRand() < 0.5 ? 1.0 : -1.0;
				for (int32 b = 0; b < 5; ++b)
				{
					Put(Bin, At(D + b * 150.0, SideSign, Half + 95.0), FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), 15.0, FLinearColor(float(b), 0.f, 0.f));   // paint palette entry b (M_Master_Paint)
				}
			}
		}
	}

	// Junction fixtures.
	TMap<int32, FName> JunctionType;
	for (const FOCRoadJunction& J : Roads.GetJunctions())
	{
		JunctionType.Add(J.Node, J.Type);
	}
	int32 CentroCorners = 0, Signals = 0;
	// The through road at a junction: the most nearly straight pair of arms of the highest rank, then
	// the widest (as the layout generator's junction arms, Source/OpenCityEditor/Private/Layout/OCMareviaPlace.cpp); every other arm is minor.
	auto Rank = [](const FOCRoadSegment& S)
	{
		return S.Kind == TEXT("pier") ? 0 : S.Kind == TEXT("alley") ? 1 : S.Kind == TEXT("street") ? 2 : 3;
	};
	auto AwayDir = [&](int32 N, const FOCRoadSegment& S) { return ((S.A == N ? Nodes[S.B].Location : Nodes[S.A].Location) - Nodes[N].Location).GetSafeNormal2D(); };
	auto MinorArms = [&](int32 N, const TArray<int32>& Arms)
	{
		int32 BestI = INDEX_NONE, BestK = INDEX_NONE;
		double BestKey[3] = { -1.0, -1.0, -1.0 };
		for (int32 i = 0; i < Arms.Num(); ++i)
		{
			for (int32 k = i + 1; k < Arms.Num(); ++k)
			{
				const FOCRoadSegment& Si = Segs[Arms[i]];
				const FOCRoadSegment& Sk = Segs[Arms[k]];
				const double Straight = -FVector::DotProduct(AwayDir(N, Si), AwayDir(N, Sk));
				const double Key[3] = { double(FMath::Min(Rank(Si), Rank(Sk))), FMath::Min(Si.WidthCm, Sk.WidthCm), Straight };
				const bool bBetter = Key[0] != BestKey[0] ? Key[0] > BestKey[0] : Key[1] != BestKey[1] ? Key[1] > BestKey[1] : Key[2] > BestKey[2];
				if (Straight > 0.5 && bBetter)
				{
					BestI = i; BestK = k;
					FMemory::Memcpy(BestKey, Key, sizeof(Key));
				}
			}
		}
		if (BestI == INDEX_NONE)  // a Y: the two widest arms are the through road
		{
			TArray<int32> Order = Arms;
			Order.Sort([&](int32 a, int32 b) { return Rank(Segs[a]) != Rank(Segs[b]) ? Rank(Segs[a]) > Rank(Segs[b]) : Segs[a].WidthCm > Segs[b].WidthCm; });
			BestI = Arms.IndexOfByKey(Order[0]);
			BestK = Arms.IndexOfByKey(Order[1]);
		}
		TArray<int32> Minor;
		for (int32 i = 0; i < Arms.Num(); ++i)
		{
			if (i != BestI && i != BestK && !Segs[Arms[i]].bPedestrian && Segs[Arms[i]].Bridge.IsNone())
			{
				Minor.Add(Arms[i]);
			}
		}
		return Minor;
	};
	for (int32 N = 0; N < Nodes.Num(); ++N)
	{
		const TArray<int32>& At = Roads.SegmentsAt(N);
		const FName* Type = JunctionType.Find(N);
		if (!Type || At.IsEmpty())
		{
			continue;
		}
		if (*Type == TEXT("ztl_gate"))
		{
			// Entering the ZTL: the panel and a Zona 30 sign on the outside approach.
			for (const int32 si : At)
			{
				if (!Segs[si].bZTL && !Segs[si].bPedestrian && Segs[si].Bridge.IsNone())
				{
					SignAt(SignZTL, Segs[si], N, 300.0);
					SignAt(Sign30, Segs[si], N, 700.0);
				}
			}
		}
		else if (*Type == TEXT("signalized") && Signal)
		{
			// A signal head on the right of every approach, just before the junction, facing the traffic
			// (unlit: signal states come with traffic, INFRA_DESIGN Phase 11).
			for (const int32 si : At)
			{
				if (!Segs[si].bPedestrian && Segs[si].Bridge.IsNone())
				{
					SignAt(Signal, Segs[si], N, 100.0);
					++Signals;
				}
			}
		}
		else if ((*Type == TEXT("stop") || *Type == TEXT("blind_t") || *Type == TEXT("priority")) && At.Num() >= 3)
		{
			// The layout's sight-triangle check (D.M. 19/4/2006 4.6) picked the control: give way where
			// the 12 s triangle is clear, STOP where only the 6 s one is, STOP and a convex mirror across
			// the through road where neither is.
			for (const int32 Minor : MinorArms(N, At))
			{
				const FOCRoadSegment& M = Segs[Minor];
				SignAt(*Type == TEXT("priority") ? SignGive : SignStop, M, N, 150.0);
				if (*Type == TEXT("blind_t"))
				{
					const FVector Away = AwayDir(N, M);
					Put(Mirror, Nodes[N].Location - Away * (Clear[N] + 60.0), YawForLocalY(-Away), 15.0);
				}
			}
		}
		// Centro storico corners: a drinking fountain at the piazza and every 7th centro junction,
		// cast-iron bollards guarding the corners of the pavement.
		bool bCentro = false;
		for (const int32 si : At) { bCentro |= Segs[si].Profile == TEXT("centro_storico"); }
		if (bCentro && At.Num() >= 3)
		{
			const FVector C = Nodes[N].Location;
			const FVector D0 = ((Segs[At[0]].A == N ? Nodes[Segs[At[0]].B].Location : Nodes[Segs[At[0]].A].Location) - C).GetSafeNormal2D();
			const FVector Corner = C + (D0 + FVector(-D0.Y, D0.X, 0.0)) * (Clear[N] * 0.72);
			if (*Type == TEXT("piazza") || ++CentroCorners % 7 == 0)
			{
				Put(Fountain, Corner, YawForLocalY(-(Corner - C).GetSafeNormal2D()), 15.0);
			}
			for (int32 b = -1; b <= 1; ++b)
			{
				Put(BollardIron, Corner + D0 * (b * 120.0 + 200.0), 0.0, 15.0);
			}
		}
	}
	// Urban entries: where an extra-urban road becomes urban, a 50 sign on the urban side.
	for (int32 N = 0; N < Nodes.Num(); ++N)
	{
		const TArray<int32>& At = Roads.SegmentsAt(N);
		bool bUrban = false, bRural = false;
		for (const int32 si : At)
		{
			if (const FOCRoadProfile* P = Roads.FindProfile(Segs[si].Profile))
			{
				(P->bUrban ? bUrban : bRural) = true;
			}
		}
		if (bUrban && bRural)
		{
			for (const int32 si : At)
			{
				const FOCRoadProfile* P = Roads.FindProfile(Segs[si].Profile);
				if (P && !P->bUrban && Segs[si].Bridge.IsNone())
				{
					// Traffic arriving from the rural road meets the sign as it enters: place it on the
					// rural segment just before the node, facing that traffic.
					SignAt(Sign50, Segs[si], N, 400.0);
				}
			}
		}
	}

	for (const TPair<FIntPoint, TArray<FInstance>>& Pair : Chunks)
	{
		FBox Bounds(ForceInit);
		for (const FInstance& I : Pair.Value)
		{
			Bounds += I.Transform.GetLocation();
		}
		AOCKitBuildingChunk* Actor = World->SpawnActor<AOCKitBuildingChunk>(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z), FRotator::ZeroRotator);
		if (!Actor)
		{
			continue;
		}
		for (const FInstance& I : Pair.Value)
		{
			UInstancedStaticMeshComponent* ISM = Actor->GetOrAddPiece(I.Mesh);
			if (I.Mesh != Lantern && I.Mesh != CableLamp && I.Mesh != Cable)  // posts, poles, bins and fountains are solid; lamps hang out of reach
			{
				ISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
			const int32 Index = ISM->AddInstance(I.Transform, /*bWorldSpace*/ true);
			ISM->SetCustomData(Index, TArrayView<const float>(I.Data, AOCKitBuildingChunk::NumCustomData));
		}
		Actor->SetActorLabel(FString::Printf(TEXT("Street_Fixtures_%d_%d"), Pair.Key.X, Pair.Key.Y));
		Actor->SetFolderPath(TEXT("Props"));
	}
	UE_LOG(LogOpenCity, Display, TEXT("BuildStreetFixtures: %d fixtures in %d chunks (%d cable lamps, %d signal heads), %d skipped for standing on a carriageway"),
		Count, Chunks.Num(), CableLamps, Signals, Skipped);
	return Count;
}


int32 UOCWorldBuilderLibrary::BuildRetainingWalls(UWorld* World, const FString& LayoutJsonPath, float ChunkSizeCm)
{
	using namespace OCKit;
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!World || !FFileHelper::LoadFileToString(Text, *LayoutJsonPath)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildRetainingWalls: cannot read %s"), *LayoutJsonPath);
		return 0;
	}
	const TArray<TSharedPtr<FJsonValue>>* Runs = nullptr;
	if (!Root->TryGetArrayField(TEXT("retaining_walls"), Runs))
	{
		return 0;
	}
	auto Kit = [](const TCHAR* Name) { return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Environment/Meshes/Kit/SM_Kit_%s.SM_Kit_%s"), Name, Name)); };
	UStaticMesh* Panels[2] = { Kit(TEXT("RetainingWall_Stone_400")), Kit(TEXT("RetainingWall_Concrete_400")) };
	UStaticMesh* Coping = Kit(TEXT("WallCoping_400"));
	if (!Panels[0] || !Panels[1] || !Coping)
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildRetainingWalls: wall pieces missing (run scripts/run_commandlet.sh OCBuildArt -steps=kit)"));
		return 0;
	}
	constexpr double PanelW = 400.0, PanelH = 200.0, Thick = 50.0;
	TMap<FIntPoint, TArray<FInstance>> Chunks;
	auto Put = [&](UStaticMesh* Mesh, const FVector& At, const FRotator& Rot, double ScaleX)
	{
		FInstance I;
		I.Mesh = Mesh;
		I.Transform = FTransform(Rot, At, FVector(ScaleX, 1.0, 1.0));
		for (int32 k = 0; k < 6; ++k) { I.Data[k] = 1.f; }
		I.Data[6] = 0.5f;
		Chunks.FindOrAdd(FIntPoint(FMath::FloorToInt32(At.X / ChunkSizeCm), FMath::FloorToInt32(At.Y / ChunkSizeCm))).Add(I);
	};
	int32 Count = 0, Columns = 0;
	for (const TSharedPtr<FJsonValue>& V : *Runs)
	{
		const TSharedPtr<FJsonObject> Run = V->AsObject();
		UStaticMesh* Panel = Run->GetStringField(TEXT("material")) == TEXT("Stone") ? Panels[0] : Panels[1];
		const bool bFill = Run->GetStringField(TEXT("type")) == TEXT("fill");
		const TArray<TSharedPtr<FJsonValue>>& Pts = Run->GetArrayField(TEXT("points"));
		for (int32 i = 0; i + 1 < Pts.Num(); ++i)
		{
			const TArray<TSharedPtr<FJsonValue>>& P0 = Pts[i]->AsArray();
			const TArray<TSharedPtr<FJsonValue>>& P1 = Pts[i + 1]->AsArray();
			FVector A = FOCRoadGraph::LayoutToWorld(P0[0]->AsNumber(), P0[1]->AsNumber(), 0.0);
			FVector B = FOCRoadGraph::LayoutToWorld(P1[0]->AsNumber(), P1[1]->AsNumber(), 0.0);
			const double Top = FMath::Max(P0[2]->AsNumber(), P1[2]->AsNumber()) * 100.0;
			const double Bottom = FMath::Min(P0[3]->AsNumber(), P1[3]->AsNumber()) * 100.0;
			// The points lie on the wall's road-side line; "outward" (layout x, y) points away from the
			// road. Cut walls face the road, fill walls face away; a panel's face is at local Y = 0
			// looking along -Y with its thickness behind, so the run direction is chosen to match.
			FVector Dir = (B - A).GetSafeNormal2D();
			const FVector Left(-Dir.Y, Dir.X, 0.0);  // local +Y for yaw = Dir
			FVector Outward = FVector::ZeroVector;
			const TArray<TSharedPtr<FJsonValue>>* Out = nullptr;
			if (Run->TryGetArrayField(TEXT("outward"), Out) && Out->Num() >= 2)
			{
				Outward = FVector((*Out)[0]->AsNumber(), -(*Out)[1]->AsNumber(), 0.0).GetSafeNormal();
			}
			const FVector Facing = bFill ? Outward : -Outward;
			if (FVector::DotProduct(Left, -Facing) < 0.0)
			{
				Swap(A, B);
				Dir = -Dir;
			}
			const FRotator Rot(0.0, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), 0.0);
			const double ScaleX = FVector::Dist2D(A, B) / PanelW;
			// Fill walls: the points are the wall's road-side face, the outer face is a thickness further out.
			const FVector FaceLine = bFill ? A + Outward * Thick : A;
			const int32 Rows = FMath::Clamp(FMath::CeilToInt32((Top - Bottom) / PanelH), 1, 24);
			for (int32 r = 0; r < Rows; ++r)
			{
				Put(Panel, FVector(FaceLine.X, FaceLine.Y, Top - PanelH * (r + 1)), Rot, ScaleX);
				++Count;
			}
			Put(Coping, FVector(FaceLine.X, FaceLine.Y, Top), Rot, ScaleX);
			++Columns;
		}
	}
	for (const TPair<FIntPoint, TArray<FInstance>>& Pair : Chunks)
	{
		FBox Bounds(ForceInit);
		for (const FInstance& I : Pair.Value)
		{
			Bounds += I.Transform.GetLocation();
		}
		AOCKitBuildingChunk* Actor = World->SpawnActor<AOCKitBuildingChunk>(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z), FRotator::ZeroRotator);
		if (!Actor)
		{
			continue;
		}
		for (const FInstance& I : Pair.Value)
		{
			UInstancedStaticMeshComponent* ISM = Actor->GetOrAddPiece(I.Mesh);
			ISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);  // walls and parapets stop cars
			const int32 Index = ISM->AddInstance(I.Transform, /*bWorldSpace*/ true);
			ISM->SetCustomData(Index, TArrayView<const float>(I.Data, AOCKitBuildingChunk::NumCustomData));
		}
		Actor->SetActorLabel(FString::Printf(TEXT("Retaining_Walls_%d_%d"), Pair.Key.X, Pair.Key.Y));
		Actor->SetFolderPath(TEXT("Walls"));
	}
	UE_LOG(LogOpenCity, Display, TEXT("BuildRetainingWalls: %d panels in %d columns, %d chunks"), Count, Columns, Chunks.Num());
	return Count;
}


int32 UOCWorldBuilderLibrary::BuildRoadDecals(UWorld* World, const FString& LayoutJsonPath, const FString& HeightmapPng,
	float SidewalkWidthCm, float ChunkSizeCm, int32 Seed)
{
	FString Text;
	TSharedPtr<FJsonObject> Root;
	OCWorld::FHeightField Height;
	FOCRoadGraph Roads;
	FString Error;
	if (!World || !FFileHelper::LoadFileToString(Text, *LayoutJsonPath)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()
		|| !Height.Load(HeightmapPng, Root) || !Roads.LoadFromLayoutJson(Text, Error))
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildRoadDecals: cannot read the layout or heightmap"));
		return 0;
	}
	auto Mat = [](const TCHAR* Id) { return LoadObject<UMaterialInterface>(nullptr, *FString::Printf(TEXT("/Game/Environment/Materials/Decals/MI_Decal_%s.MI_Decal_%s"), Id, Id)); };
	UMaterialInterface* Manholes[2] = { Mat(TEXT("ManholeCover001")), Mat(TEXT("ManholeCover004")) };
	UMaterialInterface* Tyres = Mat(TEXT("TireTracks001"));
	UMaterialInterface* Damage = Mat(TEXT("AsphaltDamage001"));
	UMaterialInterface* Gum = Mat(TEXT("ChewingGum001"));
	UMaterialInterface* Gully = Mat(TEXT("ManholeCover009"));        // optional (INFRA_DESIGN Phase 4)
	UMaterialInterface* CobbleManhole = Mat(TEXT("ManholeCover010"));
	if (!Manholes[0])
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildRoadDecals: decals not imported (OCArtMaterials.cpp (decals))"));
		return 0;
	}

	struct FDecal { UMaterialInterface* Mat; FTransform T; FVector Half; };
	TMap<FIntPoint, TArray<FDecal>> Chunks;
	FRandomStream R(Seed);
	const TArray<FOCRoadNode>& Nodes = Roads.GetNodes();
	auto Put = [&](UMaterialInterface* M, const FVector& At, double YawDeg, const FVector& Half, double AboveCm)
	{
		if (!M)
		{
			return;
		}
		// Projection along -Z: the decal's X axis points down.
		const FVector P(At.X, At.Y, Height.HeightCm(At.X, At.Y) + AboveCm);
		Chunks.FindOrAdd(FIntPoint(FMath::FloorToInt32(P.X / ChunkSizeCm), FMath::FloorToInt32(P.Y / ChunkSizeCm)))
			.Add({ M, FTransform(FRotator(-90.0, YawDeg, 0.0), P), Half });
	};

	for (const FOCRoadSegment& S : Roads.GetSegments())
	{
		const bool bCity = S.Kind == TEXT("avenue") || S.Kind == TEXT("street") || S.Kind == TEXT("coast") || S.Kind == TEXT("alley");
		if (!bCity || !S.Bridge.IsNone())
		{
			continue;
		}
		const FVector A = Nodes[S.A].Location, B = Nodes[S.B].Location;
		const double Len = FVector::Dist2D(A, B);
		if (Len < 2500.0)
		{
			continue;
		}
		const FVector Dir = FVector(B - A).GetSafeNormal2D();
		const FVector Right(-Dir.Y, Dir.X, 0.0);
		const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
		const double Lane = S.WidthCm / (4.0 * FMath::Max(1, S.LanesPerDirection));
		auto At = [&](double D, double Lateral) { return A + Dir * D + Right * Lateral; };
		// Stone paving (centro storico, piazze) gets covers set in cobbles and no asphalt damage;
		// worn roads (segment wear 0..1) get more potholes and braking marks.
		const bool bStone = S.Paving == TEXT("sampietrini") || S.Paving == TEXT("porfido") || S.Paving == TEXT("lastricato");
		const double WearK = 0.4 + 1.2 * FMath::Clamp(S.Wear, 0.f, 1.f);

		for (double D = R.FRandRange(1500.f, 4000.f); D < Len - 1200.0; D += R.FRandRange(6000.f, 8000.f))
		{
			UMaterialInterface* Cover = bStone && CobbleManhole ? CobbleManhole : Manholes[R.RandRange(0, 1)];
			Put(Cover, At(D, (R.FRand() < 0.5 ? -1.0 : 1.0) * Lane), R.FRandRange(0.f, 360.f), FVector(30.0, 45.0, 45.0), 15.0);
		}
		// Gullies in the gutter by the kerb, about every 22 m, alternating sides (streets with kerbs).
		if (Gully && (S.Kind == TEXT("avenue") || S.Kind == TEXT("street")))
		{
			int32 k = 0;
			for (double D = R.FRandRange(600.f, 1600.f); D < Len - 600.0; D += 2200.0, ++k)
			{
				Put(Gully, At(D, ((k % 2) ? 1.0 : -1.0) * (S.WidthCm * 0.5 - 35.0)), Yaw, FVector(30.0, 28.0, 28.0), 15.0);
			}
		}
		for (int32 End = 0; End < 2; ++End)
		{
			if (!bStone && R.FRand() < 0.15f + 0.45f * float(WearK - 0.4))  // braking marks on the approach to a junction
			{
				const double D = End == 0 ? 1100.0 : Len - 1100.0;
				const double Side = End == 0 ? -1.0 : 1.0;  // the lane heading into that junction
				// The decal's long (Z) axis along the lane: at Yaw + 90 the 7.6 m marks lay across the
				// road like a pale band before every stop line.
				Put(Tyres, At(D, Side * Lane), Yaw, FVector(30.0, 100.0, 380.0), 15.0);
			}
		}
		for (double D = R.FRandRange(2000.f, 9000.f); !bStone && D < Len - 1000.0; D += R.FRandRange(12000.f, 18000.f) / WearK)
		{
			Put(Damage, At(D, R.FRandRange(-0.4f, 0.4f) * S.WidthCm), R.FRandRange(0.f, 360.f), FVector(30.0, 110.0, 110.0), 15.0);
		}
		if (S.Kind != TEXT("alley"))
		{
			for (double D = R.FRandRange(500.f, 3000.f); D < Len - 500.0; D += R.FRandRange(2500.f, 4500.f))
			{
				const double Side = R.FRand() < 0.5 ? -1.0 : 1.0;
				Put(Gum, At(D, Side * (S.WidthCm * 0.5 + R.FRandRange(0.2f, 0.8f) * SidewalkWidthCm)), R.FRandRange(0.f, 360.f), FVector(30.0, 50.0, 50.0), 27.0);
			}
		}
	}

	int32 Count = 0;
	for (const TPair<FIntPoint, TArray<FDecal>>& Pair : Chunks)
	{
		FBox Bounds(ForceInit);
		for (const FDecal& D : Pair.Value)
		{
			Bounds += D.T.GetLocation();
		}
		AOCKitBuildingChunk* Actor = World->SpawnActor<AOCKitBuildingChunk>(FVector(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z), FRotator::ZeroRotator);
		if (!Actor)
		{
			continue;
		}
		for (const FDecal& D : Pair.Value)
		{
			Actor->AddDecal(D.Mat, D.T, D.Half);
			++Count;
		}
		Actor->SetActorLabel(FString::Printf(TEXT("Road_Decals_%d_%d"), Pair.Key.X, Pair.Key.Y));
		Actor->SetFolderPath(TEXT("Decals"));
	}
	UE_LOG(LogOpenCity, Display, TEXT("BuildRoadDecals: %d decals in %d chunks"), Count, Chunks.Num());
	return Count;
}
