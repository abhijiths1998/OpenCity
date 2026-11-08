#include "World/OCLockedAreaVolume.h"

#include "Components/BoxComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Progression/OCProgressionSubsystem.h"
#include "TimerManager.h"

namespace OCAreaVolume
{
	UOCProgressionSubsystem* Progression(const AActor* A)
	{
		const UGameInstance* GI = A->GetGameInstance();
		return GI ? GI->GetSubsystem<UOCProgressionSubsystem>() : nullptr;
	}
}

AOCLockedAreaVolume::AOCLockedAreaVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetCollisionProfileName(TEXT("Trigger"));
	Box->SetCanEverAffectNavigation(false);
	Box->ShapeColor = FColor(255, 80, 40);
	Box->SetLineThickness(40.f);
	RootComponent = Box;
}

void AOCLockedAreaVolume::BeginPlay()
{
	Super::BeginPlay();
	Box->OnComponentBeginOverlap.AddUniqueDynamic(this, &AOCLockedAreaVolume::HandleBeginOverlap);
	Box->OnComponentEndOverlap.AddUniqueDynamic(this, &AOCLockedAreaVolume::HandleEndOverlap);
	if (UOCProgressionSubsystem* Prog = OCAreaVolume::Progression(this))
	{
		Prog->RegisterAreaBounds(this, Area, Box->Bounds.GetBox());
	}
}

void AOCLockedAreaVolume::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UOCProgressionSubsystem* Prog = OCAreaVolume::Progression(this))
	{
		Prog->UnregisterAreaBounds(this);
	}
	Super::EndPlay(Reason);
}

APawn* AOCLockedAreaVolume::PlayerPawnInside() const
{
	APlayerController* PC = Intruder.Get();
	APawn* P = PC ? PC->GetPawn() : nullptr;
	return P && Box->IsOverlappingActor(P) ? P : nullptr;
}

void AOCLockedAreaVolume::HandleBeginOverlap(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	const APawn* P = Cast<APawn>(Other);
	APlayerController* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	UOCProgressionSubsystem* Prog = OCAreaVolume::Progression(this);
	if (!PC || !Prog || Prog->IsAreaOpen(Area) || GetWorldTimerManager().IsTimerActive(PushBackTimer))
	{
		return;
	}
	Intruder = PC;
	Prog->ShowAreaWarning(Prog->GetLockedMessage(Area));
	GetWorldTimerManager().SetTimer(PushBackTimer, this, &AOCLockedAreaVolume::PushBack, Prog->GetPushBackDelaySeconds());
}

void AOCLockedAreaVolume::HandleEndOverlap(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32)
{
	if (GetWorldTimerManager().IsTimerActive(PushBackTimer))
	{
		// Getting into or out of a vehicle swaps the pawn mid-overlap: decide once that settles.
		GetWorldTimerManager().SetTimerForNextTick(this, &AOCLockedAreaVolume::CheckStillInside);
	}
}

void AOCLockedAreaVolume::CheckStillInside()
{
	if (!PlayerPawnInside())
	{
		GetWorldTimerManager().ClearTimer(PushBackTimer);
		Intruder.Reset();
	}
}

void AOCLockedAreaVolume::PushBack()
{
	UOCProgressionSubsystem* Prog = OCAreaVolume::Progression(this);
	APawn* P = PlayerPawnInside();
	if (P && Prog && !Prog->IsAreaOpen(Area))
	{
		Prog->ReturnPlayerToOpenRoad(Intruder.Get(), P->GetActorLocation(), /*bWreckVehicle*/ false);
	}
	Intruder.Reset();
}
