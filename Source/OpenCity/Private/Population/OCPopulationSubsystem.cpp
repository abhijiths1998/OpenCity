#include "Population/OCPopulationSubsystem.h"

#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "OCLog.h"
#include "Population/OCPedestrian.h"
#include "Progression/OCProgressionSubsystem.h"
#include "Traffic/OCTrafficSubsystem.h"
#include "World/OCLaneGraph.h"
#include "World/OCRoadGraphAsset.h"
#include "World/OCWorldStateSubsystem.h"

namespace OCPopulationCVars
{
	TAutoConsoleVariable<int32> Enabled(TEXT("oc.population.Enabled"), 1, TEXT("Pedestrians around the player (M5): 1 on, 0 off."));
	TAutoConsoleVariable<int32> Budget(TEXT("oc.population.Budget"), -1, TEXT("Peds: -1 = by scalability (OpenCity Population settings), else this many."));
	constexpr int32 MaxPool = 96;
}

bool UOCPopulationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && Super::ShouldCreateSubsystem(Outer);
}

void UOCPopulationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const UOCPopulationSettings* const Defaults = GetDefault<UOCPopulationSettings>();
	Settings = const_cast<UOCPopulationSettings*>(Defaults);
	if (!Settings) return;

	// Wire the crowd sim to the settings' threat parameters.
	FOCCrowdSim::FParams SimParams;
	SimParams.ThreatRadiusCm = Settings->ThreatRadiusM * 100.0;
	SimParams.FleeDurationS = Settings->FleeDurationS;
	Sim.SetParams(SimParams);

	Rng.Initialize(0x1C9A3F);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	const int32 PoolSize = FMath::Min(OCPopulationCVars::MaxPool, Settings->MediumBudget * 2);
	for (int32 i = 0; i < PoolSize; ++i)
	{
		AOCPedestrian* P = InWorld.SpawnActor<AOCPedestrian>(FVector(0.0, 0.0, -20000.0), FRotator::ZeroRotator, Params);
		if (P)
		{
			P->SetPooled(FTransform(FVector(0.0, 0.0, -20000.0)));
			Pool.Add(P);
		}
	}
	bReady = !Pool.IsEmpty();

	// World subsystems: query from the world (traffic, world state).
	UWorld* W = GetWorld();
	if (W)
	{
		Traffic = W->GetSubsystem<UOCTrafficSubsystem>();
		WorldState = W->GetSubsystem<UOCWorldStateSubsystem>();
		if (const UGameInstance* GI = W->GetGameInstance())
		{
			if (const UOCProgressionSubsystem* Prog = GI->GetSubsystem<UOCProgressionSubsystem>())
			{
				if (const UOCRoadGraphAsset* Asset = Prog->GetRoadGraphAsset())
				{
					Lanes = &Asset->GetLanes();
				}
			}
		}
	}

	UE_LOG(LogOpenCity, Display, TEXT("Population: pool of %d peds, budget %d"), Pool.Num(), GetBudget());
}

void UOCPopulationSubsystem::Deinitialize()
{
	bReady = false;
	Pool.Reset();
	Sim.Reset();
	Super::Deinitialize();
}

TStatId UOCPopulationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOCPopulationSubsystem, STATGROUP_Tickables);
}

int32 UOCPopulationSubsystem::GetBudget() const
{
	if (OCPopulationCVars::Enabled.GetValueOnGameThread() == 0)
	{
		return 0;
	}
	const int32 Forced = OCPopulationCVars::Budget.GetValueOnGameThread();
	if (Forced >= 0)
	{
		return FMath::Min(Forced, Pool.Num());
	}
	static const float Scale[] = { 0.5f, 1.0f, 1.2f, 1.5f };
	const int32 Level = FMath::Clamp(Scalability::GetQualityLevels().ViewDistanceQuality, 0, 3);
	return FMath::Min(Pool.Num(), FMath::RoundToInt32(Settings->MediumBudget * Scale[Level]));
}

void UOCPopulationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager) return;

	const FVector View = PC->PlayerCameraManager->GetCameraLocation();
	const FVector ViewDir = PC->PlayerCameraManager->GetCameraRotation().Vector();

	AiIn -= DeltaTime;
	if (AiIn <= 0.f)
	{
		const float AiDt = 1.0f / FMath::Max(Settings->AiRateHz, 1.f);
		GatherThreats(View);
		Sim.Step(AiDt, World->GetTimeSeconds(), Rng);
	}

	PlacePeds(DeltaTime);
	Populate(View, ViewDir);
}

FString UOCPopulationSubsystem::DescribeStats() const
{
	int32 Active = 0, Fleeing = 0;
	for (const AOCPedestrian* P : Pool)
	{
		if (!P || P->SimId == INDEX_NONE) continue;
		++Active;
		if (P->GetState() == EOCPedState::Flee) ++Fleeing;
	}
	return FString::Printf(TEXT("population: %d active (budget %d, pool %d), %d fleeing; spawned %d, despawned %d"),
		Active, GetBudget(), Pool.Num(), Fleeing, Spawned, Despawned);
}

void UOCPopulationSubsystem::ReportGunshot(const FVector& Location)
{
	if (!bReady) return;
	const FVector2D Shot(Location.X, Location.Y);
	FOCThreat T;
	T.Pos = Shot;
	T.RadiusCm = Settings->GunshotRadiusM * 100.0;
	T.Weight = 1.f;
	Sim.AddThreat(T);
	for (AOCPedestrian* P : Pool)
	{
		if (!P || P->SimId == INDEX_NONE) continue;
				const FVector2D At(P->SimLocation().X, P->SimLocation().Y);
		if (FVector2D::DistSquared(At, Shot) < FMath::Square(Settings->GunshotRadiusM * 100.0))
		{
			P->SetSimPose(P->SimLocation(), P->GetActorForwardVector(), 0.0);
		}
	}
}

bool UOCPopulationSubsystem::InView(const FVector& At, const FVector& View, const FVector& ViewDir) const
{
	const FVector To = At - View;
	const double Dist = To.Size();
	return Dist < 1.0 || FVector::DotProduct(To / Dist, ViewDir) > FMath::Cos(FMath::DegreesToRadians(60.0));
}

bool UOCPopulationSubsystem::GroundAt(const FVector& At, FVector& OutGround) const
{
	FHitResult Hit;
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, At + FVector(0.0, 0.0, 400.0), At - FVector(0.0, 0.0, 800.0), Objects))
	{
		OutGround = Hit.ImpactPoint;
		return true;
	}
	return false;
}

void UOCPopulationSubsystem::Release(int32 Index)
{
	AOCPedestrian* P = Pool[Index];
	if (P && P->SimId != INDEX_NONE)
	{
		Sim.Remove(P->SimId);
		P->SetPooled(FTransform(FVector(0.0, 0.0, -20000.0)));
		++Despawned;
	}
}

void UOCPopulationSubsystem::PlacePeds(float DeltaTime)
{
	for (AOCPedestrian* P : Pool)
	{
		if (!P || P->SimId == INDEX_NONE) continue;
		const FOCPedestrian* C = Sim.FindPed(P->SimId);
		if (!C)
		{
			P->SetPooled(FTransform(FVector(0.0, 0.0, -20000.0)));
			continue;
		}
		FVector Ground;
		if (!GroundAt(FVector(C->Pos.X, C->Pos.Y, 20000.0), Ground))
		{
			continue;
		}
		P->SetSimPose(Ground, FVector(C->Dir.X, C->Dir.Y, 0.0), C->Speed);
	}
}

void UOCPopulationSubsystem::Populate(const FVector& View, const FVector& ViewDir)
{
	const double DespawnCm = Settings->DespawnM * 100.0;
	for (int32 i = 0; i < Pool.Num(); ++i)
	{
		AOCPedestrian* P = Pool[i];
		if (!P || P->SimId == INDEX_NONE) continue;
		const FOCPedestrian* C = Sim.FindPed(P->SimId);
		const FVector At = P->GetActorLocation();
		const double Dist = FVector::Dist(At, View);
		const bool bSeen = InView(At, View, ViewDir) && Dist < DespawnCm * 1.6;
		if (!C || (!bSeen && Dist > DespawnCm))
		{
			Release(i);
		}
	}
	const int32 Budget = GetBudget();
	int32 Active = 0;
	for (const AOCPedestrian* P : Pool) Active += (P && P->SimId != INDEX_NONE) ? 1 : 0;
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
				if (Score > WorstScore) { WorstScore = Score; Worst = i; }
			}
		}
		if (Worst == INDEX_NONE) break;
		Release(Worst);
		--Active;
	}
	if (Active >= Budget) return;

	if (!Lanes) return;
	const TArray<FOCLane>& L = Lanes->GetLanes();
	const double MinCm = Settings->SpawnMinM * 100.0, MaxCm = Settings->SpawnMaxM * 100.0, SeenCm = Settings->VisibleSpawnM * 100.0;
	for (int32 Try = 0; Try < 24 && Active < Budget; ++Try)
	{
		const int32 Lane = Rng.RandRange(0, L.Num() - 1);
		const FOCLane& Ln = L[Lane];
		const double Len = Ln.LengthCm();
		if (Len < 1500.0) continue;
		const double S = Rng.FRandRange(600.f, (float)(Len - 300.0));
		const FVector At = Ln.Start + Ln.Direction() * S;
		const double Dist = FVector::Dist2D(At, View);
		if (Dist < MinCm || Dist > MaxCm || (Dist < SeenCm && InView(At, View, ViewDir))) continue;
		FVector Ground;
		if (!GroundAt(At, Ground)) continue;
		AOCPedestrian* Free = nullptr;
		for (AOCPedestrian* P : Pool) { if (P && P->SimId == INDEX_NONE) { Free = P; break; } }
		if (!Free) break;
		const int32 Id = Sim.Spawn(FVector2D(At.X, At.Y), Rng);
		Free->SimId = Id;
		Free->SetActive(true);
		Free->SetSimPose(Ground, FVector(1.0, 0.0, 0.0), Sim.GetParams().WalkSpeedCm);
		++Active;
		++Spawned;
	}
}

void UOCPopulationSubsystem::GatherThreats(const FVector& View)
{
	// Moving cars are threats: every lane car within radius makes nearby peds flee.
	if (!Traffic.IsValid()) return;
	const double RadiusCm = Settings->ThreatRadiusM * 100.0;
	for (const FOCTrafficCar& C : Traffic->GetSim().GetCars())
	{
		const FOCTrafficPose P = Traffic->GetSim().GetPose(C);
		FOCThreat T;
		T.Pos = FVector2D(P.Location.X, P.Location.Y);
		T.RadiusCm = RadiusCm;
		T.Weight = 0.4f;
		Sim.AddThreat(T);
	}
}
