#include "Vehicles/OCVehicleModels.h"

// --- Damage -------------------------------------------------------------------

void FOCVehicleDamageModel::Reset(const FOCVehicleDamageSettings& InSettings)
{
	Settings = InSettings;
	Health = Settings.MaxHealth;
	LastImpactTime = -1000.0;
	CrashPeakKmh = 0.f;
}

float FOCVehicleDamageModel::ApplyImpact(float ImpactKmh, double NowSeconds)
{
	if (IsWrecked())
	{
		return 0.f;
	}

	// Chaos sends several hit events per crash, and a scrape along a wall sends many.
	// Hits closer together than the merge window form one crash, and a crash's total damage
	// depends only on its peak: each hit pays only for the part above the previous peak.
	if (NowSeconds - LastImpactTime > Settings.ImpactMergeWindow)
	{
		CrashPeakKmh = 0.f; // a new crash
	}
	LastImpactTime = NowSeconds;

	const float From = FMath::Max(CrashPeakKmh, Settings.MinImpactKmh);
	CrashPeakKmh = FMath::Max(CrashPeakKmh, ImpactKmh);
	if (ImpactKmh <= From)
	{
		return 0.f;
	}

	const float Damage = FMath::Min(Health, (ImpactKmh - From) * Settings.DamagePerKmh);
	Health -= Damage;
	return Damage;
}

float FOCVehicleDamageModel::GetHealthFraction() const
{
	return Settings.MaxHealth > 0.f ? FMath::Clamp(Health / Settings.MaxHealth, 0.f, 1.f) : 0.f;
}

float FOCVehicleDamageModel::GetPerformance() const
{
	if (IsWrecked())
	{
		return 0.f;
	}
	return FMath::Lerp(Settings.MinPerformance, 1.f, GetHealthFraction());
}

// --- Radio --------------------------------------------------------------------

void FOCRadioTuner::SetStations(TArray<FOCRadioStationInfo> InStations, uint32 Seed)
{
	Stations = MoveTemp(InStations);
	Offsets.Reset(Stations.Num());
	FRandomStream Rng(static_cast<int32>(Seed));
	for (const FOCRadioStationInfo& S : Stations)
	{
		Offsets.Add(S.LengthSeconds > 0.f ? Rng.FRandRange(0.f, S.LengthSeconds) : 0.f);
	}
	Index = Stations.IsEmpty() ? -1 : FMath::Min(Index, Stations.Num() - 1);
}

int32 FOCRadioTuner::Next()
{
	if (Stations.IsEmpty())
	{
		return Index = -1;
	}
	Index = Index + 1 >= Stations.Num() ? -1 : Index + 1;
	return Index;
}

int32 FOCRadioTuner::Previous()
{
	if (Stations.IsEmpty())
	{
		return Index = -1;
	}
	Index = Index < 0 ? Stations.Num() - 1 : Index - 1;
	return Index;
}

void FOCRadioTuner::Tune(int32 InIndex)
{
	Index = Stations.IsValidIndex(InIndex) ? InIndex : -1;
}

const FOCRadioStationInfo* FOCRadioTuner::GetCurrent() const
{
	return Stations.IsValidIndex(Index) ? &Stations[Index] : nullptr;
}

float FOCRadioTuner::GetPosition(double NowSeconds) const
{
	const FOCRadioStationInfo* S = GetCurrent();
	if (!S || S->LengthSeconds <= 0.f)
	{
		return 0.f;
	}
	const double T = FMath::Fmod(NowSeconds + Offsets[Index], static_cast<double>(S->LengthSeconds));
	return static_cast<float>(T < 0.0 ? T + S->LengthSeconds : T);
}

// --- Camera -------------------------------------------------------------------

float FOCVehicleCameraSettings::FovForSpeed(float SpeedKmh) const
{
	const float Alpha = FMath::Clamp(FMath::Abs(SpeedKmh) / FovFullSpeedKmh, 0.f, 1.f);
	return FMath::Lerp(FovAtRest, FovAtSpeed, Alpha);
}
