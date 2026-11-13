#include "Traffic/OCTrafficSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "OCLog.h"
#include "Progression/OCProgressionSubsystem.h"
#include "Scalability.h"
#include "Traffic/OCTrafficSettings.h"
#include "Traffic/OCTrafficVehicle.h"
#include "Vehicles/OCVehicleData.h"
#include "World/OCLaneGraph.h"
#include "World/OCRoadGraphAsset.h"

namespace OCTrafficCVars
{
	TAutoConsoleVariable<int32> Enabled(TEXT("oc.Traffic.Enabled"), 1, TEXT("Traffic around the player (M5): 1 on, 0 off (cars go back to the pool)."));
	TAutoConsoleVariable<int32> Budget(TEXT("oc.Traffic.Budget"), -1, TEXT("Traffic cars: -1 = by scalability (OpenCity Traffic settings), else this many."));
	constexpr int32 MaxPool = 48;
}

bool UOCTrafficSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && Super::ShouldCreateSubsystem(Outer);
}

void UOCTrafficSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const UGameInstance* GI = InWorld.GetGameInstance();
	const UOCProgressionSubsystem* Prog = GI ? GI->GetSubsystem<UOCProgressionSubsystem>() : nullptr;
	const UOCRoadGraphAsset* Asset = Prog ? Prog->GetRoadGraphAsset() : nullptr;
	if (!Asset || Asset->GetLanes().NumLanes() == 0)
	{
		return;   // a map without a road network (test maps): no traffic
	}
	const UOCTrafficSettings* Settings = GetDefault<UOCTrafficSettings>();
	for (const TSoftObjectPtr<UOCVehicleData>& M : Settings->Models)
	{
		if (UOCVehicleData* D = M.LoadSynchronous())
		{
			Models.Add(D);
		}
	}
	if (Models.IsEmpty())
	{
		UE_LOG(LogOpenCity, Warning, TEXT("Traffic: no vehicle models in OpenCity Traffic settings; no traffic"));
		return;
	}
	Lanes = &Asset->GetLanes();
	Sim.Build(*Lanes);
	Rng.Initialize(0x7AFF1C);
	// The whole pool at load: play never spawns an actor (ARCHITECTURE.md, population budgets).
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	const int32 PoolSize = FMath::Min(OCTrafficCVars::MaxPool, FMath::CeilToInt32(Settings->MediumBudget * 1.5f));
	for (int32 i = 0; i < PoolSize; ++i)
	{
		AOCTrafficVehicle* V = InWorld.SpawnActor<AOCTrafficVehicle>(FVector(0.0, 0.0, -100000.0), FRotator::ZeroRotator, Params);
		if (!V)
		{
			continue;
		}
		UOCVehicleData* Model = Models[i % Models.Num()];
		const TArray<FLinearColor>& Palette = Model->PaintColors;
		V->SetModel(Model, Palette.IsEmpty() ? FLinearColor(0.5f, 0.5f, 0.5f) : Palette[Rng.RandRange(0, Palette.Num() - 1)],
			Rng.FRandRange(Model->DirtRange.X, Model->DirtRange.Y));
		V->SetActive(false);
		Pool.Add(V);
	}
	bReady = !Pool.IsEmpty();
	UE_LOG(LogOpenCity, Display, TEXT("Traffic: %d lanes, %d links, pool of %d cars (%d models), budget %d"),
		Lanes->NumLanes(), Lanes->NumLinks(), Pool.Num(), Models.Num(), GetBudget());
}

void UOCTrafficSubsystem::Deinitialize()
{
	bReady = false;
	Pool.Reset();
	Sim.Reset();
	Super::Deinitialize();
}

TStatId UOCTrafficSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOCTrafficSubsystem, STATGROUP_Tickables);
}

int32 UOCTrafficSubsystem::GetBudget() const
{
	if (OCTrafficCVars::Enabled.GetValueOnGameThread() == 0)
	{
		return 0;
	}
	const int32 Forced = OCTrafficCVars::Budget.GetValueOnGameThread();
	if (Forced >= 0)
	{
		return FMath::Min(Forced, Pool.Num());
	}
	// Traffic and crowd by preset: Low 50%, Medium 100%, High 120%, Ultra 150% (ARCHITECTURE.md section 8).
	static const float Scale[] = { 0.5f, 1.0f, 1.2f, 1.5f };
	const int32 Level = FMath::Clamp(Scalability::GetQualityLevels().ViewDistanceQuality, 0, 3);
	return FMath::Min(Pool.Num(), FMath::RoundToInt32(GetDefault<UOCTrafficSettings>()->MediumBudget * Scale[Level]));
}

AOCTrafficVehicle* UOCTrafficSubsystem::FindActor(int32 SimId) const
{
	for (AOCTrafficVehicle* V : Pool)
	{
		if (V && V->SimId == SimId)
		{
			return V;
		}
	}
	return nullptr;
}

bool UOCTrafficSubsystem::InView(const FVector& At, const FVector& View, const FVector& ViewDir) const
{
	// Inside a generous cone round the view direction (a 90-degree horizontal FOV plus a car's width).
	const FVector To = At - View;
	const double Dist = To.Size();
	return Dist < 1.0 || FVector::DotProduct(To / Dist, ViewDir) > FMath::Cos(FMath::DegreesToRadians(60.0));
}

bool UOCTrafficSubsystem::GroundAt(const FVector& At, FVector& OutGround) const
{
	// Static world only (roads, landscape): other cars and people are not ground.
	FHitResult Hit;
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, At + FVector(0.0, 0.0, 400.0), At - FVector(0.0, 0.0, 800.0), Objects))
	{
		OutGround = Hit.ImpactPoint;
		return true;
	}
	return false;
}

void UOCTrafficSubsystem::Release(int32 Index)
{
	AOCTrafficVehicle* V = Pool[Index];
	if (V && V->SimId != INDEX_NONE)
	{
		Sim.Remove(V->SimId);
		V->SetActive(false);
		++Despawned;
	}
}

void UOCTrafficSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector View = PC->PlayerCameraManager->GetCameraLocation();
	const FVector ViewDir = PC->PlayerCameraManager->GetCameraRotation().Vector();

	if (const UGameInstance* GI = World->GetGameInstance())
	{
		if (const UOCProgressionSubsystem* Prog = GI->GetSubsystem<UOCProgressionSubsystem>())
		{
			Sim.SetClosedGates(Prog->GetClosedGates());
		}
	}

	PopulateIn -= DeltaTime;
	if (PopulateIn <= 0.f)
	{
		PopulateIn = 0.5f;
		Populate(View, ViewDir);
	}
	if (Sim.GetCars().IsEmpty())
	{
		return;
	}

	// Things outside the sim ahead of each car: a short box sweep along its heading at bumper height.
	for (AOCTrafficVehicle* V : Pool)
	{
		if (!V || V->SimId == INDEX_NONE)
		{
			continue;
		}
		const FOCTrafficCar* Car = Sim.FindCar(V->SimId);
		if (!Car)
		{
			continue;
		}
		const FVector Fwd = V->GetActorForwardVector();
		const double Reach = FMath::Clamp(Car->Speed * 1.6 + 400.0, 600.0, 3000.0);
		const FVector From = V->GetActorLocation() + Fwd * (V->GetHalfLength() + 30.0);
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_Vehicle);
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(OCTrafficProbe), false, V);
		if (World->SweepSingleByObjectType(Hit, From, From + Fwd * Reach, FQuat::Identity, Objects, FCollisionShape::MakeBox(FVector(10.0, 80.0, 40.0)), Q))
		{
			Sim.SetObstacle(V->SimId, Hit.Distance);
		}
	}
	Sim.Step(FMath::Min(DeltaTime, 0.05f), Rng);
	PlaceActors(DeltaTime);
}

void UOCTrafficSubsystem::PlaceActors(float DeltaTime)
{
	for (AOCTrafficVehicle* V : Pool)
	{
		if (!V || V->SimId == INDEX_NONE)
		{
			continue;
		}
		const FOCTrafficCar* Car = Sim.FindCar(V->SimId);
		if (!Car)
		{
			V->SetActive(false);
			continue;
		}
		const FOCTrafficPose Pose = Sim.GetPose(*Car);
		const FVector Fwd2 = Pose.Forward.GetSafeNormal2D();
		const double Axle = 0.6 * V->GetHalfLength();
		// The lane line follows node heights only; the road surface has crests and dips. Sit on it per axle.
		FVector Front, Back;
		const bool bFront = GroundAt(Pose.Location + Fwd2 * Axle, Front);
		const bool bBack = GroundAt(Pose.Location - Fwd2 * Axle, Back);
		FVector Base = Pose.Location;
		FVector Dir = Fwd2;
		if (bFront && bBack)
		{
			Base = 0.5 * (Front + Back);
			Dir = (Front - Back).GetSafeNormal();
		}
		else if (bFront || bBack)
		{
			Base = bFront ? Front - Fwd2 * Axle : Back + Fwd2 * Axle;
		}
		const FRotator Rot = Dir.Rotation();
		V->SetActorLocationAndRotation(Base + Rot.RotateVector(V->GetCentreFromChassis()), Rot, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void UOCTrafficSubsystem::Populate(const FVector& View, const FVector& ViewDir)
{
	const UOCTrafficSettings* Settings = GetDefault<UOCTrafficSettings>();
	const double DespawnCm = Settings->DespawnM * 100.0;
	// Back to the pool: far and unseen, or stuck for a minute where nobody looks.
	for (int32 i = 0; i < Pool.Num(); ++i)
	{
		AOCTrafficVehicle* V = Pool[i];
		if (!V || V->SimId == INDEX_NONE)
		{
			continue;
		}
		const FOCTrafficCar* Car = Sim.FindCar(V->SimId);
		const FVector At = V->GetActorLocation();
		const double Dist = FVector::Dist(At, View);
		const bool bSeen = InView(At, View, ViewDir) && Dist < DespawnCm * 1.6;
		if (!Car || (!bSeen && Dist > DespawnCm) || (Car && Car->StoppedFor > 60.0 && !bSeen && Dist > 4000.0))
		{
			Release(i);
		}
	}
	const int32 Budget = GetBudget();
	int32 Active = 0;
	for (const AOCTrafficVehicle* V : Pool)
	{
		Active += (V && V->SimId != INDEX_NONE) ? 1 : 0;
	}
	// Over budget (preset lowered, or oc.Traffic.Enabled 0): drop the furthest unseen ones first, then any.
	while (Active > Budget)
	{
		int32 Worst = INDEX_NONE;
		double WorstScore = -1.0;
		for (int32 i = 0; i < Pool.Num(); ++i)
		{
			if (Pool[i] && Pool[i]->SimId != INDEX_NONE)
			{
				const FVector At = Pool[i]->GetActorLocation();
				const double Score = FVector::Dist(At, View) + (InView(At, View, ViewDir) ? 0.0 : 1.0e7);
				if (Score > WorstScore)
				{
					WorstScore = Score;
					Worst = i;
				}
			}
		}
		if (Worst == INDEX_NONE)
		{
			break;
		}
		Release(Worst);
		--Active;
	}
	if (Active >= Budget)
	{
		return;
	}
	// Fill: a few tries per pass on random lanes in the ring; never on a lane whose gate is closed.
	const TArray<FOCLane>& L = Lanes->GetLanes();
	const double MinCm = Settings->SpawnMinM * 100.0, MaxCm = Settings->SpawnMaxM * 100.0, SeenCm = Settings->VisibleSpawnM * 100.0;
	TSet<FName> Closed;
	if (const UGameInstance* GI = GetWorld()->GetGameInstance())
	{
		if (const UOCProgressionSubsystem* Prog = GI->GetSubsystem<UOCProgressionSubsystem>())
		{
			Closed = Prog->GetClosedGates();
		}
	}
	for (int32 Try = 0; Try < 24 && Active < Budget; ++Try)
	{
		const int32 Lane = Rng.RandRange(0, L.Num() - 1);
		const FOCLane& Ln = L[Lane];
		if (!Ln.Gate.IsNone() && Closed.Contains(Ln.Gate))
		{
			continue;
		}
		const double Len = Ln.LengthCm();
		if (Len < 1500.0)
		{
			continue;
		}
		const double S = Rng.FRandRange(600.0, Len - 300.0);
		const FVector At = Ln.Start + Ln.Direction() * S;
		const double Dist = FVector::Dist2D(At, View);
		if (Dist < MinCm || Dist > MaxCm || (Dist < SeenCm && InView(At, View, ViewDir)))
		{
			continue;
		}
		// Room on the lane, and ground already streamed in under it.
		bool bRoom = true;
		for (const FOCTrafficCar& C : Sim.GetCars())
		{
			if (C.Link == INDEX_NONE && C.Lane == Lane && FMath::Abs(C.S - S) < 2000.0)
			{
				bRoom = false;
				break;
			}
		}
		FVector Ground;
		if (!bRoom || !GroundAt(At, Ground))
		{
			++SpawnRejected;
			continue;
		}
		AOCTrafficVehicle* Free = nullptr;
		for (AOCTrafficVehicle* V : Pool)
		{
			if (V && V->SimId == INDEX_NONE)
			{
				Free = V;
				break;
			}
		}
		if (!Free)
		{
			break;
		}
		const double Speed = Ln.SpeedKmh * (100000.0 / 3600.0) * 0.6;
		Free->SimId = Sim.Spawn(Lane, S, Speed, Rng);
		Free->SetActive(true);
		++Active;
		++Spawned;
	}
	if (Active > 0)
	{
		PlaceActors(0.f);
	}
}

FString UOCTrafficSubsystem::DescribeStats() const
{
	int32 Holding = 0, Stopped = 0, Crossing = 0;
	double SpeedSum = 0.0;
	for (const FOCTrafficCar& C : Sim.GetCars())
	{
		Holding += C.bHolding ? 1 : 0;
		Stopped += C.Speed < 10.0 ? 1 : 0;
		Crossing += C.Link != INDEX_NONE ? 1 : 0;
		SpeedSum += C.Speed;
	}
	const int32 N = Sim.GetCars().Num();
	return FString::Printf(TEXT("traffic: %d cars (budget %d, pool %d), avg %.0f km/h, %d stopped, %d holding at junctions, %d crossing, conflicts %d; spawned %d, despawned %d, rejected %d"),
		N, GetBudget(), Pool.Num(), N ? SpeedSum / N * 0.036 : 0.0, Stopped, Holding, Crossing, Sim.CountConflictingCrossings(), Spawned, Despawned, SpawnRejected);
}

namespace OCTrafficDev
{
	void Stats(const TArray<FString>& Args, UWorld* World)
	{
		const UOCTrafficSubsystem* T = World ? World->GetSubsystem<UOCTrafficSubsystem>() : nullptr;
		if (!T)
		{
			return;
		}
		UE_LOG(LogOpenCity, Display, TEXT("OCDEV %s"), *T->DescribeStats());
		// "view [n]": the dev camera looks at the n-th nearest traffic car from behind and to one side (QA shots).
		const APlayerController* PC = World->GetFirstPlayerController();
		if (Args.Num() >= 1 && Args[0] == TEXT("view") && PC && PC->GetPawn())
		{
			const FVector Me = PC->GetPawn()->GetActorLocation();
			TArray<const AOCTrafficVehicle*> Near;
			for (const FOCTrafficCar& C : T->GetSim().GetCars())
			{
				if (const AOCTrafficVehicle* V = T->FindActor(C.Id))
				{
					Near.Add(V);
				}
			}
			Near.Sort([&Me](const AOCTrafficVehicle& A, const AOCTrafficVehicle& B) { return FVector::DistSquared(A.GetActorLocation(), Me) < FVector::DistSquared(B.GetActorLocation(), Me); });
			const int32 Pick = Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : 0;
			if (Near.IsValidIndex(Pick))
			{
				const AOCTrafficVehicle* V = Near[Pick];
				const FVector Target = V->GetActorLocation();
				const FVector Cam = Target - V->GetActorForwardVector() * 650.0 + V->GetActorRightVector() * 380.0 + FVector(0.0, 0.0, 220.0);
				const FRotator Look = (Target - Cam).Rotation();
				UE_LOG(LogOpenCity, Display, TEXT("OCDEV traffic view: car %d of %d at %s"), Pick, Near.Num(), *Target.ToCompactString());
				GEngine->Exec(World, *FString::Printf(TEXT("oc.dev.viewfrom %.0f %.0f %.0f %.1f %.1f 60"), Cam.X, Cam.Y, Cam.Z, Look.Pitch, Look.Yaw));
			}
		}
	}
	FAutoConsoleCommandWithWorldAndArgs CmdStats(TEXT("oc.dev.traffic"), TEXT("Log traffic stats (cars, speeds, junction holds, conflicts)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Stats));
}
