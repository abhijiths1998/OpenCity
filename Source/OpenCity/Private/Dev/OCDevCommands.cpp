// Dev-only console commands and a timed command script, for scripted smoke tests of
// gameplay paths that need a running game (enter/drive/exit). Compiled out of Shipping.
//
//   -ocdevscript="2:oc.dev.status;3:oc.dev.interact;8:quit"
//   runs each command at the given seconds after the first world starts playing.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
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
#include "Engine/GameInstance.h"
#include "Progression/OCProgressionSubsystem.h"

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

	/** View the world from a fixed camera: X Y Z (cm) Pitch Yaw. "oc.dev.viewfrom off" returns to the pawn. */
	void ViewFrom(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!PC)
		{
			return;
		}
		ACharacter* Body = Cast<ACharacter>(PC->GetPawn());
		if (Args.IsValidIndex(0) && Args[0] == TEXT("off"))
		{
			PC->SetViewTarget(PC->GetPawn());
			if (Body)
			{
				Body->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
			}
			return;
		}
		if (Body)
		{
			// World Partition streams around the camera: hold the player still so they don't fall
			// through ground that unloads while the camera is elsewhere.
			Body->GetCharacterMovement()->DisableMovement();
		}
		if (Args.Num() < 5)
		{
			UE_LOG(LogOpenCity, Display, TEXT("OCDEV viewfrom: X Y Z Pitch Yaw"));
			return;
		}
		const FVector Loc(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2]));
		const FRotator Rot(FCString::Atof(*Args[3]), FCString::Atof(*Args[4]), 0.f);
		ACameraActor* Cam = World->SpawnActor<ACameraActor>(Loc, Rot);
		if (Cam)
		{
			Cam->GetCameraComponent()->SetFieldOfView(Args.IsValidIndex(5) ? FCString::Atof(*Args[5]) : 70.f);
			PC->SetViewTarget(Cam);
			UE_LOG(LogOpenCity, Display, TEXT("OCDEV viewfrom %s %s"), *Loc.ToCompactString(), *Rot.ToCompactString());
		}
	}

	/** Trace straight down at layout metres (x east, y north) and log the ground height. */
	void Ground(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			return;
		}
		const double X = FCString::Atod(*Args[0]) * 100.0;
		const double Y = -FCString::Atod(*Args[1]) * 100.0;
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(Hit, FVector(X, Y, 200000.0), FVector(X, Y, -200000.0), ECC_Visibility);
		UE_LOG(LogOpenCity, Display, TEXT("OCDEV ground %s %s: %s"), *Args[0], *Args[1],
			bHit ? *FString::Printf(TEXT("%.2f m (%s)"), Hit.ImpactPoint.Z / 100.0, *GetNameSafe(Hit.GetActor())) : TEXT("no hit"));
	}

	UOCProgressionSubsystem* Progression(UWorld* World)
	{
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UOCProgressionSubsystem>() : nullptr;
	}

	void LogProgress(UOCProgressionSubsystem* Prog)
	{
		TArray<FString> Closed;
		for (const FName& G : Prog->GetClosedGates())
		{
			Closed.Add(G.ToString());
		}
		Closed.Sort();
		UE_LOG(LogOpenCity, Display, TEXT("OCDEV progress: completed [%s]; closed gates [%s]"),
			*Prog->GetCompletedMilestones().ToStringSimple(), *FString::Join(Closed, TEXT(", ")));
	}

	/** Complete a story milestone: a full tag, a mission name (Funerale = Story.Mission.Funerale), or "all". */
	void Unlock(const TArray<FString>& Args, UWorld* World)
	{
		UOCProgressionSubsystem* Prog = Progression(World);
		if (!Prog || !Args.IsValidIndex(0))
		{
			UE_LOG(LogOpenCity, Display, TEXT("OCDEV unlock: oc.dev.unlock <Story.Mission.X | X | all>"));
			return;
		}
		if (Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase))
		{
			Prog->CompleteAllMilestones();
		}
		else
		{
			FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Args[0]), false);
			if (!Tag.IsValid())
			{
				Tag = FGameplayTag::RequestGameplayTag(FName(TEXT("Story.Mission.") + Args[0]), false);
			}
			if (!Tag.IsValid())
			{
				TArray<FString> Known;
				for (const FGameplayTag& M : Prog->GetScheduledMilestones())
				{
					Known.Add(M.ToString());
				}
				UE_LOG(LogOpenCity, Warning, TEXT("OCDEV unlock: unknown milestone %s (gates wait for: %s)"), *Args[0], *FString::Join(Known, TEXT(", ")));
				return;
			}
			Prog->CompleteMilestone(Tag);
		}
		LogProgress(Prog);
	}

	void Progress(const TArray<FString>& Args, UWorld* World)
	{
		if (UOCProgressionSubsystem* Prog = Progression(World))
		{
			LogProgress(Prog);
		}
	}

	void ResetProgress(const TArray<FString>& Args, UWorld* World)
	{
		if (UOCProgressionSubsystem* Prog = Progression(World))
		{
			Prog->ResetProgress();
			LogProgress(Prog);
		}
	}

	void Radio(const TArray<FString>& Args, UWorld* World)
	{
		if (AOCVehicle* V = Cast<AOCVehicle>(PlayerPawn(World)))
		{
			(Args.IsValidIndex(0) && Args[0] == TEXT("prev")) ? V->GetRadio()->Previous() : V->GetRadio()->Next();
		}
	}

	/** Spawn a vehicle 4 m in front of the player, on the ground: coupe | offroad | bike. */
	void Spawn(const TArray<FString>& Args, UWorld* World)
	{
		APawn* P = PlayerPawn(World);
		const FString Name = Args.IsValidIndex(0) ? Args[0].ToLower() : FString(TEXT("coupe"));
		const FString Bp = Name == TEXT("bike") ? TEXT("Bike") : Name == TEXT("offroad") ? TEXT("Offroad") : TEXT("Coupe");
		UClass* Class = LoadClass<AOCVehicle>(nullptr,
			*FString::Printf(TEXT("/Game/Vehicles/Blueprints/BP_OCVehicle_%s.BP_OCVehicle_%s_C"), *Bp, *Bp));
		if (!P || !Class)
		{
			UE_LOG(LogOpenCity, Warning, TEXT("OCDEV spawn %s: %s"), *Name, P ? TEXT("class not found") : TEXT("no player pawn"));
			return;
		}
		const FRotator Yaw(0.f, P->GetActorRotation().Yaw, 0.f);
		FVector Loc = P->GetActorLocation() + Yaw.Vector() * 400.0;
		FHitResult Hit;
		FCollisionQueryParams Q(TEXT("OCDevSpawn"), false, P);
		if (World->LineTraceSingleByChannel(Hit, Loc + FVector(0, 0, 500), Loc - FVector(0, 0, 2000), ECC_Visibility, Q))
		{
			Loc = Hit.ImpactPoint + FVector(0, 0, 60);
		}
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		const AActor* V = World->SpawnActor<AActor>(Class, Loc, Yaw + FRotator(0.f, 90.f, 0.f), SP);
		UE_LOG(LogOpenCity, Display, TEXT("OCDEV spawn %s: %s"), *Name, V ? *V->GetActorLocation().ToCompactString() : TEXT("failed"));
	}

	FAutoConsoleCommandWithWorldAndArgs CmdUnlock(TEXT("oc.dev.unlock"), TEXT("Complete a story milestone: Story.Mission.X, X, or all."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Unlock));
	FAutoConsoleCommandWithWorldAndArgs CmdProgress(TEXT("oc.dev.progress"), TEXT("Log completed milestones and closed gates."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Progress));
	FAutoConsoleCommandWithWorldAndArgs CmdResetProgress(TEXT("oc.dev.resetprogress"), TEXT("Forget all story progress."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ResetProgress));
	FAutoConsoleCommandWithWorldAndArgs CmdSpawn(TEXT("oc.dev.spawn"), TEXT("Spawn a vehicle in front of the player: coupe | offroad | bike."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Spawn));
	FAutoConsoleCommandWithWorldAndArgs CmdStatus(TEXT("oc.dev.status"), TEXT("Log player pawn state."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Status));
	FAutoConsoleCommandWithWorldAndArgs CmdGoto(TEXT("oc.dev.goto"), TEXT("Teleport the player pawn: X Y Z."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Goto));
	FAutoConsoleCommandWithWorldAndArgs CmdInteract(TEXT("oc.dev.interact"), TEXT("Press interact."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Interact));
	FAutoConsoleCommandWithWorldAndArgs CmdExit(TEXT("oc.dev.exitvehicle"), TEXT("Press exit vehicle."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExitVehicle));
	FAutoConsoleCommandWithWorldAndArgs CmdDrive(TEXT("oc.dev.drive"), TEXT("Set vehicle inputs: throttle steer brake."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Drive));
	FAutoConsoleCommandWithWorldAndArgs CmdImpact(TEXT("oc.dev.impact"), TEXT("Apply an impact in km/h to the driven vehicle."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Impact));
	FAutoConsoleCommandWithWorldAndArgs CmdReset(TEXT("oc.dev.resetvehicle"), TEXT("Press reset vehicle."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ResetVehicle));
	FAutoConsoleCommandWithWorldAndArgs CmdFlip(TEXT("oc.dev.flip"), TEXT("Roll the driven vehicle over: [deg/s]."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Flip));
	FAutoConsoleCommandWithWorldAndArgs CmdTilt(TEXT("oc.dev.tilt"), TEXT("Tilt the driven vehicle to a roll angle at rest: [deg]."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Tilt));
	FAutoConsoleCommandWithWorldAndArgs CmdViewFrom(TEXT("oc.dev.viewfrom"), TEXT("View from a fixed camera: X Y Z Pitch Yaw [FOV], or 'off'."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ViewFrom));
	FAutoConsoleCommandWithWorldAndArgs CmdGround(TEXT("oc.dev.ground"), TEXT("Log ground height at layout metres: X Y."), FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Ground));
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
