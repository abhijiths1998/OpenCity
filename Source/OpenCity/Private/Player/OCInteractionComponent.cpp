#include "Player/OCInteractionComponent.h"

#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Interaction/OCInteractable.h"
#include "OCLog.h"

UOCInteractionComponent::UOCInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

AActor* UOCInteractionComponent::FindBest() const
{
	const APawn* Owner = Cast<APawn>(GetOwner());
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return nullptr;
	}

	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Vehicle);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams Params(SCENE_QUERY_STAT(OCInteraction), false, Owner);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Owner->GetActorLocation(), FQuat::Identity, Objects,
		FCollisionShape::MakeSphere(Radius), Params);

	AActor* Best = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();
	for (const FOverlapResult& O : Overlaps)
	{
		AActor* A = O.GetActor();
		const IOCInteractable* I = Cast<IOCInteractable>(A);
		if (!I || !I->CanInteract(Owner))
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(A->GetActorLocation(), Owner->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = A;
		}
	}
	return Best;
}

bool UOCInteractionComponent::TryInteract()
{
	AActor* Target = FindBest();
	IOCInteractable* I = Cast<IOCInteractable>(Target);
	if (!I)
	{
		UE_LOG(LogOpenCity, Verbose, TEXT("%s: nothing to interact with"), *GetNameSafe(GetOwner()));
		return false;
	}
	return I->Interact(Cast<APawn>(GetOwner()));
}
