#include "Wanted/OCWantedSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "OCLog.h"

namespace OCWanted
{
	const FGameplayTag& TagFor(EOCrime Crime)
	{
		static const FGameplayTag None;
		switch (Crime)
		{
		case EOCrime::Gunshot:         return OCEventTags::Gunshot();
		case EOCrime::Assault:         return OCEventTags::Assault();
		case EOCrime::Murder:          return OCEventTags::Murder();
		case EOCrime::VehicleTheft:    return OCEventTags::VehicleTheft();
		case EOCrime::RecklessDriving: return OCEventTags::RecklessDriving();
		default:                       return None;
		}
	}

	/** Minor crimes that repeat many times a second (each shot, each near miss) only count once per cooldown. */
	bool HasCooldown(EOCrime Crime)
	{
		return Crime == EOCrime::Gunshot || Crime == EOCrime::RecklessDriving;
	}
}

bool UOCWantedSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && Super::ShouldCreateSubsystem(Outer);
}

TStatId UOCWantedSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UOCWantedSubsystem, STATGROUP_Tickables);
}

void UOCWantedSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	Events = InWorld.GetSubsystem<UOCEventSubsystem>();
	if (!Events.IsValid())
	{
		return;
	}
	for (const EOCrime Crime : { EOCrime::Gunshot, EOCrime::Assault, EOCrime::Murder, EOCrime::VehicleTheft, EOCrime::RecklessDriving })
	{
		Events->Subscribe(OCWanted::TagFor(Crime), this, [this, Crime](const FOCEvent& Ev) { OnCrimeEvent(Crime, Ev); });
	}
}

void UOCWantedSubsystem::Deinitialize()
{
	if (Events.IsValid())
	{
		Events->Unsubscribe(this);
	}
	Events = nullptr;
	Super::Deinitialize();
}

void UOCWantedSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ExpireZones(GetWorld()->GetTimeSeconds());
	UpdateHeat(DeltaTime);
}

void UOCWantedSubsystem::ReportCrime(EOCrime Crime, const FVector& Location, const AActor* Instigator,
	float Severity)
{
	const FGameplayTag& Tag = OCWanted::TagFor(Crime);
	if (Severity <= 0.f || !Tag.IsValid())
	{
		return;
	}
	if (Events.IsValid())
	{
		Events->Publish(Tag, Location, Instigator, Severity); // heat follows from OnCrimeEvent
	}
	else
	{
		FOCEvent Ev;
		Ev.Tag = Tag;
		Ev.Location = Location;
		Ev.Instigator = const_cast<AActor*>(Instigator);
		Ev.Severity = Severity;
		OnCrimeEvent(Crime, Ev);
	}
}

void UOCWantedSubsystem::OnCrimeEvent(EOCrime Crime, const FOCEvent& Ev)
{
	if (Ev.Severity <= 0.f || !UOCEventSubsystem::IsPlayerInstigated(Ev.Instigator.Get()))
	{
		return; // traffic running a ped over, or an NPC crime: no heat for the player
	}
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	// Every crime starts (or extends) a search around where it happened.
	FOCSearchZone Zone;
	Zone.Center = Ev.Location;
	Zone.RadiusCm = SearchRadiusCm;
	Zone.ExpiresAt = Now + SearchSeconds;
	SearchZones.Add(Zone);
	DecayAccum = 0.f;

	if (OCWanted::HasCooldown(Crime))
	{
		if (const float* Last = LastCrimeTime.Find(Crime); Last && Now - *Last < CrimeCooldownSeconds)
		{
			return;
		}
	}
	LastCrimeTime.Add(Crime, Now);
	SetHeat(Heat + Ev.Severity * HeatPerSeverity, *Ev.Tag.ToString());
}

void UOCWantedSubsystem::SetHeat(float NewHeat, const TCHAR* Why)
{
	Heat = FMath::Clamp(NewHeat, 0.f, 5.f);
	const EOCHeatLevel Old = HeatLevel;
	HeatLevel = (EOCHeatLevel)FMath::Clamp(FMath::FloorToInt32(Heat + KINDA_SMALL_NUMBER), 0, 5);
	if (HeatLevel != Old)
	{
		UE_LOG(LogOpenCity, Log, TEXT("Wanted: heat level %d -> %d (heat %.2f, %s)"),
			(int32)Old, (int32)HeatLevel, Heat, Why);
		OnHeatChanged.Broadcast(HeatLevel);
	}
}

void UOCWantedSubsystem::ClearHeat()
{
	SearchZones.Reset();
	LastCrimeTime.Reset();
	DecayAccum = 0.f;
	SetHeat(0.f, TEXT("cleared"));
}

void UOCWantedSubsystem::UpdateHeat(float DeltaTime)
{
	// Heat holds while police are searching; once every zone lapses it drops a level at a time.
	if (!SearchZones.IsEmpty() || Heat <= 0.f)
	{
		DecayAccum = 0.f;
		return;
	}
	DecayAccum += DeltaTime;
	if (DecayAccum >= DecaySecondsPerLevel)
	{
		DecayAccum = 0.f;
		// Drop to the level below; a fractional remainder below level 1 clears outright.
		const float Below = FMath::FloorToFloat(Heat + KINDA_SMALL_NUMBER) - 1.f; // 2.5 -> 1, 0.5 -> clear
		SetHeat(Below < 1.f ? 0.f : Below, TEXT("decay"));
	}
}

void UOCWantedSubsystem::ExpireZones(float Now)
{
	SearchZones.RemoveAll([Now](const FOCSearchZone& Z) { return Now > Z.ExpiresAt; });
}
