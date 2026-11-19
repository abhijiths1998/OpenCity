#include "Population/OCPopulationSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "OCLog.h"
#include "Population/OCPedestrian.h"
#include "Progression/OCProgressionSubsystem.h"
#include "Scalability.h"
#include "Traffic/OCTrafficSubsystem.h"
#include "World/OCLaneGraph.h"
#include "World/OCRoadGraphAsset.h"

namespace OCPopulationCVars
{
	TAutoConsoleVariable<int32> Enabled(TEXT("oc.population.Enabled"), 1, TEXT("Pedestrians around the player (M5): 1 on, 0 off."));
	TAutoConsoleVariable<int32> Budget(TEXT("oc.population.Budget"), -1, TEXT("Peds: -1 = by scalability (OpenCity Population settings), else this many."));
	constexpr int32 MaxPool = 96;
	const FVector PoolLocation(0.0, 0.0, -20000.0);
	// Feet follow the ground: look this far above / below the last foot height (walkable steps and kerbs).
	constexpr double StepUpCm = 150.0;
	constexpr double StepDownCm = 600.0;
}

bool UOCPopulationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && Super::ShouldCreateSubsystem(Outer);
}

void UOCPopulationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	Settings = GetDefault<UOCPopulationSettings>();
	if (!Settings) return;

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
		AOCPedestrian* P = InWorld.SpawnActor<AOCPedestrian>(OCPopulationCVars::PoolLocation, FRotator::ZeroRotator, Params);
		if (P)
		{
			P->SetPooled(FTransform(OCPopulationCVars::PoolLocation));
			Pool.Add(P);
		}
	}
	bReady = !Pool.IsEmpty();

	Traffic = InWorld.GetSubsystem<UOCTrafficSubsystem>();
	Events = InWorld.GetSubsystem<UOCEventSubsystem>();
	if (Events.IsValid())
	{
		for (const FGameplayTag& Tag : { OCEventTags::Gunshot(), OCEventTags::Murder(), OCEventTags::Assault() })
		{
			Events->Subscribe(Tag, this, [this](const FOCEvent& Ev) { OnCrimeEvent(Ev); });
		}
	}
	if (const UGameInstance* GI = InWorld.GetGameInstance())
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

void UOCPopulationSubsystem::Deinitialize()
{
	if (Events.IsValid())
	{
		Events->Unsubscribe(this);
	}
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
	if (!Settings || OCPopulationCVars::Enabled.GetValueOnGameThread() == 0)
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

	// AI at a fixed rate; in between, actors are extrapolated (no per-frame AI).
	const double AiDt = 1.0 / FMath::Max(Settings->AiRateHz, 1.f);
	AiIn -= DeltaTime;
	SinceStep += DeltaTime;
	if (AiIn <= 0.0)
	{
		AiIn = FMath::Max(AiIn + AiDt, 0.0); // a long hitch runs one step, not a burst
		GatherThreats();
		Sim.Step(AiDt, World->GetTimeSeconds(), Rng);
		SinceStep = 0.0;
	}

	KnockdownByPlayerVehicle();
	PlacePeds(FMath::Min(SinceStep, AiDt));
	Populate(View, ViewDir);
}

FString UOCPopulationSubsystem::DescribeStats() const
{
	int32 Active = 0, Fleeing = 0, Dead = 0;
	for (const AOCPedestrian* P : Pool)
	{
		if (!P || P->SimId == INDEX_NONE) continue;
		++Active;
		if (P->GetState() == EOCPedState::Flee) ++Fleeing;
		if (P->IsDead()) ++Dead;
	}
	return FString::Printf(TEXT("population: %d active (budget %d, pool %d), %d fleeing, %d dead; spawned %d, despawned %d, killed %d"),
		Active, GetBudget(), Pool.Num(), Fleeing, Dead, Spawned, Despawned, Killed);
}

void UOCPopulationSubsystem::AddThreat(const FVector& Location, double RadiusCm, float Weight)
{
	FOCThreat T;
	T.Pos = FVector2D(Location.X, Location.Y);
	T.RadiusCm = RadiusCm;
	T.Weight = Weight;
	Sim.AddThreat(T); // consumed by the next Step
}

void UOCPopulationSubsystem::ReportGunshot(const FVector& Location)
{
	if (!bReady) return;
	AddThreat(Location, Settings->GunshotRadiusM * 100.0, 1.f);
}

void UOCPopulationSubsystem::OnCrimeEvent(const FOCEvent& Ev)
{
	if (!bReady) return;
	// A killing is heard further than a shot; a scuffle less far. Louder weapons carry further.
	double Scale = 1.0;
	if (Ev.Tag == OCEventTags::Murder()) Scale = 1.5;
	else if (Ev.Tag == OCEventTags::Assault()) Scale = 0.6;
	else Scale = 0.5 + FMath::Clamp(Ev.Severity, 0.f, 1.f);
	// Run from the attacker when known (a shot is fired where the shooter stands; a body lies where the victim fell).
	const AActor* Who = Ev.Instigator.Get();
	AddThreat(Who ? Who->GetActorLocation() : Ev.Location, Settings->GunshotRadiusM * 100.0 * Scale, 1.f);
}

bool UOCPopulationSubsystem::DamagePed(AOCPedestrian* Ped, float Amount, const FVector& Impulse, const AActor* Instigator)
{
	if (!bReady || !Ped || Ped->SimId == INDEX_NONE || Ped->IsDead() || Amount <= 0.f) return false;
	const float Fraction = Amount / FMath::Max(Settings->PedestrianHealth, 1.f);
	if (!Sim.Damage(Ped->SimId, Fraction)) return false;

	const FOCPedestrian* C = Sim.FindPed(Ped->SimId);
	Ped->SetHealthFraction(C ? C->Health : 0.f);
	UWorld* World = GetWorld();
	const FVector At = Ped->GetActorLocation();
	if (!C || C->Health <= 0.f)
	{
		Ped->Die(World ? World->GetTimeSeconds() : 0.0);
		++Killed;
		UE_LOG(LogOpenCity, Log, TEXT("Population: %s killed by %s"), *Ped->GetName(), *GetNameSafe(Instigator));
		if (Events.IsValid())
		{
			Events->Publish(OCEventTags::Murder(), At, Instigator, 1.f);
		}
	}
	else
	{
		// The victim runs from whoever hurt it (a threat at its own feet has no direction to run).
		AddThreat(Instigator ? Instigator->GetActorLocation() : At, Settings->ThreatRadiusM * 100.0 * 2.0, 1.f);
		if (Events.IsValid())
		{
			Events->Publish(OCEventTags::Assault(), At, Instigator, 0.3f);
		}
	}
	return true;
}

AOCPedestrian* UOCPopulationSubsystem::FindNearestPed(const FVector& Location, double MaxDistCm) const
{
	AOCPedestrian* Best = nullptr;
	double BestSq = FMath::Square(MaxDistCm);
	for (AOCPedestrian* P : Pool)
	{
		if (!P || P->SimId == INDEX_NONE || P->IsDead()) continue;
		const double DSq = FVector::DistSquared(P->GetActorLocation(), Location);
		if (DSq < BestSq)
		{
			BestSq = DSq;
			Best = P;
		}
	}
	return Best;
}

TArray<AOCPedestrian*> UOCPopulationSubsystem::GetActivePeds() const
{
	TArray<AOCPedestrian*> Out;
	for (AOCPedestrian* P : Pool)
	{
		if (P && P->SimId != INDEX_NONE && !P->IsDead()) Out.Add(P);
	}
	return Out;
}

void UOCPopulationSubsystem::KnockdownByPlayerVehicle()
{
	// The player's car: any live ped inside its box (plus a little) above walking pace goes down.
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || Pawn->IsA<ACharacter>()) return;

	const FVector Vel = Pawn->GetVelocity();
	const double Kmh = Vel.Size() * 0.036;
	if (Kmh < Settings->VehicleKnockdownKmh) return;

	const FBox Local = Pawn->CalculateComponentsBoundingBoxInLocalSpace(/*bNonColliding*/ false);
	if (!Local.IsValid) return;
	const FTransform& Xf = Pawn->GetActorTransform();
	const FVector Pad(40.0, 40.0, 50.0);
	const double ReachSq = FMath::Square(Local.GetExtent().Size() + 300.0);
	for (AOCPedestrian* P : Pool)
	{
		if (!P || P->SimId == INDEX_NONE || P->IsDead()) continue;
		const FVector At = P->GetActorLocation();
		if (FVector::DistSquared(At, Xf.GetLocation()) > ReachSq) continue;
		const FVector L = Xf.InverseTransformPosition(At);
		if (Local.ExpandBy(Pad).IsInside(L))
		{
			// Lethal from about 40 km/h; a bump at low speed hurts.
			const float Damage = Settings->PedestrianHealth * FMath::Clamp((float)(Kmh / 40.0), 0.25f, 2.f);
			DamagePed(P, Damage, Vel, Pawn);
		}
	}
}

bool UOCPopulationSubsystem::InView(const FVector& At, const FVector& View, const FVector& ViewDir) const
{
	const FVector To = At - View;
	const double Dist = To.Size();
	return Dist < 1.0 || FVector::DotProduct(To / Dist, ViewDir) > FMath::Cos(FMath::DegreesToRadians(60.0));
}

bool UOCPopulationSubsystem::GroundAt(const FVector& At, double UpCm, double DownCm, FVector& OutGround) const
{
	FHitResult Hit;
	const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OCPedGround), false);
	if (GetWorld()->LineTraceSingleByObjectType(Hit, At + FVector(0.0, 0.0, UpCm), At - FVector(0.0, 0.0, DownCm), Objects, Params)
		&& !Hit.bStartPenetrating)
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
		P->SetPooled(FTransform(OCPopulationCVars::PoolLocation));
		++Despawned;
	}
}

void UOCPopulationSubsystem::PlacePeds(double Ahead)
{
	for (AOCPedestrian* P : Pool)
	{
		if (!P || P->SimId == INDEX_NONE) continue;
		const FOCPedestrian* C = Sim.FindPed(P->SimId);
		if (!C) continue; // Populate releases it
		P->SetState(C->State);
		// Extrapolate from the last AI step along the heading so walking is smooth, not 4 Hz hops.
		const FVector2D Pos = C->Pos + C->Dir * (C->Speed * Ahead);
		const FVector Last = P->FeetLocation();
		FVector Ground;
		if (!GroundAt(FVector(Pos.X, Pos.Y, Last.Z), OCPopulationCVars::StepUpCm, OCPopulationCVars::StepDownCm, Ground))
		{
			continue; // no ground under the new spot (a wall, a drop): stay put this frame
		}
		P->SetSimPose(Ground, FVector(C->Dir.X, C->Dir.Y, 0.0));
	}
}

void UOCPopulationSubsystem::Populate(const FVector& View, const FVector& ViewDir)
{
	const double DespawnCm = Settings->DespawnM * 100.0;
	const double Now = GetWorld()->GetTimeSeconds();
	for (int32 i = 0; i < Pool.Num(); ++i)
	{
		AOCPedestrian* P = Pool[i];
		if (!P || P->SimId == INDEX_NONE) continue;
		const FOCPedestrian* C = Sim.FindPed(P->SimId);
		const FVector At = P->GetActorLocation();
		const double Dist = FVector::Dist(At, View);
		const bool bInView = InView(At, View, ViewDir);
		const bool bSeen = bInView && Dist < DespawnCm * 1.6;
		const bool bOldBody = P->IsDead() && !bInView && Now - P->GetDeathTime() > Settings->CorpseSeconds;
		if (!C || (!bSeen && Dist > DespawnCm) || bOldBody)
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
	if (Active >= Budget || !Lanes) return;

	const TArray<FOCLane>& L = Lanes->GetLanes();
	if (L.IsEmpty()) return;
	const double MinCm = Settings->SpawnMinM * 100.0, MaxCm = Settings->SpawnMaxM * 100.0, SeenCm = Settings->VisibleSpawnM * 100.0;
	for (int32 Try = 0; Try < 24 && Active < Budget; ++Try)
	{
		const FOCLane& Ln = L[Rng.RandRange(0, L.Num() - 1)];
		const double Len = Ln.LengthCm();
		if (Len < 1500.0) continue;
		const double S = Rng.FRandRange(600.f, (float)(Len - 300.0));
		const FVector At = Ln.Start + Ln.Direction() * S;
		const double Dist = FVector::Dist2D(At, View);
		if (Dist < MinCm || Dist > MaxCm || (Dist < SeenCm && InView(At, View, ViewDir))) continue;
		FVector Ground;
		if (!GroundAt(At, 400.0, 800.0, Ground)) continue;
		AOCPedestrian* Free = nullptr;
		for (AOCPedestrian* P : Pool) { if (P && P->SimId == INDEX_NONE) { Free = P; break; } }
		if (!Free) break;
		const int32 Id = Sim.Spawn(FVector2D(At.X, At.Y), Rng);
		Free->SimId = Id;
		Free->SetActive(true);
		Free->SetSimPose(Ground, Ln.Direction());
		++Active;
		++Spawned;
	}
}

void UOCPopulationSubsystem::GatherThreats()
{
	// Moving cars are threats: every moving lane car makes peds within ThreatRadiusM flee.
	if (!Traffic.IsValid()) return;
	const double RadiusCm = Settings->ThreatRadiusM * 100.0;
	for (const FOCTrafficCar& C : Traffic->GetSim().GetCars())
	{
		if (C.Speed < 200.0) continue; // parked or queued at a light: not scary
		const FOCTrafficPose P = Traffic->GetSim().GetPose(C);
		FOCThreat T;
		T.Pos = FVector2D(P.Location.X, P.Location.Y);
		T.RadiusCm = RadiusCm;
		T.Weight = 0.4f;
		Sim.AddThreat(T);
	}
	// The player's vehicle is a threat too (it is not in the traffic sim).
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (Pawn && !Pawn->IsA<ACharacter>() && Pawn->GetVelocity().Size() * 0.036 > Settings->VehicleKnockdownKmh)
	{
		AddThreat(Pawn->GetActorLocation(), RadiusCm, 0.8f);
	}
}
