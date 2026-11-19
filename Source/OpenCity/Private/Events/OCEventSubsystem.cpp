#include "Events/OCEventSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "OCLog.h"

namespace OCEventTags
{
	namespace
	{
		const FGameplayTag& Cached(FGameplayTag& Slot, const TCHAR* Name)
		{
			if (!Slot.IsValid())
			{
				Slot = FGameplayTag::RequestGameplayTag(FName(Name));
			}
			return Slot;
		}
	}

	const FGameplayTag& Gunshot()         { static FGameplayTag T; return Cached(T, TEXT("Event.Crime.Gunshot")); }
	const FGameplayTag& Assault()         { static FGameplayTag T; return Cached(T, TEXT("Event.Crime.Assault")); }
	const FGameplayTag& Murder()          { static FGameplayTag T; return Cached(T, TEXT("Event.Crime.Murder")); }
	const FGameplayTag& VehicleTheft()    { static FGameplayTag T; return Cached(T, TEXT("Event.Crime.VehicleTheft")); }
	const FGameplayTag& RecklessDriving() { static FGameplayTag T; return Cached(T, TEXT("Event.Crime.RecklessDriving")); }
}

bool UOCEventSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && Super::ShouldCreateSubsystem(Outer);
}

void UOCEventSubsystem::Deinitialize()
{
	Listeners.Reset();
	Super::Deinitialize();
}

void UOCEventSubsystem::Publish(const FGameplayTag& Tag, const FVector& Location, const AActor* Instigator,
	float Severity)
{
	if (!Tag.IsValid()) return;

	FOCEvent Ev;
	Ev.Tag = Tag;
	Ev.Location = Location;
	Ev.Instigator = TWeakObjectPtr<AActor>(const_cast<AActor*>(Instigator));
	Ev.Severity = Severity;

	UE_LOG(LogOpenCity, Verbose, TEXT("Event: %s at %s by %s (severity %.2f)"), *Tag.ToString(),
		*Location.ToCompactString(), *GetNameSafe(Instigator), Severity);

	// Copy first: a listener may subscribe, unsubscribe or publish while we iterate.
	if (const TArray<FListener>* Found = Listeners.Find(Tag))
	{
		const TArray<FListener> Snapshot = *Found;
		for (const FListener& L : Snapshot)
		{
			L.Handle(Ev);
		}
	}
}

void UOCEventSubsystem::Subscribe(const FGameplayTag& Tag, const void* Owner, TFunction<void(const FOCEvent&)> Handle)
{
	if (!Tag.IsValid() || !Handle)
	{
		UE_LOG(LogOpenCity, Warning, TEXT("Event: ignored subscription to an invalid tag (is it in DefaultGameplayTags.ini?)"));
		return;
	}
	Listeners.FindOrAdd(Tag).Add({ Owner, MoveTemp(Handle) });
}

bool UOCEventSubsystem::IsPlayerInstigated(const AActor* Actor)
{
	// Walk a few links: projectile -> character, vehicle -> its controller's pawn.
	for (int32 Hop = 0; Actor && Hop < 4; ++Hop)
	{
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			if (Pawn->IsPlayerControlled())
			{
				return true;
			}
		}
		const AActor* Next = Actor->GetInstigator();
		Actor = Next && Next != Actor ? Next : Actor->GetOwner();
	}
	return false;
}

void UOCEventSubsystem::Unsubscribe(const void* Owner)
{
	for (auto& Pair : Listeners)
	{
		Pair.Value.RemoveAll([Owner](const FListener& L) { return L.Owner == Owner; });
	}
}
