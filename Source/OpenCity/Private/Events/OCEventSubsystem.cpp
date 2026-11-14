#include "Events/OCEventSubsystem.h"

#include "Engine/World.h"
#include "OCLog.h"

bool UOCEventSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && Super::ShouldCreateSubsystem(Outer);
}

void UOCEventSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
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

	const auto* Found = Listeners.Find(Tag);
	if (Found)
	{
		for (const auto& Handle : *Found)
		{
			Handle(Ev);
		}
	}

	UE_LOG(LogOpenCity, Verbose, TEXT("Event: %s at %s (severity %.2f)"), *Tag.ToString(),
		*Location.ToCompactString(), Severity);
}
