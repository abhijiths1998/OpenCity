#include "OCEditorHelpers.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/DataAsset.h"
#include "Factories/DataAssetFactory.h"
#include "Factories/MaterialFactoryNew.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/Paths.h"
#include "OCLog.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/Package.h"

namespace OCEd
{
	void Log(const TCHAR* Tag, const FString& Msg)
	{
		UE_LOG(LogOpenCity, Display, TEXT("[%s] %s"), Tag, *Msg);
	}

	UObject* LoadAsset(const FString& Path)
	{
		FString Object = Path;
		if (!Path.Contains(TEXT(".")))
		{
			Object = Path + TEXT(".") + FPaths::GetBaseFilename(Path);
		}
		return StaticLoadObject(UObject::StaticClass(), nullptr, *Object, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	UObject* Require(const FString& Path)
	{
		UObject* O = LoadAsset(Path);
		if (!O)
		{
			UE_LOG(LogOpenCity, Fatal, TEXT("missing asset %s"), *Path);
		}
		return O;
	}

	UObject* GetOrCreate(const FString& Name, const FString& Folder, UClass* Class, UFactory* Factory, const TCHAR* Tag)
	{
		const FString Path = Folder / Name;
		if (UObject* Existing = LoadAsset(Path))
		{
			return Existing;
		}
		UObject* Asset = nullptr;
		if (Factory)
		{
			Asset = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().CreateAsset(Name, Folder, Class, Factory);
		}
		else
		{
			UPackage* Package = CreatePackage(*Path);
			Asset = NewObject<UObject>(Package, Class, *Name, RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Asset);
			Package->MarkPackageDirty();
		}
		if (!Asset)
		{
			UE_LOG(LogOpenCity, Fatal, TEXT("could not create %s"), *Path);
		}
		Log(Tag, TEXT("created ") + Path);
		return Asset;
	}

	UObject* GetOrCreateDataAsset(const FString& Name, const FString& Folder, UClass* Class, const TCHAR* Tag)
	{
		UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
		Factory->DataAssetClass = Class;
		return GetOrCreate(Name, Folder, Class, Factory, Tag);
	}

	void Save(UObject* Asset)
	{
		if (!UEditorAssetLibrary::SaveLoadedAsset(Asset, /*bOnlyIfIsDirty*/ false))
		{
			UE_LOG(LogOpenCity, Fatal, TEXT("could not save %s"), *Asset->GetPathName());
		}
	}

	bool SetPropOn(void* Container, UStruct* Struct, FName Prop, const FString& Text)
	{
		FProperty* P = Struct->FindPropertyByName(Prop);
		if (!P)
		{
			UE_LOG(LogOpenCity, Error, TEXT("no property %s on %s"), *Prop.ToString(), *Struct->GetName());
			return false;
		}
		return P->ImportText_InContainer(*Text, Container, nullptr, PPF_None) != nullptr;
	}

	bool SetProp(UObject* Obj, FName Prop, const FString& Text)
	{
		Obj->PreEditChange(nullptr);
		const bool bOk = SetPropOn(Obj, Obj->GetClass(), Prop, Text);
		FProperty* P = Obj->GetClass()->FindPropertyByName(Prop);
		FPropertyChangedEvent Event(P);
		Obj->PostEditChangeProperty(Event);
		return bOk;
	}

	AActor* Spawn(UClass* Class, const FVector& Location, const FRotator& Rotation, const FString& Label, const FString& Folder)
	{
		UEditorActorSubsystem* Actors = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
		AActor* A = Actors->SpawnActorFromClass(Class, Location, Rotation);
		if (!A)
		{
			UE_LOG(LogOpenCity, Fatal, TEXT("failed to spawn %s"), *Class->GetName());
		}
		if (!Label.IsEmpty()) { A->SetActorLabel(Label); }
		if (!Folder.IsEmpty()) { A->SetFolderPath(*Folder); }
		return A;
	}

	FMat FMat::Fresh(const FString& Name, const FString& Folder, const TCHAR* Tag)
	{
		FMat R;
		R.M = Cast<UMaterial>(GetOrCreate(Name, Folder, UMaterial::StaticClass(), NewObject<UMaterialFactoryNew>(), Tag));
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(R.M);
		return R;
	}

	UMaterialExpression* FMat::Expr(UClass* Class, int32 X, int32 Y)
	{
		return UMaterialEditingLibrary::CreateMaterialExpression(M, Class, X, Y);
	}

	UMaterialExpression* FMat::Const(float V, int32 X, int32 Y)
	{
		UMaterialExpressionConstant* E = Cast<UMaterialExpressionConstant>(Expr(UMaterialExpressionConstant::StaticClass(), X, Y));
		E->R = V;
		return E;
	}

	UMaterialExpression* FMat::Color(const FLinearColor& C, int32 X, int32 Y)
	{
		UMaterialExpressionConstant3Vector* E = Cast<UMaterialExpressionConstant3Vector>(Expr(UMaterialExpressionConstant3Vector::StaticClass(), X, Y));
		E->Constant = FLinearColor(C.R, C.G, C.B, 1.0f);
		return E;
	}

	UMaterialExpression* FMat::UV(int32 Index, int32 X, int32 Y)
	{
		UMaterialExpressionTextureCoordinate* E = Cast<UMaterialExpressionTextureCoordinate>(Expr(UMaterialExpressionTextureCoordinate::StaticClass(), X, Y));
		E->CoordinateIndex = Index;
		return E;
	}

	UMaterialExpression* FMat::WorldPos(int32 X, int32 Y)
	{
		return Expr(UMaterialExpressionWorldPosition::StaticClass(), X, Y);
	}

	UMaterialExpression* FMat::Scalar(const FName& Name, float Default, int32 X, int32 Y)
	{
		UMaterialExpressionScalarParameter* E = Cast<UMaterialExpressionScalarParameter>(Expr(UMaterialExpressionScalarParameter::StaticClass(), X, Y));
		E->ParameterName = Name;
		E->DefaultValue = Default;
		return E;
	}

	UMaterialExpression* FMat::Vector(const FName& Name, const FLinearColor& Default, int32 X, int32 Y)
	{
		UMaterialExpressionVectorParameter* E = Cast<UMaterialExpressionVectorParameter>(Expr(UMaterialExpressionVectorParameter::StaticClass(), X, Y));
		E->ParameterName = Name;
		E->DefaultValue = Default;
		return E;
	}

	UMaterialExpression* FMat::InstanceData(int32 Index, float Default, int32 X, int32 Y)
	{
		UMaterialExpressionPerInstanceCustomData* E = Cast<UMaterialExpressionPerInstanceCustomData>(Expr(UMaterialExpressionPerInstanceCustomData::StaticClass(), X, Y));
		E->DataIndex = Index;
		E->ConstDefaultValue = Default;
		return E;
	}

	UMaterialExpressionCustom* FMat::Custom(const FString& Code, const TArray<TPair<FString, UMaterialExpression*>>& Inputs, int32 X, int32 Y,
		ECustomMaterialOutputType Out, const TArray<TPair<FString, ECustomMaterialOutputType>>& Extra)
	{
		UMaterialExpressionCustom* E = Cast<UMaterialExpressionCustom>(Expr(UMaterialExpressionCustom::StaticClass(), X, Y));
		E->Code = Code;
		E->OutputType = Out;
		E->Inputs.Reset();
		for (const TPair<FString, UMaterialExpression*>& In : Inputs)
		{
			FCustomInput CI;
			CI.InputName = *In.Key;
			E->Inputs.Add(CI);
		}
		E->AdditionalOutputs.Reset();
		for (const TPair<FString, ECustomMaterialOutputType>& O : Extra)
		{
			FCustomOutput CO;
			CO.OutputName = *O.Key;
			CO.OutputType = O.Value;
			E->AdditionalOutputs.Add(CO);
		}
		E->RebuildOutputs();
		for (const TPair<FString, UMaterialExpression*>& In : Inputs)
		{
			Connect(In.Value, FString(), E, In.Key);
		}
		return E;
	}

	void FMat::Connect(UMaterialExpression* From, const FString& FromOut, UMaterialExpression* To, const FString& ToIn)
	{
		if (!UMaterialEditingLibrary::ConnectMaterialExpressions(From, FromOut, To, ToIn))
		{
			UE_LOG(LogOpenCity, Error, TEXT("%s: could not connect %s.%s -> %s.%s"), *M->GetName(), *From->GetName(), *FromOut, *To->GetName(), *ToIn);
		}
	}

	void FMat::Output(UMaterialExpression* From, const FString& FromOut, EMaterialProperty Property)
	{
		if (!UMaterialEditingLibrary::ConnectMaterialProperty(From, FromOut, Property))
		{
			UE_LOG(LogOpenCity, Error, TEXT("%s: could not connect %s.%s to a material output"), *M->GetName(), *From->GetName(), *FromOut);
		}
	}

	void FMat::Finish(const TCHAR* Tag)
	{
		UMaterialEditingLibrary::RecompileMaterial(M);
		Save(M);
		Log(Tag, TEXT("built ") + M->GetPathName());
	}

	FString ProjectDir() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()); }
	FString WorldDir() { return ProjectDir() / TEXT("SourceArt/World"); }
}

void OCEd::Usage(UMaterial* M, EMaterialUsage U)
{
	bool bRecompile = false;
	UMaterialEditingLibrary::SetMaterialUsage(M, U, bRecompile);
}

// ---- Material graph extras, instances, imports ----------------------------------------------------

#include "AssetImportTask.h"
#include "Engine/Texture.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialParameterCollection.h"

namespace OCEd
{
	UMaterialExpression* FMat::StaticFlag(const FName& Name, bool bDefault, int32 X, int32 Y)
	{
		UMaterialExpressionStaticSwitchParameter* E = Cast<UMaterialExpressionStaticSwitchParameter>(Expr(UMaterialExpressionStaticSwitchParameter::StaticClass(), X, Y));
		E->ParameterName = Name;
		E->DefaultValue = bDefault;
		Connect(Const(1.0f, X - 150, Y), FString(), E, TEXT("True"));
		Connect(Const(0.0f, X - 150, Y + 40), FString(), E, TEXT("False"));
		return E;
	}

	UMaterialExpression* FMat::TexObject(const FName& Name, EMaterialSamplerType Sampler, UTexture* Default, int32 X, int32 Y)
	{
		UMaterialExpressionTextureObjectParameter* E = Cast<UMaterialExpressionTextureObjectParameter>(Expr(UMaterialExpressionTextureObjectParameter::StaticClass(), X, Y));
		E->ParameterName = Name;
		E->SamplerType = Sampler;
		E->SamplerSource = SSM_Wrap_WorldGroupSettings;
		if (Default)
		{
			E->Texture = Default;
		}
		return E;
	}

	UMaterialExpression* FMat::Collection(UMaterialParameterCollection* Mpc, const FName& Name, int32 X, int32 Y)
	{
		UMaterialExpressionCollectionParameter* E = Cast<UMaterialExpressionCollectionParameter>(Expr(UMaterialExpressionCollectionParameter::StaticClass(), X, Y));
		E->Collection = Mpc;
		E->ParameterName = Name;
		E->ParameterId = Mpc->GetParameterId(Name);
		return E;
	}

	UMaterialInstanceConstant* FreshInstance(const FString& Name, const FString& Folder, UMaterialInterface* Parent, const TCHAR* Tag)
	{
		UMaterialInstanceConstant* Mi = CastChecked<UMaterialInstanceConstant>(GetOrCreate(Name, Folder, UMaterialInstanceConstant::StaticClass(), NewObject<UMaterialInstanceConstantFactoryNew>(), Tag));
		UMaterialEditingLibrary::SetMaterialInstanceParent(Mi, Parent);
		UMaterialEditingLibrary::ClearAllMaterialInstanceParameters(Mi);
		return Mi;
	}

	void SetScalar(UMaterialInstanceConstant* Mi, const FName& Name, float V)
	{
		// The library setters always return false (5.8), so check the parameter exists by reading it back.
		UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Mi, Name, V);
		float Out = 0.0f;
		if (!Mi->GetScalarParameterValue(FHashedMaterialParameterInfo(Name), Out))
		{
			UE_LOG(LogOpenCity, Warning, TEXT("%s: no scalar parameter %s"), *Mi->GetName(), *Name.ToString());
		}
	}

	void SetVector(UMaterialInstanceConstant* Mi, const FName& Name, const FLinearColor& V)
	{
		UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(Mi, Name, V);
		FLinearColor Out;
		if (!Mi->GetVectorParameterValue(FHashedMaterialParameterInfo(Name), Out))
		{
			UE_LOG(LogOpenCity, Warning, TEXT("%s: no vector parameter %s"), *Mi->GetName(), *Name.ToString());
		}
	}

	void SetTexture(UMaterialInstanceConstant* Mi, const FName& Name, UTexture* V)
	{
		UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Mi, Name, V);
		UTexture* Out = nullptr;
		if (!Mi->GetTextureParameterValue(FHashedMaterialParameterInfo(Name), Out))
		{
			UE_LOG(LogOpenCity, Warning, TEXT("%s: no texture parameter %s"), *Mi->GetName(), *Name.ToString());
		}
	}

	void SetSwitch(UMaterialInstanceConstant* Mi, const FName& Name, bool V)
	{
		UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(Mi, Name, V);
		bool Out = false;
		FGuid Id;
		if (!Mi->GetStaticSwitchParameterValue(FHashedMaterialParameterInfo(Name), Out, Id))
		{
			UE_LOG(LogOpenCity, Warning, TEXT("%s: no static switch %s"), *Mi->GetName(), *Name.ToString());
		}
	}

	void FinishInstance(UMaterialInstanceConstant* Mi)
	{
		UMaterialEditingLibrary::UpdateMaterialInstance(Mi);
		Save(Mi);
	}

	TArray<UObject*> ImportFile(const FString& File, const FString& DestPath, const FString& DestName, UObject* Options)
	{
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = File;
		Task->DestinationPath = DestPath;
		Task->DestinationName = DestName;
		Task->bReplaceExisting = true;
		Task->bAutomated = true;
		Task->bSave = false;
		Task->Options = Options;
		FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().ImportAssetTasks({ Task });
		TArray<UObject*> Out;
		for (UObject* O : Task->GetObjects())
		{
			if (O) { Out.Add(O); }
		}
		if (Out.IsEmpty())
		{
			UE_LOG(LogOpenCity, Error, TEXT("import failed: %s -> %s"), *File, *DestPath);
		}
		return Out;
	}

	void TextureSettings(UTexture* T, bool bSrgb, TextureCompressionSettings Compression, TextureGroup LodGroup, int32 MaxSize)
	{
		T->Modify();
		T->SRGB = bSrgb;
		T->CompressionSettings = Compression;
		T->LODGroup = LodGroup;
		if (MaxSize > 0)
		{
			T->MaxTextureSize = MaxSize;
		}
		T->PostEditChange();
	}
}
