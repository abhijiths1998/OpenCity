#include "Progression/OCProgressionSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "OCLog.h"
#include "Progression/OCAreaUnlockData.h"
#include "Progression/OCProgressionSettings.h"
#include "TimerManager.h"
#include "Vehicles/OCDrivable.h"
#include "World/OCRoadGraphAsset.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

namespace OCProgression
{
	constexpr float FadeOutSeconds = 0.35f;
	constexpr float FadeInSeconds = 0.5f;
	constexpr float StreamingWaitSeconds = 0.25f;
	constexpr int32 MaxStreamingWaits = 40; // 10 s

	/** Move a pawn to stand (or sit, for a vehicle) on the ground at Ground, at rest. */
	void PlacePawn(APawn* P, const FVector& Ground, const FRotator& Facing)
	{
		FVector Origin, Extent;
		P->GetActorBounds(/*bOnlyColliding*/ true, Origin, Extent);
		const double Above = P->GetActorLocation().Z - (Origin.Z - Extent.Z); // pivot height over its lowest point
		P->SetActorLocationAndRotation(Ground + FVector(0.0, 0.0, Above + 30.0), Facing, false, nullptr, ETeleportType::ResetPhysics);
		if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(P->GetRootComponent()); Root && Root->IsSimulatingPhysics())
		{
			Root->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Root->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
		if (UPawnMovementComponent* Move = P->GetMovementComponent())
		{
			Move->StopMovementImmediately();
		}
		if (AController* C = P->GetController())
		{
			C->SetControlRotation(Facing);
		}
	}
}

void UOCProgressionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const UOCProgressionSettings* Settings = GetDefault<UOCProgressionSettings>();
	UnlockData = Settings->UnlockData.LoadSynchronous();
	RoadGraph = Settings->RoadGraph.LoadSynchronous();
	if (UnlockData)
	{
		for (const FOCAreaUnlock& A : UnlockData->Areas)
		{
			Schedule.AddRule(A.Area, A.RequiredMilestone);
		}
	}
	UE_LOG(LogOpenCity, Log, TEXT("Progression: %d unlock rules (%s), road graph %s"), Schedule.Num(),
		*GetNameSafe(UnlockData), *GetNameSafe(RoadGraph));
	WorldInitHandle = FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &UOCProgressionSubsystem::HandleWorldActorsInitialized);
}

void UOCProgressionSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldInitializedActors.Remove(WorldInitHandle);
	Super::Deinitialize();
}

bool UOCProgressionSubsystem::CompleteMilestone(FGameplayTag Milestone)
{
	if (!Milestone.IsValid() || Completed.HasTagExact(Milestone))
	{
		return false;
	}
	Completed.AddTag(Milestone);
	UE_LOG(LogOpenCity, Display, TEXT("Progression: completed %s"), *Milestone.ToString());
	ApplyToCurrentWorld();
	OnMilestoneCompleted.Broadcast(Milestone);
	return true;
}

void UOCProgressionSubsystem::CompleteAllMilestones()
{
	for (const FGameplayTag& M : Schedule.AllMilestones())
	{
		CompleteMilestone(M);
	}
}

void UOCProgressionSubsystem::ResetProgress()
{
	Completed.Reset();
	UE_LOG(LogOpenCity, Display, TEXT("Progression: reset"));
	ApplyToCurrentWorld();
}

FText UOCProgressionSubsystem::GetLockedMessage(FGameplayTag Area) const
{
	const FOCAreaUnlock* A = UnlockData ? UnlockData->Find(Area) : nullptr;
	return A && !A->LockedMessage.IsEmpty() ? A->LockedMessage : NSLOCTEXT("OpenCity", "AreaLocked", "You can't go there yet.");
}

float UOCProgressionSubsystem::GetPushBackDelaySeconds() const
{
	return UnlockData ? UnlockData->PushBackDelaySeconds : 6.f;
}

const FOCRoadGraph* UOCProgressionSubsystem::GetRoadGraph() const
{
	return RoadGraph && RoadGraph->GetGraph().NumNodes() > 0 ? &RoadGraph->GetGraph() : nullptr;
}

bool UOCProgressionSubsystem::FindOpenRoadNear(const FVector& From, FVector& OutLocation, FRotator& OutFacing) const
{
	const FOCRoadGraph* G = GetRoadGraph();
	if (!G)
	{
		return false;
	}
	TBitArray<> Open = G->ReachableFrom(G->FindNearestNode(RoadGraph->StartLocation), GetClosedGates());
	// A road can be reachable yet inside an area that is still locked (e.g. unlocked out of story
	// order): skip those, with a margin so the player lands clear of the volume.
	for (const FAreaBounds& A : AreaBounds)
	{
		if (A.Owner.IsValid() && !IsAreaOpen(A.Area))
		{
			const FBox Keep = A.Bounds.ExpandBy(FVector(1500.0, 1500.0, 0.0));
			for (int32 i = 0; i < G->NumNodes(); ++i)
			{
				const FVector& P = G->GetNodes()[i].Location;
				if (Open[i] && P.X >= Keep.Min.X && P.X <= Keep.Max.X && P.Y >= Keep.Min.Y && P.Y <= Keep.Max.Y)
				{
					Open[i] = false;
				}
			}
		}
	}
	const int32 Node = G->FindNearestNode(From, Open);
	if (Node == INDEX_NONE)
	{
		return false;
	}
	OutLocation = G->GetNodes()[Node].Location;
	const FVector Away = (OutLocation - From).GetSafeNormal2D();
	OutFacing = FRotator(0.f, Away.IsNearlyZero() ? 0.f : Away.Rotation().Yaw, 0.f);
	return true;
}

void UOCProgressionSubsystem::RegisterAreaBounds(const UObject* Owner, FGameplayTag Area, const FBox& Bounds)
{
	UnregisterAreaBounds(Owner);
	AreaBounds.Add({ Owner, Area, Bounds });
}

void UOCProgressionSubsystem::UnregisterAreaBounds(const UObject* Owner)
{
	AreaBounds.RemoveAll([Owner](const FAreaBounds& A) { return !A.Owner.IsValid() || A.Owner.Get() == Owner; });
}

void UOCProgressionSubsystem::ShowAreaWarning(const FText& Message)
{
	UE_LOG(LogOpenCity, Display, TEXT("Progression: warning \"%s\""), *Message.ToString());
	OnAreaWarning.Broadcast(Message);
	if (GEngine && !OnAreaWarning.IsBound())
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 4.f, FColor(255, 200, 80), Message.ToString(), true, FVector2D(1.6f, 1.6f));
	}
}

void UOCProgressionSubsystem::ReturnPlayerToOpenRoad(APlayerController* PC, const FVector& From, bool bWreckVehicle)
{
	if (bReturning || !PC || !PC->GetPawn())
	{
		return;
	}
	bReturning = true; // before any ejection below: the driver's own overlap would re-enter here

	if (bWreckVehicle)
	{
		if (IOCDrivable* Vehicle = Cast<IOCDrivable>(PC->GetPawn()))
		{
			APawn* VehiclePawn = PC->GetPawn();
			Vehicle->EjectDriverTo(VehiclePawn->GetActorLocation() + FVector(0.0, 0.0, 250.0));
			Vehicle->Wreck();
			UE_LOG(LogOpenCity, Display, TEXT("Progression: %s sank"), *VehiclePawn->GetName());
		}
	}
	UE_LOG(LogOpenCity, Display, TEXT("Progression: returning %s from %s"), *GetNameSafe(PC->GetPawn()), *From.ToCompactString());
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StartCameraFade(0.f, 1.f, OCProgression::FadeOutSeconds, FLinearColor::Black, false, /*bHoldWhenFinished*/ true);
	}
	FTimerHandle Handle;
	PC->GetWorldTimerManager().SetTimer(Handle,
		FTimerDelegate::CreateUObject(this, &UOCProgressionSubsystem::FinishReturn, TWeakObjectPtr<APlayerController>(PC), From, 0),
		OCProgression::FadeOutSeconds + 0.05f, false);
}

void UOCProgressionSubsystem::FinishReturn(TWeakObjectPtr<APlayerController> WeakPC, FVector From, int32 Attempt)
{
	APlayerController* PC = WeakPC.Get();
	APawn* P = PC ? PC->GetPawn() : nullptr;
	FVector Road;
	FRotator Facing;
	if (!P || !FindOpenRoadNear(From, Road, Facing))
	{
		UE_LOG(LogOpenCity, Warning, TEXT("Progression: no open road to return to (check OpenCity Progression settings)"));
		bReturning = false;
		if (PC && PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraFade(1.f, 0.f, OCProgression::FadeInSeconds, FLinearColor::Black);
		}
		return;
	}
	FHitResult Hit;
	FCollisionQueryParams Q(TEXT("OCReturnGround"), false, P);
	const bool bGround = P->GetWorld()->LineTraceSingleByChannel(Hit, Road + FVector(0.0, 0.0, 5000.0), Road - FVector(0.0, 0.0, 20000.0), ECC_Visibility, Q);
	OCProgression::PlacePawn(P, bGround ? Hit.ImpactPoint : Road, Facing);
	if (!bGround && Attempt < OCProgression::MaxStreamingWaits)
	{
		// The destination's World Partition cells are still streaming in (the camera just moved
		// there): stay faded out and hold the pawn at the road until there is ground under it.
		FTimerHandle Handle;
		PC->GetWorldTimerManager().SetTimer(Handle,
			FTimerDelegate::CreateUObject(this, &UOCProgressionSubsystem::FinishReturn, WeakPC, From, Attempt + 1),
			OCProgression::StreamingWaitSeconds, false);
		return;
	}
	bReturning = false;
	UE_LOG(LogOpenCity, Display, TEXT("Progression: returned %s to %s%s"), *P->GetName(), *P->GetActorLocation().ToCompactString(),
		bGround ? TEXT("") : TEXT(" (no ground streamed in)"));
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StartCameraFade(1.f, 0.f, OCProgression::FadeInSeconds, FLinearColor::Black);
	}
}

void UOCProgressionSubsystem::HandleWorldActorsInitialized(const FActorsInitializedParams& Params)
{
	if (Params.World && Params.World->IsGameWorld() && Params.World->GetGameInstance() == GetGameInstance())
	{
		ApplyToWorld(Params.World);
	}
}

void UOCProgressionSubsystem::ApplyToCurrentWorld() const
{
	ApplyToWorld(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
}

void UOCProgressionSubsystem::ApplyToWorld(UWorld* World) const
{
	UDataLayerManager* Layers = World ? UDataLayerManager::GetDataLayerManager(World) : nullptr;
	if (!UnlockData || !Layers)
	{
		return;
	}
	int32 Open = 0;
	for (const FOCAreaUnlock& A : UnlockData->Areas)
	{
		const bool bOpen = IsAreaOpen(A.Area);
		Open += bOpen;
		for (const UDataLayerAsset* L : A.LayersWhileLocked)
		{
			Layers->SetDataLayerRuntimeState(L, bOpen ? EDataLayerRuntimeState::Unloaded : EDataLayerRuntimeState::Activated);
		}
		for (const UDataLayerAsset* L : A.LayersWhenOpen)
		{
			Layers->SetDataLayerRuntimeState(L, bOpen ? EDataLayerRuntimeState::Activated : EDataLayerRuntimeState::Unloaded);
		}
	}
	UE_LOG(LogOpenCity, Display, TEXT("Progression: %s has %d of %d areas and gates open"), *World->GetName(), Open, UnlockData->Areas.Num());
}
