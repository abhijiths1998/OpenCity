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

#include "Components/InstancedStaticMeshComponent.h"
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
		TEXT("Prop_RoofHut"), TEXT("Drainpipe_320") };
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
				Add(bFour ? TEXT("Plinth_400") : TEXT("Plinth_200"), BayStart + FVector(0, 0, Z0), Rot);
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
								Add(TEXT("Wall_200"), P + Along * 200.0, Rot);
							}
						}
						else if (!bFour)
						{
							Add(TEXT("Wall_200"), P, Rot);
						}
						else if (Fa.bStreet && R.FRand() < Style.Shop)
						{
							Add(TEXT("Shopfront_400"), P, Rot);
						}
						else
						{
							Add(R.FRand() < (Fa.bStreet ? 0.8 : Style.SideWindows * 0.6) ? TEXT("GroundWindow_400") : TEXT("Wall_400"), P, Rot);
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
					Add(TEXT("Drainpipe_320"), Start + Along * 45.0 + Fa.Out * 8.0 + FVector(0, 0, Z0 + s * StoreyCm), Rot);
				}
			}
		}

		// Roof.
		const FRotator Yaw(0.0, FMath::RadiansToDegrees(FMath::Atan2(U.Y, U.X)), 0.0); // yaw only: roofs stay upright
		if (bPitched)
		{
			const double Ridge = FMath::Clamp(FMath::Min(W, D) / 100.0 * 0.28, 1.4, 4.0);
			Add(TEXT("RoofHip"), C + FVector(0, 0, TopZ + 30.0), Yaw, FVector(W / 100.0 + 0.5, D / 100.0 + 0.5, Ridge));
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
