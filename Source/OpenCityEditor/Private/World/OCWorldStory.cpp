// M3 story layer of the Marevia map (formerly OCWorldStory.cpp): bridges, the story landmarks, area
// gates, volumes and the progression data. Everything comes from SourceArt/World/marevia_layout.json
// and STORY.md:
//   Bridges    decks with parapets. The two moving bridges (Ponte Mobile lifts, Ponte Girevole swings)
//              have their river span in two runtime Data Layers: lowered (open) and raised or turned (locked).
//   Landmarks  a named grey-box (LM_<Name>_*) for every story landmark, in folder Landmarks/<Name>.
//   Gates      barriers where gated roads leave open ground, each gate's props in DL_Gate_<Name>_Locked.
//   Volumes    AOCLockedAreaVolume per locked district; AOCDeepWaterVolume for the sea, river and reservoir.
//   Data       DA_MareviaRoads/Rail/Water, DA_AreaUnlocks (the unlock schedule, STORY.md).
#include "World/OCWorldBuild.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMeshActor.h"
#include "OCEditorHelpers.h"
#include "OCLog.h"
#include "OCWorldBuilderLibrary.h"
#include "Progression/OCAreaUnlockData.h"
#include "World/OCDeepWaterVolume.h"
#include "World/OCLockedAreaVolume.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"

namespace OCWorldBuild
{
	namespace
	{
		using OCLayout::FJson;

		const TCHAR* LayerPath = TEXT("/Game/World/DataLayers");

		// Decks meet the road at the bank nodes with a small overlap, a few cm low: a slab running 5 m onto a
		// sloping bank left a lip the car hit at the footbridge.
		constexpr double DeckLapM = 0.6;
		constexpr double DeckDropM = 0.04;
		// Bridge parapets start this far (m) from each bank node, clear of the junction there.
		constexpr double RailClearM = 8.0;

		// The two bridges that move; every other gate is a barrier.
		FString MovingMode(const FString& Gate)
		{
			if (Gate == TEXT("Gate.PonteMobile")) { return TEXT("lift"); }
			if (Gate == TEXT("Gate.Molo")) { return TEXT("swing"); }
			return FString();
		}

		FString LockedMessage(const FString& District)
		{
			// STORY.md "Unlocking the city"
			static const TMap<FString, FString> Messages = {
				{ TEXT("PortoVecchio"), TEXT("Police cordon. The fire scene at La Cala is still taped off.") },
				{ TEXT("MoloSalvi"), TEXT("Salvi's harbour. Not yet.") },
				{ TEXT("Lungomare"), TEXT("Salvi's territory. Not yet.") },
				{ TEXT("BorgoMarino"), TEXT("Salvi's territory. Not yet.") },
				{ TEXT("LeColline"), TEXT("Private road. The Hill families keep the hills quiet.") },
				{ TEXT("MonteFerrale"), TEXT("The pass road is blocked by a rockfall.") } };
			const FString* M = Messages.Find(District);
			return M ? *M : FString(TEXT("You can't go there yet."));
		}

		/** Python str.capitalize(): first character upper case, the rest lower case. */
		FString Capitalize(const FString& S)
		{
			if (S.IsEmpty()) { return S; }
			return S.Left(1).ToUpper() + S.Mid(1).ToLower();
		}

		FString GateName(const FString& GateTag)
		{
			FString Left, Right;
			GateTag.Split(TEXT("."), &Left, &Right);
			return Right;
		}

		FVector Node(const FJsonPtr& Nodes, int32 I)
		{
			const FJsonPtr N = Nodes->Items[I];
			return FVector(At(N, 0), At(N, 1), At(N, 2));
		}

		/** Rotator from the scripts' (roll, pitch, yaw) order. */
		FRotator Rpy(double Roll, double Pitch, double Yaw) { return FRotator(Pitch, Yaw, Roll); }

		void ToLayer(AActor* A, UDataLayerAsset* Layer)
		{
			if (Layer && !UOCWorldBuilderLibrary::AddActorToDataLayer(A, Layer))
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("could not add %s to %s"), *A->GetActorLabel(), *Layer->GetName());
			}
		}

		// ---- Bridges --------------------------------------------------------------------------

		/** (first, last) distance along a bridge over the Fiume Ardo channel plus Margin; false if it doesn't cross. */
		bool RiverSpan(const FJsonPtr& Layout, const FVector& A, double Ux, double Uy, double Length, double& OutFirst, double& OutLast, double Margin = 6.0)
		{
			const TArray<FJsonPtr>& Line = Arr(Layout->Get(TEXT("water"))->Get(TEXT("river")), TEXT("centerline"));
			auto DistToRiver = [&Line](double X, double Y)
			{
				double Best = 1e18;
				for (int32 i = 0; i + 1 < Line.Num(); ++i)
				{
					const double X0 = At(Line[i], 0), Y0 = At(Line[i], 1), X1 = At(Line[i + 1], 0), Y1 = At(Line[i + 1], 1);
					const double Dx = X1 - X0, Dy = Y1 - Y0;
					const double T = FMath::Clamp(((X - X0) * Dx + (Y - Y0) * Dy) / FMath::Max(Dx * Dx + Dy * Dy, 1e-9), 0.0, 1.0);
					Best = FMath::Min(Best, FMath::Sqrt(FMath::Square(X - X0 - T * Dx) + FMath::Square(Y - Y0 - T * Dy)));
				}
				return Best;
			};
			bool bAny = false;
			for (int32 D = 0; D <= int32(Length); ++D)
			{
				const double Y = A.Y + Uy * D;
				if (DistToRiver(A.X + Ux * D, Y) < RiverHalfWidth(Y) + Margin)
				{
					if (!bAny) { OutFirst = D; bAny = true; }
					OutLast = D;
				}
			}
			return bAny;
		}

		void BuildBridges(const FJsonPtr& Layout, TMap<FString, UDataLayerAsset*>& Layers)
		{
			const FJsonPtr Roads = Layout->Get(TEXT("roads"));
			const FJsonPtr Nodes = Roads->Get(TEXT("nodes"));
			int32 Count = 0;
			for (const FJsonPtr& S : Arr(Roads, TEXT("segments")))
			{
				if (!Truthy(S, TEXT("bridge")))
				{
					continue;
				}
				const FString Bridge = Str(S, TEXT("bridge"));
				const FVector A = Node(Nodes, Int(S, TEXT("a"))), B = Node(Nodes, Int(S, TEXT("b")));
				const double Length = FMath::Sqrt(FMath::Square(B.X - A.X) + FMath::Square(B.Y - A.Y));
				const double Ux = (B.X - A.X) / Length, Uy = (B.Y - A.Y) / Length;
				const double Width = Num(S, TEXT("width")) + 3.0;
				const FString Folder = TEXT("Bridges/") + Bridge;
				const FString Gate = Truthy(S, TEXT("gate")) ? Str(S, TEXT("gate")) : FString();
				const FString Mode = MovingMode(Gate);
				const double Yaw = UeYaw(Ux, Uy);
				UMaterialInterface* Mat = BoxMaterial(TEXT("Bridge"));

				auto AtD = [&](double D) { return FVector(A.X + Ux * D, A.Y + Uy * D, A.Z + (B.Z - A.Z) * (D / Length)); };
				double Wet0 = 0.0, Wet1 = 0.0;
				const bool bWet = RiverSpan(Layout, A, Ux, Uy, Length, Wet0, Wet1);

				auto Deck = [&](double D0, double D1, const FString& Suffix, UDataLayerAsset* Layer)
				{
					const FVector P0 = AtD(D0), P1 = AtD(D1);
					const FVector Mid = (P0 + P1) / 2.0;
					const double Pitch = FMath::RadiansToDegrees(FMath::Atan2(P1.Z - P0.Z, D1 - D0));
					TArray<AActor*> Actors;
					Actors.Add(Shape(FString::Printf(TEXT("Bridge_%s_%s"), *Bridge, *Suffix), Folder, FVector(Mid.X, Mid.Y, Mid.Z - 0.5 - DeckDropM),
						FVector(D1 - D0, Width, 1.0), Mat, Cube, Rpy(0, Pitch, Yaw)));
					// Parapets along both edges, but only clear of the bank nodes: the deck runs onto the bank,
					// and parapets there stood across the junction roads at each end. Over the river they also
					// stop 6 m past the channel: the Passerella's west end lies beyond the riverside avenue, and
					// parapets across its lanes stopped traffic (drive test 2026-11-09).
					const double R0 = FMath::Max3(D0, RailClearM, bWet ? Wet0 : -1e9);
					const double R1 = FMath::Min3(D1, Length - RailClearM, bWet ? Wet1 : 1e9);
					const FVector RMid = (AtD(R0) + AtD(R1)) / 2.0;
					if (R1 - R0 > 1.0)
					{
						for (int32 Side : { -1, 1 })
						{
							const double Ox = -Uy * Side * (Width / 2.0 - 0.2), Oy = Ux * Side * (Width / 2.0 - 0.2);
							Actors.Add(Shape(FString::Printf(TEXT("Bridge_%s_%s_Parapet%s"), *Bridge, *Suffix, Side < 0 ? TEXT("L") : TEXT("R")), Folder,
								FVector(RMid.X + Ox, RMid.Y + Oy, RMid.Z + 0.55), FVector(R1 - R0, 0.4, 1.1), Mat, Cube, Rpy(0, Pitch, Yaw)));
						}
					}
					for (AActor* Act : Actors)
					{
						ToLayer(Act, Layer);
					}
				};

				if (Mode.IsEmpty())
				{
					Deck(-DeckLapM, Length + DeckLapM, TEXT("Deck"), nullptr);
				}
				else
				{
					const FString Name = GateName(Gate);
					const double Span0 = 30.0, Span1 = Length - 30.0;
					Deck(-DeckLapM, Span0, TEXT("ApproachA"), nullptr);
					Deck(Span1, Length + DeckLapM, TEXT("ApproachB"), nullptr);
					Deck(Span0, Span1, TEXT("Span"), Layers.FindChecked(Name + TEXT("_Open")));
					UDataLayerAsset* Locked = Layers.FindChecked(Name + TEXT("_Locked"));
					if (Mode == TEXT("lift"))
					{
						// Bascule: two leaves hinged at the banks, raised to 70 degrees.
						const double Leaf = (Span1 - Span0) / 2.0;
						const double C70 = FMath::Cos(FMath::DegreesToRadians(70.0)), S70 = FMath::Sin(FMath::DegreesToRadians(70.0));
						const struct { double Hinge, Sign; const TCHAR* Suffix; } Leaves[] = { { Span0, 1.0, TEXT("LeafA") }, { Span1, -1.0, TEXT("LeafB") } };
						for (const auto& L : Leaves)
						{
							const FVector H = AtD(L.Hinge);
							const FVector C(H.X + Ux * L.Sign * Leaf / 2.0 * C70, H.Y + Uy * L.Sign * Leaf / 2.0 * C70, H.Z + Leaf / 2.0 * S70);
							AActor* Act = Shape(FString::Printf(TEXT("Bridge_%s_%s_Raised"), *Bridge, L.Suffix), Folder, C, FVector(Leaf, Width, 1.0), Mat, Cube,
								Rpy(0, L.Sign > 0 ? 70.0 : -70.0, Yaw));
							ToLayer(Act, Locked);
						}
					}
					else
					{
						// Swing bridge: the span turned 90 degrees on its pivot pier, parallel to the quay.
						const FVector M = AtD((Span0 + Span1) / 2.0);
						AActor* Act = Shape(FString::Printf(TEXT("Bridge_%s_Span_Turned"), *Bridge), Folder, FVector(M.X, M.Y, M.Z - 0.5),
							FVector(Span1 - Span0, Width, 1.0), Mat, Cube, Rpy(0, 0, Yaw + 90.0));
						ToLayer(Act, Locked);
						Shape(FString::Printf(TEXT("Bridge_%s_Pivot"), *Bridge), Folder, FVector(M.X, M.Y, M.Z - 6.0), FVector(8.0, 8.0, 10.0),
							BoxMaterial(TEXT("Concrete")), Cylinder);
					}
				}
				++Count;
			}
			OCEd::Log(Tag, FString::Printf(TEXT("bridges: %d"), Count));
		}

		// ---- Landmarks ------------------------------------------------------------------------

		void BuildLandmarks(const FJsonPtr& Layout)
		{
			// Parts replaced by the kit-built main volume (the layout generator's kit landmarks).
			static const TSet<FString> KitReplaced = { TEXT("Main"), TEXT("Roof"), TEXT("Tower"), TEXT("Crown"), TEXT("Band"), TEXT("Neon"), TEXT("Door") };
			const double Crest = Num(Layout->Get(TEXT("water")), TEXT("reservoir_level")) + 2.0;
			TSet<FString> KitBuilt;
			for (const FJsonPtr& B : Arr(Layout, TEXT("buildings")))
			{
				if (Truthy(B, TEXT("landmark")))
				{
					KitBuilt.Add(Str(B, TEXT("landmark")));
				}
			}
			int32 Built = 0;
			for (const FJsonPtr& Lm : Arr(Layout, TEXT("landmarks")))
			{
				const FString Name = Str(Lm, TEXT("name")), Kind = Str(Lm, TEXT("kind"));
				const double X = At(Lm->Get(TEXT("pos")), 0), Y = At(Lm->Get(TEXT("pos")), 1);
				const double G = Num(Lm, TEXT("ground_z"));
				const double W = At(Lm->Get(TEXT("footprint")), 0), D = At(Lm->Get(TEXT("footprint")), 1);
				const double H = Num(Lm, TEXT("height"));
				const FString Folder = TEXT("Landmarks/") + Name;
				const bool bKit = KitBuilt.Contains(Name);

				// Off = (east, north, base above ground) in m; the base sinks Sink m into slopes.
				auto Part = [&](const TCHAR* Suffix, FVector Off, FVector Size, const TCHAR* Mat = TEXT("Landmark"), const TCHAR* Mesh = Cube,
					FRotator Rot = FRotator::ZeroRotator, double Sink = 2.0)
				{
					const FString S(Suffix);
					const FString Label = S == TEXT("Main") ? TEXT("LM_") + Name : FString::Printf(TEXT("LM_%s_%s"), *Name, Suffix);
					if (bKit && KitReplaced.Contains(S))
					{
						if (S == TEXT("Main"))
						{
							// Keep the named marker missions and checks look for (a hidden 20 cm box: World
							// Partition descriptors need real bounds to place it).
							AStaticMeshActor* Marker = Shape(Label, Folder, FVector(X + Off.X, Y + Off.Y, G + Off.Z + 1.0), FVector(0.2), BoxMaterial(Mat));
							UStaticMeshComponent* C = Marker->GetStaticMeshComponent();
							C->SetVisibility(false);
							C->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
						}
						return;
					}
					Shape(Label, Folder, FVector(X + Off.X, Y + Off.Y, G + Off.Z - Sink + (Size.Z + Sink) / 2.0),
						FVector(Size.X, Size.Y, Size.Z + Sink), BoxMaterial(Mat), Mesh, Rot);
				};
				const FRotator Z = FRotator::ZeroRotator;

				if (Kind == TEXT("ruin"))   // La Cala: the burned house, roofless walls
				{
					Part(TEXT("Main"), FVector(0, 0, 0), FVector(W, D, 0.4), TEXT("Burnt"));
					Part(TEXT("WallS"), FVector(0, -D / 2, 0), FVector(W, 0.6, H), TEXT("Burnt"));
					Part(TEXT("WallN"), FVector(0, D / 2, 0), FVector(W, 0.6, H * 0.5), TEXT("Burnt"));
					Part(TEXT("WallW"), FVector(-W / 2, 0, 0), FVector(0.6, D, H * 0.8), TEXT("Burnt"));
					Part(TEXT("WallE"), FVector(W / 2, 0, 0), FVector(0.6, D, H * 0.35), TEXT("Burnt"));
				}
				else if (Kind == TEXT("hall"))
				{
					Part(TEXT("Main"), FVector(0, 0, 0), FVector(W, D, H * 0.7));
					Part(TEXT("Roof"), FVector(0, 0, H * 0.7), FVector(W + 2, D + 2, 1.2), TEXT("LandmarkAccent"), Cube, Z, 0);
					Part(TEXT("Canopy"), FVector(0, -D / 2 - 3, 3.5), FVector(W, 6, 0.4), TEXT("Metal"), Cube, Z, 0);
				}
				else if (Kind == TEXT("pier"))
				{
					Part(TEXT("Main"), FVector(0, 0, 0.3), FVector(W, D, 0.6), TEXT("Concrete"), Cube, Z, 4.0);
					Part(TEXT("Booth"), FVector(0, D / 2 - 6, 0.9), FVector(6, 4, 3), TEXT("Landmark"), Cube, Z, 0);
					const double Oys[] = { -D / 2 + 4, 0, D / 2 - 14 };
					for (int32 i = 0; i < 3; ++i)
					{
						Part(*FString::Printf(TEXT("Bollard%d"), i), FVector(W / 2 - 1, Oys[i], 0.9), FVector(0.6, 0.6, 0.8), TEXT("Metal"), Cylinder, Z, 0);
					}
				}
				else if (Kind == TEXT("tower"))
				{
					Part(TEXT("Main"), FVector(0, 0, 0), FVector(W, D, 10));
					Part(TEXT("Tower"), FVector(0, 0, 10), FVector(W * 0.5, D * 0.5, H - 10), TEXT("Glass"), Cube, Z, 0);
					Part(TEXT("Crown"), FVector(0, 0, H), FVector(W * 0.5 + 1, D * 0.5 + 1, 2), TEXT("Landmark"), Cube, Z, 0);
				}
				else if (Kind == TEXT("lighthouse"))
				{
					Part(TEXT("Main"), FVector(0, 0, 0), FVector(W, W, H), TEXT("Landmark"), Cylinder);
					Part(TEXT("Gallery"), FVector(0, 0, H), FVector(W * 1.4, W * 1.4, 0.6), TEXT("Metal"), Cylinder, Z, 0);
					Part(TEXT("Lantern"), FVector(0, 0, H + 0.6), FVector(W * 0.7, W * 0.7, 3), TEXT("Glass"), Cylinder, Z, 0);
					Part(TEXT("Cap"), FVector(0, 0, H + 3.6), FVector(W * 0.8, W * 0.8, 1.2), TEXT("LandmarkAccent"), Cylinder, Z, 0);
				}
				else if (Kind == TEXT("church"))   // Santa Lucia
				{
					Part(TEXT("Main"), FVector(0, -D * 0.125, 0), FVector(W, D * 0.75, H * 0.5));
					Part(TEXT("Roof"), FVector(0, -D * 0.125, H * 0.5), FVector(W + 1, D * 0.75 + 1, 1.2), TEXT("LandmarkAccent"), Cube, Z, 0);
					Part(TEXT("Apse"), FVector(0, D * 0.25, 0), FVector(W * 0.8, W * 0.8, H * 0.45), TEXT("Landmark"), Cylinder);
					Part(TEXT("Campanile"), FVector(W / 2 + 4, -D / 2 + 4, 0), FVector(7, 7, H));
					Part(TEXT("CampanileRoof"), FVector(W / 2 + 4, -D / 2 + 4, H), FVector(8, 8, 1.5), TEXT("LandmarkAccent"), Cube, Z, 0);
				}
				else if (Kind == TEXT("house"))
				{
					Part(TEXT("Main"), FVector(0, 0, 0), FVector(W, D, H));
					Part(TEXT("Roof"), FVector(0, 0, H), FVector(W + 0.6, D + 0.6, 0.8), TEXT("LandmarkAccent"), Cube, Z, 0);
					Part(TEXT("Door"), FVector(0, -D / 2 - 0.05, 0), FVector(2.2, 0.3, 3.2), TEXT("Burnt"), Cube, Z, 0);
				}
				else if (Kind == TEXT("civic"))
				{
					Part(TEXT("Main"), FVector(0, 0, 0), FVector(W, D, H));
					Part(TEXT("Canopy"), FVector(0, -D / 2 - 2.5, 4), FVector(W * 0.4, 5, 0.4), TEXT("Metal"), Cube, Z, 0);
					Part(TEXT("Band"), FVector(0, 0, H - 3), FVector(W + 0.3, D + 0.3, 1.2), Name.Contains(TEXT("Police"), ESearchCase::CaseSensitive) ? TEXT("Police") : TEXT("Glass"), Cube, Z, 0);
				}
				else if (Kind == TEXT("yard"))   // Beppe's garage: workshop and a walled yard
				{
					Part(TEXT("Main"), FVector(-W * 0.225, 0, 0), FVector(W * 0.55, D, H));
					Part(TEXT("Roof"), FVector(-W * 0.225, 0, H), FVector(W * 0.55 + 0.6, D + 0.6, 0.5), TEXT("Metal"), Cube, Z, 0);
					Part(TEXT("WallN"), FVector(W * 0.275, D / 2, 0), FVector(W * 0.45, 0.4, 2.4));
					Part(TEXT("WallE"), FVector(W / 2, 0, 0), FVector(0.4, D, 2.4));
					Part(TEXT("WallS"), FVector(W * 0.33, -D / 2, 0), FVector(W * 0.34, 0.4, 2.4));
				}
				else if (Kind == TEXT("kiln"))   // the abandoned kiln (mission 7)
				{
					Part(TEXT("Main"), FVector(0, -D * 0.15, 0), FVector(W, D * 0.7, 12));
					Part(TEXT("Roof"), FVector(0, -D * 0.15, 12), FVector(W + 1, D * 0.7 + 1, 0.6), TEXT("Metal"), Cube, Z, 0);
					Part(TEXT("Chimney"), FVector(W / 3, D * 0.3, 0), FVector(5, 5, H), TEXT("LandmarkAccent"), Cylinder);
					Part(TEXT("Kiln"), FVector(-W / 4, D * 0.3, 0), FVector(14, 14, 8), TEXT("LandmarkAccent"), Cylinder);
				}
				else if (Kind == TEXT("station"))
				{
					Part(TEXT("Main"), FVector(0, 0, 0), FVector(W * 0.4, D, H));
					Part(TEXT("WingW"), FVector(-W * 0.35, 0, 0), FVector(W * 0.3, D * 0.8, H * 0.6));
					Part(TEXT("WingE"), FVector(W * 0.35, 0, 0), FVector(W * 0.3, D * 0.8, H * 0.6));
					Part(TEXT("Clock"), FVector(0, -D / 2 + 3, 0), FVector(6, 6, H + 10));
					Part(TEXT("Canopy"), FVector(0, D / 2 + 5, 5), FVector(W, 8, 0.4), TEXT("Metal"), Cube, Z, 0);
				}
				else if (Kind == TEXT("club"))
				{
					Part(TEXT("Main"), FVector(0, 0, 0), FVector(W, D, H));
					Part(TEXT("Neon"), FVector(0, 0, H - 2), FVector(W + 0.3, D + 0.3, 1.5), TEXT("Glass"), Cube, Z, 0);
				}
				else if (Kind == TEXT("hotel"))
				{
					Part(TEXT("Main"), FVector(0, 0, 0), FVector(W + 6, D + 6, 6));
					Part(TEXT("Tower"), FVector(0, 0, 6), FVector(W, D, H - 6), TEXT("Landmark"), Cube, Z, 0);
					Part(TEXT("Crown"), FVector(0, 0, H), FVector(W * 0.5, D * 0.5, 3), TEXT("LandmarkAccent"), Cube, Z, 0);
				}
				else if (Kind == TEXT("villa"))
				{
					Part(TEXT("Main"), FVector(-W * 0.2, 0, 0), FVector(W * 0.6, D, H));
					Part(TEXT("Roof"), FVector(-W * 0.2, 0, H), FVector(W * 0.6 + 1, D + 1, 1), TEXT("LandmarkAccent"), Cube, Z, 0);
					Part(TEXT("Wing"), FVector(W * 0.3, -D * 0.2, 0), FVector(W * 0.4, D * 0.6, H * 0.6));
					Part(TEXT("Pool"), FVector(W * 0.3, D * 0.35, 0), FVector(12, 6, 0.3), TEXT("Glass"), Cube, Z, 0.5);
				}
				else if (Kind == TEXT("lookout"))   // Il Belvedere (the finale)
				{
					Part(TEXT("Main"), FVector(0, 0, -0.5), FVector(W, W, 1.0), TEXT("Concrete"));
					Part(TEXT("RailS"), FVector(0, -W / 2, 0.5), FVector(W, 0.2, 1.1), TEXT("Metal"), Cube, Z, 0);
					Part(TEXT("RailN"), FVector(0, W / 2, 0.5), FVector(W, 0.2, 1.1), TEXT("Metal"), Cube, Z, 0);
					Part(TEXT("RailW"), FVector(-W / 2, 0, 0.5), FVector(0.2, W, 1.1), TEXT("Metal"), Cube, Z, 0);
					Part(TEXT("RailE"), FVector(W / 2, 0, 0.5), FVector(0.2, W, 1.1), TEXT("Metal"), Cube, Z, 0);
				}
				else if (Kind == TEXT("quarry"))   // La Cava
				{
					Part(TEXT("Main"), FVector(W / 3, D / 3, 0), FVector(20, 15, H));
					Part(TEXT("Face"), FVector(0, D / 2, 0), FVector(W, 4, 25), TEXT("Rock"));
					Part(TEXT("Conveyor"), FVector(W / 3 - 18, D / 3, 6), FVector(30, 2, 1.5), TEXT("Metal"), Cube, Rpy(0, 12, 0), 0);
					// The Python build seeded this from the name string; any fixed seed will do.
					OCLayout::FPyRandom Rng(uint32(FCrc::StrCrc32(*Name)));
					for (int32 i = 0; i < 3; ++i)
					{
						const double Yaw = Rng.Uniform(0, 90);
						Part(*FString::Printf(TEXT("Gravel%d"), i), FVector(-W / 4 + i * 14, -D / 4, 0), FVector(12, 12, 6), TEXT("Rock"), Cube, Rpy(0, 0, Yaw));
					}
				}
				else if (Kind == TEXT("dam"))   // Diga del Ferrale: concrete face downstream of the crest road
				{
					const double RoadHalf = 3.5 + 1.0;
					const double Top = Crest - 1.0;
					Part(TEXT("Main"), FVector(0, -(RoadHalf + D / 2), Top - G), FVector(W, D, 0.01), TEXT("Concrete"), Cube, Z, H + (Top - G) + 10);
					Part(TEXT("ParapetS"), FVector(0, -(RoadHalf - 0.3), Crest - G), FVector(W, 0.4, 1.0), TEXT("Concrete"), Cube, Z, 0.3);
					Part(TEXT("ParapetN"), FVector(0, RoadHalf - 0.3, Crest - G), FVector(W, 0.4, 1.0), TEXT("Concrete"), Cube, Z, 0.3);
					Part(TEXT("Spillway"), FVector(0, -(RoadHalf + D + 6), Top - G - 20), FVector(30, 12, 0.01), TEXT("Concrete"), Cube, Z, 20);
				}
				else if (Kind == TEXT("hermitage"))   // L'Eremo delle Aquile
				{
					Part(TEXT("Main"), FVector(0, 0, 0), FVector(W * 0.6, D * 0.7, H));
					Part(TEXT("Roof"), FVector(0, 0, H), FVector(W * 0.6 + 1, D * 0.7 + 1, 1), TEXT("LandmarkAccent"), Cube, Z, 0);
					Part(TEXT("Bell"), FVector(W * 0.3 + 1.5, 0, H), FVector(3, 2, 4), TEXT("Landmark"), Cube, Z, 0);
					Part(TEXT("Wall"), FVector(0, -D / 2, 0), FVector(W, 0.5, 2.5));
				}
				else if (Kind == TEXT("mast"))
				{
					Part(TEXT("Main"), FVector(0, 0, 0), FVector(2, 2, H), TEXT("Metal"), Cylinder);
					Part(TEXT("Hut"), FVector(4, 0, 0), FVector(6, 4, 3));
				}
				else
				{
					Part(TEXT("Main"), FVector(0, 0, 0), FVector(W, D, H));
					OCEd::Log(Tag, FString::Printf(TEXT("landmark %s: unknown kind %s, plain block"), *Name, *Kind));
				}
				++Built;
			}
			OCEd::Log(Tag, FString::Printf(TEXT("landmarks: %d"), Built));
		}

		// ---- Gates ----------------------------------------------------------------------------

		struct FFrontier { FString Gate; FJsonPtr Seg; int32 NodeIndex; };

		/** (gate tag, segment, node) wherever a gated road meets road outside that gate. */
		TArray<FFrontier> GateFrontiers(const FJsonPtr& Layout)
		{
			const TArray<FJsonPtr>& Segs = Arr(Layout->Get(TEXT("roads")), TEXT("segments"));
			auto GateOf = [](const FJsonPtr& S) { return Truthy(S, TEXT("gate")) ? Str(S, TEXT("gate")) : FString(); };
			TMap<int32, TArray<int32>> Adj;
			for (int32 i = 0; i < Segs.Num(); ++i)
			{
				Adj.FindOrAdd(Int(Segs[i], TEXT("a"))).Add(i);
				Adj.FindOrAdd(Int(Segs[i], TEXT("b"))).Add(i);
			}
			TArray<FFrontier> Out;
			for (int32 i = 0; i < Segs.Num(); ++i)
			{
				const FString Gate = GateOf(Segs[i]);
				if (Gate.IsEmpty() || !MovingMode(Gate).IsEmpty())
				{
					continue;
				}
				for (int32 N : { Int(Segs[i], TEXT("a")), Int(Segs[i], TEXT("b")) })
				{
					for (int32 T : Adj[N])
					{
						if (T != i && GateOf(Segs[T]) != Gate)
						{
							Out.Add({ Gate, Segs[i], N });
							break;
						}
					}
				}
			}
			return Out;
		}

		void BuildGateProps(const FJsonPtr& Layout, TMap<FString, UDataLayerAsset*>& Layers)
		{
			const FJsonPtr Nodes = Layout->Get(TEXT("roads"))->Get(TEXT("nodes"));
			OCLayout::FPyRandom Rng(1974);
			int32 Count = 0;
			const TArray<FFrontier> Frontiers = GateFrontiers(Layout);
			for (int32 K = 0; K < Frontiers.Num(); ++K)
			{
				const FFrontier& F = Frontiers[K];
				const FString Name = GateName(F.Gate);
				const int32 Sa = Int(F.Seg, TEXT("a")), Sb = Int(F.Seg, TEXT("b"));
				const FVector A = Node(Nodes, F.NodeIndex);
				const FVector B = Node(Nodes, Sa == F.NodeIndex ? Sb : Sa);
				const double Length = FMath::Sqrt(FMath::Square(B.X - A.X) + FMath::Square(B.Y - A.Y));
				const double Ux = (B.X - A.X) / Length, Uy = (B.Y - A.Y) / Length;   // from open ground into the gate
				const double Sx = -Uy, Sy = Ux;                                       // across the road
				const double Off = FMath::Min(8.0, Length / 3.0);
				const double Px = A.X + Ux * Off, Py = A.Y + Uy * Off;
				const double Pz = A.Z + (B.Z - A.Z) * Off / Length;
				const double Across = UeYaw(Sx, Sy);
				const FString Kind = Str(F.Seg, TEXT("kind"));
				const double Width = Num(F.Seg, TEXT("width")) + ((Kind == TEXT("avenue") || Kind == TEXT("street")) ? 5.0 : 2.0);
				const FString Folder = TEXT("Gates/") + Name;
				UDataLayerAsset* Layer = Layers.FindChecked(Name + TEXT("_Locked"));
				TArray<AActor*> Acts;

				auto Put = [&](const FString& Suffix, double Along, double Side, double Z, const FVector& Size, const TCHAR* Mat, const FRotator* Rot = nullptr)
				{
					const FVector C(Px + Ux * Along + Sx * Side, Py + Uy * Along + Sy * Side, Pz + Z);
					Acts.Add(Shape(FString::Printf(TEXT("Gate_%s_%d_%s"), *Name, K, *Suffix), Folder, C, Size, BoxMaterial(Mat), Cube,
						Rot ? *Rot : Rpy(0, 0, Across)));
				};

				if (F.Gate == TEXT("Gate.PortCordon"))
				{
					Put(TEXT("Barrier"), 0, 0, 0.6, FVector(Width, 0.3, 1.0), TEXT("Barrier"));
					for (int32 Side : { -1, 1 })
					{
						Put(FString::Printf(TEXT("Car%d"), Side), 4.0, Side * (Width / 2 - 3.0), 0.75, FVector(1.9, 4.6, 1.5), TEXT("Police"));
					}
				}
				else if (F.Gate == TEXT("Gate.Colline"))
				{
					Put(TEXT("Arm"), 0, 0, 1.0, FVector(Width * 0.9, 0.25, 0.25), TEXT("Barrier"));
					Put(TEXT("Post"), 0, -Width / 2 + 0.3, 0.6, FVector(0.5, 0.5, 1.2), TEXT("Metal"));
					Put(TEXT("Booth"), 2.0, Width / 2 + 2.0, 1.5, FVector(3, 3, 3), TEXT("Landmark"));
					Put(TEXT("Block"), 1.0, 0, 0.4, FVector(Width * 0.9, 0.6, 0.8), TEXT("Concrete"));   // a car can't duck under the arm
				}
				else if (F.Gate == TEXT("Gate.Ferrale"))
				{
					for (int32 i = 0; i < 6; ++i)
					{
						// Drawn in the Python argument order: size, along, side, then roll, pitch, yaw.
						const double Sz = Rng.Uniform(2.5, 4.5);
						const double Along = Rng.Uniform(-2, 4);
						const double Side = Rng.Uniform(-Width / 2, Width / 2);
						const double Roll = Rng.Uniform(-15, 15);
						const double Pitch = Rng.Uniform(-15, 15);
						const double Yaw = Rng.Uniform(0, 90);
						const FRotator Rot = Rpy(Roll, Pitch, Yaw);
						Put(FString::Printf(TEXT("Rock%d"), i), Along, Side, Sz * 0.35, FVector(Sz, Sz * 0.8, Sz * 0.7), TEXT("Rock"), &Rot);
					}
				}
				else if (F.Gate == TEXT("Gate.RailCrossing"))
				{
					Put(TEXT("Arm"), 0, 0, 1.0, FVector(Width * 0.9, 0.25, 0.25), TEXT("Barrier"));
					Put(TEXT("Wagon"), 6.0, 0, 2.2, FVector(FMath::Max(Width + 6.0, 14.0), 3.0, 3.6), TEXT("Train"));
				}
				for (AActor* Act : Acts)
				{
					ToLayer(Act, Layer);
				}
				++Count;
			}
			OCEd::Log(Tag, FString::Printf(TEXT("gate barriers: %d"), Count));
		}

		TMap<FString, UDataLayerAsset*> MakeLayers(UWorld* World, const FJsonPtr& Layout)
		{
			TMap<FString, UDataLayerAsset*> Layers;
			for (const FJsonPtr& Gate : Arr(Layout, TEXT("gates")))
			{
				const FString GateTag = Str(Gate, TEXT("tag"));
				const FString Name = GateName(GateTag);
				Layers.Add(Name + TEXT("_Locked"), UOCWorldBuilderLibrary::CreateRuntimeDataLayer(World, LayerPath, FString::Printf(TEXT("DL_Gate_%s_Locked"), *Name), true));
				if (!MovingMode(GateTag).IsEmpty())
				{
					Layers.Add(Name + TEXT("_Open"), UOCWorldBuilderLibrary::CreateRuntimeDataLayer(World, LayerPath, FString::Printf(TEXT("DL_Gate_%s_Open"), *Name), false));
				}
			}
			for (const TPair<FString, UDataLayerAsset*>& L : Layers)
			{
				if (!L.Value)
				{
					UE_LOG(LogOpenCity, Fatal, TEXT("could not create the gate data layer %s"), *L.Key);
				}
			}
			OCEd::Log(Tag, FString::Printf(TEXT("data layers: %d"), Layers.Num()));
			return Layers;
		}

		// ---- Volumes --------------------------------------------------------------------------

		template <typename T>
		T* BoxVolume(const FString& Label, const FString& Folder, const FVector& CenterM, const FVector& ExtentM, double Yaw = 0.0, bool bSpatial = true)
		{
			T* V = CastChecked<T>(OCEd::Spawn(T::StaticClass(), Cm(CenterM.X, CenterM.Y, CenterM.Z), FRotator(0, Yaw, 0), Label, Folder));
			V->GetBox()->SetBoxExtent(ExtentM * 100.0, false);
			if (!bSpatial)
			{
				V->SetIsSpatiallyLoaded(false);
			}
			return V;
		}

		void BuildLockedVolumes(const FJsonPtr& Layout)
		{
			int32 Count = 0;
			for (const FJsonPtr& Dist : Arr(Layout, TEXT("districts")))
			{
				if (!Truthy(Dist, TEXT("unlock")))
				{
					continue;
				}
				const FJsonPtr R = Dist->Get(TEXT("rect"));
				const double X0 = At(R, 0), Y0 = At(R, 1), X1 = At(R, 2), Y1 = At(R, 3);
				const FString Name = Str(Dist, TEXT("name"));
				AOCLockedAreaVolume* V = BoxVolume<AOCLockedAreaVolume>(TEXT("Locked_") + Name, TEXT("Gates/Areas"),
					FVector((X0 + X1) / 2.0, (Y0 + Y1) / 2.0, 250.0), FVector((X1 - X0) / 2.0, (Y1 - Y0) / 2.0, 400.0), 0.0, false);
				V->Area = UOCWorldBuilderLibrary::MakeTag(*(TEXT("Area.") + Name));
				++Count;
			}
			OCEd::Log(Tag, FString::Printf(TEXT("locked area volumes: %d"), Count));
		}

		void BuildWaterVolumes(const FJsonPtr& Layout)
		{
			const FJsonPtr Water = Layout->Get(TEXT("water"));
			const double Sea = Layout->Get(TEXT("sea_level")).IsValid() ? Num(Layout, TEXT("sea_level")) : 0.0;
			// The sea, the estuary and the Darsena: everything 1.2 m below sea level, over the whole map.
			BoxVolume<AOCDeepWaterVolume>(TEXT("Water_Sea"), TEXT("Water"), FVector(1008.0, 1212.0, Sea - 76.2), FVector(1700.0, 2100.0, 75.0), 0.0, false);
			// The river above sea level: boxes along the bed profile, 1 m below the surface, banks excluded.
			const FJsonPtr River = Water->Get(TEXT("river"));
			const TArray<FJsonPtr>& Prof = Arr(River, TEXT("profile_y_x_bed"));
			const double Depth = Num(River, TEXT("depth"));
			int32 N = 0;
			for (int32 i = 0; i + 1 < Prof.Num(); ++i)
			{
				const double Y0 = At(Prof[i], 0), X0 = At(Prof[i], 1), B0 = At(Prof[i], 2);
				const double Y1 = At(Prof[i + 1], 0), X1 = At(Prof[i + 1], 1), B1 = At(Prof[i + 1], 2);
				const double Top = FMath::Min(B0, B1) + Depth - 1.0;
				if (Top <= Sea - 1.2)
				{
					continue;   // the sea volume covers it
				}
				const double Bottom = FMath::Min(B0, B1) - 6.0;
				const double Hw = RiverHalfWidth((Y0 + Y1) / 2.0) - 3.0;
				const double Seg = FMath::Sqrt(FMath::Square(X1 - X0) + FMath::Square(Y1 - Y0));
				BoxVolume<AOCDeepWaterVolume>(FString::Printf(TEXT("Water_River_%03d"), N), TEXT("Water/River"),
					FVector((X0 + X1) / 2.0, (Y0 + Y1) / 2.0, (Top + Bottom) / 2.0), FVector(Seg / 2.0 + 1.0, Hw, (Top - Bottom) / 2.0), UeYaw(X1 - X0, Y1 - Y0));
				++N;
			}
			// The reservoir behind the dam.
			const FJsonPtr R = Water->Get(TEXT("reservoir"));
			const double Rx0 = At(R, 0), Ry0 = At(R, 1), Rx1 = At(R, 2), Ry1 = At(R, 3);
			const double Level = Num(Water, TEXT("reservoir_level"));
			BoxVolume<AOCDeepWaterVolume>(TEXT("Water_Reservoir"), TEXT("Water"), FVector((Rx0 + Rx1) / 2.0, (Ry0 + Ry1) / 2.0, Level - 21.2),
				FVector((Rx1 - Rx0) / 2.0, (Ry1 - Ry0) / 2.0, 20.0), 0.0, false);
			OCEd::Log(Tag, FString::Printf(TEXT("deep water volumes: sea, reservoir and %d river sections"), N));
		}

		// ---- Data assets ----------------------------------------------------------------------

		void BuildData(const FJsonPtr& Layout, TMap<FString, UDataLayerAsset*>& Layers)
		{
			WriteNetworkAssets(Layout);   // road, rail and water data (INFRA_DESIGN.md)
			TArray<FOCAreaUnlock> Areas;
			for (const FJsonPtr& Dist : Arr(Layout, TEXT("districts")))
			{
				FOCAreaUnlock E;
				const FString Name = Str(Dist, TEXT("name"));
				E.Area = UOCWorldBuilderLibrary::MakeTag(*(TEXT("Area.") + Name));
				if (Truthy(Dist, TEXT("unlock")))
				{
					E.RequiredMilestone = UOCWorldBuilderLibrary::MakeTag(*Str(Dist, TEXT("unlock")));
					E.LockedMessage = FText::FromString(LockedMessage(Name));
				}
				Areas.Add(E);
			}
			for (const FJsonPtr& Gate : Arr(Layout, TEXT("gates")))
			{
				const FString GateTag = Str(Gate, TEXT("tag"));
				const FString Name = GateName(GateTag);
				FOCAreaUnlock E;
				E.Area = UOCWorldBuilderLibrary::MakeTag(*GateTag);
				E.RequiredMilestone = UOCWorldBuilderLibrary::MakeTag(*Str(Gate, TEXT("opens_with")));
				E.LockedMessage = FText::FromString(Capitalize(Str(Gate, TEXT("while_locked"))) + TEXT("."));
				E.LayersWhileLocked.Add(Layers.FindChecked(Name + TEXT("_Locked")));
				if (UDataLayerAsset** Open = Layers.Find(Name + TEXT("_Open")))
				{
					E.LayersWhenOpen.Add(*Open);
				}
				Areas.Add(E);
			}
			UOCAreaUnlockData* Unlocks = CastChecked<UOCAreaUnlockData>(OCEd::GetOrCreateDataAsset(TEXT("DA_AreaUnlocks"), TEXT("/Game/Core/Progression"), UOCAreaUnlockData::StaticClass(), Tag));
			Unlocks->Areas = Areas;
			Unlocks->MarkPackageDirty();
			OCEd::Save(Unlocks);
			OCEd::Log(Tag, FString::Printf(TEXT("data: /Game/World/DA_MareviaRoads (%d nodes), %s (%d areas and gates)"),
				Arr(Layout->Get(TEXT("roads")), TEXT("nodes")).Num(), *Unlocks->GetPathName(), Areas.Num()));
		}
	}

	void BuildStory(UWorld* World, const FJsonPtr& Layout)
	{
		TMap<FString, UDataLayerAsset*> Layers = MakeLayers(World, Layout);
		BuildBridges(Layout, Layers);
		BuildLandmarks(Layout);
		BuildGateProps(Layout, Layers);
		BuildLockedVolumes(Layout);
		BuildWaterVolumes(Layout);
		BuildData(Layout, Layers);
	}
}
