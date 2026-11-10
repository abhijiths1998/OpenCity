// Assembles Marevia's buildings from the modular kit (Tools/blender/KIT_SPEC.md):
// UOCWorldBuilderLibrary::BuildKitBuildings.
//
// Each building record in the layout (footprint on the 2 m grid, storeys of 3.2 m, district style)
// becomes: a stone plinth, a ground floor (shopfronts, a street door, barred windows) on the sides
// that face a street, upper floors of shuttered windows and balconies, quoins on every corner,
// a cornice, and either a tiled hip roof or a flat roof with a parapet and rooftop clutter.
// Variation (wall and shutter colours, shop and balcony mix, lit windows, wear) is seeded per
// building, so neighbours differ but every rebuild is identical.

#include "OCWorldBuilderLibrary.h"

#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "OCHeightField.h"
#include "OCLog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/OCKitBuildingChunk.h"
#include "World/OCRoadGraph.h"

namespace OCKit
{
	constexpr double StoreyCm = 320.0;
	constexpr double StreetProbeCm = 500.0;   // a facade faces a street if a road edge is this close
	constexpr double StreetGapCm = 700.0;

	/** District look: what the street floors, upper floors and roofs are made of. */
	struct FStyle
	{
		double PitchedRoof = 0.5;   // chance of a tiled hip roof (else flat roof + parapet)
		double Shop = 0.5;          // chance a street-side 4 m ground bay is a shopfront
		double Balcony = 0.25;      // chance an upper street-side 4 m bay is a balcony
		double Shutters = 0.8;      // chance windows have shutters (per building)
		double SideWindows = 0.5;   // chance a side/back bay has a window
		int32 Palette = 0;          // 0 warm Mediterranean, 1 pastel seafront, 2 industrial, 3 pale stone
	};

	FStyle StyleFor(const FString& Name)
	{
		FStyle S;
		if (Name == TEXT("oldtown")) { S.PitchedRoof = 0.7; S.Shop = 0.45; S.Balcony = 0.35; S.Shutters = 0.85; }
		else if (Name == TEXT("grid90")) { S.PitchedRoof = 0.35; S.Shop = 0.55; S.Balcony = 0.3; S.Shutters = 0.7; }
		else if (Name == TEXT("grid120")) { S.PitchedRoof = 0.15; S.Shop = 0.3; S.Balcony = 0.05; S.Shutters = 0.3; S.SideWindows = 0.3; S.Palette = 2; }
		else if (Name == TEXT("beach")) { S.PitchedRoof = 0.1; S.Shop = 0.7; S.Balcony = 0.6; S.Shutters = 0.4; S.Palette = 1; }
		else if (Name == TEXT("village")) { S.PitchedRoof = 0.8; S.Shop = 0.2; S.Balcony = 0.2; S.Shutters = 0.9; }
		else if (Name == TEXT("terminal")) { S.PitchedRoof = 0.0; S.Shop = 0.2; S.Balcony = 0.0; S.Shutters = 0.0; S.SideWindows = 0.6; S.Palette = 2; }
		else if (Name == TEXT("hills")) { S.PitchedRoof = 0.9; S.Shop = 0.0; S.Balcony = 0.35; S.Shutters = 0.9; S.Palette = 3; }
		return S;
	}

	// Multipliers on the plaster and painted-wood textures (ART_BIBLE.md palette).
	const FLinearColor WallPalettes[4][6] = {
		{ {1.00f, 0.90f, 0.74f}, {0.96f, 0.76f, 0.48f}, {0.90f, 0.58f, 0.44f}, {1.00f, 0.86f, 0.62f}, {0.98f, 0.95f, 0.88f}, {0.94f, 0.72f, 0.60f} },
		{ {1.00f, 0.82f, 0.76f}, {0.86f, 0.92f, 0.90f}, {1.00f, 0.95f, 0.80f}, {0.84f, 0.88f, 0.96f}, {1.00f, 1.00f, 0.97f}, {0.98f, 0.86f, 0.66f} },
		{ {0.80f, 0.78f, 0.74f}, {0.86f, 0.80f, 0.70f}, {0.72f, 0.70f, 0.68f}, {0.88f, 0.70f, 0.56f}, {0.78f, 0.76f, 0.70f}, {0.82f, 0.74f, 0.64f} },
		{ {1.00f, 0.95f, 0.86f}, {0.95f, 0.88f, 0.74f}, {0.98f, 0.92f, 0.82f}, {0.92f, 0.80f, 0.64f}, {1.00f, 0.98f, 0.93f}, {0.90f, 0.84f, 0.74f} },
	};
	const FLinearColor WoodColors[7] = {
		{0.30f, 0.52f, 0.36f}, {0.30f, 0.46f, 0.68f}, {0.26f, 0.52f, 0.52f}, {0.52f, 0.36f, 0.24f},
		{0.55f, 0.60f, 0.64f}, {0.92f, 0.92f, 0.88f}, {0.62f, 0.24f, 0.20f},
	};

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

	/** Bay widths (cm) for a facade of Length cm: 4 m bays, plus one 2 m bay if needed. */
	TArray<double> Bays(double Length, int32 TwoMetreAt)
	{
		TArray<double> Out;
		const int32 Fours = FMath::FloorToInt32(Length / 400.0 + 0.01);
		const bool bTwo = FMath::Abs(Length - Fours * 400.0) > 100.0;
		for (int32 i = 0; i < Fours; ++i)
		{
			Out.Add(400.0);
		}
		if (bTwo)
		{
			Out.Insert(200.0, FMath::Clamp(TwoMetreAt, 0, Out.Num()));
		}
		return Out;
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

	static const TCHAR* PieceNames[] = {
		TEXT("Wall_200"), TEXT("Wall_400"), TEXT("Window_200"), TEXT("Window_400"), TEXT("WindowPlain_200"), TEXT("WindowPlain_400"),
		TEXT("Balcony_400"), TEXT("Shopfront_400"), TEXT("Door_200"), TEXT("GroundWindow_400"), TEXT("Corner_320"),
		TEXT("Plinth_200"), TEXT("Plinth_400"), TEXT("Cornice_200"), TEXT("Cornice_400"), TEXT("Parapet_200"), TEXT("Parapet_400"),
		TEXT("RoofHip"), TEXT("RoofFlat"), TEXT("Prop_WaterTank"), TEXT("Prop_ACUnit"), TEXT("Prop_Dish"), TEXT("Prop_Chimney"),
		TEXT("Prop_RoofHut"), TEXT("Drainpipe_320"), TEXT("CorniceCorner") };
	FPieces Pieces;
	for (const TCHAR* Name : PieceNames)
	{
		const FString Path = FString::Printf(TEXT("%s/SM_Kit_%s.SM_Kit_%s"), *KitFolder, Name, Name);
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path))
		{
			Pieces.Meshes.Add(FName(Name), Mesh);
		}
	}
	if (!Pieces.Meshes.Contains(FName(TEXT("Wall_400"))))
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildKitBuildings: no kit in %s (run Content/Python/oc_import_kit.py)"), *KitFolder);
		return 0;
	}

	// Optional doorstep and shopfront clutter (Content/Python/oc_import_props.py).
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

	TMap<FIntPoint, TArray<FInstance>> Chunks;
	int32 Buildings = 0;
	const TArray<TSharedPtr<FJsonValue>>& List = Root->GetArrayField(TEXT("buildings"));
	for (int32 BuildingIndex = 0; BuildingIndex < List.Num(); ++BuildingIndex)
	{
		const TSharedPtr<FJsonObject>& Rec = List[BuildingIndex]->AsObject();
		const TArray<TSharedPtr<FJsonValue>>& Pos = Rec->GetArrayField(TEXT("pos"));
		const TArray<TSharedPtr<FJsonValue>>& Size = Rec->GetArrayField(TEXT("size"));
		const int32 StyleIndex = static_cast<int32>(Rec->GetNumberField(TEXT("style")));
		const FStyle Style = StyleFor(StyleNames.IsValidIndex(StyleIndex) ? StyleNames[StyleIndex] : FString());
		double StoreysD = 0.0;
		const int32 Storeys = Rec->TryGetNumberField(TEXT("storeys"), StoreysD) ? FMath::Max(1, static_cast<int32>(StoreysD))
			: FMath::Max(1, FMath::RoundToInt32((Rec->GetNumberField(TEXT("top")) - Rec->GetNumberField(TEXT("base")) - 1.0) / 3.2));
		FRandomStream R(Seed * 7919 + BuildingIndex * 104729 + 17);

		// Building frame in world space (layout: x east, y north, yaw counter-clockwise).
		const double YawRad = FMath::DegreesToRadians(Rec->GetNumberField(TEXT("yaw")));
		// Layout u = (cos, sin) and v = (-sin, cos) in x-east/y-north; Unreal flips y.
		const FVector U(FMath::Cos(YawRad), -FMath::Sin(YawRad), 0.0);   // along the width
		const FVector Vw(-FMath::Sin(YawRad), -FMath::Cos(YawRad), 0.0); // along the depth
		const FVector C = FOCRoadGraph::LayoutToWorld(Pos[0]->AsNumber(), Pos[1]->AsNumber(), 0.0);
		const double W = FMath::Max(400.0, FMath::RoundToDouble(Size[0]->AsNumber() / 2.0) * 200.0);
		const double D = FMath::Max(400.0, FMath::RoundToDouble(Size[1]->AsNumber() / 2.0) * 200.0);

		// The four facades: outward normal and length; which ones face a street.
		struct FFacade { FVector Out; double Length; double Half; bool bStreet = false; FVector Mid; };
		FFacade F[4] = { { -Vw, W, D * 0.5 }, { U, D, W * 0.5 }, { Vw, W, D * 0.5 }, { -U, D, W * 0.5 } };
		int32 StreetCount = 0;
		for (FFacade& Fa : F)
		{
			Fa.Mid = C + Fa.Out * Fa.Half;
			Fa.bStreet = RoadIndex.EdgeDistance(FVector2D(Fa.Mid + Fa.Out * StreetProbeCm)) < StreetGapCm;
			StreetCount += Fa.bStreet ? 1 : 0;
		}
		if (StreetCount == 0)
		{
			// Not near any road (rare): treat the facade closest to a road as the front.
			int32 Best = 0;
			double BestD = TNumericLimits<double>::Max();
			for (int32 i = 0; i < 4; ++i)
			{
				const double Dist = RoadIndex.EdgeDistance(FVector2D(F[i].Mid + F[i].Out * StreetProbeCm));
				if (Dist < BestD) { BestD = Dist; Best = i; }
			}
			F[Best].bStreet = true;
		}

		// Ground floor level: the street side's ground, never burying the uphill side by > 1.2 m.
		double Z0 = TNumericLimits<double>::Max(), MaxCorner = -TNumericLimits<double>::Max();
		for (const FFacade& Fa : F)
		{
			if (Fa.bStreet)
			{
				Z0 = FMath::Min(Z0, Height.HeightCm(Fa.Mid.X, Fa.Mid.Y));
			}
		}
		for (int32 sx = -1; sx <= 1; sx += 2)
		{
			for (int32 sy = -1; sy <= 1; sy += 2)
			{
				const FVector Corner = C + U * (sx * W * 0.5) + Vw * (sy * D * 0.5);
				MaxCorner = FMath::Max(MaxCorner, Height.HeightCm(Corner.X, Corner.Y));
			}
		}
		Z0 = FMath::Max(Z0, MaxCorner - 120.0) + 8.0;

		// Per-building look.
		float Data[AOCKitBuildingChunk::NumCustomData];
		const FLinearColor WallTint = WallPalettes[Style.Palette][R.RandRange(0, 5)] * R.FRandRange(0.93f, 1.05f);
		const FLinearColor WoodTint = WoodColors[R.RandRange(0, 6)];
		Data[0] = WallTint.R; Data[1] = WallTint.G; Data[2] = WallTint.B;
		Data[3] = WoodTint.R; Data[4] = WoodTint.G; Data[5] = WoodTint.B;
		Data[6] = R.FRand();
		const bool bShutters = R.FRand() < Style.Shutters;
		const bool bPitched = R.FRand() < Style.PitchedRoof;
		const double TopZ = Z0 + Storeys * StoreyCm;

		TArray<FInstance>& Out = Chunks.FindOrAdd(FIntPoint(FMath::FloorToInt32(C.X / ChunkSizeCm), FMath::FloorToInt32(C.Y / ChunkSizeCm)));
		auto Add = [&](const TCHAR* Name, const FVector& Loc, const FRotator& Rot, const FVector& Scale = FVector::OneVector)
		{
			UStaticMesh* Mesh = Pieces.Get(Name);
			if (!Mesh)
			{
				return;
			}
			FInstance I;
			I.Mesh = Mesh;
			I.Transform = FTransform(Rot, Loc, Scale);
			FMemory::Memcpy(I.Data, Data, sizeof(Data));
			I.Data[7] = R.FRand() < 0.3f ? 1.f : 0.f;   // this window lit at night
			Out.Add(I);
		};
		auto AddProp = [&](UStaticMesh* Mesh, const FVector& Loc, double YawDeg)
		{
			if (Mesh)
			{
				FInstance I;
				I.Mesh = Mesh;
				I.Transform = FTransform(FRotator(0.0, YawDeg, 0.0), FVector(Loc.X, Loc.Y, Height.HeightCm(Loc.X, Loc.Y) + 20.0));
				for (int32 k = 0; k < 6; ++k) { I.Data[k] = 1.f; }
				Out.Add(I);
			}
		};
		const bool bSeafront = Style.Palette == 1;

		for (int32 f = 0; f < 4; ++f)
		{
			const FFacade& Fa = F[f];
			// Piece frame: X along the facade, Y = -outward (the outside face looks along -Y), Z up.
			const FVector Along = FVector::CrossProduct(-Fa.Out, FVector::UpVector).GetSafeNormal();
			const FRotator Rot = FRotationMatrix::MakeFromXY(Along, -Fa.Out).Rotator();
			const FVector Start = C + Fa.Out * Fa.Half - Along * (Fa.Length * 0.5);
			const TArray<double> BayW = Bays(Fa.Length, Fa.bStreet ? R.RandRange(0, FMath::FloorToInt32(Fa.Length / 400.0)) : 0);
			int32 DoorBay = INDEX_NONE;
			if (Fa.bStreet)
			{
				DoorBay = BayW.IndexOfByKey(200.0);
				if (DoorBay == INDEX_NONE && BayW.Num() > 0)
				{
					DoorBay = R.RandRange(0, BayW.Num() - 1);
				}
			}
			double Offset = 0.0;
			for (int32 b = 0; b < BayW.Num(); ++b)
			{
				const double Bw = BayW[b];
				const bool bFour = Bw > 300.0;
				const FVector BayStart = Start + Along * Offset;
				// Plinths go under plain ground bays only: doors and shopfronts carry their own stubs.
				for (int32 s = 0; s < Storeys; ++s)
				{
					const FVector P = BayStart + FVector(0, 0, Z0 + s * StoreyCm);
					if (s == 0)
					{
						if (b == DoorBay)
						{
							Add(TEXT("Door_200"), P, Rot);
							if (bFour)
							{
								Add(TEXT("Plinth_200"), BayStart + Along * 200.0 + FVector(0, 0, Z0), Rot);
							}
							if (R.FRand() < 0.55f)  // potted plants either side of the step
							{
								const FVector Step = BayStart + Fa.Out * 60.0;
								AddProp(R.FRand() < 0.5f ? Pot : Fern, Step + Along * 20.0, Rot.Yaw + R.FRandRange(0.f, 360.f));
								AddProp(Pot, Step + Along * 180.0, Rot.Yaw + R.FRandRange(0.f, 360.f));
							}
							if (bFour)
							{
								Add(TEXT("Wall_200"), P + Along * 200.0, Rot);
							}
						}
						else if (!bFour)
						{
							Add(TEXT("Wall_200"), P, Rot);
							Add(TEXT("Plinth_200"), P, Rot);
						}
						else if (Fa.bStreet && R.FRand() < Style.Shop)
						{
							Add(TEXT("Shopfront_400"), P, Rot);
							const FVector Front = BayStart + Fa.Out * 90.0;
							const float Pick = R.FRand();
							if (bSeafront && Pick < 0.45f)
							{
								AddProp(Cafe, Front + Fa.Out * 60.0 + Along * 200.0, Rot.Yaw + 90.0);
							}
							else if (Pick < 0.35f)
							{
								AddProp(R.FRand() < 0.6f ? Crate : WoodCrate, Front + Along * R.FRandRange(30.f, 90.f), Rot.Yaw + R.FRandRange(-20.f, 20.f));
								AddProp(Crate, Front + Along * R.FRandRange(300.f, 370.f), Rot.Yaw + R.FRandRange(-20.f, 20.f));
							}
						}
						else
						{
							Add(R.FRand() < (Fa.bStreet ? 0.8 : Style.SideWindows * 0.6) ? TEXT("GroundWindow_400") : TEXT("Wall_400"), P, Rot);
							Add(TEXT("Plinth_400"), P, Rot);
						}
						continue;
					}
					const bool bWindow = Fa.bStreet || R.FRand() < Style.SideWindows;
					if (!bWindow)
					{
						Add(bFour ? TEXT("Wall_400") : TEXT("Wall_200"), P, Rot);
					}
					else if (bFour && Fa.bStreet && s < Storeys && R.FRand() < Style.Balcony)
					{
						Add(TEXT("Balcony_400"), P, Rot);
					}
					else if (bShutters)
					{
						Add(bFour ? TEXT("Window_400") : TEXT("Window_200"), P, Rot);
					}
					else
					{
						Add(bFour ? TEXT("WindowPlain_400") : TEXT("WindowPlain_200"), P, Rot);
					}
				}
				Add(bFour ? TEXT("Cornice_400") : TEXT("Cornice_200"), BayStart + FVector(0, 0, TopZ), Rot);
				if (!bPitched)
				{
					Add(bFour ? TEXT("Parapet_400") : TEXT("Parapet_200"), BayStart + FVector(0, 0, TopZ + 35.0), Rot);
				}
				Offset += Bw;
			}
			// Quoins on the corner this facade starts at, every storey; a downpipe beside it on streets.
			for (int32 s = 0; s < Storeys; ++s)
			{
				Add(TEXT("Corner_320"), Start + FVector(0, 0, Z0 + s * StoreyCm), Rot);
				if (Fa.bStreet && f % 2 == 0)
				{
					// The pipe's brackets reach Y = 0, so its pivot sits on the wall face.
					Add(TEXT("Drainpipe_320"), Start + Along * 45.0 + FVector(0, 0, Z0 + s * StoreyCm), Rot);
				}
			}
			Add(TEXT("CorniceCorner"), Start + FVector(0, 0, TopZ), Rot);  // mitre block: closes the cornice at the corner
		}

		// Roof.
		const FRotator Yaw(0.0, FMath::RadiansToDegrees(FMath::Atan2(U.Y, U.X)), 0.0); // yaw only: roofs stay upright
		if (bPitched)
		{
			// Ridge along the piece's X: turn it to run along the longer side; 0.4 m eaves all round,
			// sitting on top of the cornice.
			const double Ridge = FMath::Clamp(FMath::Min(W, D) / 100.0 * 0.28, 1.4, 4.0);
			const bool bTurn = D > W;
			const FRotator RoofRot(0.0, Yaw.Yaw + (bTurn ? 90.0 : 0.0), 0.0);
			const FVector RoofScale = bTurn ? FVector(D / 100.0 + 0.8, W / 100.0 + 0.8, Ridge) : FVector(W / 100.0 + 0.8, D / 100.0 + 0.8, Ridge);
			Add(TEXT("RoofHip"), C + FVector(0, 0, TopZ + 35.0), RoofRot, RoofScale);
			if (R.FRand() < 0.5)
			{
				Add(TEXT("Prop_Chimney"), C + U * (R.FRandRange(-0.3f, 0.3f) * W) + Vw * (R.FRandRange(-0.2f, 0.2f) * D) + FVector(0, 0, TopZ + 30.0), Yaw);
			}
		}
		else
		{
			Add(TEXT("RoofFlat"), C + FVector(0, 0, TopZ), Yaw, FVector(W / 100.0, D / 100.0, 1.0));
			const int32 Props = FMath::Clamp(FMath::RoundToInt32(W * D / 1.0e4 / 70.0), 1, 5);
			static const TCHAR* RoofProps[] = { TEXT("Prop_WaterTank"), TEXT("Prop_ACUnit"), TEXT("Prop_ACUnit"), TEXT("Prop_Dish"), TEXT("Prop_RoofHut") };
			for (int32 p = 0; p < Props; ++p)
			{
				const FVector Spot = C + U * (R.FRandRange(-0.5f, 0.5f) * (W - 300.0)) + Vw * (R.FRandRange(-0.5f, 0.5f) * (D - 300.0));
				Add(RoofProps[R.RandRange(0, 4)], Spot + FVector(0, 0, TopZ + 25.0), FRotator(0.0, Yaw.Yaw + 90.0 * R.RandRange(0, 3), 0.0));
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
			const int32 Index = ISM->AddInstance(I.Transform, /*bWorldSpace*/ true);
			ISM->SetCustomData(Index, TArrayView<const float>(I.Data, AOCKitBuildingChunk::NumCustomData));
			++Count;
		}
		Actor->SetActorLabel(FString::Printf(TEXT("Kit_Buildings_%d_%d"), Pair.Key.X, Pair.Key.Y));
		Actor->SetFolderPath(TEXT("Buildings/Kit"));
	}
	TArray<FString> Missing;
	for (const FName& N : Pieces.Missing)
	{
		Missing.Add(N.ToString());
	}
	UE_LOG(LogOpenCity, Display, TEXT("BuildKitBuildings: %d buildings, %d piece instances in %d chunks%s"), Buildings, Count, Chunks.Num(),
		Missing.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("; missing pieces skipped: %s"), *FString::Join(Missing, TEXT(", "))));
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
		UE_LOG(LogOpenCity, Error, TEXT("BuildStreetProps: props not imported (Content/Python/oc_import_props.py)"));
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
	if (!Lantern || !LedPole || !SignZTL || !Bin)
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildStreetFixtures: fixtures not imported (Tools/blender/make_kit.py, oc_import_kit.py)"));
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

	TMap<FIntPoint, TArray<FInstance>> Chunks;
	TSet<UStaticMesh*> Solid;
	FRandomStream R(Seed);
	int32 Count = 0;
	auto Put = [&](UStaticMesh* Mesh, const FVector& At, double YawDeg, double Lift = 0.0, const FLinearColor& Colour = FLinearColor::White, bool bOnGround = true)
	{
		if (!Mesh)
		{
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
		I.Data[7] = 1.f;  // lamp glass lit at night (M_Master_KitGlass)
		Chunks.FindOrAdd(FIntPoint(FMath::FloorToInt32(At.X / ChunkSizeCm), FMath::FloorToInt32(At.Y / ChunkSizeCm))).Add(I);
		++Count;
	};

	// Signs where traffic approaches node N along segment S: on the approaching driver's right,
	// Offset cm before the junction edge, facing the traffic.
	auto SignAt = [&](UStaticMesh* Sign, const FOCRoadSegment& S, int32 N, double Offset)
	{
		const FVector A = Nodes[S.A].Location, B = Nodes[S.B].Location;
		const FVector Dir = (B - A).GetSafeNormal2D();
		const FVector Side(-Dir.Y, Dir.X, 0.0);
		const bool bAtB = N == S.B;
		const FVector Travel = bAtB ? Dir : -Dir;
		const FVector Right = bAtB ? Side : -Side;
		const double Len = FVector::Dist2D(A, B);
		const double Back = FMath::Min(Clear[N] + Offset, Len * 0.45);
		const FVector P = Nodes[N].Location - Travel * Back + Right * (S.WidthCm * 0.5 + (HasWalk(S) ? SidewalkWidthCm * 0.6 : 60.0));
		Put(Sign, P, YawForLocalY(Travel), HasWalk(S) ? 15.0 : 0.0);
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

		// Centro storico: lanterns on the facades, alternating sides every 18 m, 4.2 m up.
		if (S.Profile == TEXT("centro_storico"))
		{
			int32 k = 0;
			for (double D = From; D <= To; D += 1800.0, ++k)
			{
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
			static const FLinearColor Streams[5] = { FLinearColor(0.05f, 0.25f, 0.06f), FLinearColor(0.75f, 0.6f, 0.02f),
				FLinearColor(0.03f, 0.12f, 0.45f), FLinearColor(0.22f, 0.12f, 0.05f), FLinearColor(0.25f, 0.25f, 0.25f) };
			for (double D = From + R.FRandRange(600.f, 4000.f); D + 800.0 <= To; D += 15000.0)
			{
				const double SideSign = R.FRand() < 0.5 ? 1.0 : -1.0;
				for (int32 b = 0; b < 5; ++b)
				{
					Put(Bin, At(D + b * 150.0, SideSign, Half + 95.0), FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), 15.0, Streams[b]);
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
	int32 CentroCorners = 0;
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
		else if (*Type == TEXT("blind_t") && At.Num() == 3)
		{
			// The narrowest road is the minor one: STOP on it, a convex mirror across the main road.
			int32 Minor = At[0];
			for (const int32 si : At)
			{
				if (Segs[si].WidthCm < Segs[Minor].WidthCm) { Minor = si; }
			}
			const FOCRoadSegment& M = Segs[Minor];
			if (!M.bPedestrian && M.Bridge.IsNone())
			{
				SignAt(SignStop, M, N, 150.0);
				const FVector Away = ((M.A == N ? Nodes[M.B].Location : Nodes[M.A].Location) - Nodes[N].Location).GetSafeNormal2D();
				Put(Mirror, Nodes[N].Location - Away * (Clear[N] + 60.0), YawForLocalY(-Away), 15.0);
			}
		}
		else if (*Type == TEXT("priority"))
		{
			bool bViale = false;
			for (const int32 si : At) { bViale |= Segs[si].Profile == TEXT("viale"); }
			if (bViale)
			{
				for (const int32 si : At)
				{
					if (Segs[si].Profile != TEXT("viale") && !Segs[si].bPedestrian && Segs[si].Bridge.IsNone())
					{
						SignAt(SignGive, Segs[si], N, 150.0);
					}
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
			if (I.Mesh != Lantern)  // posts, poles, bins and fountains are solid; lanterns hang out of reach
			{
				ISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
			const int32 Index = ISM->AddInstance(I.Transform, /*bWorldSpace*/ true);
			ISM->SetCustomData(Index, TArrayView<const float>(I.Data, AOCKitBuildingChunk::NumCustomData));
		}
		Actor->SetActorLabel(FString::Printf(TEXT("Street_Fixtures_%d_%d"), Pair.Key.X, Pair.Key.Y));
		Actor->SetFolderPath(TEXT("Props"));
	}
	UE_LOG(LogOpenCity, Display, TEXT("BuildStreetFixtures: %d fixtures in %d chunks"), Count, Chunks.Num());
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
		UE_LOG(LogOpenCity, Error, TEXT("BuildRetainingWalls: wall pieces not imported (Tools/blender/make_kit.py, oc_import_kit.py)"));
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
	if (!Manholes[0])
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildRoadDecals: decals not imported (Content/Python/oc_decals.py)"));
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

		for (double D = R.FRandRange(1500.f, 4000.f); D < Len - 1200.0; D += R.FRandRange(6000.f, 8000.f))
		{
			Put(Manholes[R.RandRange(0, 1)], At(D, (R.FRand() < 0.5 ? -1.0 : 1.0) * Lane), R.FRandRange(0.f, 360.f), FVector(30.0, 45.0, 45.0), 15.0);
		}
		for (int32 End = 0; End < 2; ++End)
		{
			if (R.FRand() < 0.4f)  // braking marks on the approach to a junction
			{
				const double D = End == 0 ? 1100.0 : Len - 1100.0;
				const double Side = End == 0 ? -1.0 : 1.0;  // the lane heading into that junction
				Put(Tyres, At(D, Side * Lane), Yaw + 90.0, FVector(30.0, 100.0, 380.0), 15.0);
			}
		}
		for (double D = R.FRandRange(2000.f, 9000.f); D < Len - 1000.0; D += R.FRandRange(12000.f, 18000.f))
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
