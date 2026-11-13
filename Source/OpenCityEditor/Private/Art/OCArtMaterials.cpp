// Final-art textures and materials (formerly OCArtMaterials.cpp and OCArtMaterials.cpp (decals)).
// 1. Imports every set in SourceArt/Downloads/<slug>/T_<slug>_{BC,N,ORM}.* into /Game/Environment/Textures/<slug>/
//    (BC sRGB, N BC5 normal map, ORM linear masks; World texture groups; capped at 2048).
// 2. M_Master_Surface: one opaque master for kit pieces, props, roads and ground. A Custom HLSL node samples
//    BaseColor / Normal / ORM with a projection mode (static switches ProjectionBit0/1: 0 mesh UV0 in metres,
//    1 roof planes aligned to the eaves, 2 world top-down, 3 world box projection), macro variation,
//    per-instance tints from PerInstanceCustomData, roughness remap, normal strength, grime and wetness.
// 3. M_Master_KitGlass, M_Master_LampGlow, M_Master_Road, M_Master_Landscape, M_Master_Water, M_PP_Rain,
//    M_Master_CarGlass, M_Master_Foliage, M_Master_CarPaint, M_Master_Paint, M_Master_Character, M_Master_Decal.
// 4. Material instances in /Game/Environment/Materials/. Everything is regenerated on each run.
#include "Art/OCArt.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "EditorAssetLibrary.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionPerInstanceRandom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/Paths.h"
#include "OCEditorHelpers.h"
#include "OCLog.h"
#include "Factories/MaterialParameterCollectionFactoryNew.h"

namespace OCArt
{
	namespace Hlsl
	{
#include "Art/OCArtHlsl.inl"
	}

	namespace
	{
		const TCHAR* Tag = TEXT("oc_art");
		const TCHAR* TexRoot = TEXT("/Game/Environment/Textures");
		const TCHAR* MiRoot = TEXT("/Game/Environment/Materials");
		const TCHAR* MatRoot = TEXT("/Game/Materials");

		using FSet = TMap<FString, UTexture*>;          // role (BC, N, ORM) -> texture
		using FSets = TMap<FString, FSet>;              // slug -> set
		using FInputs = TArray<TPair<FString, UMaterialExpression*>>;
		using FOutputs = TArray<TPair<FString, ECustomMaterialOutputType>>;

		FString Downloads() { return OCEd::ProjectDir() / TEXT("SourceArt/Downloads"); }

		UTexture* Role(const FSet& S, const TCHAR* R)
		{
			UTexture* const* T = S.Find(R);
			return T ? *T : nullptr;
		}

		const FSet& TexturesFor(const FSets& Sets, const FString& Slug)
		{
			const FSet* S = Sets.Find(Slug);
			if (!S || S->IsEmpty())
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("textures for %s missing: run Tools/fetch_assets"), *Slug);
			}
			return *S;
		}

		// ---- Textures --------------------------------------------------------------------------

		FSets ImportTextures()
		{
			FSets Imported;
			TArray<FString> Slugs;
			IFileManager::Get().FindFiles(Slugs, *(Downloads() / TEXT("*")), false, true);
			Slugs.Sort();
			for (const FString& Slug : Slugs)
			{
				const FString Folder = Downloads() / Slug;
				TArray<FString> Files;
				IFileManager::Get().FindFiles(Files, *(Folder / TEXT("T_*")), true, false);
				if (Files.IsEmpty())
				{
					continue;
				}
				Files.Sort();
				const FString Dest = FString(TexRoot) / Slug;
				FSet Maps;
				for (const FString& F : Files)
				{
					OCEd::ImportFile(Folder / F, Dest);
					const FString Name = FPaths::GetBaseFilename(F);
					UTexture* Tex = OCEd::Load<UTexture>(Dest / Name);
					if (!Tex)
					{
						UE_LOG(LogOpenCity, Fatal, TEXT("import failed: %s/%s"), *Dest, *Name);
					}
					FString Left, R;
					Name.Split(TEXT("_"), &Left, &R, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
					if (R == TEXT("N"))
					{
						OCEd::TextureSettings(Tex, false, TC_Normalmap, TEXTUREGROUP_WorldNormalMap, 2048);
					}
					else if (R == TEXT("ORM"))
					{
						OCEd::TextureSettings(Tex, false, TC_Masks, TEXTUREGROUP_WorldSpecular, 2048);
					}
					else
					{
						OCEd::TextureSettings(Tex, true, TC_Default, TEXTUREGROUP_World, 2048);
					}
					OCEd::Save(Tex);
					Maps.Add(R, Tex);
				}
				TArray<FString> Roles;
				Maps.GetKeys(Roles);
				Roles.Sort();
				OCEd::Log(Tag, FString::Printf(TEXT("textures: %s [%s]"), *Slug, *FString::Join(Roles, TEXT(", "))));
				Imported.Add(Slug, MoveTemp(Maps));
			}
			return Imported;
		}

		/** Water normal maps from the engine's DatasmithContent plugin (authored by Epic; usable in Unreal
		 *  projects under the Epic EULA), copied into the project. No scanned CC0 water normals exist. */
		TMap<FString, UTexture*> ImportWaterNormals()
		{
			TMap<FString, UTexture*> Out;
			for (const TCHAR* Name : { TEXT("T_Water_N"), TEXT("T_Water_LF_N") })
			{
				const FString Dst = FString(TexRoot) / TEXT("Water") / Name;
				if (!UEditorAssetLibrary::DoesAssetExist(Dst))
				{
					// Plugin content isn't in a commandlet's asset registry until scanned.
					FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().ScanPathsSynchronous({ TEXT("/DatasmithContent/Materials/Water") }, true);
					if (UObject* Src = OCEd::LoadAsset(FString(TEXT("/DatasmithContent/Materials/Water/Textures/")) + Name))
					{
						UObject* Copy = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().DuplicateAsset(Name, FString(TexRoot) / TEXT("Water"), Src);
						if (Copy) { OCEd::Save(Copy); }
					}
				}
				UTexture* T = OCEd::Load<UTexture>(Dst);
				if (!T)
				{
					UE_LOG(LogOpenCity, Fatal, TEXT("could not copy %s from DatasmithContent (scripts/build_art.sh enables it for this step)"), Name);
				}
				Out.Add(Name, T);
			}
			return Out;
		}

		/** The colour-grade LUT made by Tools/make_lut (256 x 16 strip, no mips, uncompressed). */
		void ImportLut()
		{
			const FString Src = OCEd::ProjectDir() / TEXT("SourceArt/LUT/T_LUT_Marevia.png");
			if (!FPaths::FileExists(Src))
			{
				OCEd::Log(Tag, TEXT("no LUT (Tools/make_lut)"));
				return;
			}
			OCEd::ImportFile(Src, TEXT("/Game/Environment/Textures/LUT"));
			UTexture* Lut = CastChecked<UTexture>(OCEd::Require(TEXT("/Game/Environment/Textures/LUT/T_LUT_Marevia")));
			Lut->Modify();
			Lut->MipGenSettings = TMGS_NoMipmaps;
			OCEd::TextureSettings(Lut, false, TC_VectorDisplacementmap, TEXTUREGROUP_ColorLookupTable, 0);
			OCEd::Save(Lut);
			OCEd::Log(Tag, TEXT("imported T_LUT_Marevia"));
		}

		// ---- Master helpers --------------------------------------------------------------------

		/** Get or create the master, empty it, and mark it for instanced meshes and Nanite. */
		OCEd::FMat FreshMaster(const TCHAR* Name)
		{
			OCEd::FMat Mt = OCEd::FMat::Fresh(Name, MatRoot, Tag);
			OCEd::Usage(Mt.M, MATUSAGE_InstancedStaticMeshes);
			OCEd::Usage(Mt.M, MATUSAGE_Nanite);
			return Mt;
		}

		/** The master's Custom node with Inputs and Outputs, its result to base colour. */
		UMaterialExpressionCustom* Node(OCEd::FMat& Mt, const FString& Code, const FInputs& Inputs, const FOutputs& Outputs, int32 X = -300, int32 Y = 0)
		{
			return Mt.Custom(Code, Inputs, X, Y, CMOT_Float3, Outputs);
		}

		void Finish(OCEd::FMat& Mt)
		{
			Mt.Finish(Tag);
		}

		UMaterialExpression* Coord(OCEd::FMat& Mt, int32 Index, int32 X, int32 Y) { return Mt.UV(Index, X, Y); }
		UMaterialExpression* E(OCEd::FMat& Mt, UClass* C, int32 X, int32 Y) { return Mt.Expr(C, X, Y); }

		/** MPC_World: written only by UOCWorldStateSubsystem (ARCHITECTURE.md). Keeps existing entries
		 *  (materials reference their IDs) and only adds missing ones. */
		UMaterialParameterCollection* BuildWorldParameters()
		{
			UMaterialParameterCollection* Mpc = CastChecked<UMaterialParameterCollection>(OCEd::GetOrCreate(TEXT("MPC_World"), TEXT("/Game/Core"),
				UMaterialParameterCollection::StaticClass(), NewObject<UMaterialParameterCollectionFactoryNew>(), Tag));
			bool bAdded = false;
			const TPair<const TCHAR*, float> Wanted[] = { { TEXT("NightAmount"), 0.0f }, { TEXT("TimeOfDay"), 12.0f }, { TEXT("Wetness"), 0.0f },
				{ TEXT("RainAmount"), 0.0f }, { TEXT("Wind"), 0.3f } };
			for (const TPair<const TCHAR*, float>& W : Wanted)
			{
				if (!Mpc->ScalarParameters.ContainsByPredicate([&W](const FCollectionScalarParameter& P) { return P.ParameterName == FName(W.Key); }))
				{
					if (!bAdded) { Mpc->Modify(); }
					FCollectionScalarParameter P;
					P.ParameterName = W.Key;
					P.DefaultValue = W.Value;
					Mpc->ScalarParameters.Add(P);
					bAdded = true;
				}
			}
			if (bAdded)
			{
				Mpc->PostEditChange();   // recompiles every material that uses the collection
			}
			OCEd::Save(Mpc);
			return Mpc;
		}

		/** Inputs the WET_HLSL block needs (besides bc, Rough, WorldNormal, nGeo, pn, WP). Ground is a
		 *  compile-time 1/0 (static switch or constant): puddles, ripples and splashes only where it is 1. */
		FInputs WetInputs(OCEd::FMat& Mt, UMaterialParameterCollection* Mpc, UTexture* Ripples, UMaterialExpression* Ground, float Puddles = 0.0f)
		{
			return { { TEXT("WetGround"), Ground },
				{ TEXT("Wet"), Mt.Collection(Mpc, TEXT("Wetness"), -1100, 600) },
				{ TEXT("Rain"), Mt.Collection(Mpc, TEXT("RainAmount"), -1100, 680) },
				{ TEXT("Time"), E(Mt, UMaterialExpressionTime::StaticClass(), -1100, 760) },
				{ TEXT("RipN"), Mt.TexObject(TEXT("Ripples"), SAMPLERTYPE_Normal, Ripples, -1100, 840) },
				{ TEXT("PuddleAmt"), Mt.Scalar(TEXT("PuddleAmount"), Puddles, -1100, 920) } };
		}

		// ---- Masters ---------------------------------------------------------------------------

		UMaterial* BuildSurfaceMaster(const FSet& Defaults, UMaterialParameterCollection* Mpc, UTexture* Ripples, const TCHAR* Name, bool bTwoSided)
		{
			OCEd::FMat Mt = FreshMaster(Name);
			Mt.M->TwoSided = bTwoSided;                // two-sided variant: foliage (leaves are cut geometry)
			Mt.M->bTangentSpaceNormal = false;         // the Custom node outputs a world normal
			FInputs In = {
				{ TEXT("BCTex"), Mt.TexObject(TEXT("BaseColor"), SAMPLERTYPE_Color, Role(Defaults, TEXT("BC")), -1400, -400) },
				{ TEXT("NTex"), Mt.TexObject(TEXT("Normal"), SAMPLERTYPE_Normal, Role(Defaults, TEXT("N")), -1400, -250) },
				{ TEXT("ORMTex"), Mt.TexObject(TEXT("ORM"), SAMPLERTYPE_Masks, Role(Defaults, TEXT("ORM")), -1400, -100) },
				{ TEXT("UV"), Coord(Mt, 0, -1400, 50) },
				{ TEXT("WP"), Mt.WorldPos(-1400, 150) },
				{ TEXT("VN"), E(Mt, UMaterialExpressionVertexNormalWS::StaticClass(), -1400, 250) },
				{ TEXT("Tile"), Mt.Scalar(TEXT("TileSizeM"), 2.0f, -1100, -400) },
				{ TEXT("ProjA"), Mt.StaticFlag(TEXT("ProjectionBit0"), false, -1100, -340) },
				{ TEXT("ProjB"), Mt.StaticFlag(TEXT("ProjectionBit1"), false, -1100, -300) },
				{ TEXT("Macro"), Mt.Scalar(TEXT("Macro"), 0.5f, -1100, -240) },
				{ TEXT("Slot"), Mt.Scalar(TEXT("TintSlot"), -1.0f, -1100, -160) },
				{ TEXT("Tint"), Mt.Vector(TEXT("Tint"), FLinearColor(1, 1, 1, 1), -1100, -80) },
				{ TEXT("NStrength"), Mt.Scalar(TEXT("NormalStrength"), 1.0f, -1100, 0) },
				{ TEXT("RoughMul"), Mt.Scalar(TEXT("RoughMul"), 1.0f, -1100, 80) },
				{ TEXT("RoughAdd"), Mt.Scalar(TEXT("RoughAdd"), 0.0f, -1100, 160) },
				{ TEXT("MetalMul"), Mt.Scalar(TEXT("MetalMul"), 1.0f, -1100, 240) } };
			for (int32 i = 0; i < 6; ++i)
			{
				In.Add({ FString::Printf(TEXT("I%d"), i), Mt.InstanceData(i, 1.0f, -800, 350 + 70 * i) });
			}
			In.Add({ TEXT("I6"), Mt.InstanceData(6, 0.5f, -800, 350 + 70 * 6) });   // wear
			In.Add({ TEXT("I8"), Mt.InstanceData(8, 0.0f, -800, 350 + 70 * 8) });   // kit: ground-floor piece (base band, splash)
			In.Add({ TEXT("I9"), Mt.InstanceData(9, 0.0f, -800, 350 + 70 * 9) });   // kit: stone trim palette index (TintSlot 2)
			In.Add({ TEXT("Rand"), E(Mt, UMaterialExpressionPerInstanceRandom::StaticClass(), -800, 1100) });
			In.Add({ TEXT("LP"), E(Mt, UMaterialExpressionLocalPosition::StaticClass(), -800, 1180) });
			UTexture* Grime = OCEd::Load<UTexture>(TEXT("/Game/Environment/Textures/Decals/Leaking012A/T_Leaking012A_Color"));
			In.Add({ TEXT("GrimeT"), Mt.TexObject(TEXT("Grime"), SAMPLERTYPE_Color, Grime, -800, 900) });
			In.Add({ TEXT("GrimeAmt"), Mt.Scalar(TEXT("GrimeAmount"), 0.0f, -800, 1000) });
			In.Append(WetInputs(Mt, Mpc, Ripples, Mt.StaticFlag(TEXT("WetGround"), false, -1100, 1000)));
			const FString Code = Hlsl::SURFACE_HLSL.Replace(TEXT("return bc;"), *(FString(TEXT("float3 nGeo = n;\nfloat pn = lumBig / lumMean * 0.5;\n")) + Hlsl::WET_HLSL + TEXT("\nreturn bc;")));
			UMaterialExpressionCustom* N = Node(Mt, Code, In, { { TEXT("WorldNormal"), CMOT_Float3 }, { TEXT("AO"), CMOT_Float1 }, { TEXT("Rough"), CMOT_Float1 }, { TEXT("Metal"), CMOT_Float1 } });
			Mt.Output(N, FString(), MP_BaseColor);
			Mt.Output(N, TEXT("WorldNormal"), MP_Normal);
			Mt.Output(N, TEXT("Rough"), MP_Roughness);
			Mt.Output(N, TEXT("Metal"), MP_Metallic);
			Mt.Output(N, TEXT("AO"), MP_AmbientOcclusion);
			Finish(Mt);
			return Mt.M;
		}

		void BuildLampMaster(UMaterialParameterCollection* Mpc)
		{
			OCEd::FMat Mt = FreshMaster(TEXT("M_Master_LampGlow"));
			UMaterialExpressionCustom* N = Node(Mt, Hlsl::LAMP_HLSL,
				{ { TEXT("Night"), Mt.Collection(Mpc, TEXT("NightAmount"), -800, 0) }, { TEXT("Strength"), Mt.Scalar(TEXT("Glow"), 40.0f, -800, 100) } },
				{ { TEXT("Emis"), CMOT_Float3 } });
			Mt.Output(N, FString(), MP_BaseColor);
			Mt.Output(N, TEXT("Emis"), MP_EmissiveColor);
			Mt.Output(Mt.Const(0.2f, -300, 200), FString(), MP_Roughness);
			Finish(Mt);
		}

		UMaterial* BuildGlassMaster(UMaterialParameterCollection* Mpc)
		{
			OCEd::FMat Mt = FreshMaster(TEXT("M_Master_KitGlass"));
			UMaterialExpressionCustom* N = Node(Mt, Hlsl::GLASS_HLSL,
				{ { TEXT("LP"), E(Mt, UMaterialExpressionLocalPosition::StaticClass(), -800, 0) }, { TEXT("Rand"), E(Mt, UMaterialExpressionPerInstanceRandom::StaticClass(), -800, 300) },
				  { TEXT("Night"), Mt.Collection(Mpc, TEXT("NightAmount"), -800, 100) },
				  { TEXT("Strength"), Mt.Scalar(TEXT("WindowLight"), 3.0f, -800, 200) } },
				{ { TEXT("Emis"), CMOT_Float3 } });
			Mt.Output(N, FString(), MP_BaseColor);
			Mt.Output(N, TEXT("Emis"), MP_EmissiveColor);
			Mt.Output(Mt.Const(0.06f, -300, 200), FString(), MP_Roughness);
			Mt.Output(Mt.Const(1.0f, -300, 300), FString(), MP_Specular);
			Finish(Mt);
			return Mt.M;
		}

		/** A master whose Custom node samples several texture sets: Sets = (prefix, set), Tiles = (param, default). */
		UMaterial* BuildTexturedMaster(const TCHAR* Name, const FString& Code, const TArray<TPair<FString, const FSet*>>& Sets,
			const TArray<TPair<FString, float>>& Tiles, bool bLandscape, TFunctionRef<FInputs(OCEd::FMat&)> Extra)
		{
			OCEd::FMat Mt = FreshMaster(Name);
			if (bLandscape)
			{
				Mt.M->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, false);
				Mt.M->SetUsageByFlag(MATUSAGE_Nanite, false);
			}
			Mt.M->bTangentSpaceNormal = false;
			FInputs In;
			int32 Y = -600;
			for (const TPair<FString, const FSet*>& S : Sets)
			{
				const TPair<const TCHAR*, EMaterialSamplerType> Roles[] = { { TEXT("BC"), SAMPLERTYPE_Color }, { TEXT("N"), SAMPLERTYPE_Normal }, { TEXT("ORM"), SAMPLERTYPE_Masks } };
				for (const auto& R : Roles)
				{
					In.Add({ S.Key + R.Key, Mt.TexObject(*FString::Printf(TEXT("%s_%s"), *S.Key, R.Key), R.Value, Role(*S.Value, R.Key), -1400, Y) });
					Y += 120;
				}
			}
			In.Add({ TEXT("WP"), Mt.WorldPos(-1100, -600) });
			In.Add({ TEXT("VN"), E(Mt, UMaterialExpressionVertexNormalWS::StaticClass(), -1100, -500) });
			In.Add({ TEXT("Cam"), E(Mt, UMaterialExpressionCameraPositionWS::StaticClass(), -1100, -400) });
			const int32 Coords = Code.Contains(TEXT("R2"), ESearchCase::CaseSensitive) ? 3 : 2;
			for (int32 i = 0; i < Coords; ++i)
			{
				In.Add({ FString::Printf(TEXT("R%d"), i), Coord(Mt, i, -1100, -300 + 100 * i) });
			}
			int32 Yy = 0;
			for (const TPair<FString, float>& T : Tiles)
			{
				In.Add({ T.Key, Mt.Scalar(*T.Key, T.Value, -1100, Yy) });
				Yy += 80;
			}
			In.Append(Extra(Mt));
			UMaterialExpressionCustom* N = Node(Mt, Code, In, { { TEXT("WorldNormal"), CMOT_Float3 }, { TEXT("AO"), CMOT_Float1 }, { TEXT("Rough"), CMOT_Float1 } });
			Mt.Output(N, FString(), MP_BaseColor);
			Mt.Output(N, TEXT("WorldNormal"), MP_Normal);
			Mt.Output(N, TEXT("Rough"), MP_Roughness);
			Mt.Output(N, TEXT("AO"), MP_AmbientOcclusion);
			Finish(Mt);
			return Mt.M;
		}

		/** The wetness inputs of a road or terrain master: always ground (a constant 1). */
		FInputs GroundWet(OCEd::FMat& Mt, UMaterialParameterCollection* Mpc, UTexture* Ripples, float Puddles)
		{
			return WetInputs(Mt, Mpc, Ripples, Mt.Const(1.0f, -1250, 1000), Puddles);
		}

		void BuildCarGlass(UMaterialParameterCollection* Mpc)
		{
			OCEd::FMat Mt = OCEd::FMat::Fresh(TEXT("M_Master_CarGlass"), MatRoot, Tag);
			// Opaque: translucent versions measured 3,093 (forward shading), 1,869 (surface) and 486
			// (volumetric, no reflections) instructions, and showed the empty body shell through the glass.
			Mt.M->BlendMode = BLEND_Opaque;
			Mt.M->TwoSided = false;
			UMaterialExpressionCustom* N = Node(Mt, Hlsl::CAR_GLASS_HLSL,
				{ { TEXT("LP"), E(Mt, UMaterialExpressionLocalPosition::StaticClass(), -1100, 0) },
				  { TEXT("Rain"), Mt.Collection(Mpc, TEXT("RainAmount"), -1100, 100) },
				  { TEXT("N"), E(Mt, UMaterialExpressionVertexNormalWS::StaticClass(), -1100, 200) } },
				{ { TEXT("WorldNormal"), CMOT_Float3 }, { TEXT("Rough"), CMOT_Float1 } });
			Mt.Output(N, FString(), MP_BaseColor);
			Mt.Output(N, TEXT("Rough"), MP_Roughness);
			Mt.Output(N, TEXT("WorldNormal"), MP_Normal);
			Mt.M->bTangentSpaceNormal = false;
			Finish(Mt);
		}

		/** M_Master_Foliage: two-sided foliage shading, per-instance variation, wetness and wind (ART_PIPELINE.md
		 *  target 400 instructions). Used by the tree and plant props. */
		void BuildFoliageMaster(UMaterialParameterCollection* Mpc)
		{
			OCEd::FMat Mt = FreshMaster(TEXT("M_Master_Foliage"));
			Mt.M->TwoSided = true;
			Mt.M->SetShadingModel(MSM_TwoSidedFoliage);
			Mt.M->MaxWorldPositionOffsetDisplacement = 50.0f;   // Nanite culls by bounds: wind may push 0.5 m past them
			UMaterialExpression* Sss = Mt.Scalar(TEXT("Subsurface"), 0.0f, -1400, 350);
			UMaterialExpressionCustom* N = Node(Mt, Hlsl::FOLIAGE_HLSL,
				{ { TEXT("BCTex"), Mt.TexObject(TEXT("BaseColor"), SAMPLERTYPE_Color, nullptr, -1400, -300) },
				  { TEXT("NTex"), Mt.TexObject(TEXT("Normal"), SAMPLERTYPE_Normal, nullptr, -1400, -150) },
				  { TEXT("ORMTex"), Mt.TexObject(TEXT("ORM"), SAMPLERTYPE_Masks, nullptr, -1400, 0) },
				  { TEXT("UV"), Coord(Mt, 0, -1400, 150) },
				  { TEXT("Rand"), E(Mt, UMaterialExpressionPerInstanceRandom::StaticClass(), -1400, 250) },
				  { TEXT("Subsurface"), Sss },
				  { TEXT("Wet"), Mt.Collection(Mpc, TEXT("Wetness"), -1400, 450) } },
				{ { TEXT("TangentNormal"), CMOT_Float3 }, { TEXT("AO"), CMOT_Float1 }, { TEXT("Rough"), CMOT_Float1 }, { TEXT("Sss"), CMOT_Float3 } });
			Mt.Output(N, FString(), MP_BaseColor);
			Mt.Output(N, TEXT("TangentNormal"), MP_Normal);
			Mt.Output(N, TEXT("Rough"), MP_Roughness);
			Mt.Output(N, TEXT("AO"), MP_AmbientOcclusion);
			Mt.Output(N, TEXT("Sss"), MP_SubsurfaceColor);
			UMaterialExpressionCustom* Wpo = Node(Mt, Hlsl::FOLIAGE_WPO_HLSL,
				{ { TEXT("LP"), E(Mt, UMaterialExpressionLocalPosition::StaticClass(), -1000, 600) },
				  { TEXT("WP"), Mt.WorldPos(-1000, 680) },
				  { TEXT("VN"), E(Mt, UMaterialExpressionVertexNormalWS::StaticClass(), -1000, 760) },
				  { TEXT("Time"), E(Mt, UMaterialExpressionTime::StaticClass(), -1000, 840) },
				  { TEXT("Wind"), Mt.Collection(Mpc, TEXT("Wind"), -1000, 920) },
				  { TEXT("Subsurface"), Sss } }, {}, -300, 600);
			Mt.Output(Wpo, FString(), MP_WorldPositionOffset);
			Finish(Mt);
		}

		void BuildPaintMaster(UMaterialParameterCollection* Mpc)
		{
			OCEd::FMat Mt = FreshMaster(TEXT("M_Master_Paint"));
			UTexture* Grime = OCEd::Load<UTexture>(TEXT("/Game/Environment/Textures/Decals/Leaking013A/T_Leaking013A_Color"));
			FInputs In = {
				{ TEXT("Colour"), Mt.Vector(TEXT("Colour"), FLinearColor(0.5f, 0.5f, 0.5f, 1.0f), -1100, -300) },
				{ TEXT("UseInstance"), Mt.StaticFlag(TEXT("UseInstanceColour"), false, -1100, -200) },   // static switches: each instance
				{ TEXT("Reflect"), Mt.StaticFlag(TEXT("Retroreflective"), false, -1100, -120) },       // compiles only its own paths
				{ TEXT("Dirt"), Mt.Scalar(TEXT("Dirt"), 0.35f, -1100, -40) },
				{ TEXT("Grime"), Mt.TexObject(TEXT("Grime"), SAMPLERTYPE_Color, Grime, -1100, 40) },
				{ TEXT("WP"), Mt.WorldPos(-1100, 160) },
				{ TEXT("Night"), Mt.Collection(Mpc, TEXT("NightAmount"), -1100, 240) },
				{ TEXT("Wet"), Mt.Collection(Mpc, TEXT("Wetness"), -1100, 320) } };
			In.Add({ TEXT("I0"), Mt.InstanceData(0, 0.0f, -800, 300) });   // palette index (OCKitBuilder: recycling streams, signal box)
			UMaterialExpressionCustom* N = Node(Mt, Hlsl::PAINT_HLSL, In, { { TEXT("Rough"), CMOT_Float1 }, { TEXT("Emis"), CMOT_Float3 } });
			Mt.Output(N, FString(), MP_BaseColor);
			Mt.Output(N, TEXT("Rough"), MP_Roughness);
			Mt.Output(N, TEXT("Emis"), MP_EmissiveColor);
			Finish(Mt);
			struct FPaint { const TCHAR* Name; FLinearColor Colour; float Reflect, Inst; };
			const FPaint Paints[] = {
				{ TEXT("MI_Kit_PaintRed"), FLinearColor(0.55f, 0.02f, 0.02f), 1.0f, 0.0f },
				{ TEXT("MI_Kit_PaintWhite"), FLinearColor(0.82f, 0.82f, 0.8f), 1.0f, 0.0f },
				{ TEXT("MI_Kit_PaintBlack"), FLinearColor(0.02f, 0.02f, 0.02f), 0.0f, 0.0f },
				{ TEXT("MI_Kit_PaintBlue"), FLinearColor(0.02f, 0.1f, 0.42f), 1.0f, 0.0f },
				{ TEXT("MI_Kit_Paint"), FLinearColor(0.2f, 0.3f, 0.2f), 0.0f, 1.0f } };
			for (const FPaint& P : Paints)
			{
				UMaterialInstanceConstant* Mi = OCEd::FreshInstance(P.Name, MiRoot, Mt.M, Tag);
				OCEd::SetVector(Mi, TEXT("Colour"), P.Colour);
				OCEd::SetSwitch(Mi, TEXT("Retroreflective"), P.Reflect > 0.5f);
				OCEd::SetSwitch(Mi, TEXT("UseInstanceColour"), P.Inst > 0.5f);
				OCEd::FinishInstance(Mi);
			}
			OCEd::Log(Tag, TEXT("paint instances: MI_Kit_PaintRed, MI_Kit_PaintWhite, MI_Kit_PaintBlack, MI_Kit_PaintBlue, MI_Kit_Paint"));
		}

		/** M_Master_Character (ART_PIPELINE.md target 450): skin, cloth and simple hair, and one instance of each. */
		void BuildCharacterMaster(UMaterialParameterCollection* Mpc)
		{
			OCEd::FMat Mt = FreshMaster(TEXT("M_Master_Character"));
			Mt.M->SetShadingModel(MSM_Subsurface);
			OCEd::Usage(Mt.M, MATUSAGE_SkeletalMesh);
			UMaterialExpressionCustom* N = Node(Mt, Hlsl::CHARACTER_HLSL,
				{ { TEXT("BCTex"), Mt.TexObject(TEXT("BaseColor"), SAMPLERTYPE_Color, OCEd::Load<UTexture>(TEXT("/Engine/EngineMaterials/DefaultDiffuse")), -1100, -300) },
				  { TEXT("NTex"), Mt.TexObject(TEXT("Normal"), SAMPLERTYPE_Normal, OCEd::Load<UTexture>(TEXT("/Engine/EngineMaterials/DefaultNormal")), -1100, -150) },
				  { TEXT("UV"), Coord(Mt, 0, -1100, 0) },
				  { TEXT("Tint"), Mt.Vector(TEXT("Tint"), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), -1100, 80) },
				  { TEXT("Roughness"), Mt.Scalar(TEXT("Roughness"), 0.6f, -1100, 160) },
				  { TEXT("Skin"), Mt.Scalar(TEXT("Skin"), 0.0f, -1100, 240) },
				  { TEXT("Wet"), Mt.Collection(Mpc, TEXT("Wetness"), -1100, 320) } },
				{ { TEXT("TangentNormal"), CMOT_Float3 }, { TEXT("Rough"), CMOT_Float1 }, { TEXT("Sss"), CMOT_Float3 }, { TEXT("SssAmount"), CMOT_Float1 } });
			Mt.Output(N, FString(), MP_BaseColor);
			Mt.Output(N, TEXT("TangentNormal"), MP_Normal);
			Mt.Output(N, TEXT("Rough"), MP_Roughness);
			Mt.Output(N, TEXT("Sss"), MP_SubsurfaceColor);
			Mt.Output(N, TEXT("SssAmount"), MP_Opacity);   // the Subsurface model's scattering strength
			Finish(Mt);
			struct FPart { const TCHAR* Name; FLinearColor Tint; float Rough, Skin; };
			const FPart Parts[] = {
				{ TEXT("MI_Character_Skin"), FLinearColor(0.62f, 0.45f, 0.36f), 0.5f, 1.0f },
				{ TEXT("MI_Character_Cloth"), FLinearColor(0.18f, 0.2f, 0.24f), 0.85f, 0.0f },
				{ TEXT("MI_Character_Hair"), FLinearColor(0.05f, 0.035f, 0.025f), 0.45f, 0.0f } };
			for (const FPart& P : Parts)
			{
				UMaterialInstanceConstant* Mi = OCEd::FreshInstance(P.Name, MiRoot, Mt.M, Tag);
				OCEd::SetVector(Mi, TEXT("Tint"), P.Tint);
				OCEd::SetScalar(Mi, TEXT("Roughness"), P.Rough);
				OCEd::SetScalar(Mi, TEXT("Skin"), P.Skin);
				OCEd::FinishInstance(Mi);
			}
			OCEd::Log(Tag, TEXT("character instances: MI_Character_Skin, MI_Character_Cloth, MI_Character_Hair"));
		}

		/** M_Master_CarPaint (ART_PIPELINE.md target 450) and one instance per car body. */
		void BuildCarPaint(UMaterialParameterCollection* Mpc)
		{
			OCEd::FMat Mt = FreshMaster(TEXT("M_Master_CarPaint"));
			Mt.M->SetShadingModel(MSM_ClearCoat);
			OCEd::Usage(Mt.M, MATUSAGE_SkeletalMesh);
			UMaterialExpressionVectorParameter* Paint = Cast<UMaterialExpressionVectorParameter>(Mt.Vector(TEXT("PaintColor"), FLinearColor(0.8f, 0.05f, 0.02f, 1.0f), -1400, 300));
			Paint->bUseCustomPrimitiveData = true;
			Paint->PrimitiveDataIndex = 0;
			UMaterialExpressionScalarParameter* Dirt = Cast<UMaterialExpressionScalarParameter>(Mt.Scalar(TEXT("Dirt"), 0.15f, -1400, 380));
			Dirt->bUseCustomPrimitiveData = true;
			Dirt->PrimitiveDataIndex = 4;
			UMaterialExpressionScalarParameter* Damage = Cast<UMaterialExpressionScalarParameter>(Mt.Scalar(TEXT("Damage"), 0.0f, -1400, 460));
			Damage->bUseCustomPrimitiveData = true;
			Damage->PrimitiveDataIndex = 5;
			UTexture* Grime = OCEd::Load<UTexture>(TEXT("/Game/Environment/Textures/Decals/Leaking012A/T_Leaking012A_Color"));
			UMaterialExpressionCustom* N = Node(Mt, Hlsl::CAR_PAINT_HLSL,
				{ { TEXT("BaseTex"), Mt.TexObject(TEXT("BaseColor"), SAMPLERTYPE_Color, nullptr, -1400, -300) },
				  { TEXT("MRATex"), Mt.TexObject(TEXT("MRA"), SAMPLERTYPE_Masks, nullptr, -1400, -150) },
				  { TEXT("NTex"), Mt.TexObject(TEXT("Normal"), SAMPLERTYPE_Normal, nullptr, -1400, 0) },
				  { TEXT("UV"), Coord(Mt, 0, -1400, 150) },
				  { TEXT("LP"), E(Mt, UMaterialExpressionLocalPosition::StaticClass(), -1400, 220) },
				  { TEXT("Paint"), Paint }, { TEXT("Dirt"), Dirt }, { TEXT("Damage"), Damage },
				  { TEXT("Grime"), Mt.TexObject(TEXT("Grime"), SAMPLERTYPE_Color, Grime, -1400, 540) },
				  { TEXT("Wet"), Mt.Collection(Mpc, TEXT("Wetness"), -1400, 640) } },
				{ { TEXT("TangentNormal"), CMOT_Float3 }, { TEXT("Metal"), CMOT_Float1 }, { TEXT("Rough"), CMOT_Float1 }, { TEXT("AO"), CMOT_Float1 },
				  { TEXT("Coat"), CMOT_Float1 }, { TEXT("CoatRough"), CMOT_Float1 } });
			// Everything goes through a Make Material Attributes node, whose ClearCoat inputs are ordinary pins.
			Mt.M->bUseMaterialAttributes = true;
			UMaterialExpression* Make = E(Mt, UMaterialExpressionMakeMaterialAttributes::StaticClass(), 100, 0);
			const TPair<const TCHAR*, const TCHAR*> Pins[] = { { TEXT(""), TEXT("BaseColor") }, { TEXT("TangentNormal"), TEXT("Normal") },
				{ TEXT("Metal"), TEXT("Metallic") }, { TEXT("Rough"), TEXT("Roughness") }, { TEXT("AO"), TEXT("AmbientOcclusion") },
				{ TEXT("Coat"), TEXT("ClearCoat") }, { TEXT("CoatRough"), TEXT("ClearCoatRoughness") } };
			for (const auto& P : Pins)
			{
				if (!UMaterialEditingLibrary::ConnectMaterialExpressions(N, P.Key, Make, P.Value))
				{
					UE_LOG(LogOpenCity, Fatal, TEXT("M_Master_CarPaint: could not connect %s to %s"), P.Key, P.Value);
				}
			}
			Mt.Output(Make, FString(), MP_MaterialAttributes);
			Finish(Mt);
			struct FCar { const TCHAR* Name; const TCHAR* Base; const TCHAR* Mra; const TCHAR* Normal; };
			const FCar Cars[] = { { TEXT("MI_CarPaint_SportsCar"), TEXT("/Game/Vehicles/SportsCar/Materials/Textures/T_SportsCar_Body_D"),
				TEXT("/Game/Vehicles/SportsCar/Materials/Textures/T_SportsCar_Body_MRA"), TEXT("/Game/Vehicles/SportsCar/Materials/Textures/T_SportsCar_Body_BotN") } };
			for (const FCar& C : Cars)
			{
				UMaterialInstanceConstant* Mi = OCEd::FreshInstance(C.Name, TEXT("/Game/Vehicles/Materials"), Mt.M, Tag);
				OCEd::SetTexture(Mi, TEXT("BaseColor"), CastChecked<UTexture>(OCEd::Require(C.Base)));
				OCEd::SetTexture(Mi, TEXT("MRA"), CastChecked<UTexture>(OCEd::Require(C.Mra)));
				OCEd::SetTexture(Mi, TEXT("Normal"), CastChecked<UTexture>(OCEd::Require(C.Normal)));
				OCEd::FinishInstance(Mi);
			}
		}

		void BuildWaterMaster(UMaterialParameterCollection* Mpc, const TMap<FString, UTexture*>& Water)
		{
			OCEd::FMat Mt = FreshMaster(TEXT("M_Master_Water"));
			Mt.M->bTangentSpaceNormal = false;
			UMaterialExpressionCustom* N = Node(Mt, Hlsl::WATER_HLSL,
				{ { TEXT("WN"), Mt.TexObject(TEXT("WaterNormal"), SAMPLERTYPE_Normal, Water.FindChecked(TEXT("T_Water_N")), -1100, -300) },
				  { TEXT("LF"), Mt.TexObject(TEXT("SwellNormal"), SAMPLERTYPE_Normal, Water.FindChecked(TEXT("T_Water_LF_N")), -1100, -150) },
				  { TEXT("WP"), Mt.WorldPos(-1100, 0) },
				  { TEXT("Cam"), E(Mt, UMaterialExpressionCameraPositionWS::StaticClass(), -1100, 80) },
				  { TEXT("Time"), E(Mt, UMaterialExpressionTime::StaticClass(), -1100, 160) },
				  { TEXT("Rain"), Mt.Collection(Mpc, TEXT("RainAmount"), -1100, 240) },
				  { TEXT("Strength"), Mt.Scalar(TEXT("WaveStrength"), 0.35f, -1100, 320) },
				  { TEXT("Deep"), Mt.Vector(TEXT("DeepColor"), FLinearColor(0.008f, 0.035f, 0.05f, 1.0f), -1100, 400) },
				  { TEXT("Shallow"), Mt.Vector(TEXT("ShallowColor"), FLinearColor(0.025f, 0.09f, 0.09f, 1.0f), -1100, 480) } },
				{ { TEXT("WorldNormal"), CMOT_Float3 }, { TEXT("Rough"), CMOT_Float1 } });
			Mt.Output(N, FString(), MP_BaseColor);
			Mt.Output(N, TEXT("WorldNormal"), MP_Normal);
			Mt.Output(N, TEXT("Rough"), MP_Roughness);
			Mt.M->TwoSided = true;
			Finish(Mt);
			OCEd::FinishInstance(OCEd::FreshInstance(TEXT("MI_Water"), MiRoot, Mt.M, Tag));
		}

		void BuildRainPostProcess(UMaterialParameterCollection* Mpc)
		{
			OCEd::FMat Mt = OCEd::FMat::Fresh(TEXT("M_PP_Rain"), MatRoot, Tag);
			Mt.M->MaterialDomain = MD_PostProcess;
			UMaterialExpressionSceneTexture* Scene = Cast<UMaterialExpressionSceneTexture>(E(Mt, UMaterialExpressionSceneTexture::StaticClass(), -1100, -200));
			Scene->SceneTextureId = PPI_PostProcessInput0;
			UMaterialExpressionCustom* N = Node(Mt, Hlsl::RAIN_PP_HLSL,
				{ { TEXT("Scene"), Scene }, { TEXT("UV"), Coord(Mt, 0, -1100, 0) },
				  { TEXT("Time"), E(Mt, UMaterialExpressionTime::StaticClass(), -1100, 100) },
				  { TEXT("Rain"), Mt.Collection(Mpc, TEXT("RainAmount"), -1100, 200) } }, {});
			Mt.Connect(Scene, TEXT("Color"), N, TEXT("Scene"));
			Mt.Output(N, FString(), MP_EmissiveColor);
			Finish(Mt);
		}

		// ---- Instances -------------------------------------------------------------------------

		struct FInstanceSpec
		{
			const TCHAR* Name;
			const TCHAR* Slug;
			float Tile;
			FLinearColor Tint;
			TArray<TPair<FString, float>> Settings;   // ProjectionMode and WetGround become static switches
		};

		void MakeInstance(const FInstanceSpec& S, UMaterialInterface* Parent, const FSet& T)
		{
			UMaterialInstanceConstant* Mi = OCEd::FreshInstance(S.Name, MiRoot, Parent, Tag);
			const TPair<const TCHAR*, const TCHAR*> Params[] = { { TEXT("BaseColor"), TEXT("BC") }, { TEXT("Normal"), TEXT("N") }, { TEXT("ORM"), TEXT("ORM") } };
			for (const auto& P : Params)
			{
				if (UTexture* Tex = Role(T, P.Value))
				{
					OCEd::SetTexture(Mi, P.Key, Tex);
				}
			}
			OCEd::SetScalar(Mi, TEXT("TileSizeM"), S.Tile);
			OCEd::SetVector(Mi, TEXT("Tint"), S.Tint);
			for (const TPair<FString, float>& K : S.Settings)
			{
				if (K.Key == TEXT("ProjectionMode"))
				{
					OCEd::SetSwitch(Mi, TEXT("ProjectionBit0"), (int32(K.Value) & 1) != 0);
					OCEd::SetSwitch(Mi, TEXT("ProjectionBit1"), (int32(K.Value) & 2) != 0);
				}
				else if (K.Key == TEXT("WetGround"))
				{
					OCEd::SetSwitch(Mi, TEXT("WetGround"), K.Value != 0.0f);
				}
				else
				{
					OCEd::SetScalar(Mi, *K.Key, K.Value);
				}
			}
			OCEd::FinishInstance(Mi);
		}

		TArray<FInstanceSpec> Instances()
		{
			const FLinearColor White(1, 1, 1, 1);
			return {
				{ TEXT("MI_Kit_Wall"), TEXT("plastered_wall"), 2.5f, FLinearColor(1.0f, 0.97f, 0.92f), { { TEXT("TintSlot"), 0 }, { TEXT("Macro"), 0.6f }, { TEXT("RoughMul"), 1.0f }, { TEXT("RoughAdd"), 0.05f }, { TEXT("GrimeAmount"), 0.7f } } },
				{ TEXT("MI_Kit_WallStucco"), TEXT("white_stucco"), 2.0f, FLinearColor(1.0f, 0.98f, 0.94f), { { TEXT("TintSlot"), 0 }, { TEXT("Macro"), 0.5f }, { TEXT("GrimeAmount"), 0.6f } } },
				{ TEXT("MI_Kit_Trim"), TEXT("old_sandstone_02"), 1.5f, FLinearColor(1.0f, 0.98f, 0.93f), { { TEXT("TintSlot"), 2 }, { TEXT("Macro"), 0.3f }, { TEXT("GrimeAmount"), 0.3f } } },
				{ TEXT("MI_Kit_Wood"), TEXT("wood_peeling_paint_weathered"), 1.2f, White, { { TEXT("TintSlot"), 1 }, { TEXT("Macro"), 0.2f } } },
				// Ribbed painted shutter metal: the tread plate used before read wrong on shop shutters (QA 2026-11-09).
				{ TEXT("MI_Kit_Metal"), TEXT("painted_metal_shutter"), 1.0f, FLinearColor(0.85f, 0.87f, 0.88f), { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.2f } } },
				// Port sheds (painted corrugated sheet, wall tint), brick warehouses (natural brick), awnings and laundry (linen, shutter colour).
				{ TEXT("MI_Kit_Corrugated"), TEXT("corrugated_iron_02"), 2.7f, FLinearColor(0.95f, 0.95f, 0.95f), { { TEXT("TintSlot"), 0 }, { TEXT("Macro"), 0.4f }, { TEXT("GrimeAmount"), 0.5f } } },
				{ TEXT("MI_Kit_Brick"), TEXT("red_brick_03"), 1.0f, FLinearColor(1.0f, 0.97f, 0.95f), { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.4f }, { TEXT("GrimeAmount"), 0.5f } } },
				{ TEXT("MI_Kit_Fabric"), TEXT("rough_linen"), 0.6f, FLinearColor(1.2f, 1.2f, 1.2f), { { TEXT("TintSlot"), 1 }, { TEXT("Macro"), 0.1f } } },
				{ TEXT("MI_Kit_Slate"), TEXT("roof_slates_02"), 1.5f, FLinearColor(0.95f, 0.96f, 1.0f), { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.5f }, { TEXT("ProjectionMode"), 1 } } },
				// Church marble: box-projected (the landmark pieces are scaled), white and a dark green for the bands.
				{ TEXT("MI_Kit_Marble"), TEXT("white_marble"), 2.0f, FLinearColor(1.0f, 0.99f, 0.97f), { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.3f }, { TEXT("ProjectionMode"), 3 }, { TEXT("RoughAdd"), -0.1f } } },
				{ TEXT("MI_Kit_MarbleDark"), TEXT("white_marble"), 2.0f, FLinearColor(0.11f, 0.18f, 0.15f), { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.3f }, { TEXT("ProjectionMode"), 3 }, { TEXT("RoughAdd"), -0.1f } } },
				{ TEXT("MI_Kit_Roof"), TEXT("clay_roof_tiles"), 2.0f, White, { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.5f }, { TEXT("ProjectionMode"), 1 } } },
				{ TEXT("MI_Kit_Concrete"), TEXT("brushed_concrete"), 2.0f, FLinearColor(0.95f, 0.94f, 0.92f), { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.5f }, { TEXT("PuddleAmount"), 0.3f }, { TEXT("WetGround"), 1 } } },
				{ TEXT("MI_Road_Asphalt"), TEXT("asphalt_02"), 4.0f, White, { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.7f }, { TEXT("WetGround"), 1 } } },
				{ TEXT("MI_Road_Setts"), TEXT("cobblestone_floor_01"), 2.0f, White, { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.5f }, { TEXT("WetGround"), 1 } } },
				{ TEXT("MI_Sidewalk"), TEXT("concrete_pavers_02"), 1.5f, White, { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.5f }, { TEXT("PuddleAmount"), 0.45f }, { TEXT("WetGround"), 1 } } },
				// Box-projected (ProjectionMode 3) for grey-box landmarks, bridges and distant scenery.
				{ TEXT("MI_Box_Stucco"), TEXT("white_stucco"), 2.0f, FLinearColor(1.0f, 0.95f, 0.88f), { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.5f }, { TEXT("ProjectionMode"), 3 } } },
				{ TEXT("MI_Box_Stone"), TEXT("old_sandstone_02"), 2.0f, FLinearColor(1.0f, 0.97f, 0.92f), { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.4f }, { TEXT("ProjectionMode"), 3 }, { TEXT("WetGround"), 1 } } },
				{ TEXT("MI_Box_Concrete"), TEXT("brushed_concrete"), 2.5f, FLinearColor(0.92f, 0.91f, 0.9f), { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.5f }, { TEXT("ProjectionMode"), 3 }, { TEXT("WetGround"), 1 } } },
				{ TEXT("MI_Box_Metal"), TEXT("metal_plate"), 1.5f, FLinearColor(0.4f, 0.42f, 0.44f), { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.3f }, { TEXT("ProjectionMode"), 3 } } },
				{ TEXT("MI_Box_Roof"), TEXT("clay_roof_tiles"), 2.0f, White, { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.4f }, { TEXT("ProjectionMode"), 3 } } },
				{ TEXT("MI_Box_Rock"), TEXT("cliff_side"), 12.0f, FLinearColor(0.95f, 0.92f, 0.88f), { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.8f }, { TEXT("ProjectionMode"), 3 } } },
				{ TEXT("MI_Box_Hill"), TEXT("aerial_grass_rock"), 30.0f, FLinearColor(0.8f, 0.82f, 0.7f), { { TEXT("TintSlot"), -1 }, { TEXT("Macro"), 0.8f }, { TEXT("ProjectionMode"), 2 } } } };
		}

		/** Road paving (INFRA_DESIGN.md Phase 4): one instance of M_Master_Road per "paving" value in the road data. */
		TArray<FString> BuildPavingInstances(UMaterial* Road, const FSets& Textures)
		{
			struct FPaving { const TCHAR* Paving; const TCHAR* Slug; float Tile; FLinearColor Tint; float Gaps; };
			const FPaving Pavings[] = {
				{ TEXT("asphalt"), TEXT("asphalt_02"), 4.0f, FLinearColor(1.0f, 1.0f, 1.0f), 0.0f },
				{ TEXT("sampietrini"), TEXT("cobblestone_square"), 1.6f, FLinearColor(0.52f, 0.52f, 0.56f), 1.0f },   // dark basalt setts
				{ TEXT("porfido"), TEXT("cobblestone_square"), 1.4f, FLinearColor(0.95f, 0.72f, 0.66f), 1.0f },       // reddish porphyry cubes
				{ TEXT("lastricato"), TEXT("granite_tile"), 3.0f, FLinearColor(1.0f, 0.98f, 0.95f), 0.8f },           // large stone slabs
				{ TEXT("stone_slab"), TEXT("granite_tile"), 3.0f, FLinearColor(1.05f, 1.02f, 0.97f), 0.8f },          // the Passerella's deck
				{ TEXT("gravel_white"), TEXT("gravel_floor"), 2.5f, FLinearColor(1.45f, 1.38f, 1.22f), 0.0f },        // strade bianche
				{ TEXT("concrete"), TEXT("concrete_pavers_02"), 3.0f, FLinearColor(1.0f, 1.0f, 1.0f), 0.5f } };       // quays and piers
			TArray<FString> Made;
			for (const FPaving& P : Pavings)
			{
				const FSet* T = Textures.Find(P.Slug);
				if (!T)
				{
					OCEd::Log(Tag, FString::Printf(TEXT("skip paving %s: textures for %s not downloaded"), P.Paving, P.Slug));
					continue;
				}
				UMaterialInstanceConstant* Mi = OCEd::FreshInstance(FString(TEXT("MI_Paving_")) + P.Paving, FString(MiRoot) / TEXT("Paving"), Road, Tag);
				for (const TCHAR* R : { TEXT("BC"), TEXT("N"), TEXT("ORM") })
				{
					OCEd::SetTexture(Mi, *FString::Printf(TEXT("A_%s"), R), Role(*T, R));
				}
				OCEd::SetScalar(Mi, TEXT("TileA"), P.Tile);
				OCEd::SetScalar(Mi, TEXT("GapWater"), P.Gaps);
				OCEd::SetVector(Mi, TEXT("Tint"), P.Tint);
				OCEd::FinishInstance(Mi);
				Made.Add(Mi->GetName());
			}
			return Made;
		}

		// ---- Decals ----------------------------------------------------------------------------

		struct FDecalSet { const TCHAR* Id; float FromColor, Strength, Rough, Key, Gain; };

		// Opacity from colour = how far the colour is below the key, times the gain: the wall leaks sit on white
		// (key 0.92), the tyre tracks on light grey (0.48 to 0.60 in linear colour, as the shader sees it).
		const FDecalSet DecalSets[] = {
			{ TEXT("ManholeCover001"), 0.0f, 1.0f, -1.0f, 0.92f, 2.5f },
			{ TEXT("ManholeCover004"), 0.0f, 1.0f, -1.0f, 0.92f, 2.5f },
			{ TEXT("AsphaltDamage001"), 0.0f, 1.0f, -1.0f, 0.92f, 2.5f },
			{ TEXT("ChewingGum001"), 0.0f, 1.0f, -1.0f, 0.92f, 2.5f },
			{ TEXT("ManholeCover009"), 0.0f, 1.0f, -1.0f, 0.92f, 2.5f },   // gutter gullies (Phase 4)
			{ TEXT("ManholeCover010"), 0.0f, 1.0f, -1.0f, 0.92f, 2.5f },   // covers set in cobbles (Phase 4)
			{ TEXT("TireTracks001"), 1.0f, 0.55f, 0.6f, 0.42f, 4.0f },     // linear: background 0.48-0.60, tracks 0.07-0.32
			{ TEXT("Leaking012A"), 1.0f, 0.8f, -1.0f, 0.92f, 2.5f },
			{ TEXT("Leaking013A"), 1.0f, 0.8f, -1.0f, 0.92f, 2.5f },
			{ TEXT("Leaking016A"), 1.0f, 0.8f, -1.0f, 0.92f, 2.5f },
			{ TEXT("Leaking019A"), 1.0f, 0.8f, -1.0f, 0.92f, 2.5f } };

		FSet ImportDecalSet(const FString& Id)
		{
			const FString Folder = OCEd::ProjectDir() / TEXT("SourceArt/Downloads/Decals") / Id;
			const FString Dest = FString(TEXT("/Game/Environment/Textures/Decals/")) + Id;
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.png")), true, false);
			Files.Sort();
			FSet Maps;
			for (const FString& F : Files)
			{
				const FString Name = TEXT("T_") + FPaths::GetBaseFilename(F).Replace(TEXT("_1K-PNG"), TEXT(""));
				OCEd::ImportFile(Folder / F, Dest, Name);
				UTexture* Tex = CastChecked<UTexture>(OCEd::Require(Dest / Name));
				FString Left, R;
				Name.Split(TEXT("_"), &Left, &R, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
				if (R == TEXT("NormalDX"))
				{
					OCEd::TextureSettings(Tex, false, TC_Normalmap, TEXTUREGROUP_World, 1024);
				}
				else if (R == TEXT("Roughness") || R == TEXT("Opacity"))
				{
					OCEd::TextureSettings(Tex, false, TC_Grayscale, TEXTUREGROUP_World, 1024);
				}
				else
				{
					OCEd::TextureSettings(Tex, true, TC_Default, TEXTUREGROUP_World, 1024);
				}
				OCEd::Save(Tex);
				Maps.Add(R, Tex);
			}
			return Maps;
		}

		UMaterial* BuildDecalMaster(const FSet& Defaults)
		{
			OCEd::FMat Mt = OCEd::FMat::Fresh(TEXT("M_Master_Decal"), MatRoot, TEXT("oc_decals"));
			// Translucent deferred decal; with r.DBuffer on, the connected outputs (colour, normal, roughness)
			// make it a DBuffer decal (the decal blend mode is derived in 5.8, not set).
			Mt.M->MaterialDomain = MD_DeferredDecal;
			Mt.M->BlendMode = BLEND_Translucent;
			UMaterialExpressionCustom* N = Node(Mt, Hlsl::DECAL_HLSL,
				{ { TEXT("BC"), Mt.TexObject(TEXT("Color"), SAMPLERTYPE_Color, Role(Defaults, TEXT("Color")), -1100, -300) },
				  { TEXT("NM"), Mt.TexObject(TEXT("Normal"), SAMPLERTYPE_Normal, Role(Defaults, TEXT("NormalDX")), -1100, -150) },
				  { TEXT("RO"), Mt.TexObject(TEXT("Roughness"), SAMPLERTYPE_Grayscale, Role(Defaults, TEXT("Roughness")), -1100, 0) },
				  { TEXT("OP"), Mt.TexObject(TEXT("Opacity"), SAMPLERTYPE_Grayscale, Role(Defaults, TEXT("Opacity")), -1100, 150) },
				  { TEXT("UV"), Coord(Mt, 0, -1100, 300) },
				  { TEXT("FromColor"), Mt.Scalar(TEXT("OpacityFromColor"), 0.0f, -1100, 380) },
				  { TEXT("Strength"), Mt.Scalar(TEXT("Strength"), 1.0f, -1100, 460) },
				  { TEXT("RoughOverride"), Mt.Scalar(TEXT("RoughnessOverride"), -1.0f, -1100, 540) },
				  { TEXT("ColorKey"), Mt.Scalar(TEXT("ColorKey"), 0.92f, -1100, 620) },
				  { TEXT("ColorGain"), Mt.Scalar(TEXT("ColorGain"), 2.5f, -1100, 700) },
				  { TEXT("Wet"), Mt.Collection(BuildWorldParameters(), TEXT("Wetness"), -1100, 780) } },
				{ { TEXT("Opacity"), CMOT_Float1 }, { TEXT("Normal"), CMOT_Float3 }, { TEXT("Rough"), CMOT_Float1 } });
			Mt.Output(N, FString(), MP_BaseColor);
			Mt.Output(N, TEXT("Normal"), MP_Normal);
			Mt.Output(N, TEXT("Rough"), MP_Roughness);
			Mt.Output(N, TEXT("Opacity"), MP_Opacity);
			Mt.Finish(TEXT("oc_decals"));
			return Mt.M;
		}
	}

	bool BuildDecals()
	{
		TArray<TPair<const FDecalSet*, FSet>> Sets;
		for (const FDecalSet& S : DecalSets)
		{
			if (IFileManager::Get().DirectoryExists(*(OCEd::ProjectDir() / TEXT("SourceArt/Downloads/Decals") / S.Id)))
			{
				Sets.Add({ &S, ImportDecalSet(S.Id) });
			}
		}
		if (Sets.IsEmpty())
		{
			UE_LOG(LogOpenCity, Error, TEXT("no decals downloaded: run Tools/fetch_ambientcg"));
			return false;
		}
		UMaterial* Master = BuildDecalMaster(Sets[0].Value);   // ManholeCover001 when downloaded (first in the table)
		TArray<FString> Names;
		for (const TPair<const FDecalSet*, FSet>& S : Sets)
		{
			UMaterialInstanceConstant* Mi = OCEd::FreshInstance(FString(TEXT("MI_Decal_")) + S.Key->Id, TEXT("/Game/Environment/Materials/Decals"), Master, TEXT("oc_decals"));
			const TPair<const TCHAR*, const TCHAR*> Params[] = { { TEXT("Color"), TEXT("Color") }, { TEXT("Normal"), TEXT("NormalDX") },
				{ TEXT("Roughness"), TEXT("Roughness") }, { TEXT("Opacity"), TEXT("Opacity") } };
			for (const auto& P : Params)
			{
				if (UTexture* T = Role(S.Value, P.Value))
				{
					OCEd::SetTexture(Mi, P.Key, T);
				}
			}
			OCEd::SetScalar(Mi, TEXT("OpacityFromColor"), S.Key->FromColor);
			OCEd::SetScalar(Mi, TEXT("Strength"), S.Key->Strength);
			OCEd::SetScalar(Mi, TEXT("RoughnessOverride"), S.Key->Rough);
			OCEd::SetScalar(Mi, TEXT("ColorKey"), S.Key->Key);
			OCEd::SetScalar(Mi, TEXT("ColorGain"), S.Key->Gain);
			OCEd::FinishInstance(Mi);
			Names.Add(S.Key->Id);
		}
		Names.Sort();
		OCEd::Log(TEXT("oc_decals"), TEXT("decal instances: ") + FString::Join(Names, TEXT(", ")));
		return true;
	}

	bool BuildMaterials()
	{
		ImportLut();
		const FSets Textures = ImportTextures();
		const TMap<FString, UTexture*> Water = ImportWaterNormals();
		static const FSet Empty;
		const FSet* First = Textures.Find(TEXT("plastered_wall"));
		UMaterialParameterCollection* Mpc = BuildWorldParameters();
		UTexture* Ripples = Water.FindChecked(TEXT("T_Water_N"));
		UMaterial* Surface = BuildSurfaceMaster(First ? *First : Empty, Mpc, Ripples, TEXT("M_Master_Surface"), false);
		UMaterial* Glass = BuildGlassMaster(Mpc);
		BuildLampMaster(Mpc);
		BuildSurfaceMaster(First ? *First : Empty, Mpc, Ripples, TEXT("M_Master_SurfaceTwoSided"), true);
		BuildWaterMaster(Mpc, Water);
		BuildRainPostProcess(Mpc);
		BuildCarGlass(Mpc);
		BuildFoliageMaster(Mpc);
		BuildCarPaint(Mpc);
		BuildPaintMaster(Mpc);
		BuildCharacterMaster(Mpc);
		TArray<FString> Made;
		for (const FInstanceSpec& S : Instances())
		{
			const FSet* T = Textures.Find(S.Slug);
			if (!T)
			{
				OCEd::Log(Tag, FString::Printf(TEXT("skip %s: textures for %s not downloaded"), S.Name, S.Slug));
				continue;
			}
			MakeInstance(S, Surface, *T);
			Made.Add(S.Name);
		}
		OCEd::FinishInstance(OCEd::FreshInstance(TEXT("MI_Kit_Glass"), MiRoot, Glass, Tag));
		Made.Add(TEXT("MI_Kit_Glass"));
		UMaterial* Road = BuildTexturedMaster(TEXT("M_Master_Road"), Hlsl::ROAD_HLSL, { { TEXT("A"), &TexturesFor(Textures, TEXT("asphalt_02")) } },
			{ { TEXT("TileA"), 4.0f }, { TEXT("GapWater"), 0.0f } }, false,
			[&](OCEd::FMat& Mt)
			{
				FInputs In = GroundWet(Mt, Mpc, Ripples, 0.6f);
				In.Add({ TEXT("Tint"), Mt.Vector(TEXT("Tint"), FLinearColor(1, 1, 1, 1), -1100, 700) });
				return In;
			});
		Made.Append(BuildPavingInstances(Road, Textures));
		BuildTexturedMaster(TEXT("M_Master_Landscape"), Hlsl::LAND_HLSL,
			{ { TEXT("G"), &TexturesFor(Textures, TEXT("aerial_grass_rock")) }, { TEXT("D"), &TexturesFor(Textures, TEXT("brown_mud_rocks_01")) },
			  { TEXT("R"), &TexturesFor(Textures, TEXT("cliff_side")) }, { TEXT("S"), &TexturesFor(Textures, TEXT("coast_sand_01")) } },
			{ { TEXT("TileG"), 6.0f }, { TEXT("TileD"), 4.0f }, { TEXT("TileR"), 8.0f }, { TEXT("TileS"), 4.0f } }, true,
			[&](OCEd::FMat& Mt) { return GroundWet(Mt, Mpc, Ripples, 0.5f); });
		OCEd::Log(Tag, TEXT("instances: ") + FString::Join(Made, TEXT(", ")));
		return true;
	}
}
