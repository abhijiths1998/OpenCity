// Visual-QA loop (ART_PIPELINE.md step 10), dev builds only:
//
//   oc.dev.qa [bookmarks asset] [hours=13,19.4,22.5] [settle=6] [quit]
//
// For each hour and each camera bookmark (UOCCameraBookmarks): set the time of day, look from the
// bookmark, wait for streaming and exposure to settle, measure frame and GPU time for 1.5 s, and
// take a HighResShot named QA_<hour>_<bookmark>. scripts/qa_shots.sh runs it and files the shots.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "Dev/OCCameraBookmarks.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "OCLog.h"
#include "RHI.h"
#include "World/OCWorldStateSubsystem.h"

namespace OCQA
{
	struct FRun
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<ACameraActor> Camera;
		TArray<FOCCameraBookmark> Marks;
		TArray<float> Hours;
		FTSTicker::FDelegateHandle Ticker;
		float Settle = 6.f;
		bool bQuit = false;
		int32 Index = 0;        // over Hours x Marks
		double Clock = 0.0;
		int32 Phase = 0;        // 0 = settle, 1 = measure, 2 = shot taken
		double FrameSum = 0.0, GpuSum = 0.0;
		int32 Frames = 0;

		const FOCCameraBookmark& Mark() const { return Marks[Index % Marks.Num()]; }
		float Hour() const { return Hours[Index / Marks.Num()]; }
		int32 Total() const { return Marks.Num() * Hours.Num(); }

		void Begin()
		{
			UWorld* W = World.Get();
			APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
			if (!W || !PC)
			{
				return;
			}
			if (UOCWorldStateSubsystem* WS = W->GetSubsystem<UOCWorldStateSubsystem>())
			{
				WS->SetClockRunning(false);
				WS->SetTimeOfDay(Hour());
			}
			const FOCCameraBookmark& M = Mark();
			Camera->SetActorLocationAndRotation(M.Location, M.Rotation);
			Camera->GetCameraComponent()->SetFieldOfView(M.FOV);
			PC->SetViewTarget(Camera.Get());
			Clock = 0.0;
			Phase = 0;
			FrameSum = GpuSum = 0.0;
			Frames = 0;
		}

		bool Tick(float Dt)
		{
			UWorld* W = World.Get();
			APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
			if (!PC || !Camera.IsValid())
			{
				return false;
			}
			Clock += Dt;
			if (Phase == 0 && Clock > Settle)
			{
				Phase = 1;
				Clock = 0.0;
			}
			else if (Phase == 1)
			{
				FrameSum += FApp::GetDeltaTime() * 1000.0;
				GpuSum += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
				++Frames;
				if (Clock > 1.5)
				{
					const FOCCameraBookmark& M = Mark();
					UE_LOG(LogOpenCity, Display, TEXT("OCDEV qa %d/%d %s (%s, %s) %.1f h: frame %.2f ms, gpu %.2f ms"), Index + 1, Total(),
						*M.Name.ToString(), *M.District.ToString(), *M.Kind.ToString(), Hour(), FrameSum / FMath::Max(1, Frames), GpuSum / FMath::Max(1, Frames));
					PC->ConsoleCommand(FString::Printf(TEXT("HighResShot 1600x900 filename=QA_%04d_%s"), FMath::RoundToInt32(FMath::Floor(Hour()) * 100 + FMath::Frac(Hour()) * 60), *M.Name.ToString()), true);
					Phase = 2;
					Clock = 0.0;
				}
			}
			else if (Phase == 2 && Clock > 1.5)
			{
				if (++Index >= Total())
				{
					UE_LOG(LogOpenCity, Display, TEXT("OCDEV qa DONE: %d shots"), Total());
					if (bQuit)
					{
						FPlatformMisc::RequestExit(false, TEXT("oc.dev.qa quit"));
					}
					return false;
				}
				Begin();
			}
			return true;
		}
	};

	static FRun GRun;

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		FString Asset = TEXT("/Game/Dev/DA_QABookmarks.DA_QABookmarks");
		TArray<float> Hours = { 13.f, 19.4f, 22.5f };
		float Settle = 6.f;
		bool bQuit = false;
		for (const FString& A : Args)
		{
			if (A.StartsWith(TEXT("/Game/"))) { Asset = A; }
			else if (A.StartsWith(TEXT("hours=")))
			{
				TArray<FString> Parts;
				A.Mid(6).ParseIntoArray(Parts, TEXT(","));
				Hours.Reset();
				for (const FString& P : Parts) { Hours.Add(FCString::Atof(*P)); }
			}
			else if (A.StartsWith(TEXT("settle="))) { Settle = FCString::Atof(*A.Mid(7)); }
			else if (A == TEXT("quit")) { bQuit = true; }
		}
		const UOCCameraBookmarks* Marks = LoadObject<UOCCameraBookmarks>(nullptr, *Asset);
		if (!PC || !Marks || Marks->Bookmarks.IsEmpty() || Hours.IsEmpty())
		{
			UE_LOG(LogOpenCity, Warning, TEXT("OCDEV qa: needs a player and bookmarks (%s)"), *Asset);
			return;
		}
		if (ACharacter* Body = Cast<ACharacter>(PC->GetPawn()))
		{
			Body->GetCharacterMovement()->DisableMovement();  // streaming follows the camera, not the body
		}
		if (GRun.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GRun.Ticker);
		}
		GRun = FRun();
		GRun.World = World;
		GRun.Marks = Marks->Bookmarks;
		GRun.Hours = Hours;
		GRun.Settle = Settle;
		GRun.bQuit = bQuit;
		GRun.Camera = World->SpawnActor<ACameraActor>();
		// Every frame while running: frame times are sampled per frame.
		GRun.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float Dt) { return GRun.Tick(Dt); }));
		UE_LOG(LogOpenCity, Display, TEXT("OCDEV qa START: %d bookmarks x %d hours"), GRun.Marks.Num(), Hours.Num());
		GRun.Begin();
	}

	FAutoConsoleCommandWithWorldAndArgs Cmd(TEXT("oc.dev.qa"),
		TEXT("Visual QA: screenshots from every camera bookmark at several hours: [asset] [hours=13,19.4,22.5] [settle=6] [quit]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
