// Dev-only road-following autopilot for the driven vehicle, used to check the world end to end:
// that every route is drivable (slopes, bridges, the dam, the pass road) and that World Partition
// streams ground in ahead of a car at top speed. Compiled out of Shipping.
//
//   oc.dev.route X0 Y0 X1 Y1 [X2 Y2 ...] [kmh=N] [quit]   drive via waypoints (layout metres), N = speed cap;
//                                                        quit exits the game when the route ends
//   oc.dev.route stop
//
// The route is the fastest road path (with the gates as they are now) between the nodes nearest
// each waypoint, driven along the lane graph. Results are logged with the prefix "OCDEV route":
// progress every 10 s, every problem as it happens (STUCK, FLIPPED, OFFROUTE, NOGROUND, WRECKED),
// and a DONE / ABORTED summary.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "OCLog.h"
#include "Progression/OCProgressionSubsystem.h"
#include "Vehicles/OCVehicle.h"
#include "World/OCLaneGraph.h"
#include "World/OCRoadGraph.h"

namespace OCAutoDrive
{
	constexpr double LateralAccel = 4.0;    // m/s^2 allowed in corners
	constexpr double BrakeDecel = 4.0;      // m/s^2 planned braking (the coupe manages a little more)
	constexpr double StuckSeconds = 5.0;
	constexpr double OffRouteM = 15.0;
	constexpr int32 MaxRecoveries = 8;

	struct FPoint
	{
		FVector Pos;
		double Along = 0.0;     // m from the route start
		double MaxSpeed = 1e9;  // m/s allowed at this point (corners)
		double Limit = 1e9;     // m/s speed limit of the road from this point on (the segment's SpeedKmh)
	};

	enum class EState : uint8 { Idle, Boarding, Settling, Driving };

	struct FDriver
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AOCVehicle> Vehicle;
		TWeakObjectPtr<APawn> Walker;   // on foot at the start: gets a car once ground streams in
		TArray<FPoint> Route;
		FTSTicker::FDelegateHandle Ticker;
		EState State = EState::Idle;
		double CapMps = 70.0;
		int32 Seg = 0;               // current route segment (Route[Seg] -> Route[Seg + 1])
		double Clock = 0.0, SettleClock = 0.0, SlowClock = 0.0, FlipClock = 0.0, GroundClock = 0.0, LogClock = 0.0;
		double DrivenM = 0.0, DriveSeconds = 0.0, MaxKmh = 0.0;
		FVector LastPos = FVector::ZeroVector;
		int32 Stuck = 0, Flipped = 0, OffRoute = 0, NoGround = 0, Recoveries = 0;
		bool bOffRoute = false;
		bool bInTick = false;
		bool bQuitWhenDone = false;
		bool bGrounded = false;
		double WorstGroundAheadM = 1e9;  // nearest the car came to a stretch with no ground loaded

		bool Tick(float Dt);
		bool Board(float Dt);
		void Place(int32 AtSeg);
		void Finish(const TCHAR* How);
		bool HasGround(const FVector& At, FHitResult* OutHit = nullptr) const;
		FVector PointAt(double AlongM) const;
		double TargetSpeed(double AlongM) const;
	};

	static FDriver GDriver;

	double SegAlong(const TArray<FPoint>& R, int32 i, const FVector& P, double& OutT)
	{
		const FVector A = R[i].Pos, B = R[i + 1].Pos;
		const FVector AB = B - A;
		const double L2 = FMath::Max(1.0, AB.SizeSquared());
		OutT = FVector::DotProduct(P - A, AB) / L2;
		return R[i].Along + FMath::Clamp(OutT, 0.0, 1.0) * (R[i + 1].Along - R[i].Along);
	}

	FVector FDriver::PointAt(double AlongM) const
	{
		for (int32 i = 0; i + 1 < Route.Num(); ++i)
		{
			if (AlongM <= Route[i + 1].Along)
			{
				const double Span = FMath::Max(0.01, Route[i + 1].Along - Route[i].Along);
				return FMath::Lerp(Route[i].Pos, Route[i + 1].Pos, FMath::Clamp((AlongM - Route[i].Along) / Span, 0.0, 1.0));
			}
		}
		return Route.Last().Pos;
	}

	double FDriver::TargetSpeed(double AlongM) const
	{
		// Slowest of: the cap, and every corner ahead reachable with planned braking.
		// Speed limits count like corners: the current road's limit now, the next ones braked for.
		double V = FMath::Min(CapMps, Route.IsValidIndex(Seg) ? Route[Seg].Limit : CapMps);
		for (int32 i = Seg; i < Route.Num(); ++i)
		{
			const double D = Route[i].Along - AlongM;
			if (D < -1.0)
			{
				continue;
			}
			const double Allowed = FMath::Min(Route[i].MaxSpeed, Route[i].Limit);
			const double Reach = FMath::Sqrt(FMath::Square(Allowed) + 2.0 * BrakeDecel * FMath::Max(0.0, D));
			V = FMath::Min(V, Reach);
			if (D > 400.0)
			{
				break;
			}
		}
		const double ToEnd = Route.Last().Along - AlongM;
		return FMath::Min(V, FMath::Sqrt(2.0 * BrakeDecel * FMath::Max(0.0, ToEnd)) + 1.0);
	}

	bool FDriver::HasGround(const FVector& At, FHitResult* OutHit) const
	{
		UWorld* W = World.Get();
		if (!W)
		{
			return false;
		}
		FHitResult Hit;
		FCollisionQueryParams Q(TEXT("OCAutoDrive"), false, Vehicle.Get());
		Q.AddIgnoredActor(Walker.Get());
		const bool bHit = W->LineTraceSingleByChannel(Hit, At + FVector(0, 0, 5000), At - FVector(0, 0, 5000), ECC_WorldStatic, Q);
		if (OutHit)
		{
			*OutHit = Hit;
		}
		return bHit;
	}

	/** Teleport a vehicle like AOCVehicle::ResetUpright does (TeleportPhysics, then zero velocities). */
	void TeleportCar(AOCVehicle* V, const FVector& To, const FRotator& Rot)
	{
		V->SetActorLocationAndRotation(To, Rot, false, nullptr, ETeleportType::TeleportPhysics);
		V->GetMesh()->SetPhysicsLinearVelocity(FVector::ZeroVector);
		V->GetMesh()->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}

	/** Teleport to the start of route segment AtSeg, facing along it, and wait for ground to stream in.
	 *  Moves on to a later segment while the car would stand inside something there (a building corner
	 *  on the lane at an alley junction): depenetration from inside geometry wrecked the car in 0.1 s. */
	void FDriver::Place(int32 AtSeg)
	{
		AOCVehicle* V = Vehicle.Get();
		if (!V)
		{
			return;
		}
		Seg = FMath::Clamp(AtSeg, 0, Route.Num() - 2);
		if (UWorld* W = World.Get())
		{
			const FVector Extent = V->GetMesh()->Bounds.BoxExtent;
			FCollisionQueryParams Q(TEXT("OCAutoDrivePlace"), false, V);
			Q.AddIgnoredActor(Walker.Get());
			for (int32 Try = 0; Try < 8 && Seg < Route.Num() - 2; ++Try)
			{
				FHitResult Ground;
				if (!HasGround(Route[Seg].Pos, &Ground))
				{
					break;  // nothing streamed in yet: Settling waits for the ground and sets the car down
				}
				const FVector Dir = (Route[Seg + 1].Pos - Route[Seg].Pos).GetSafeNormal2D();
				const FQuat Rot = FRotator(0.f, Dir.Rotation().Yaw, 0.f).Quaternion();
				const FVector Centre = Ground.ImpactPoint + FVector(0, 0, Extent.Z + 40.0);
				TArray<FOverlapResult> Hits;
				if (!W->OverlapMultiByChannel(Hits, Centre, Rot, ECC_WorldStatic, FCollisionShape::MakeBox(Extent), Q))
				{
					break;
				}
				FString What;
				for (const FOverlapResult& H : Hits)
				{
					if (H.bBlockingHit && H.GetActor())
					{
						What += (What.IsEmpty() ? TEXT("") : TEXT(", ")) + H.GetActor()->GetActorNameOrLabel();
					}
				}
				UE_LOG(LogOpenCity, Display, TEXT("OCDEV route: segment %d starts inside geometry at %s (%s), placing one segment on"), Seg, *Route[Seg].Pos.ToCompactString(), *What);
				++Seg;
			}
		}
		const FVector From = Route[Seg].Pos;
		const FVector Dir = (Route[Seg + 1].Pos - From).GetSafeNormal2D();
		TeleportCar(V, From + FVector(0, 0, 150), FRotator(0.f, Dir.Rotation().Yaw, 0.f));
		State = EState::Settling;
		SettleClock = 0.0;
		bGrounded = false;
		SlowClock = FlipClock = 0.0;
		LastPos = V->GetActorLocation();
	}

	void FDriver::Finish(const TCHAR* How)
	{
		if (AOCVehicle* V = Vehicle.Get())
		{
			if (UChaosVehicleMovementComponent* M = V->GetVehicleMovementComponent())
			{
				// Handbrake, not brake: with auto-reverse, a held brake at a standstill reverses the car.
				M->SetThrottleInput(0.f);
				M->SetSteeringInput(0.f);
				M->SetBrakeInput(0.f);
				M->SetHandbrakeInput(true);
			}
		}
		const double Pct = Route.Num() > 1 ? 100.0 * Route[FMath::Min(Seg + 1, Route.Num() - 1)].Along / FMath::Max(1.0, Route.Last().Along) : 0.0;
		UE_LOG(LogOpenCity, Display,
			TEXT("OCDEV route %s: route %.0f m, reached %.0f%%, driven %.0f m in %.0f s (avg %.0f km/h, max %.0f km/h); ")
			TEXT("stuck %d, flipped %d, offroute %d, noground %d, recoveries %d; nearest unloaded ground ahead %s"),
			How, Route.Num() ? Route.Last().Along : 0.0, Pct, DrivenM, DriveSeconds,
			DriveSeconds > 0.0 ? DrivenM / DriveSeconds * 3.6 : 0.0, MaxKmh, Stuck, Flipped, OffRoute, NoGround, Recoveries,
			WorstGroundAheadM < 1e8 ? *FString::Printf(TEXT("%.0f m"), WorstGroundAheadM) : TEXT("never"));
		State = EState::Idle;
		if (bQuitWhenDone)
		{
			FPlatformMisc::RequestExit(false, TEXT("oc.dev.route quit"));
		}
		// From inside Tick, returning false removes the ticker; otherwise remove it here.
		if (Ticker.IsValid() && !bInTick)
		{
			FTSTicker::RemoveTicker(Ticker);
		}
		Ticker.Reset();
	}

	bool FDriver::Tick(float Dt)
	{
		if (State == EState::Idle)
		{
			return false;
		}
		TGuardValue<bool> InTick(bInTick, true);
		if (State == EState::Boarding)
		{
			return Board(Dt);
		}
		AOCVehicle* V = Vehicle.Get();
		UChaosVehicleMovementComponent* M = V ? V->GetVehicleMovementComponent() : nullptr;
		if (!World.IsValid() || !M)
		{
			Finish(TEXT("ABORTED (vehicle or world gone)"));
			return false;
		}
		Clock += Dt;
		const FVector Pos = V->GetActorLocation();

		if (State == EState::Settling)
		{
			// Hold the car above the start until World Partition has streamed in ground there, then
			// set it down on the surface (never inside geometry: depenetration reads as a crash).
			SettleClock += Dt;
			FHitResult Ground;
			if (!bGrounded && !HasGround(Route[Seg].Pos, &Ground))
			{
				TeleportCar(V, Route[Seg].Pos + FVector(0, 0, 150), V->GetActorRotation());
				if (SettleClock > 30.0)
				{
					Finish(TEXT("ABORTED (no ground streamed in at the start after 30 s)"));
					return false;
				}
				return true;
			}
			if (!bGrounded)
			{
				bGrounded = true;
				TeleportCar(V, Ground.ImpactPoint + FVector(0, 0, 60), V->GetActorRotation());
				UE_LOG(LogOpenCity, Display, TEXT("OCDEV route: set down on %s at %s"), *GetNameSafe(Ground.GetActor()), *Ground.ImpactPoint.ToCompactString());
			}
			M->SetThrottleInput(0.f);
			M->SetBrakeInput(1.f);
			if (SettleClock > 1.5)
			{
				State = EState::Driving;
				LastPos = Pos;
			}
			return true;
		}

		// Progress along the route.
		double T = 0.0;
		double Along = SegAlong(Route, Seg, Pos, T);
		while (T > 1.0 && Seg + 2 < Route.Num())
		{
			++Seg;
			Along = SegAlong(Route, Seg, Pos, T);
		}
		const double SpeedMps = V->GetSpeedKmh() / 3.6;
		DriveSeconds += Dt;
		DrivenM += FVector::Dist2D(Pos, LastPos) / 100.0;
		LastPos = Pos;
		MaxKmh = FMath::Max(MaxKmh, SpeedMps * 3.6);

		if (V->IsWrecked())
		{
			Finish(TEXT("ABORTED (WRECKED)"));
			return false;
		}
		if (Route.Last().Along - Along < 8.0 && Seg + 2 >= Route.Num())
		{
			Finish(TEXT("DONE"));
			return false;
		}

		// Pure pursuit on a point ahead along the lane.
		const double Lookahead = FMath::Clamp(SpeedMps * 0.9, 7.0, 35.0);
		const FVector Target = PointAt(Along + Lookahead);
		const FVector Local = V->GetActorTransform().InverseTransformPosition(Target);
		const double SteerDeg = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
		const double Want = TargetSpeed(Along);
		const double Err = Want - SpeedMps;
		M->SetHandbrakeInput(false);
		M->SetSteeringInput(static_cast<float>(FMath::Clamp(SteerDeg / 25.0, -1.0, 1.0)));
		M->SetThrottleInput(static_cast<float>(FMath::Clamp(Err / 4.0, 0.0, 1.0)));
		M->SetBrakeInput(static_cast<float>(Err < -1.5 ? FMath::Clamp(-Err / 4.0, 0.0, 1.0) : 0.0));

		// Problems.
		const FVector OnRoute = PointAt(Along);
		const double Lateral = FVector::Dist2D(Pos, OnRoute) / 100.0;
		if (Lateral > OffRouteM && !bOffRoute)
		{
			++OffRoute;
			UE_LOG(LogOpenCity, Warning, TEXT("OCDEV route OFFROUTE %.0f m from the lane at %s (%.0f m along)"), Lateral, *Pos.ToCompactString(), Along);
		}
		bOffRoute = Lateral > OffRouteM * 0.5 && (bOffRoute || Lateral > OffRouteM);

		bool bRecover = false;
		SlowClock = SpeedMps < 0.7 ? SlowClock + Dt : 0.0;
		if (SlowClock > StuckSeconds)
		{
			++Stuck;
			UE_LOG(LogOpenCity, Warning, TEXT("OCDEV route STUCK at %s (%.0f m along, segment %d)"), *Pos.ToCompactString(), Along, Seg);
			if (Stuck == 1)
			{
				GEngine->Exec(World.Get(), TEXT("HighResShot 1280x720")); // see what the car ran into
			}
			bRecover = true;
		}
		FlipClock = V->GetActorUpVector().Z < 0.3 ? FlipClock + Dt : 0.0;
		if (FlipClock > 2.0)
		{
			++Flipped;
			UE_LOG(LogOpenCity, Warning, TEXT("OCDEV route FLIPPED at %s (%.0f m along)"), *Pos.ToCompactString(), Along);
			bRecover = true;
		}
		if (bRecover)
		{
			if (++Recoveries > MaxRecoveries)
			{
				Finish(TEXT("ABORTED (too many recoveries)"));
				return false;
			}
			Place(FMath::Min(Seg + 1, Route.Num() - 2));
			return true;
		}

		// Streaming: ground must exist under the car and over the next 3 s of road.
		GroundClock += Dt;
		if (GroundClock > 0.2)
		{
			GroundClock = 0.0;
			const double AheadM = FMath::Max(30.0, SpeedMps * 3.0);
			for (double D = 0.0; D <= AheadM; D += 15.0)
			{
				if (!HasGround(PointAt(Along + D)))
				{
					WorstGroundAheadM = FMath::Min(WorstGroundAheadM, D);
					++NoGround;
					UE_LOG(LogOpenCity, Warning, TEXT("OCDEV route NOGROUND %.0f m ahead at %.0f km/h (car at %s, %.0f m along)"),
						D, SpeedMps * 3.6, *Pos.ToCompactString(), Along);
					break;
				}
			}
		}

		LogClock += Dt;
		if (LogClock > 10.0)
		{
			LogClock = 0.0;
			UE_LOG(LogOpenCity, Display, TEXT("OCDEV route progress %.0f%% (%.0f of %.0f m) at %.0f km/h, want %.0f, at %s"),
				100.0 * Along / FMath::Max(1.0, Route.Last().Along), Along, Route.Last().Along, SpeedMps * 3.6, Want * 3.6, *Pos.ToCompactString());
		}
		return true;
	}

	/** Corner speed at each interior point: v = sqrt(a * R), R from the turn angle over the shorter neighbour. */
	void ComputeCornerSpeeds(TArray<FPoint>& R)
	{
		for (int32 i = 1; i + 1 < R.Num(); ++i)
		{
			const FVector In = (R[i].Pos - R[i - 1].Pos).GetSafeNormal2D();
			const FVector Out = (R[i + 1].Pos - R[i].Pos).GetSafeNormal2D();
			const double Turn = FMath::Acos(FMath::Clamp(FVector::DotProduct(In, Out), -1.0, 1.0));
			if (Turn < FMath::DegreesToRadians(6.0))
			{
				continue;
			}
			const double Span = FMath::Clamp(FMath::Min(R[i].Along - R[i - 1].Along, R[i + 1].Along - R[i].Along), 4.0, 25.0);
			R[i].MaxSpeed = FMath::Max(3.0, FMath::Sqrt(LateralAccel * Span / Turn));
		}
	}

	/** On foot: hold the player at the route start until ground streams in, then spawn the coupe there and drive it. */
	bool FDriver::Board(float Dt)
	{
		APawn* P = Walker.Get();
		UWorld* W = World.Get();
		if (!P || !W)
		{
			Finish(TEXT("ABORTED (player or world gone)"));
			return false;
		}
		SettleClock += Dt;
		const FVector Start = Route[0].Pos;
		FHitResult Ground;
		if (!HasGround(Start, &Ground))
		{
			P->SetActorLocation(Start + FVector(0, 0, 300), false, nullptr, ETeleportType::TeleportPhysics);
			if (SettleClock > 30.0)
			{
				Finish(TEXT("ABORTED (no ground streamed in at the start after 30 s)"));
				return false;
			}
			return true;
		}
		UClass* Class = LoadClass<AOCVehicle>(nullptr, TEXT("/Game/Vehicles/Blueprints/BP_OCVehicle_Coupe.BP_OCVehicle_Coupe_C"));
		const FVector Dir = (Route[1].Pos - Start).GetSafeNormal2D();
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AOCVehicle* V = Class ? W->SpawnActor<AOCVehicle>(Class, Ground.ImpactPoint + FVector(0, 0, 60), FRotator(0.f, Dir.Rotation().Yaw, 0.f), SP) : nullptr;
		if (ACharacter* C = Cast<ACharacter>(P))
		{
			C->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
		const bool bIn = V && V->Interact(P) && W->GetFirstPlayerController() && W->GetFirstPlayerController()->GetPawn() == V;
		UE_LOG(LogOpenCity, Display, TEXT("OCDEV route: spawned a coupe on %s and %s"), *GetNameSafe(Ground.GetActor()), bIn ? TEXT("got in") : TEXT("could NOT get in"));
		if (!bIn)
		{
			Finish(TEXT("ABORTED (could not board a vehicle)"));
			return false;
		}
		Vehicle = V;
		Place(0);
		return true;
	}

	void Route(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.IsValidIndex(0) && Args[0].Equals(TEXT("stop"), ESearchCase::IgnoreCase))
		{
			if (GDriver.State != EState::Idle)
			{
				GDriver.Finish(TEXT("ABORTED (stopped)"));
			}
			return;
		}
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		AOCVehicle* V = PC ? Cast<AOCVehicle>(PC->GetPawn()) : nullptr;
		APawn* OnFoot = PC && !V ? PC->GetPawn() : nullptr;
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		const UOCProgressionSubsystem* Prog = GI ? GI->GetSubsystem<UOCProgressionSubsystem>() : nullptr;
		const FOCRoadGraph* Roads = Prog ? Prog->GetRoadGraph() : nullptr;
		TArray<FVector> Waypoints;
		double CapKmh = 250.0;
		for (int32 i = 0; i < Args.Num(); ++i)
		{
			if (Args[i].Equals(TEXT("quit"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (Args[i].StartsWith(TEXT("kmh=")))
			{
				CapKmh = FCString::Atod(*Args[i].Mid(4));
			}
			else if (i + 1 < Args.Num())
			{
				Waypoints.Add(FOCRoadGraph::LayoutToWorld(FCString::Atod(*Args[i]), FCString::Atod(*Args[i + 1]), 0.0));
				++i;
			}
		}
		if ((!V && !OnFoot) || !Roads || Waypoints.Num() < 2)
		{
			UE_LOG(LogOpenCity, Display, TEXT("OCDEV route: needs a driven vehicle, a road graph and 2+ waypoints: oc.dev.route X0 Y0 X1 Y1 [...] [kmh=N] | stop"));
			return;
		}
		if (GDriver.State != EState::Idle)
		{
			GDriver.Finish(TEXT("ABORTED (replaced)"));
		}

		// Node path through every waypoint with the gates as they are now.
		const TSet<FName> Closed = Prog->GetClosedGates();
		TArray<int32> Nodes;
		for (int32 w = 0; w + 1 < Waypoints.Num(); ++w)
		{
			// Waypoints are map x, y: match in plan, or stacked hairpin legs snap to the lower one.
			const int32 A = Roads->FindNearestNodePlanar(Waypoints[w]);
			const int32 B = Roads->FindNearestNodePlanar(Waypoints[w + 1]);
			// Each leg continues from the way the previous one arrived: reversing at a waypoint in
			// the middle of a road needs a U-turn, which the car can't make (the tour stuck there).
			TArray<int32> Leg = Roads->FindPathNoUTurn(A, Nodes.Num() >= 2 ? Nodes[Nodes.Num() - 2] : INDEX_NONE, B, Closed);
			if (Leg.IsEmpty())
			{
				UE_LOG(LogOpenCity, Warning, TEXT("OCDEV route: no open road from waypoint %d to %d (closed gates: %d)"), w, w + 1, Closed.Num());
				return;
			}
			if (!Nodes.IsEmpty())
			{
				Leg.RemoveAt(0);
			}
			Nodes.Append(Leg);
		}

		// Follow the innermost lane of each segment in the travel direction.
		FOCLaneGraph Lanes;
		Lanes.Build(*Roads);
		TArray<FPoint> R;
		auto Add = [&R](const FVector& P, double LimitMps)
		{
			if (!R.IsEmpty() && FVector::Dist(R.Last().Pos, P) < 50.0)
			{
				return;
			}
			const double Along = R.IsEmpty() ? 0.0 : R.Last().Along + FVector::Dist(R.Last().Pos, P) / 100.0;
			FPoint Pt{ P, Along };
			Pt.Limit = LimitMps;
			R.Add(Pt);
		};
		for (int32 i = 0; i + 1 < Nodes.Num(); ++i)
		{
			int32 Seg = INDEX_NONE;
			for (int32 s = 0; s < Roads->NumSegments() && Seg == INDEX_NONE; ++s)
			{
				const FOCRoadSegment& S = Roads->GetSegments()[s];
				if ((S.A == Nodes[i] && S.B == Nodes[i + 1]) || (S.B == Nodes[i] && S.A == Nodes[i + 1]))
				{
					Seg = s;
				}
			}
			const TArray<int32> SegLanes = Lanes.GetSegmentLanes(Seg, Nodes[i]);
			if (SegLanes.IsEmpty())
			{
				continue;
			}
			const FOCLane& L = Lanes.GetLanes()[SegLanes[0]];
			const double Limit = FMath::Max(10.0, static_cast<double>(Roads->GetSegments()[Seg].SpeedKmh)) / 3.6;
			Add(L.Start, Limit);
			Add(L.End, Limit);
		}
		if (R.Num() < 2)
		{
			UE_LOG(LogOpenCity, Warning, TEXT("OCDEV route: route too short"));
			return;
		}
		ComputeCornerSpeeds(R);

		GDriver = FDriver();
		GDriver.World = World;
		GDriver.Vehicle = V;
		GDriver.Walker = OnFoot;
		GDriver.Route = MoveTemp(R);
		GDriver.CapMps = CapKmh / 3.6;
		GDriver.bQuitWhenDone = Args.ContainsByPredicate([](const FString& A) { return A.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
		// A core ticker, every frame, only while a route is being driven: steering needs per-frame input.
		GDriver.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float Dt) { return GDriver.Tick(Dt); }));
		if (V)
		{
			GDriver.Place(0);
		}
		else
		{
			// On foot: freeze the player at the start (so they don't fall before ground loads) and board there.
			if (ACharacter* C = Cast<ACharacter>(OnFoot))
			{
				C->GetCharacterMovement()->DisableMovement();
			}
			GDriver.State = EState::Boarding;
		}
		UE_LOG(LogOpenCity, Display, TEXT("OCDEV route START: %d waypoints, %d road nodes, %.0f m, cap %.0f km/h, %d gates closed"),
			Waypoints.Num(), Nodes.Num(), GDriver.Route.Last().Along, CapKmh, Closed.Num());
	}

	FAutoConsoleCommandWithWorldAndArgs CmdRoute(TEXT("oc.dev.route"),
		TEXT("Autopilot the driven vehicle along roads: X0 Y0 X1 Y1 [X2 Y2 ...] [kmh=N] [quit] (layout metres), or 'stop'."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Route));
}

#endif // !UE_BUILD_SHIPPING
