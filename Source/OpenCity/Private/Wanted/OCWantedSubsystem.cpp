#include "Wanted/OCWantedSubsystem.h"

#include "Engine/World.h"
#include "OCLog.h"

bool UOCWantedSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && Super::ShouldCreateSubsystem(Outer);
}

void UOCWantedSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
}

void UOCWantedSubsystem::Deinitialize()
{
	Events = nullptr;
	Super::Deinitialize();
}

void UOCWantedSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateHeat(DeltaTime);
	ExpireZones(GetWorld()->GetTimeSeconds());

	if (!Events.IsValid())
	{
		if (const UWorld* W = GetWorld())
		{
			Events = W->GetSubsystem<UOCEventSubsystem>();
		}
	}
}

void UOCWantedSubsystem::ReportCrime(EOCrime Crime, const FVector& Location, const AActor* Instigator,
	float Severity)
{
	if (Severity <= 0.f) return;

	// Publish to the event bus so population (panic) and missions (objectives) can react.
	if (Events.IsValid())
	{
		FGameplayTag Tag;
		switch (Crime)
		{
		case EOCrime::Gunshot:   Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Event.Crime.Gunshot"))); break;
		case EOCrime::Assault:   Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Event.Crime.Assault"))); break;
		case EOCrime::Murder:    Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Event.Crime.Murder"))); break;
		case EOCrime::VehicleTheft: Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Event.Crime.VehicleTheft"))); break;
		case EOCrime::RecklessDriving: Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Event.Crime.RecklessDriving"))); break;
		default: return;
		}
		Events->Publish(Tag, Location, Instigator, Severity);
	}

	// Witness check: any crime witnessed raises heat proportional to severity.
	const int32 Old = (int32)HeatLevel;
	const float Added = Severity * HeatPerSeverity;
	HeatLevel = (EOCHeatLevel)FMath::Clamp((int32)HeatLevel + FMath::RoundToInt32(Added), 0, 5);
	if ((int32)HeatLevel != Old)
	{
		UE_LOG(LogOpenCity, Log, TEXT("Wanted: heat %d -> %d (crime %d, severity %.2f)"),
			Old, (int32)HeatLevel, (int32)Crime, Severity);
		OnHeatChanged.Broadcast(HeatLevel);
	}

	// Add a search zone around the crime location.
	FOCSearchZone Zone;
	Zone.Center = Location;
	Zone.RadiusCm = 3000.f;
	Zone.ExpiresAt = GetWorld()->GetTimeSeconds() + 60.f;
	SearchZones.Add(Zone);
}

void UOCWantedSubsystem::ClearHeat()
{
	HeatLevel = EOCHeatLevel::None;
	SearchZones.Reset();
	DecayAccum = 0.f;
	OnHeatChanged.Broadcast(HeatLevel);
	UE_LOG(LogOpenCity, Log, TEXT("Wanted: heat cleared"));
}

void UOCWantedSubsystem::UpdateHeat(float DeltaTime)
{
	// Decay one level every DecaySecondsPerLevel while no active search zones.
	if (SearchZones.IsEmpty() && HeatLevel != EOCHeatLevel::None)
	{
		DecayAccum += DeltaTime;
		if (DecayAccum >= DecaySecondsPerLevel)
		{
			DecayAccum = 0.f;
			const int32 Old = (int32)HeatLevel;
			HeatLevel = (EOCHeatLevel)FMath::Max(0, (int32)HeatLevel - 1);
			if ((int32)HeatLevel != Old)
			{
				UE_LOG(LogOpenCity, Log, TEXT("Wanted: heat decayed %d -> %d"), Old, (int32)HeatLevel);
				OnHeatChanged.Broadcast(HeatLevel);
			}
		}
	}
	else if (!SearchZones.IsEmpty())
	{
		// Being searched: decay pauses.
	}
}

void UOCWantedSubsystem::ExpireZones(float Now)
{
	for (int32 i = SearchZones.Num() - 1; i >= 0; --i)
	{
		if (Now > SearchZones[i].ExpiresAt)
		{
			SearchZones.RemoveAt(i);
		}
	}
}
