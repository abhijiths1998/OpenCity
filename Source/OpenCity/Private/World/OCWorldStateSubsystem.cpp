#include "World/OCWorldStateSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/SpotLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OCLog.h"
#include "TimerManager.h"
#include "World/OCKitBuildingChunk.h"
#include "World/OCWeatherPreset.h"
#include "World/OCWorldStateSettings.h"

namespace OCWorldState
{
	constexpr float StepSeconds = 0.1f;
	constexpr int32 LampEverySteps = 5;          // street lights re-picked twice a second
	constexpr double LampSearchCm = 30000.0;     // only chunks within 300 m of the camera
	const FName NightName(TEXT("NightAmount"));
	const FName HourName(TEXT("TimeOfDay"));
	const FName RainName(TEXT("RainAmount"));
	const FName WindName(TEXT("Wind"));
	const FName WetName(TEXT("Wetness"));
}

bool UOCWorldStateSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && Super::ShouldCreateSubsystem(Outer);
}

void UOCWorldStateSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const UOCWorldStateSettings* S = GetDefault<UOCWorldStateSettings>();
	Sun.SunriseHour = S->SunriseHour;
	Sun.SunsetHour = S->SunsetHour;
	Sun.MaxElevationDeg = S->MaxSunElevationDeg;
	DayMinutes = S->DayLengthMinutes;
	Parameters = S->WorldParameters.LoadSynchronous();
	Hour = S->StartHour;
	float Fixed = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("octod="), Fixed))
	{
		Hour = Fixed;
		bRunning = false;
	}
	if (TActorIterator<ADirectionalLight> It(&InWorld); It)
	{
		SunLight = *It;
	}
	if (TActorIterator<ASkyLight> It(&InWorld); It)
	{
		SkyLight = *It;
	}
	if (TActorIterator<AExponentialHeightFog> It(&InWorld); It)
	{
		Fog = *It;
		BaseFogDensity = It->GetComponent()->FogDensity;
	}
	for (TActorIterator<AActor> It(&InWorld); It; ++It)
	{
		if (It->FindComponentByClass<USkyAtmosphereComponent>())
		{
			Atmosphere = Cast<ASkyAtmosphere>(*It);
			break;
		}
	}
	for (TActorIterator<APostProcessVolume> It(&InWorld); It; ++It)
	{
		if (It->bUnbound)
		{
			PostVolume = *It;
			BaseExposureBias = It->Settings.bOverride_AutoExposureBias ? It->Settings.AutoExposureBias : 0.f;
			break;
		}
	}
	// Weather: the settings' start preset, or -ocweather=<name>.
	const UOCWeatherPreset* StartPreset = S->StartWeather.LoadSynchronous();
	FString WeatherArg;
	if (FParse::Value(FCommandLine::Get(), TEXT("ocweather="), WeatherArg))
	{
		if (const UOCWeatherPreset* P = FindWeather(WeatherArg))
		{
			StartPreset = P;
		}
	}
	Current = From = To = FWeather::From(StartPreset);
	Wetness = Current.WetTarget;
	for (int32 i = 0; i < S->StreetLightPool; ++i)
	{
		FActorSpawnParameters P;
		P.ObjectFlags |= RF_Transient;
		ASpotLight* A = InWorld.SpawnActor<ASpotLight>(FVector::ZeroVector, FRotator(-90.f, 0.f, 0.f), P);
		if (!A)
		{
			continue;
		}
		USpotLightComponent* L = A->SpotLightComponent;
		L->SetMobility(EComponentMobility::Movable);
		L->SetIntensityUnits(ELightUnits::Lumens);
		L->SetIntensity(S->StreetLightIntensity);
		L->SetLightColor(S->StreetLightColor);
		L->SetAttenuationRadius(S->StreetLightRadius);
		L->SetInnerConeAngle(35.f);
		L->SetOuterConeAngle(70.f);
		L->SetCastShadows(false);  // 24 shadowed lights would cost more than the whole city
		L->SetVisibility(false);
		Lamps.Add(L);
	}
	InWorld.GetTimerManager().SetTimer(Timer, FTimerDelegate::CreateUObject(this, &UOCWorldStateSubsystem::Step), OCWorldState::StepSeconds, true);
	Apply();
	UE_LOG(LogOpenCity, Display, TEXT("WorldState: %.2f h, clock %s, sun %s, MPC %s, %d street lights"), Hour,
		bRunning ? TEXT("running") : TEXT("fixed"), SunLight.IsValid() ? TEXT("found") : TEXT("MISSING"),
		Parameters ? *Parameters->GetName() : TEXT("MISSING"), Lamps.Num());
}

void UOCWorldStateSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(Timer);
	}
	Super::Deinitialize();
}

void UOCWorldStateSubsystem::SetTimeOfDay(float NewHour)
{
	Hour = FOCSunModel::Wrap(NewHour);
	Apply();
	UpdateStreetLights();
}

UOCWorldStateSubsystem::FWeather UOCWorldStateSubsystem::FWeather::From(const UOCWeatherPreset* P)
{
	FWeather W;
	if (P)
	{
		W.Sun = P->SunScale;
		W.Sky = P->SkyLightScale;
		W.Fog = P->FogScale;
		W.Rayleigh = P->RayleighScale;
		W.Mie = P->MieScale;
		W.Rain = P->RainAmount;
		W.WetTarget = P->WetnessTarget;
		W.Exposure = P->ExposureBias;
		W.Wind = P->WindStrength;
	}
	return W;
}

UOCWorldStateSubsystem::FWeather UOCWorldStateSubsystem::FWeather::Lerp(const FWeather& A, const FWeather& B, float T)
{
	FWeather W;
	W.Sun = FMath::Lerp(A.Sun, B.Sun, T);
	W.Sky = FMath::Lerp(A.Sky, B.Sky, T);
	W.Fog = FMath::Lerp(A.Fog, B.Fog, T);
	W.Rayleigh = FMath::Lerp(A.Rayleigh, B.Rayleigh, T);
	W.Mie = FMath::Lerp(A.Mie, B.Mie, T);
	W.Rain = FMath::Lerp(A.Rain, B.Rain, T);
	W.WetTarget = FMath::Lerp(A.WetTarget, B.WetTarget, T);
	W.Exposure = FMath::Lerp(A.Exposure, B.Exposure, T);
	W.Wind = FMath::Lerp(A.Wind, B.Wind, T);
	return W;
}

const UOCWeatherPreset* UOCWorldStateSubsystem::FindWeather(const FString& Name) const
{
	for (const TSoftObjectPtr<UOCWeatherPreset>& Soft : GetDefault<UOCWorldStateSettings>()->WeatherPresets)
	{
		const UOCWeatherPreset* P = Soft.LoadSynchronous();
		if (P && (P->GetName().Equals(Name, ESearchCase::IgnoreCase) || P->GetName().Equals(TEXT("DA_Weather_") + Name, ESearchCase::IgnoreCase)))
		{
			return P;
		}
	}
	return nullptr;
}

void UOCWorldStateSubsystem::SetWeather(const UOCWeatherPreset* Preset, float Seconds)
{
	From = Current;
	To = FWeather::From(Preset);
	BlendLength = Seconds >= 0.f ? Seconds : GetDefault<UOCWorldStateSettings>()->WeatherBlendSeconds;
	BlendTime = 0.f;
	if (BlendLength <= 0.f)
	{
		Current = To;
		Wetness = FMath::Max(To.WetTarget, To.Rain);  // instant change (QA, debugging): no soaking time
	}
	Apply();
}

void UOCWorldStateSubsystem::StepWeather(float Dt)
{
	if (BlendTime < BlendLength)
	{
		BlendTime = FMath::Min(BlendTime + Dt, BlendLength);
		const float T = FMath::SmoothStep(0.f, 1.f, BlendTime / FMath::Max(BlendLength, 0.01f));
		Current = FWeather::Lerp(From, To, T);
	}
	// Surfaces soak while it rains and dry slowly after, toward the weather's wetness.
	const UOCWorldStateSettings* S = GetDefault<UOCWorldStateSettings>();
	const float Target = FMath::Max(Current.WetTarget, Current.Rain);
	const float Rate = Wetness < Target ? 1.f / S->SoakSeconds * FMath::Max(Current.Rain, 0.25f) : 1.f / S->DrySeconds;
	Wetness = Wetness < Target ? FMath::Min(Target, Wetness + Rate * Dt) : FMath::Max(Target, Wetness - Rate * Dt);
}

void UOCWorldStateSubsystem::Step()
{
	StepWeather(OCWorldState::StepSeconds);
	if (bRunning)
	{
		Hour = FOCSunModel::Wrap(Hour + OCWorldState::StepSeconds * 24.f / FMath::Max(1.f, DayMinutes * 60.f));
	}
	Apply();
	if (++StepCount % OCWorldState::LampEverySteps == 0)
	{
		UpdateStreetLights();
	}
}

void UOCWorldStateSubsystem::Apply()
{
	const UOCWorldStateSettings* S = GetDefault<UOCWorldStateSettings>();
	const float Elevation = Sun.ElevationDeg(Hour);
	Night = FOCSunModel::NightAmount(Elevation);
	if (ADirectionalLight* Light = SunLight.Get())
	{
		// By day the sun; by night the same light is a dim blue moon on the opposite side of the sky.
		const bool bDay = Elevation > -1.f;
		const float Up = bDay ? FMath::Max(Elevation, 0.5f) : FMath::Clamp(-Elevation * 0.8f + 10.f, 10.f, 45.f);
		const float Bearing = Sun.AzimuthDeg(Hour) + (bDay ? 0.f : 180.f);
		// Compass bearing to Unreal yaw: light travels away from the sun (bearing + 180); east = yaw 0.
		Light->SetActorRotation(FRotator(-Up, Bearing + 90.f + S->SunPathYawOffset, 0.f));
		UDirectionalLightComponent* C = Light->GetComponent();
		const float Low = FMath::SmoothStep(0.f, 25.f, Elevation);
		C->SetLightColor(bDay ? FMath::Lerp(S->LowSunColor, S->NoonColor, Low) : FLinearColor(0.55f, 0.65f, 1.f));
		C->SetIntensity((bDay ? S->SunIntensity * FMath::SmoothStep(-1.f, 6.f, Elevation) : 0.12f) * Current.Sun);
	}
	if (ASkyLight* Sky = SkyLight.Get())
	{
		// The weather's sky-light scale makes up for a dimmed sun by day; night keeps its own level.
		Sky->GetLightComponent()->SetIntensity(FMath::Lerp(Current.Sky, S->NightSkyLightIntensity, Night));
	}
	if (AExponentialHeightFog* F = Fog.Get(); F && BaseFogDensity > 0.f)
	{
		F->GetComponent()->SetFogDensity(BaseFogDensity * Current.Fog);
	}
	if (ASkyAtmosphere* A = Atmosphere.Get())
	{
		USkyAtmosphereComponent* C = A->FindComponentByClass<USkyAtmosphereComponent>();
		C->SetRayleighScatteringScale(0.0331f * Current.Rayleigh);  // engine default 0.0331
		C->SetMieScatteringScale(0.003996f * Current.Mie);          // engine default 0.003996
	}
	if (APostProcessVolume* V = PostVolume.Get())
	{
		V->Settings.bOverride_AutoExposureBias = true;
		V->Settings.AutoExposureBias = BaseExposureBias + Current.Exposure * (1.f - Night);
	}
	if (Parameters)
	{
		if (UMaterialParameterCollectionInstance* MPC = GetWorld()->GetParameterCollectionInstance(Parameters))
		{
			MPC->SetScalarParameterValue(OCWorldState::NightName, Night);
			MPC->SetScalarParameterValue(OCWorldState::HourName, Hour);
			MPC->SetScalarParameterValue(OCWorldState::RainName, Current.Rain);
			MPC->SetScalarParameterValue(OCWorldState::WindName, Current.Wind);
			MPC->SetScalarParameterValue(OCWorldState::WetName, Wetness);
		}
	}
}

void UOCWorldStateSubsystem::UpdateStreetLights()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (Lamps.IsEmpty() || !PC || !PC->PlayerCameraManager)
	{
		return;
	}
	if (Night < 0.05f)
	{
		for (USpotLightComponent* L : Lamps)
		{
			L->SetVisibility(false);
		}
		return;
	}
	// Lamp heads near the camera, from the streamed-in street prop chunks.
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	TArray<TPair<double, FVector>> Heads;
	for (TActorIterator<AOCKitBuildingChunk> It(World); It; ++It)
	{
		if (FVector::Dist2D(It->GetActorLocation(), Cam) > OCWorldState::LampSearchCm + 18000.0)
		{
			continue;
		}
		TArray<UInstancedStaticMeshComponent*> Parts;
		It->GetComponents(Parts);
		for (const UInstancedStaticMeshComponent* ISM : Parts)
		{
			const UStaticMesh* Mesh = ISM->GetStaticMesh();
			if (!Mesh || !Mesh->GetName().Contains(TEXT("street_lamp")))
			{
				continue;
			}
			const FBox B = Mesh->GetBoundingBox();
			const FVector HeadLocal(B.GetCenter().X + B.GetExtent().X * 0.6, B.GetCenter().Y, B.Max.Z - 30.0);
			for (int32 i = 0; i < ISM->GetInstanceCount(); ++i)
			{
				FTransform T;
				ISM->GetInstanceTransform(i, T, /*bWorldSpace*/ true);
				const FVector Head = T.TransformPosition(HeadLocal);
				Heads.Add({ FVector::DistSquared(Head, Cam), Head });
			}
		}
	}
	const int32 Use = FMath::Min(Heads.Num(), Lamps.Num());
	Heads.Sort([](const TPair<double, FVector>& A, const TPair<double, FVector>& B) { return A.Key < B.Key; });
	const float Intensity = GetDefault<UOCWorldStateSettings>()->StreetLightIntensity * Night;
	for (int32 i = 0; i < Lamps.Num(); ++i)
	{
		USpotLightComponent* L = Lamps[i];
		const bool bOn = i < Use;
		L->SetVisibility(bOn);
		if (bOn)
		{
			L->GetOwner()->SetActorLocation(Heads[i].Value);
			L->SetIntensity(Intensity);
		}
	}
}

#if !UE_BUILD_SHIPPING
namespace OCWorldState
{
	UOCWorldStateSubsystem* Get(UWorld* World)
	{
		return World ? World->GetSubsystem<UOCWorldStateSubsystem>() : nullptr;
	}

	FAutoConsoleCommandWithWorldAndArgs CmdTod(TEXT("oc.tod"), TEXT("Set the time of day (hours, 0-24) and stop the clock."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UOCWorldStateSubsystem* S = Get(World); S && Args.Num() > 0)
			{
				S->SetClockRunning(false);
				S->SetTimeOfDay(FCString::Atof(*Args[0]));
				UE_LOG(LogOpenCity, Display, TEXT("OCDEV tod %.2f (night %.2f)"), S->GetTimeOfDay(), S->GetNightAmount());
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdWeather(TEXT("oc.weather"), TEXT("Blend to a weather preset: Clear | Cloudy | Rain [seconds]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UOCWorldStateSubsystem* S = Get(World);
			const UOCWeatherPreset* P = S && Args.Num() > 0 ? S->FindWeather(Args[0]) : nullptr;
			if (P)
			{
				S->SetWeather(P, Args.Num() > 1 ? FCString::Atof(*Args[1]) : -1.f);
			}
			UE_LOG(LogOpenCity, Display, TEXT("OCDEV weather %s: %s"), Args.Num() > 0 ? *Args[0] : TEXT("?"), P ? TEXT("blending") : TEXT("unknown preset"));
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdTodRun(TEXT("oc.tod.run"), TEXT("Run (1) or stop (0) the day/night clock."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UOCWorldStateSubsystem* S = Get(World))
			{
				S->SetClockRunning(Args.Num() == 0 || FCString::Atoi(*Args[0]) != 0);
			}
		}));
}
#endif
