#include "Dev/OCPerfCameraPath.h"

#include "Camera/CameraComponent.h"
#include "Components/SplineComponent.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "OCLog.h"
#include "RenderTimer.h"
#include "RHIGlobals.h"
#include "UnrealEngine.h"

AOCPerfCameraPath::AOCPerfCameraPath()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // enabled only while a run is active

	Path = CreateDefaultSubobject<USplineComponent>(TEXT("Path"));
	Path->SetClosedLoop(true);
	RootComponent = Path;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Path);
	Camera->SetUsingAbsoluteLocation(true);
	Camera->SetUsingAbsoluteRotation(true);
}

void AOCPerfCameraPath::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (bGenerateCircle)
	{
		BuildCircle();
	}
	UpdateCamera();
}

void AOCPerfCameraPath::BuildCircle()
{
	Path->ClearSplinePoints(false);
	for (int32 i = 0; i < CirclePoints; ++i)
	{
		const float Angle = 2.f * PI * static_cast<float>(i) / static_cast<float>(CirclePoints);
		const FVector Local(CircleRadius * FMath::Cos(Angle), CircleRadius * FMath::Sin(Angle), CircleHeight);
		Path->AddSplinePoint(Local, ESplineCoordinateSpace::Local, false);
	}
	Path->SetClosedLoop(true, false);
	Path->UpdateSpline();
}

void AOCPerfCameraPath::UpdateCamera()
{
	const FVector Location = Path->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
	FRotator Rotation = Path->GetRotationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
	Rotation.Pitch = CameraPitch;
	Rotation.Roll = 0.f;
	Camera->SetWorldLocationAndRotation(Location, Rotation);
}

void AOCPerfCameraPath::BeginPlay()
{
	Super::BeginPlay();

	const TCHAR* Cmd = FCommandLine::Get();
	const bool bFromCommandLine = FParse::Param(Cmd, TEXT("ocperf"));
	if (!bAutoStart && !bFromCommandLine)
	{
		return;
	}

	LapsWanted = DefaultLaps;
	FParse::Value(Cmd, TEXT("ocperfloops="), LapsWanted);
	LapsWanted = FMath::Max(0, LapsWanted);
	bQuitWhenDone = FParse::Param(Cmd, TEXT("ocperfquit"));

	if (Path->GetSplineLength() < 1.f)
	{
		UE_LOG(LogOpenCity, Error, TEXT("OCPerf: path on %s is empty, nothing to measure."), *GetName());
		return;
	}

	StartTimestamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
	WarmupLeft = WarmupSeconds;
	Distance = 0.f;
	bActive = true;
	SetActorTickEnabled(true);
	UE_LOG(LogOpenCity, Display, TEXT("OCPerf: started on %s, laps=%d (0 = forever), warmup=%.1fs, path=%.0fm"),
		*GetWorld()->GetMapName(), LapsWanted, WarmupSeconds, Path->GetSplineLength() / 100.f);
}

void AOCPerfCameraPath::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bActive && LapSummaries.Num() > 0)
	{
		WriteReport(); // keep partial results if the run is cut short
	}
	bActive = false;
	Super::EndPlay(EndPlayReason);
}

bool AOCPerfCameraPath::EnsureViewTarget()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC)
	{
		return false;
	}
	// Possession can reset the view target, so check every frame (cheap).
	if (PC->GetViewTarget() != this)
	{
		PC->SetViewTarget(this);
	}
	return true;
}

void AOCPerfCameraPath::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bActive || !EnsureViewTarget())
	{
		return;
	}

	if (WarmupLeft > 0.f)
	{
		WarmupLeft -= DeltaSeconds;
		if (WarmupLeft <= 0.f)
		{
			bMeasuring = true;
			Distance = 0.f;
			CurrentLap.Reset();
			UE_LOG(LogOpenCity, Display, TEXT("OCPerf: warmup done, measuring."));
		}
	}

	if (bMeasuring)
	{
		FOCFrameSample Sample;
		Sample.FrameMs = DeltaSeconds * 1000.f;
		Sample.GameMs = static_cast<float>(FPlatformTime::ToMilliseconds(GGameThreadTime));
		Sample.RenderMs = static_cast<float>(FPlatformTime::ToMilliseconds(GRenderThreadTime));
		Sample.GpuMs = static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
		CurrentLap.Add(Sample);
	}

	const float Length = Path->GetSplineLength();
	Distance += SpeedCmPerSec * DeltaSeconds;
	if (Distance >= Length)
	{
		Distance = FMath::Fmod(Distance, Length);
		if (bMeasuring)
		{
			FinishLap();
		}
	}
	UpdateCamera();
}

void AOCPerfCameraPath::FinishLap()
{
	const FOCFrameStatsSummary Summary = CurrentLap.Summarize();
	LapSummaries.Add(Summary);
	CurrentLap.Reset();
	UE_LOG(LogOpenCity, Display, TEXT("OCPerf: lap %d %s"), LapSummaries.Num(), *Summary.ToString());

	if (LapsWanted == 0)
	{
		// Stability runs: rewrite the report every lap so a freeze still leaves data on disk.
		WriteReport();
		return;
	}
	if (LapSummaries.Num() >= LapsWanted)
	{
		Finish();
	}
}

void AOCPerfCameraPath::Finish()
{
	WriteReport();
	bActive = false;
	bMeasuring = false;
	SetActorTickEnabled(false);
	UE_LOG(LogOpenCity, Display, TEXT("OCPerf: finished %d lap(s)."), LapSummaries.Num());

	if (bQuitWhenDone)
	{
		UKismetSystemLibrary::QuitGame(this, GetWorld()->GetFirstPlayerController(), EQuitPreference::Quit, false);
	}
}

void AOCPerfCameraPath::WriteReport() const
{
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("Profiling") / TEXT("OCPerf");
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString File = Dir / FString::Printf(TEXT("%s_%s.json"), *GetWorld()->GetMapName(), *StartTimestamp);

	FString Laps;
	for (int32 i = 0; i < LapSummaries.Num(); ++i)
	{
		const FOCFrameStatsSummary& S = LapSummaries[i];
		Laps += FString::Printf(
			TEXT("%s\n    {\"lap\": %d, \"frames\": %d, \"avg_ms\": %.3f, \"avg_fps\": %.2f, \"p50_ms\": %.3f, \"p99_ms\": %.3f, \"max_ms\": %.3f, \"one_percent_low_fps\": %.2f, \"game_ms\": %.3f, \"render_ms\": %.3f, \"gpu_ms\": %.3f}"),
			i == 0 ? TEXT("") : TEXT(","), i + 1, S.NumFrames, S.AvgFrameMs, S.AvgFps, S.P50FrameMs, S.P99FrameMs,
			S.MaxFrameMs, S.OnePercentLowFps, S.AvgGameMs, S.AvgRenderMs, S.AvgGpuMs);
	}

	const FString Json = FString::Printf(
		TEXT("{\n  \"map\": \"%s\",\n  \"started\": \"%s\",\n  \"rhi\": \"%s\",\n  \"gpu\": \"%s\",\n  \"resolution\": \"%dx%d\",\n  \"laps\": [%s\n  ]\n}\n"),
		*GetWorld()->GetMapName(), *StartTimestamp, GDynamicRHI ? GDynamicRHI->GetName() : TEXT("none"),
		*GRHIAdapterName, GSystemResolution.ResX, GSystemResolution.ResY, *Laps);

	FFileHelper::SaveStringToFile(Json, *File);
}
