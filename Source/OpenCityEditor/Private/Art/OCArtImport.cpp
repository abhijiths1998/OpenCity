// Mesh and texture imports (formerly oc_import_props.py, oc_import_kit.py and oc_import_textures.py).
#include "Art/OCArt.h"

#include "EditorAssetLibrary.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Factories/FbxImportUI.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/Paths.h"
#include "OCEditorHelpers.h"
#include "OCLog.h"
#include "PhysicsEngine/BodySetup.h"

namespace OCArt
{
	namespace
	{
		void EnableNanite(UStaticMesh* Mesh, bool bPreserveArea)
		{
			Mesh->Modify();
			FMeshNaniteSettings& N = Mesh->GetNaniteSettings();
			N.bEnabled = true;
			if (bPreserveArea)
			{
				N.ShapePreservation = ENaniteShapePreservation::PreserveArea;
			}
			Mesh->PostEditChange();
		}

		FString Size(const UStaticMesh* Mesh)
		{
			const FVector E = Mesh->GetBounds().BoxExtent;
			return FString::Printf(TEXT("%.0f x %.0f x %.0f cm"), E.X * 2, E.Y * 2, E.Z * 2);
		}

		// ---- Props -----------------------------------------------------------------------------

		const TCHAR* PropTag = TEXT("oc_props");
		const TCHAR* PropDest = TEXT("/Game/Environment/Props");

		/** MI_Prop_<slot> from the slot's Poly Haven maps (<slot>_diff/_nor_gl/_arm). Interchange imports the
		 *  textures but no materials from these glTFs, so the props would render default grey. */
		UMaterialInterface* MaterialFor(const FString& Slot, const FString& Folder, const TMap<FString, UTexture*>& Textures, bool bFoliage)
		{
			if (Slot.EndsWith(TEXT("_glass")) || Slot.EndsWith(TEXT("_bulb")))
			{
				return OCEd::Load<UMaterialInterface>(TEXT("/Game/Materials/M_Master_LampGlow"));
			}
			auto Tex = [&](const TCHAR* Kind) -> UTexture*
			{
				FString Head, Tail;
				const FString Short = Slot.Split(TEXT("_"), &Head, &Tail, ESearchCase::CaseSensitive, ESearchDir::FromEnd) ? Head : Slot;
				for (const FString& Name : { FString::Printf(TEXT("%s_%s_1k"), *Slot, Kind), FString::Printf(TEXT("%s_%s_1k"), *Short, Kind) })
				{
					if (UTexture* const* T = Textures.Find(Name)) { return *T; }
				}
				return nullptr;
			};
			UTexture* Bc = Tex(TEXT("diff"));
			UTexture* N = Tex(TEXT("nor_gl"));
			UTexture* Arm = Tex(TEXT("arm"));
			if (!Bc)
			{
				OCEd::Log(PropTag, TEXT("warning: no textures for slot ") + Slot);
				return nullptr;
			}
			if (N)   // glTF normals are OpenGL style (green up); the master expects DirectX
			{
				N->Modify();
				N->bFlipGreenChannel = true;
				OCEd::TextureSettings(N, false, TC_Normalmap, N->LODGroup, 0);
				OCEd::Save(N);
			}
			if (Arm)
			{
				OCEd::TextureSettings(Arm, false, TC_Masks, Arm->LODGroup, 0);
				OCEd::Save(Arm);
			}
			const bool bLeafy = Slot.Contains(TEXT("leaves")) || Slot.Contains(TEXT("fern"));
			// Plants: every slot (trunk, branches, leaves) on the foliage master, so the wind moves them together.
			const TCHAR* Parent = bFoliage ? TEXT("/Game/Materials/M_Master_Foliage") : (bLeafy ? TEXT("/Game/Materials/M_Master_SurfaceTwoSided") : TEXT("/Game/Materials/M_Master_Surface"));
			UMaterialInstanceConstant* Mi = OCEd::FreshInstance(TEXT("MI_Prop_") + Slot, Folder, CastChecked<UMaterialInterface>(OCEd::Require(Parent)), PropTag);
			if (Bc) { OCEd::SetTexture(Mi, TEXT("BaseColor"), Bc); }
			if (N) { OCEd::SetTexture(Mi, TEXT("Normal"), N); }
			if (Arm) { OCEd::SetTexture(Mi, TEXT("ORM"), Arm); }
			if (bFoliage)
			{
				OCEd::SetScalar(Mi, TEXT("Subsurface"), bLeafy ? 1.0f : 0.0f);
			}
			else
			{
				OCEd::SetScalar(Mi, TEXT("TileSizeM"), 1.0f);
				OCEd::SetScalar(Mi, TEXT("Macro"), 0.0f);
				OCEd::SetScalar(Mi, TEXT("TintSlot"), -1.0f);
			}
			OCEd::FinishInstance(Mi);
			return Mi;
		}

		// ---- Textures --------------------------------------------------------------------------

		struct FTexSpec { const TCHAR* Suffix; bool bSrgb; TextureCompressionSettings Compression; int32 MaxSize; const TCHAR* Param; };
		const FTexSpec TexSpecs[] = {   // ART_PIPELINE.md section 3
			{ TEXT("BC"), true, TC_Default, 2048, TEXT("BaseColor") },
			{ TEXT("N"), false, TC_Normalmap, 2048, TEXT("Normal") },
			{ TEXT("ORM"), false, TC_Masks, 2048, TEXT("ORM") },
			{ TEXT("E"), true, TC_Default, 512, TEXT("Emissive") },
			{ TEXT("M"), false, TC_Masks, 1024, TEXT("Mask") } };

		/** (base, spec) from T_<Asset>_<SUFFIX>.ext, or false. */
		bool ParseTextureName(const FString& File, FString& OutBase, const FTexSpec*& OutSpec)
		{
			TArray<FString> Parts;
			FPaths::GetBaseFilename(File).ParseIntoArray(Parts, TEXT("_"), false);
			if (Parts.Num() < 3 || Parts[0] != TEXT("T"))
			{
				return false;
			}
			for (const FTexSpec& S : TexSpecs)
			{
				if (Parts.Last() == S.Suffix)
				{
					OutSpec = &S;
					OutBase = FString::Join(TArrayView<const FString>(Parts).Slice(1, Parts.Num() - 2), TEXT("_"));
					return true;
				}
			}
			return false;
		}
	}

	bool ImportProps()
	{
		const FString Src = OCEd::ProjectDir() / TEXT("SourceArt/Downloads/Props");
		static const TSet<FString> Foliage = { TEXT("SM_Prop_island_tree_01"), TEXT("SM_Prop_island_tree_02"), TEXT("SM_Prop_fern_02") };
		// Earlier imports straight from the glTF packs split every node into its own mesh.
		for (const FString& Old : UEditorAssetLibrary::ListAssets(PropDest, false, true))
		{
			FString Trimmed = Old;
			Trimmed.RemoveFromEnd(TEXT("/"));
			if (!FPaths::GetCleanFilename(Trimmed).StartsWith(TEXT("SM_Prop_")) && UEditorAssetLibrary::DoesDirectoryExist(Trimmed))
			{
				UEditorAssetLibrary::DeleteDirectory(Trimmed);
			}
		}
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Src / TEXT("*.glb")), true, false);
		Files.Sort();
		if (Files.IsEmpty())
		{
			UE_LOG(LogOpenCity, Error, TEXT("no props in %s (scripts/build_art.sh merges them)"), *Src);
			return false;
		}
		bool bOk = true;
		for (const FString& F : Files)
		{
			const FString Name = FPaths::GetBaseFilename(F);
			const FString Folder = FString(PropDest) / Name;
			OCEd::ImportFile(Src / F, Folder);
			TArray<UStaticMesh*> Meshes;
			for (const FString& Path : UEditorAssetLibrary::ListAssets(Folder, true))
			{
				UObject* A = UEditorAssetLibrary::LoadAsset(Path);
				if (UStaticMesh* M = Cast<UStaticMesh>(A))
				{
					EnableNanite(M, Foliage.Contains(Name));
					Meshes.Add(M);
					OCEd::Save(M);
				}
				else if (UTexture2D* T = Cast<UTexture2D>(A))
				{
					T->Modify();
					T->MaxTextureSize = 1024;
					T->PostEditChange();
					OCEd::Save(T);
				}
			}
			if (Meshes.Num() != 1)
			{
				UE_LOG(LogOpenCity, Error, TEXT("%s: expected one mesh, got %d"), *Name, Meshes.Num());
				bOk = false;
				continue;
			}
			// Give the mesh the prop's name so placement can find it.
			const FString Target = Folder / Name;
			const FString Current = Meshes[0]->GetPackage()->GetName();
			if (Current != Target)
			{
				if (UEditorAssetLibrary::DoesAssetExist(Target))
				{
					UEditorAssetLibrary::DeleteAsset(Target);
				}
				UEditorAssetLibrary::RenameAsset(Current, Target);
			}
			UStaticMesh* Mesh = CastChecked<UStaticMesh>(OCEd::Require(Target));
			TMap<FString, UTexture*> Textures;
			for (const FString& Path : UEditorAssetLibrary::ListAssets(Folder, true))
			{
				if (UTexture2D* T = Cast<UTexture2D>(UEditorAssetLibrary::LoadAsset(Path)))
				{
					Textures.Add(T->GetName(), T);
				}
			}
			const TArray<FStaticMaterial>& Slots = Mesh->GetStaticMaterials();
			for (int32 i = 0; i < Slots.Num(); ++i)
			{
				if (UMaterialInterface* Mi = MaterialFor(Slots[i].MaterialSlotName.ToString(), Folder, Textures, Foliage.Contains(Name)))
				{
					Mesh->SetMaterial(i, Mi);
				}
			}
			OCEd::Save(Mesh);
			OCEd::Log(PropTag, FString::Printf(TEXT("%s: %s"), *Target, *Size(Mesh)));
		}
		return bOk;
	}

	bool ImportKit()
	{
		const TCHAR* KitTag = TEXT("oc_kit");
		const FString Src = OCEd::ProjectDir() / TEXT("SourceArt/Kit/FBX");
		const TCHAR* Dest = TEXT("/Game/Environment/Meshes/Kit");
		// Pieces placed with collision on (BuildRetainingWalls, BuildStreetFixtures): complex as simple.
		const TCHAR* Solid[] = { TEXT("RetainingWall_"), TEXT("WallCoping_"), TEXT("Sign_"), TEXT("Prop_Bollard"), TEXT("Prop_LedPole"), TEXT("Prop_ConvexMirror"),
			TEXT("Prop_RecyclingBin"), TEXT("Prop_Fountain"), TEXT("Prop_TrafficLight") };
		// Paint slots use M_Master_Paint (street fixtures).
		const TCHAR* SlotNames[] = { TEXT("Wall"), TEXT("Trim"), TEXT("Wood"), TEXT("Glass"), TEXT("Metal"), TEXT("Roof"), TEXT("Concrete"),
			TEXT("PaintRed"), TEXT("PaintWhite"), TEXT("PaintBlack"), TEXT("PaintBlue"), TEXT("Paint") };
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Src / TEXT("*.fbx")), true, false);
		Files.Sort();
		if (Files.IsEmpty())
		{
			UE_LOG(LogOpenCity, Error, TEXT("no FBX in %s: build the kit first"), *Src);
			return false;
		}
		TMap<FString, UMaterialInterface*> Mis;
		for (const TCHAR* S : SlotNames)
		{
			UMaterialInterface* Mi = OCEd::Load<UMaterialInterface>(FString(TEXT("/Game/Environment/Materials/MI_Kit_")) + S);
			if (!Mi)
			{
				UE_LOG(LogOpenCity, Error, TEXT("missing kit material MI_Kit_%s: run the materials step first"), S);
				return false;
			}
			Mis.Add(S, Mi);
		}
		int32 Done = 0;
		for (const FString& F : Files)
		{
			const FString Name = TEXT("SM_Kit_") + FPaths::GetBaseFilename(F);
			UFbxImportUI* Ui = NewObject<UFbxImportUI>();
			Ui->bImportMesh = true;
			Ui->bImportAsSkeletal = false;
			Ui->bImportMaterials = false;
			Ui->bImportTextures = false;
			Ui->bImportAnimations = false;
			Ui->bCreatePhysicsAsset = false;
			Ui->MeshTypeToImport = FBXIT_StaticMesh;
			Ui->bAutomatedImportShouldDetectType = false;
			UFbxStaticMeshImportData* Sm = Ui->StaticMeshImportData;
			Sm->bCombineMeshes = true;
			Sm->bAutoGenerateCollision = false;
			Sm->bGenerateLightmapUVs = false;
			Sm->bRemoveDegenerates = true;
			Sm->NormalImportMethod = FBXNIM_ImportNormals;
			OCEd::ImportFile(Src / F, Dest, Name, Ui);
			UStaticMesh* Mesh = OCEd::Load<UStaticMesh>(FString(Dest) / Name);
			if (!Mesh)
			{
				OCEd::Log(KitTag, TEXT("FAILED to import ") + F);
				continue;
			}
			EnableNanite(Mesh, false);
			TArray<FString> Unknown;
			const TArray<FStaticMaterial>& Slots = Mesh->GetStaticMaterials();
			for (int32 i = 0; i < Slots.Num(); ++i)
			{
				// Blender may append .001; FBX may add _ suffixes.
				const FString Slot = Slots[i].MaterialSlotName.ToString();
				int32 Cut = Slot.Len();
				for (int32 c = 0; c < Slot.Len(); ++c)
				{
					if (Slot[c] == TEXT('.') || Slot[c] == TEXT('_')) { Cut = c; break; }
				}
				const FString Base = Slot.Left(Cut);
				if (UMaterialInterface** Mi = Mis.Find(Base))
				{
					Mesh->SetMaterial(i, *Mi);
				}
				else
				{
					Unknown.Add(Slot);
				}
			}
			for (const TCHAR* S : Solid)
			{
				if (Name.StartsWith(FString(TEXT("SM_Kit_")) + S))
				{
					// The UCX_ boxes don't survive the 5.8 FBX import (0 simple shapes), and Chaos vehicles only
					// collide with simple shapes: walls, coping and street fixtures use their render mesh as
					// collision, so cars stop at parapets instead of driving through them.
					UBodySetup* Body = Mesh->GetBodySetup();
					Body->Modify();
					Body->CollisionTraceFlag = CTF_UseComplexAsSimple;
					Body->PostEditChange();
					break;
				}
			}
			OCEd::Save(Mesh);
			++Done;
			OCEd::Log(KitTag, FString::Printf(TEXT("%s: %s, origin %s%s"), *Name, *Size(Mesh), *Mesh->GetBounds().Origin.ToString(),
				Unknown.IsEmpty() ? TEXT("") : *(TEXT("; unknown slots ") + FString::Join(Unknown, TEXT(", ")))));
		}
		OCEd::Log(KitTag, FString::Printf(TEXT("imported %d of %d kit pieces into %s"), Done, Files.Num(), Dest));
		return Done == Files.Num();
	}

	bool ImportTextureFolder(const FString& Src, const FString& Dest, const FString& ParentPath)
	{
		const TCHAR* TexTag = TEXT("oc_import_textures");
		if (!Dest.StartsWith(TEXT("/Game/")))
		{
			UE_LOG(LogOpenCity, Error, TEXT("-dest must be a /Game/ path"));
			return false;
		}
		UMaterialInterface* Parent = OCEd::Load<UMaterialInterface>(ParentPath);
		if (!Parent)
		{
			UE_LOG(LogOpenCity, Error, TEXT("could not load master material %s"), *ParentPath);
			return false;
		}
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Src / TEXT("*")), true, false);
		Files.Sort();
		TMap<FString, TArray<TPair<const FTexSpec*, UTexture*>>> Sets;   // base -> (spec, texture)
		for (const FString& F : Files)
		{
			FString Base;
			const FTexSpec* Spec = nullptr;
			if (!ParseTextureName(F, Base, Spec))
			{
				UE_LOG(LogOpenCity, Error, TEXT("unknown texture name pattern (T_<Asset>_<BC|N|ORM|E|M>): %s"), *F);
				return false;
			}
			const FString TexName = FString::Printf(TEXT("T_%s_%s"), *Base, Spec->Suffix);
			OCEd::ImportFile(Src / F, Dest, TexName);
			UTexture* Tex = OCEd::Load<UTexture>(Dest / TexName);
			if (!Tex)
			{
				UE_LOG(LogOpenCity, Error, TEXT("import produced no asset: %s"), *F);
				return false;
			}
			OCEd::TextureSettings(Tex, Spec->bSrgb, Spec->Compression, TEXTUREGROUP_World, Spec->MaxSize);
			OCEd::Save(Tex);
			OCEd::Log(TexTag, FString::Printf(TEXT("imported %s (%d)"), *TexName, Spec->MaxSize));
			Sets.FindOrAdd(Base).Add({ Spec, Tex });
		}
		for (const TPair<FString, TArray<TPair<const FTexSpec*, UTexture*>>>& S : Sets)
		{
			UMaterialInstanceConstant* Mi = CastChecked<UMaterialInstanceConstant>(OCEd::GetOrCreate(TEXT("MI_") + S.Key, Dest,
				UMaterialInstanceConstant::StaticClass(), NewObject<UMaterialInstanceConstantFactoryNew>(), TexTag));
			UMaterialEditingLibrary::SetMaterialInstanceParent(Mi, Parent);
			TArray<FString> Parts;
			for (const TPair<const FTexSpec*, UTexture*>& T : S.Value)
			{
				OCEd::SetTexture(Mi, T.Key->Param, T.Value);
				Parts.Add(FString::Printf(TEXT("%s -> %s"), T.Key->Suffix, T.Key->Param));
			}
			OCEd::FinishInstance(Mi);
			OCEd::Log(TexTag, FString::Printf(TEXT("instance MI_%s: %s"), *S.Key, *FString::Join(Parts, TEXT(", "))));
		}
		OCEd::Log(TexTag, FString::Printf(TEXT("done: %d texture sets imported into %s"), Sets.Num(), *Dest));
		return true;
	}
}
