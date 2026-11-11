#pragma once

// Shared helpers for the OpenCity editor commandlets (world build, materials, kit and asset imports),
// which replaced the editor Python scripts: assets, properties by name, actors, material graphs.

#include "CoreMinimal.h"
#include "Materials/MaterialExpressionCustom.h"
#include "SceneTypes.h"
#include "Materials/MaterialInterface.h"

class AActor;
class UFactory;
class UMaterial;
class UMaterialExpression;

namespace OCEd
{
	void Log(const TCHAR* Tag, const FString& Msg);

	/** Load an asset by package path ("/Game/X/Y" or "/Game/X/Y.Y"); null if missing. */
	UObject* LoadAsset(const FString& Path);
	template <typename T> T* Load(const FString& Path) { return Cast<T>(LoadAsset(Path)); }
	/** Like Load, but a missing asset is a fatal error. */
	UObject* Require(const FString& Path);

	/** The asset at Folder/Name, or a new one made by Factory (null Factory: NewObject in a new package). */
	UObject* GetOrCreate(const FString& Name, const FString& Folder, UClass* Class, UFactory* Factory, const TCHAR* Tag);
	/** A data asset of Class at Folder/Name. */
	UObject* GetOrCreateDataAsset(const FString& Name, const FString& Folder, UClass* Class, const TCHAR* Tag);
	/** Save an asset's package (fatal on failure, like the scripts' L.save). */
	void Save(UObject* Asset);

	/** Set a property by name from its text form (as Python's set_editor_property did; works on protected ones). */
	bool SetProp(UObject* Obj, FName Prop, const FString& Text);
	bool SetPropOn(void* Container, UStruct* Struct, FName Prop, const FString& Text);

	/** Spawn an actor in the editor world, labelled and foldered. */
	AActor* Spawn(UClass* Class, const FVector& Location, const FRotator& Rotation, const FString& Label = FString(), const FString& Folder = FString());

	/** Turn on a material usage flag (the old bUsedWith* properties). */
	void Usage(UMaterial* M, EMaterialUsage U);

	/** Material graph building (UMaterialEditingLibrary underneath). */
	struct FMat
	{
		UMaterial* M = nullptr;
		/** Get or create the material at Folder/Name and empty its graph. */
		static FMat Fresh(const FString& Name, const FString& Folder, const TCHAR* Tag);

		UMaterialExpression* Expr(UClass* Class, int32 X, int32 Y);
		UMaterialExpression* Const(float V, int32 X, int32 Y);
		UMaterialExpression* Color(const FLinearColor& C, int32 X, int32 Y);
		UMaterialExpression* UV(int32 Index, int32 X, int32 Y);
		UMaterialExpression* WorldPos(int32 X, int32 Y);
		UMaterialExpression* Scalar(const FName& Name, float Default, int32 X, int32 Y);
		UMaterialExpression* Vector(const FName& Name, const FLinearColor& Default, int32 X, int32 Y);
		UMaterialExpression* InstanceData(int32 Index, float Default, int32 X, int32 Y);
		/** A Custom HLSL node; inputs are (name, expression), extra outputs (name, type). */
		UMaterialExpressionCustom* Custom(const FString& Code, const TArray<TPair<FString, UMaterialExpression*>>& Inputs, int32 X, int32 Y,
			ECustomMaterialOutputType Out = CMOT_Float3, const TArray<TPair<FString, ECustomMaterialOutputType>>& Extra = {});
		void Connect(UMaterialExpression* From, const FString& FromOut, UMaterialExpression* To, const FString& ToIn);
		void Output(UMaterialExpression* From, const FString& FromOut, EMaterialProperty Property);
		/** Recompile and save. */
		void Finish(const TCHAR* Tag);
	};

	/** Project paths. */
	FString ProjectDir();
	FString WorldDir();   // SourceArt/World
}
