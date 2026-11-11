// M3 water surfaces and distant scenery for the Marevia map (formerly OCWorldWater.cpp). Water is a
// custom surface, not the Water plugin (ARCHITECTURE.md section 7, "Water"):
//   Water_SeaSurface        one plane at sea level reaching to the horizon; the landscape cuts the
//                           coastline, the Darsena and the estuary out of it (not spatially loaded)
//   Water_RiverSurface_*    tilted planes along the Fiume Ardo bed profile, wide enough to run into both banks
//   Water_ReservoirSurface  the lake behind the Diga del Ferrale, cut off at the dam crest
// Scenery (STORY.md: seen, never reached in Part 1), not spatially loaded, no collision, no shadows:
//   Scenery_Peak_*, Scenery_EastHill_*, Scenery_IsolaVentura_*
// Deep-water gameplay stays in the AOCDeepWaterVolume actors (OCWorldStory.cpp); these are visual only.
#include "World/OCWorldBuild.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "OCEditorHelpers.h"

namespace OCWorldBuild
{
	namespace
	{
		const TCHAR* BoulderA = TEXT("/Game/Environment/Props/SM_Prop_namaqualand_boulder_03/SM_Prop_namaqualand_boulder_03");
		const TCHAR* BoulderB = TEXT("/Game/Environment/Props/SM_Prop_boulder_01/SM_Prop_boulder_01");
		constexpr double SeaExtentM = 40000.0;   // half size of the sea plane: past the horizon from any hill in the map
		constexpr double CascadeDeg = 17.0;      // steeper river sections (cascades, the gorge below the dam) get no surface
		constexpr double BankM = 12.0;           // banks rise from the bed over 12 m beside the channel

		/** A collision-free mesh centred at CenterM (layout metres), size in metres. */
		AStaticMeshActor* Visual(const FString& Label, const FString& Folder, const FVector& CenterM, const FVector& SizeM, const TCHAR* Mat,
			const TCHAR* Mesh, const FRotator& Rot = FRotator::ZeroRotator, bool bSpatial = true, bool bShadows = true)
		{
			AStaticMeshActor* A = Shape(Label, Folder, CenterM, SizeM, BoxMaterial(Mat), Mesh, Rot);
			UStaticMeshComponent* C = A->GetStaticMeshComponent();
			C->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			C->SetCastShadow(bShadows);
			if (!bSpatial)
			{
				A->SetIsSpatiallyLoaded(false);
			}
			return A;
		}

		/** A rock scan scaled to SizeM, its bounds centred on (X, Y) and its base at BaseZ (layout metres). */
		void Scan(const FString& Label, const FString& Folder, const TCHAR* Mesh, double X, double Y, double BaseZ, const FVector& SizeM, const TCHAR* Mat, double Yaw)
		{
			const FVector Ext = Cast<UStaticMesh>(Asset(Mesh))->GetBounds().BoxExtent;
			AStaticMeshActor* A = Visual(Label, Folder, FVector(X, Y, BaseZ), FVector(1.0), Mat, Mesh, FRotator(0, Yaw, 0), false, false);
			A->SetActorScale3D(FVector(SizeM.X * 50.0 / Ext.X, SizeM.Y * 50.0 / Ext.Y, SizeM.Z * 50.0 / Ext.Z));
			FVector Origin, Half;
			A->GetActorBounds(false, Origin, Half);
			const FVector Want(X * 100.0, -Y * 100.0, BaseZ * 100.0 + Half.Z);
			A->SetActorLocation(A->GetActorLocation() + (Want - Origin), false);
		}

		void BuildSurfaces(const FJsonPtr& Layout)
		{
			const FJsonPtr Water = Layout->Get(TEXT("water"));
			const double Sea = Layout->Get(TEXT("sea_level")).IsValid() ? Num(Layout, TEXT("sea_level")) : 0.0;
			const double Ox = At(Layout->Get(TEXT("origin")), 0), Oy = At(Layout->Get(TEXT("origin")), 1);
			const double Sx = At(Layout->Get(TEXT("size")), 0), Sy = At(Layout->Get(TEXT("size")), 1);

			Visual(TEXT("Water_SeaSurface"), TEXT("Water/Surface"), FVector(Ox + Sx / 2.0, Oy + Sy / 2.0, Sea), FVector(2 * SeaExtentM, 2 * SeaExtentM, 1.0),
				TEXT("Water"), Plane, FRotator::ZeroRotator, false, false);

			// River: one plane per bed-profile step, surface = bed + depth, tilted to follow the bed.
			const FJsonPtr River = Water->Get(TEXT("river"));
			const TArray<FJsonPtr>& Prof = Arr(River, TEXT("profile_y_x_bed"));
			const double Depth = Num(River, TEXT("depth"));
			int32 N = 0;
			for (int32 i = 0; i + 1 < Prof.Num(); ++i)
			{
				const double Y0 = At(Prof[i], 0), X0 = At(Prof[i], 1), Z0 = At(Prof[i], 2) + Depth;
				const double Y1 = At(Prof[i + 1], 0), X1 = At(Prof[i + 1], 1), Z1 = At(Prof[i + 1], 2) + Depth;
				if (FMath::Max(Z0, Z1) <= Sea + 0.05)
				{
					continue;   // the sea surface covers the river mouth
				}
				const double Horiz = FMath::Sqrt(FMath::Square(X1 - X0) + FMath::Square(Y1 - Y0));
				const double Pitch = FMath::RadiansToDegrees(FMath::Atan2(Z1 - Z0, Horiz));
				const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(-(Y1 - Y0), X1 - X0));
				if (FMath::Abs(Pitch) > CascadeDeg)
				{
					continue;   // a dry rock cascade, not a tilted water sheet (QA 2026-11-09: slabs stood out of the gorge)
				}
				// Steep reaches keep to the channel; a sheet as wide as the banks stood out of the relaxed terrain.
				const double Bank = FMath::Abs(Pitch) < 6.0 ? BankM : 0.0;
				const double Width = 2.0 * (RiverHalfWidth((Y0 + Y1) / 2.0) + Bank);
				const double Length = FMath::Sqrt(Horiz * Horiz + FMath::Square(Z1 - Z0)) + 1.0;   // 0.5 m overlap at each joint
				Visual(FString::Printf(TEXT("Water_RiverSurface_%03d"), N), TEXT("Water/Surface/River"), FVector((X0 + X1) / 2.0, (Y0 + Y1) / 2.0, (Z0 + Z1) / 2.0),
					FVector(Length, Width, 1.0), TEXT("Water"), Plane, FRotator(Pitch, Yaw, 0), true, false);
				++N;
			}

			// Reservoir: bounded by the basin's rising sides, cut at the dam crest road (the dam's centre line).
			const FJsonPtr R = Water->Get(TEXT("reservoir"));
			const double Rx0 = At(R, 0), Ry0 = At(R, 1), Rx1 = At(R, 2), Ry1 = At(R, 3);
			const double Level = Num(Water, TEXT("reservoir_level"));
			const double DamY = Num(River, TEXT("dam_y"));
			const double Ax = (Rx1 - Rx0) / 2.0 * 1.3, Ay = (Ry1 - Ry0) / 2.0 * 1.3;
			const double North = (Ry0 + Ry1) / 2.0 + Ay;
			Visual(TEXT("Water_ReservoirSurface"), TEXT("Water/Surface"), FVector((Rx0 + Rx1) / 2.0, (DamY + North) / 2.0, Level), FVector(2 * Ax, North - DamY, 1.0),
				TEXT("Water"), Plane, FRotator::ZeroRotator, false, false);
			OCEd::Log(Tag, FString::Printf(TEXT("water surfaces: sea, reservoir and %d river sections (none steeper than %.0f deg)"), N, CascadeDeg));
		}

		void BuildScenery(const FJsonPtr& Layout)
		{
			const double Ox = At(Layout->Get(TEXT("origin")), 0), Oy = At(Layout->Get(TEXT("origin")), 1);
			const double Sx = At(Layout->Get(TEXT("size")), 0), Sy = At(Layout->Get(TEXT("size")), 1);
			const double NorthEdge = Oy + Sy, EastEdge = Ox + Sx;
			OCLayout::FPyRandom Rng(1951);
			int32 Count = 0;

			// Distant peaks: a ragged ridge 1.5-6 km behind Monte Ferrale. Draws follow the Python order.
			for (int32 i = 0; i < 16; ++i)
			{
				const double X = Ox - 2500.0 + i * (Sx + 5000.0) / 15.0 + Rng.Uniform(-200, 200);
				const double H = Rng.Uniform(700.0, 1500.0);
				const double R = H * Rng.Uniform(1.4, 2.2);
				const double Y = NorthEdge + R + Rng.Uniform(300.0, 2500.0);   // the whole peak stays behind the map edge
				const double Depth = 2 * R * Rng.Uniform(0.7, 1.0);
				const double Yaw = Rng.Uniform(0, 360);
				Scan(FString::Printf(TEXT("Scenery_Peak_%02d"), i), TEXT("Scenery/Peaks"), (i % 2) ? BoulderA : BoulderB, X, Y, -80.0,
					FVector(2 * R, Depth, H + 80.0), TEXT("Terrain"), Yaw);
				++Count;
			}

			// The coast continues east: low hills near the sea rising to the mountains in the north.
			const double Yaws[] = { -8.0, 0.0, 172.0, 180.0 };
			for (int32 i = 0; i < 12; ++i)
			{
				const double Y = Oy + 250.0 + i * (Sy + 600.0) / 11.0;
				const double T = FMath::Clamp((Y - 300.0) / (Sy - 300.0), 0.0, 1.0);
				const double H = 40.0 + 520.0 * T * T + Rng.Uniform(-20, 40);
				const double SizeX = Rng.Uniform(900, 1400);
				const double X = EastEdge + SizeX / 2.0 + Rng.Uniform(60.0, 300.0);   // the whole hill stays past the map edge
				const double SizeY = Rng.Uniform(500, 800);
				const double Yaw = Yaws[Rng.RandBelow(4)];
				Scan(FString::Printf(TEXT("Scenery_EastHill_%02d"), i), TEXT("Scenery/EastCoast"), (i % 2) ? BoulderB : BoulderA, X, Y, -0.3 * H,
					FVector(SizeX, SizeY, 1.3 * H), TEXT("Hill"), Yaw);
				++Count;
			}

			// Isola Ventura: a rocky island ~3.5 km offshore with an old sea fortress (STORY.md).
			const double Ix = Ox + Sx * 0.62, Iy = Oy - 3500.0;
			const struct { double Dx, Dy; FVector Size; double Yaw; } Parts[] = {
				{ 0, 0, FVector(2200, 900, 2 * 70), 0 }, { -450, 80, FVector(900, 600, 2 * 160), 10 }, { 520, -60, FVector(700, 450, 2 * 110), -15 } };
			for (int32 K = 0; K < 3; ++K)
			{
				const auto& P = Parts[K];
				Scan(FString::Printf(TEXT("Scenery_IsolaVentura_Rock%d"), K), TEXT("Scenery/IsolaVentura"), (K % 2) ? BoulderA : BoulderB, Ix + P.Dx, Iy + P.Dy,
					-P.Size.Z * 0.12, FVector(P.Size.X, P.Size.Y, P.Size.Z * 0.62), TEXT("Terrain"), P.Yaw);
			}
			const double FortZ = Parts[1].Size.Z * (0.5 - 0.12) - 6.0;   // top of the western rock, fortress sunk 6 m
			Visual(TEXT("Scenery_IsolaVentura_Fortress"), TEXT("Scenery/IsolaVentura"), FVector(Ix - 450, Iy + 80, FortZ + 12), FVector(110, 70, 24), TEXT("Landmark"), Cube,
				FRotator::ZeroRotator, false, false);
			Visual(TEXT("Scenery_IsolaVentura_Keep"), TEXT("Scenery/IsolaVentura"), FVector(Ix - 480, Iy + 90, FortZ + 30), FVector(24, 24, 36), TEXT("Landmark"), Cube,
				FRotator::ZeroRotator, false, false);
			Count += 3 + 2;
			OCEd::Log(Tag, FString::Printf(TEXT("scenery: %d pieces (peaks, east coast, Isola Ventura)"), Count));
		}
	}

	void BuildWater(const FJsonPtr& Layout)
	{
		BuildSurfaces(Layout);
		BuildScenery(Layout);
	}
}
