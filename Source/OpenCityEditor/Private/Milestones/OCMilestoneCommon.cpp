#include "Milestones/OCMilestoneCommon.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Brush.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EnhancedActionKeyMapping.h"
#include "EngineUtils.h"
#include "Factories/BlueprintFactory.h"
#include "GameFramework/WorldSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LevelEditorSubsystem.h"
#include "OCEditorHelpers.h"
#include "OCLog.h"
#include "Subsystems/EditorActorSubsystem.h"

namespace OCMilestone
{
	namespace
	{
		/** A factory by class path (the Enhanced Input editor module isn't a dependency); null makes a plain object. */
		UFactory* FactoryByPath(const TCHAR* Path)
		{
			UClass* C = StaticLoadClass(UFactory::StaticClass(), nullptr, Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
			return C ? NewObject<UFactory>(GetTransientPackage(), C) : nullptr;
		}
	}

	void NewOrClearLevel(const FString& Path)
	{
		ULevelEditorSubsystem* Levels = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
		if (UEditorAssetLibrary::DoesAssetExist(Path))
		{
			if (!Levels->LoadLevel(Path))
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("could not load %s"), *Path);
			}
			UEditorActorSubsystem* Actors = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
			auto Doomed = [Actors]()
			{
				TArray<AActor*> Out;
				for (AActor* A : Actors->GetAllLevelActors())
				{
					if (!A->IsA<AWorldSettings>() && !A->IsA<ABrush>()) { Out.Add(A); }
				}
				return Out;
			};
			const TArray<AActor*> Old = Doomed();
			if (!Old.IsEmpty())
			{
				Actors->DestroyActors(Old);
			}
			const int32 Left = Doomed().Num();
			if (Left > 0)
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("could not clear %s: %d actors left"), *Path, Left);
			}
			return;
		}
		if (!Levels->NewLevel(Path))
		{
			UE_LOG(LogOpenCity, Fatal, TEXT("new level failed for %s"), *Path);
		}
	}

	void SaveCurrentLevel(const FString& Path, const TCHAR* Tag)
	{
		if (!GEditor->GetEditorSubsystem<ULevelEditorSubsystem>()->SaveCurrentLevel())
		{
			UE_LOG(LogOpenCity, Fatal, TEXT("save failed for %s"), *Path);
		}
		OCEd::Log(Tag, TEXT("saved ") + Path);
	}

	void Daylight(const FString& Folder, double SunPitch, double SunYaw, float FogDensity, float FogFalloff)
	{
		AActor* Sun = OCEd::Spawn(ADirectionalLight::StaticClass(), FVector(0, 0, 1000), FRotator(SunPitch, SunYaw, 0), TEXT("Sun"), Folder);
		UDirectionalLightComponent* SunC = Sun->FindComponentByClass<UDirectionalLightComponent>();
		OCEd::SetProp(SunC, TEXT("Mobility"), TEXT("Movable"));
		OCEd::SetProp(SunC, TEXT("bAtmosphereSunLight"), TEXT("True"));
		OCEd::SetProp(SunC, TEXT("Intensity"), TEXT("10.0"));
		OCEd::Spawn(StaticLoadClass(AActor::StaticClass(), nullptr, TEXT("/Script/Engine.SkyAtmosphere")), FVector::ZeroVector, FRotator::ZeroRotator, TEXT("SkyAtmosphere"), Folder);
		AActor* Fog = OCEd::Spawn(AExponentialHeightFog::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, TEXT("HeightFog"), Folder);
		UExponentialHeightFogComponent* FogC = Fog->FindComponentByClass<UExponentialHeightFogComponent>();
		if (FogDensity >= 0.0f) { OCEd::SetProp(FogC, TEXT("FogDensity"), FString::SanitizeFloat(FogDensity)); }
		if (FogFalloff >= 0.0f) { OCEd::SetProp(FogC, TEXT("FogHeightFalloff"), FString::SanitizeFloat(FogFalloff)); }
		AActor* Sky = OCEd::Spawn(ASkyLight::StaticClass(), FVector(0, 0, 500), FRotator::ZeroRotator, TEXT("SkyLight"), Folder);
		USkyLightComponent* SkyC = Sky->FindComponentByClass<USkyLightComponent>();
		OCEd::SetProp(SkyC, TEXT("Mobility"), TEXT("Movable"));
		OCEd::SetProp(SkyC, TEXT("bRealTimeCapture"), TEXT("True"));
	}

	AStaticMeshActor* Block(const FString& Label, const FVector& Center, const FVector& Size, const FRotator& Rot, const FString& Folder)
	{
		AStaticMeshActor* A = CastChecked<AStaticMeshActor>(OCEd::Spawn(AStaticMeshActor::StaticClass(), Center, Rot, Label, Folder));
		A->GetStaticMeshComponent()->SetStaticMesh(CastChecked<UStaticMesh>(OCEd::Require(TEXT("/Engine/BasicShapes/Cube"))));
		A->SetActorScale3D(Size / 100.0);
		return A;
	}

	UBlueprint* Blueprint(const FString& Name, const FString& Folder, UClass* Parent, const TCHAR* Tag)
	{
		UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
		Factory->ParentClass = Parent;
		UBlueprint* Bp = CastChecked<UBlueprint>(OCEd::GetOrCreate(Name, Folder, UBlueprint::StaticClass(), Factory, Tag));
		FKismetEditorUtilities::CompileBlueprint(Bp);
		return Bp;
	}

	UObject* Cdo(UBlueprint* Bp)
	{
		return Bp->GeneratedClass->GetDefaultObject();
	}

	UInputAction* Action(const FString& Name, const FString& Folder, EInputActionValueType Type, const TCHAR* Tag)
	{
		UInputAction* A = CastChecked<UInputAction>(OCEd::GetOrCreate(Name, Folder, UInputAction::StaticClass(),
			FactoryByPath(TEXT("/Script/InputEditor.InputAction_Factory")), Tag));
		A->Modify();
		A->ValueType = Type;
		A->PostEditChange();
		OCEd::Save(A);
		return A;
	}

	UInputMappingContext* Context(const FString& Name, const FString& Folder, TFunctionRef<TArray<FRow>(UInputMappingContext*)> Rows, const TCHAR* Tag)
	{
		UInputMappingContext* Imc = CastChecked<UInputMappingContext>(OCEd::GetOrCreate(Name, Folder, UInputMappingContext::StaticClass(),
			FactoryByPath(TEXT("/Script/InputEditor.InputMappingContext_Factory")), Tag));
		Imc->Modify();
		Imc->UnmapAll();
		const TArray<FRow> All = Rows(Imc);
		for (const FRow& R : All)
		{
			FEnhancedActionKeyMapping& M = Imc->MapKey(R.Action, FKey(R.Key));
			M.Modifiers.Reset();
			for (UInputModifier* Mod : R.Modifiers) { M.Modifiers.Add(Mod); }
			M.Triggers.Reset();
			for (UInputTrigger* T : R.Triggers) { M.Triggers.Add(T); }
		}
		Imc->PostEditChange();
		OCEd::Save(Imc);
		OCEd::Log(Tag, FString::Printf(TEXT("%s: %d mappings"), *Name, All.Num()));
		return Imc;
	}

	UInputModifier* Negate(UObject* Outer, bool bX, bool bY, bool bZ)
	{
		UInputModifierNegate* M = NewObject<UInputModifierNegate>(Outer);
		M->bX = bX; M->bY = bY; M->bZ = bZ;
		return M;
	}

	UInputModifier* Swizzle(UObject* Outer)
	{
		UInputModifierSwizzleAxis* M = NewObject<UInputModifierSwizzleAxis>(Outer);
		M->Order = EInputAxisSwizzle::YXZ;
		return M;
	}

	UInputModifier* DeadZone(UObject* Outer, float Lower, bool bRadial)
	{
		UInputModifierDeadZone* M = NewObject<UInputModifierDeadZone>(Outer);
		M->Type = bRadial ? EDeadZoneType::Radial : EDeadZoneType::Axial;
		M->LowerThreshold = Lower;
		return M;
	}

	UInputModifier* Scalar(UObject* Outer, double X, double Y, double Z)
	{
		UInputModifierScalar* M = NewObject<UInputModifierScalar>(Outer);
		M->Scalar = FVector(X, Y, Z);
		return M;
	}

	UInputModifier* ScaleByDt(UObject* Outer) { return NewObject<UInputModifierScaleByDeltaTime>(Outer); }

	UInputTrigger* Down(UObject* Outer, float Threshold)
	{
		UInputTriggerDown* T = NewObject<UInputTriggerDown>(Outer);
		T->ActuationThreshold = Threshold;
		return T;
	}

	FString Ref(const UObject* O)
	{
		return O ? O->GetPathName() : FString(TEXT("None"));
	}
}
