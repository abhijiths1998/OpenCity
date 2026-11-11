#pragma once

// Shared pieces of the milestone setup (formerly OCEditorHelpers's level, input and Blueprint helpers).

#include "CoreMinimal.h"

class AActor;
class AStaticMeshActor;
class UBlueprint;
class UInputAction;
class UInputMappingContext;
class UInputModifier;
class UInputTrigger;
enum class EInputActionValueType : uint8;

namespace OCMilestone
{
	/** Create a level, or load and empty an existing one so it is rebuilt in place (deleting a map the
	 *  editor has referenced, e.g. the startup map, fails in a commandlet). */
	void NewOrClearLevel(const FString& Path);
	void SaveCurrentLevel(const FString& Path, const TCHAR* Tag);

	/** Movable sun at SunPitch/SunYaw, sky atmosphere, height fog, real-time sky light (folder optional). */
	void Daylight(const FString& Folder, double SunPitch = -40.0, double SunYaw = 35.0, float FogDensity = -1.0f, float FogFalloff = -1.0f);

	/** A cube from a centre and a size in cm (the engine cube is 100 cm). */
	AStaticMeshActor* Block(const FString& Label, const FVector& Center, const FVector& Size, const FRotator& Rot = FRotator::ZeroRotator,
		const FString& Folder = TEXT("Course"));

	/** Get or create a Blueprint subclass of Parent, compiled. */
	UBlueprint* Blueprint(const FString& Name, const FString& Folder, UClass* Parent, const TCHAR* Tag);
	/** The class default object of a compiled Blueprint. */
	UObject* Cdo(UBlueprint* Bp);

	/** An input action of the given value type. */
	UInputAction* Action(const FString& Name, const FString& Folder, EInputActionValueType Type, const TCHAR* Tag);

	/** One key mapping row: action, key name, modifiers and triggers (created with the context as outer). */
	struct FRow
	{
		UInputAction* Action;
		const TCHAR* Key;
		TArray<UInputModifier*> Modifiers;
		TArray<UInputTrigger*> Triggers;
	};
	/** Replace a mapping context's mappings with Rows (made by a callback, so modifiers get the context as outer). */
	UInputMappingContext* Context(const FString& Name, const FString& Folder, TFunctionRef<TArray<FRow>(UInputMappingContext*)> Rows, const TCHAR* Tag);

	UInputModifier* Negate(UObject* Outer, bool bX = true, bool bY = true, bool bZ = true);
	UInputModifier* Swizzle(UObject* Outer);
	UInputModifier* DeadZone(UObject* Outer, float Lower = 0.2f, bool bRadial = true);
	UInputModifier* Scalar(UObject* Outer, double X, double Y, double Z = 1.0);
	UInputModifier* ScaleByDt(UObject* Outer);
	UInputTrigger* Down(UObject* Outer, float Threshold);

	/** Object path text for SetProp ("/Game/X/Y.Y"), or "None". */
	FString Ref(const UObject* O);

	bool SetupM0();
	bool SetupM1();
	bool SetupM2();
	bool VerifyM1();
	bool VerifyM2();
}
