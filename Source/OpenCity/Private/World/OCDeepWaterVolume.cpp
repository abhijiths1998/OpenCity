#include "World/OCDeepWaterVolume.h"

#include "Components/BoxComponent.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Progression/OCProgressionSubsystem.h"

AOCDeepWaterVolume::AOCDeepWaterVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetCollisionProfileName(TEXT("Trigger"));
	Box->SetCanEverAffectNavigation(false);
	Box->ShapeColor = FColor(40, 120, 255);
	RootComponent = Box;
	bGenerateOverlapEventsDuringLevelStreaming = true;
}

void AOCDeepWaterVolume::BeginPlay()
{
	Super::BeginPlay();
	Box->OnComponentBeginOverlap.AddUniqueDynamic(this, &AOCDeepWaterVolume::HandleBeginOverlap);
	TArray<AActor*> Inside; // anyone already in the water when this streamed in
	Box->GetOverlappingActors(Inside, APawn::StaticClass());
	for (AActor* A : Inside)
	{
		HandleBeginOverlap(Box, A, nullptr, 0, false, FHitResult());
	}
}

void AOCDeepWaterVolume::HandleBeginOverlap(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	const APawn* P = Cast<APawn>(Other);
	APlayerController* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	const UGameInstance* GI = GetGameInstance();
	if (PC && GI)
	{
		if (UOCProgressionSubsystem* Prog = GI->GetSubsystem<UOCProgressionSubsystem>())
		{
			Prog->ReturnPlayerToOpenRoad(PC, P->GetActorLocation(), /*bWreckVehicle*/ true);
		}
	}
}
