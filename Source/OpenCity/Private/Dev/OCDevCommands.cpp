// Dev-only console commands and a timed command script, for scripted smoke tests of
// gameplay paths that need a running game (enter/drive/exit). Compiled out of Shipping.
//
//   -ocdevscript="2:oc.dev.status;3:oc.dev.interact;8:quit"
//   runs each command at the given seconds after the first world starts playing.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "ChaosWheeledVehicleMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "OCLog.h"
#include "Player/OCInteractionComponent.h"
#include "TimerManager.h"
#include "Vehicles/OCMotorbike.h"
#include "Vehicles/OCVehicle.h"
#include "Vehicles/OCVehicleComponents.h"

namespace OCDev
{
	APawn* PlayerPawn(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC ? PC->GetPawn() : nullptr;
	}

	void Status(const TArray<FString>& Args, UWorld* World)
	{
		const APawn* P = PlayerPawn(World);
		if (!P)
		{
			UE_LOG(LogOpenCity, Display, TEXT("OCDEV status: no player pawn"));
			return;
		}
		FString Extra;
		if (const AOCVehicle* V = Cast<AOCVehicle>(P))
		{
			const UChaosVehicleMovementComponent* M = V->GetVehicleMovementComponent();
			Extra = FString::Printf(TEXT(" speed=%.1fkmh gear=%d health=%.0f%% perf=%.2f wrecked=%d radio=%d"),
				V->GetSpeedKmh(), M ? M->GetCurrentGear() : 0, V->GetDamage()->GetHealthFraction() * 100.f,
				V->GetDamage()->GetPerformance(), V->IsWrecked(), V->GetRadio()->GetTuner().GetIndex());
			if (const AOCMotorbike* B = Cast<AOCMotorbike>(V))
			{
				Extra += FString::Printf(TEXT(" lean=%.1f roll=%.1f"), B->GetLeanDegrees(), B->GetActorRotation().Roll);
			}
		}
		UE_LOG(LogOpenCity, Display, TEXT("OCDEV status: pawn=%s loc=%s hidden=%d%s"), *P->GetClass()->GetName(),
			*P->GetActorLocation().ToCompactString(), P->IsHidden(), *Extra);
	}

	void Goto(const TArray<FString>& Args, UWorld* World)
	{
		APawn* P = PlayerPawn(World);
		if (P && Args.Num() >= 3)
		{
			const FVector To(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2]));
			P->SetActorLocation(To, false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogOpenCity, Display, TEXT("OCDEV goto %s"), *To.ToCompactString());
		}
	}

	void Interact(const TArray<FString>& Args, UWorld* World)
	{
		APawn* P = PlayerPawn(World);
		UOCInteractionComponent* I = P ? P->FindComponentByClass<UOCInteractionComponent>() : nullptr;
		const bool bOk = I && I->TryInteract();
		UE_LOG(LogOpenCity, Display, TEXT("OCDEV interact: %s"), bOk ? TEXT("ok") : TEXT("nothing"));
	}

	void ExitVehicle(const TArray<FString>& Args, UWorld* World)
	{
		AOCVehicle* V = Cast<AOCVehicle>(PlayerPawn(World));
		UE_LOG(LogOpenCity, Display, TEXT("OCDEV exitvehicle: %s"), V ? TEXT("requested") : TEXT("not in a vehicle"));
		if (V)
		{
			V->RequestExit();
		}
	}

	void Drive(const TArray<FString>& Args, UWorld* World)
	{
		AOCVehicle* V = Cast<AOCVehicle>(PlayerPawn(World));
		UChaosVehicleMovementComponent* M = V ? V->GetVehicleMovementComponent() : nullptr;
		if (!M)
		{
			UE_LOG(LogOpenCity, Display, TEXT("OCDEV drive: not in a vehicle"));
			return;
		}
		const float Throttle = Args.IsValidIndex(0) ? FCString::Atof(*Args[0]) : 0.f;
		const float Steer = Args.IsValidIndex(1) ? FCString::Atof(*Args[1]) : 0.f;
		const float Brake = Args.IsValidIndex(2) ? FCString::Atof(*Args[2]) : 0.f;
		M->SetHandbrakeInput(false);
		M->SetThrottleInput(V->IsWrecked() ? 0.f : Throttle);
		M->SetSteeringInput(Steer);
		M->SetBrakeInput(Brake);
		UE_LOG(LogOpenCity, Display, TEXT("OCDEV drive throttle=%.2f steer=%.2f brake=%.2f"), Throttle, Steer, Brake);
	}

	void Impact(const TArray<FString>& Args, UWorld* World)
	{
		AOCVehicle* V = Cast<AOCVehicle>(PlayerPawn(World));
		if (V && Args.Num() >= 1)
		{
			V->GetDamage()->ApplyImpact(FCString::Atof(*Args[0]));
		}
	}

	void ResetVehicle(const TArray<FString>& Args, UWorld* World)
	{
		if (AOCVehicle* V = Cast<AOCVehicle>(PlayerPawn(World)))
		{
			V->ResetUpright();
			UE_LOG(LogOpenCity, Display, TEXT("OCDEV resetvehicle"));
		}
	}

	/** Roll the driven vehicle over with an angular impulse, to test crash recovery. */
	void Flip(const TArray<FString>& Args, UWorld* World)
	{
		if (AOCVehicle* V = Cast<AOCVehicle>(PlayerPawn(World)))
		{
			const float DegPerSec = Args.IsValidIndex(0) ? FCString::Atof(*Args[0]) : 400.f;
			V->GetMesh()->SetPhysicsAngularVelocityInDegrees(V->GetActorForwardVector() * DegPerSec);
			V->GetMesh()->AddImpulse(FVector(0.f, 0.f, 400.f), NAME_None, /*bVelChange*/ true);
			UE_LOG(LogOpenCity, Display, TEXT("OCDEV flip %.0f deg/s"), DegPerSec);
		}
	}

	/** Tilt the driven vehicle to an exact roll and release it at rest (stabiliser tests). */
	void Tilt(const TArray<FString>& Args, UWorld* World)
	{
		if (AOCVehicle* V = Cast<AOCVehicle>(PlayerPawn(World)))
		{
			const float Roll = Args.IsValidIndex(0) ? FCString::Atof(*Args[0]) : 10.f;
			FRotator R = V->GetActorRotation();
			R.Roll = Roll;
			V->SetActorLocationAndRotation(V->GetActorLocation() + FVector(0.f, 0.f, 15.f), R, false, nullptr, ETeleportType::TeleportPhysics);
			V->GetMesh()->SetPhysicsLinearVelocity(FVector::ZeroVector);
			V->GetMesh()->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			UE_LOG(LogOpenCity, Display, TEXT("OCDEV tilt roll=%.1f"), Roll);
		}
	}

	void Radio(const TArray<FString>& Args, UWorld* World)
	{
		if (AOCVehicle* V = Cast<AOCVehicle>(PlayerPawn(World)))
		{
			(Args.IsValidIndex(0) && Args[0] == TEXT("prev")) ? V->GetRadio()->Previous() : V->GetRadio()->Next();
		}
	}

	FAutoConsoleCommandWithWorldAndArgs CmdStatus(TEXT("oc.dev.status"), TEXT("Log player pawn state."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Status));
	FAutoConsoleCommandWithWorldAndArgs CmdGoto(TEXT("oc.dev.goto"), TEXT("Teleport the player pawn: X Y Z."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Goto));
	FAutoConsoleCommandWithWorldAndArgs CmdInteract(TEXT("oc.dev.interact"), TEXT("Press interact."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Interact));
	FAutoConsoleCommandWithWorldAndArgs CmdExit(TEXT("oc.dev.exitvehicle"), TEXT("Press exit vehicle."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExitVehicle));
	FAutoConsoleCommandWithWorldAndArgs CmdDrive(TEXT("oc.dev.drive"), TEXT("Set vehicle inputs: throttle steer brake."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Drive));
	FAutoConsoleCommandWithWorldAndArgs CmdImpact(TEXT("oc.dev.impact"), TEXT("Apply an impact in km/h to the driven vehicle."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Impact));
	FAutoConsoleCommandWithWorldAndArgs CmdReset(TEXT("oc.dev.resetvehicle"), TEXT("Press reset vehicle."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ResetVehicle));
	FAutoConsoleCommandWithWorldAndArgs CmdFlip(TEXT("oc.dev.flip"), TEXT("Roll the driven vehicle over: [deg/s]."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Flip));
	FAutoConsoleCommandWithWorldAndArgs CmdTilt(TEXT("oc.dev.tilt"), TEXT("Tilt the driven vehicle to a roll angle at rest: [deg]."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Tilt));
	FAutoConsoleCommandWithWorldAndArgs CmdRadio(TEXT("oc.dev.radio"), TEXT("Radio next (or 'prev')."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Radio));

	/** Runs -ocdevscript once, on the first tick of the first game world that has begun play. */
	void StartScript(UWorld* World, const FString& Script)
	{
		TArray<FString> Steps;
		Script.ParseIntoArray(Steps, TEXT(";"));
		UE_LOG(LogOpenCity, Display, TEXT("OCDEV script: %d steps on %s"), Steps.Num(), *World->GetMapName());
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		for (const FString& Step : Steps)
		{
			FString When, Cmd;
			if (!Step.Split(TEXT(":"), &When, &Cmd))
			{
				continue;
			}
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, Cmd]()
			{
				if (UWorld* W = WeakWorld.Get())
				{
					UE_LOG(LogOpenCity, Display, TEXT("OCDEV run: %s"), *Cmd);
					// Through the player's console like a typed command, so viewport commands
					// (HighResShot, stat ...) work too; fall back to the engine without a player.
					if (APlayerController* PC = W->GetFirstPlayerController())
					{
						PC->ConsoleCommand(Cmd, /*bWriteToLog*/ true);
					}
					else
					{
						GEngine->Exec(W, *Cmd);
					}
				}
			}), FMath::Max(0.01f, FCString::Atof(*When)), false);
		}
	}

	struct FScriptRunner
	{
		FScriptRunner()
		{
			FWorldDelegates::OnWorldTickStart.AddLambda([](UWorld* World, ELevelTick, float)
			{
				static bool bStarted = false;
				if (bStarted || !World || !World->IsGameWorld() || !World->HasBegunPlay())
				{
					return;
				}
				FString Script;
				if (FParse::Value(FCommandLine::Get(), TEXT("ocdevscript="), Script, /*bShouldStopOnSeparator*/ false))
				{
					bStarted = true;
					StartScript(World, Script);
				}
			});
		}
	};
	static FScriptRunner GScriptRunner;
}

#endif // !UE_BUILD_SHIPPING
