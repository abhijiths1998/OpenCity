// Editor tools (formerly the OCQABookmarks commandlet, the OCPluginProbe commandlet, the OCVerifyBlockout commandlet and the OCAudit commandlet).
#include "OCToolCommandlets.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Dev/OCCameraBookmarks.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Interfaces/IPluginManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialInstance.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "OCAuditLibrary.h"
#include "OCEditorHelpers.h"
#include "OCLog.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "World/OCBlockoutActor.h"
#include "World/OCWorldBuild.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHelpers.h"

using namespace OCWorldBuild;

namespace
{
	void InitTool(UCommandlet* C)
	{
		C->IsClient = false;
		C->IsEditor = true;
		C->IsServer = false;
		C->LogToConsole = true;
	}

	FJsonPtr ReadLayout()
	{
		FString Text, Error;
		const FString Path = OCEd::WorldDir() / TEXT("marevia_layout.json");
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			UE_LOG(LogOpenCity, Error, TEXT("cannot read %s"), *Path);
			return nullptr;
		}
		return OCLayout::FJson::Parse(Text, Error);
	}

	/** Labels and classes of every actor in the loaded map: World Partition descriptors when partitioned, so
	 *  unloaded actors count too. */
	struct FActorInfo { FString Label; FString Class; };
	TArray<FActorInfo> MapActors(UWorld* World)
	{
		TArray<FActorInfo> Out;
		if (UWorldPartition* Wp = World->GetWorldPartition())
		{
			FWorldPartitionHelpers::ForEachActorDescInstance(Wp, AActor::StaticClass(), [&Out](const FWorldPartitionActorDescInstance* D)
			{
				Out.Add({ D->GetActorLabel().ToString(), D->GetActorNativeClass() ? D->GetActorNativeClass()->GetPathName() : FString() });
				return true;
			});
		}
		return Out;
	}

	/** Python's x % y for a positive y (the result takes the divisor's sign). */
	double PyMod(double X, double Y) { return X - Y * FMath::FloorToDouble(X / Y); }
}

// ---- OCQABookmarks ------------------------------------------------------------------------------

UOCQABookmarksCommandlet::UOCQABookmarksCommandlet() { InitTool(this); }

int32 UOCQABookmarksCommandlet::Main(const FString& Params)
{
	// Street: 1.7 m eye height by the kerb of the district's longest street, looking along it. Driving: chase-camera
	// height on its longest drivable road, down the lane. Elevated: 45 m up, 160 m outside the centre, looking across.
	const FJsonPtr Layout = ReadLayout();
	if (!Layout.IsValid())
	{
		return 1;
	}
	const FJsonPtr Roads = Layout->Get(TEXT("roads"));
	const TArray<FJsonPtr>& Nodes = Arr(Roads, TEXT("nodes"));
	const TArray<FJsonPtr>& Segs = Arr(Roads, TEXT("segments"));
	auto N = [&Nodes](int32 I) { return FVector(At(Nodes[I], 0), At(Nodes[I], 1), At(Nodes[I], 2)); };
	auto Mark = [](const FString& Name, const FString& District, const TCHAR* Kind, const FVector& Pos, const FRotator& Rot, float Fov)
	{
		FOCCameraBookmark B;
		B.Name = *Name;
		B.District = *District;
		B.Kind = Kind;
		B.Location = Pos;
		B.Rotation = Rot;
		B.FOV = Fov;
		return B;
	};
	TArray<FOCCameraBookmark> Marks;
	for (const FJsonPtr& D : Arr(Layout, TEXT("districts")))
	{
		const FString Name = Str(D, TEXT("name"));
		const FJsonPtr R = D->Get(TEXT("rect"));
		const double X0 = At(R, 0), Y0 = At(R, 1), X1 = At(R, 2), Y1 = At(R, 3);
		auto Inside = [&](const FVector& P) { return X0 <= P.X && P.X <= X1 && Y0 <= P.Y && P.Y <= Y1; };
		auto Longest = [&](std::initializer_list<const TCHAR*> Kinds) -> FJsonPtr
		{
			FJsonPtr Best;
			double BestLen = 0.0;
			for (const FJsonPtr& S : Segs)
			{
				const FString Kind = Str(S, TEXT("kind"));
				bool bKind = false;
				for (const TCHAR* K : Kinds) { bKind |= Kind == K; }
				const FVector A = N(Int(S, TEXT("a"))), B = N(Int(S, TEXT("b")));
				if (bKind && !Truthy(S, TEXT("bridge")) && Inside(A) && Inside(B))
				{
					const double Len = FVector2D::Distance(FVector2D(A), FVector2D(B));
					if (Len > BestLen) { Best = S; BestLen = Len; }
				}
			}
			return Best;
		};
		FJsonPtr Street = Longest({ TEXT("street"), TEXT("avenue"), TEXT("coast"), TEXT("pier"), TEXT("pass") });
		if (!Street.IsValid()) { Street = Longest({ TEXT("alley") }); }
		FJsonPtr Drive = Longest({ TEXT("avenue"), TEXT("coast"), TEXT("pass"), TEXT("street"), TEXT("pier") });
		if (!Drive.IsValid()) { Drive = Street; }
		if (Street.IsValid())
		{
			const FVector A = N(Int(Street, TEXT("a"))), B = N(Int(Street, TEXT("b")));
			const double Len = FVector2D::Distance(FVector2D(A), FVector2D(B));
			const double Ux = (B.X - A.X) / Len, Uy = (B.Y - A.Y) / Len;
			const double Side = Num(Street, TEXT("width")) / 2 - 0.8;   // on the road by the kerb: pavements have trees and lamps
			const double Px = A.X + Ux * Len * 0.25 - Uy * Side, Py = A.Y + Uy * Len * 0.25 + Ux * Side;
			Marks.Add(Mark(Name + TEXT("_Street"), Name, TEXT("Street"), Cm(Px, Py, A.Z + 0.3 + 1.7), FRotator(2.0, UeYaw(Ux, Uy), 0.0), 75.0f));
		}
		if (Drive.IsValid())
		{
			const FVector A = N(Int(Drive, TEXT("a"))), B = N(Int(Drive, TEXT("b")));
			const double Len = FVector2D::Distance(FVector2D(A), FVector2D(B));
			const double Ux = (B.X - A.X) / Len, Uy = (B.Y - A.Y) / Len;
			const double Lane = Num(Drive, TEXT("width")) / 4;
			const double Px = A.X + Ux * Len * 0.15 + Uy * Lane, Py = A.Y + Uy * Len * 0.15 - Ux * Lane;
			Marks.Add(Mark(Name + TEXT("_Driving"), Name, TEXT("Driving"), Cm(Px, Py, A.Z + 2.2), FRotator(-4.0, UeYaw(Ux, Uy), 0.0), 80.0f));
		}
		const double Cx = (X0 + X1) / 2, Cy = (Y0 + Y1) / 2;
		int32 Near = 0;
		for (int32 i = 1; i < Nodes.Num(); ++i)
		{
			if (FVector2D::Distance(FVector2D(N(i)), FVector2D(Cx, Cy)) < FVector2D::Distance(FVector2D(N(Near)), FVector2D(Cx, Cy))) { Near = i; }
		}
		const double Ex = Cx - 160.0, Ey = Cy - 160.0;
		const double H = N(Near).Z + 45.0;
		const double Pitch = -FMath::RadiansToDegrees(FMath::Atan2(45.0, FVector2D::Distance(FVector2D(Ex, Ey), FVector2D(Cx, Cy))));
		Marks.Add(Mark(Name + TEXT("_Elevated"), Name, TEXT("Elevated"), Cm(Ex, Ey, H), FRotator(Pitch, UeYaw(Cx - Ex, Cy - Ey), 0.0), 70.0f));
	}
	UOCCameraBookmarks* Asset = CastChecked<UOCCameraBookmarks>(OCEd::GetOrCreateDataAsset(TEXT("DA_QABookmarks"), TEXT("/Game/Dev"), UOCCameraBookmarks::StaticClass(), TEXT("oc_qa")));
	Asset->Modify();
	Asset->Bookmarks = Marks;
	OCEd::Save(Asset);
	OCEd::Log(TEXT("oc_qa"), FString::Printf(TEXT("%d bookmarks in %s"), Marks.Num(), *Asset->GetPathName()));
	return 0;
}

// ---- OCPluginProbe ------------------------------------------------------------------------------

UOCPluginProbeCommandlet::UOCPluginProbeCommandlet() { InitTool(this); }

int32 UOCPluginProbeCommandlet::Main(const FString& Params)
{
	FString List;
	FParse::Value(*Params, TEXT("plugins="), List, /*bShouldStopOnSeparator*/ false);
	TArray<FString> Names;
	List.ParseIntoArray(Names, TEXT(","));
	bool bFailed = false;
	for (const FString& Name : Names)
	{
		const TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(Name);
		if (P.IsValid() && P->IsEnabled())
		{
			UE_LOG(LogOpenCity, Display, TEXT("OCPROBE OK %s"), *Name);
		}
		else
		{
			UE_LOG(LogOpenCity, Warning, TEXT("OCPROBE FAIL %s is not enabled"), *Name);
			bFailed = true;
		}
	}
	UE_LOG(LogOpenCity, Display, TEXT("OCPROBE DONE%s"), bFailed ? TEXT(" with failures") : TEXT(""));
	return bFailed ? 1 : 0;
}

// ---- OCVerifyBlockout ---------------------------------------------------------------------------

UOCVerifyBlockoutCommandlet::UOCVerifyBlockoutCommandlet() { InitTool(this); }

int32 UOCVerifyBlockoutCommandlet::Main(const FString& Params)
{
	const TCHAR* BlockTag = TEXT("oc_verify_blockout");
	constexpr double GridCm = 100.0;     // WORLD_METRICS.md modular grid
	constexpr double StoreyCm = 320.0;   // WORLD_METRICS.md floor-to-floor
	TArray<FString> Failures;
	auto Check = [&](const FString& Name, bool bOk, const FString& Detail = FString())
	{
		OCEd::Log(BlockTag, FString::Printf(TEXT("%s %s%s%s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *Name, Detail.IsEmpty() ? TEXT("") : TEXT(": "), *Detail));
		if (!bOk) { Failures.Add(Name + TEXT(": ") + Detail); }
	};
	UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(MapPath);
	if (!World)
	{
		UE_LOG(LogOpenCity, Error, TEXT("could not load %s"), MapPath);
		return 1;
	}
	const TArray<FActorInfo> Descs = MapActors(World);
	OCEd::Log(BlockTag, FString::Printf(TEXT("%d actor descriptors in %s"), Descs.Num(), MapPath));

	// Buildings: old maps were saved before the BLOCKOUT_* label change; accept both, flag the old ones.
	int32 NewLabel = 0, OldLabel = 0;
	for (const FActorInfo& D : Descs)
	{
		if (D.Label.StartsWith(TEXT("BLOCKOUT_Buildings_"), ESearchCase::CaseSensitive)) { ++NewLabel; }
		else if (D.Label.StartsWith(TEXT("Buildings_"), ESearchCase::CaseSensitive))
		{
			++OldLabel;
			Check(TEXT("blockout label ") + D.Label, false, TEXT("not BLOCKOUT_* (pre-update map)"));
		}
	}
	Check(TEXT("building chunks exist"), NewLabel + OldLabel > 0, FString::FromInt(NewLabel + OldLabel));
	OCEd::Log(BlockTag, FString::Printf(TEXT("building chunks: %d BLOCKOUT_*, %d old-style"), NewLabel, OldLabel));

	// Instanced mesh and per-instance transforms of every blockout actor, loaded one by one (a commandlet streams
	// nothing in, so the loaded-actor view alone sees none).
	int32 Actors = 0, Total = 0, OffGrid = 0, OffStorey = 0;
	auto CheckActor = [&](AOCBlockoutActor* It)
	{
		++Actors;
		TArray<UInstancedStaticMeshComponent*> Comps;
		It->GetComponents(Comps);
		for (UInstancedStaticMeshComponent* C : Comps)
		{
			if (C->GetStaticMesh() && C->GetStaticMesh()->GetPathName() != TEXT("/Engine/BasicShapes/Cube.Cube"))
			{
				Check(It->GetActorLabel() + TEXT(" mesh"), false, TEXT("not engine cube: ") + C->GetStaticMesh()->GetPathName());
			}
			const int32 Count = C->GetInstanceCount();
			Total += Count;
			for (int32 i = 0; i < Count; ++i)
			{
				FTransform T;
				C->GetInstanceTransform(i, T, /*bWorldSpace*/ true);
				const FVector L = T.GetTranslation();
				if (FMath::Abs(PyMod(L.X, GridCm)) > 0.5 || FMath::Abs(PyMod(L.Y, GridCm)) > 0.5) { ++OffGrid; }
				const FVector S = T.GetScale3D() * 100.0;   // 100 cm cube: size in cm = scale * 100
				if (FMath::Abs(PyMod(S.X, GridCm)) > 0.5 || FMath::Abs(PyMod(S.Y, GridCm)) > 0.5) { ++OffGrid; }
				if (FMath::Abs(PyMod(S.Z, StoreyCm)) > 0.5) { ++OffStorey; }
			}
		}
	};
	if (UWorldPartition* Wp = World->GetWorldPartition())
	{
		FWorldPartitionHelpers::FForEachActorWithLoadingParams P;
		P.ActorClasses = { AOCBlockoutActor::StaticClass() };
		FWorldPartitionHelpers::ForEachActorWithLoading(Wp, [&](const FWorldPartitionActorDescInstance* D)
		{
			if (AOCBlockoutActor* A = Cast<AOCBlockoutActor>(D->GetActor())) { CheckActor(A); }
			return true;
		}, P);
	}
	Check(TEXT("blockout actors in world"), Actors > 0, FString::FromInt(Actors));
	Check(TEXT("blockout instances"), Total > 0, FString::FromInt(Total));
	Check(TEXT("instances on 100 cm grid"), OffGrid == 0, FString::Printf(TEXT("%d off-grid"), OffGrid));
	Check(TEXT("heights on 320 cm storeys"), OffStorey == 0, FString::Printf(TEXT("%d off-storey"), OffStorey));

	int32 RoadChunks = 0, Mannequins = 0, Cars = 0;
	for (const FActorInfo& D : Descs)
	{
		RoadChunks += D.Label.StartsWith(TEXT("Roads_"), ESearchCase::CaseSensitive) ? 1 : 0;
		Mannequins += (D.Label.Contains(TEXT("Mannequin"), ESearchCase::CaseSensitive) || D.Label.Contains(TEXT("SK_Manny"), ESearchCase::CaseSensitive)) ? 1 : 0;
		Cars += (D.Label.Contains(TEXT("Car"), ESearchCase::CaseSensitive) || D.Label.Contains(TEXT("Vehicle"), ESearchCase::CaseSensitive)) ? 1 : 0;
	}
	Check(TEXT("road chunks exist"), RoadChunks > 0, FString::FromInt(RoadChunks));
	Check(TEXT("scale-reference mannequin present"), Mannequins > 0, FString::FromInt(Mannequins));
	Check(TEXT("scale-reference car present"), Cars > 0, FString::FromInt(Cars));
	if (!Failures.IsEmpty())
	{
		for (const FString& F : Failures) { OCEd::Log(BlockTag, TEXT("  FAIL ") + F); }
		UE_LOG(LogOpenCity, Error, TEXT("%d blockout check(s) failed"), Failures.Num());
		return 1;
	}
	OCEd::Log(BlockTag, TEXT("all blockout checks passed"));
	return 0;
}

// ---- OCAudit --------------------------------------------------------------------------------------

namespace
{
	using FProblems = TArray<FString>;
	struct FFinding { FString Asset; FProblems Problems; };

	const TCHAR* MasterPrefix = TEXT("/Game/Materials/M_Master_");

	FString Short(const FString& Path)
	{
		FString L, R;
		return Path.Split(TEXT("."), &L, &R) ? L : Path;
	}

	const TPair<const TCHAR*, const TCHAR*> CodeGenerated[] = {   // path prefix -> what made it (blockout only)
		{ TEXT("/Game/World/Roads/"), TEXT("road chunks built in C++ from the road graph (OCWorldBuilderLibrary)") },
		{ TEXT("/Game/Vehicles/Bike/"), TEXT("motorbike chassis generated as glTF (bike chassis generator)") },
		{ TEXT("/Game/Dev/"), TEXT("dev/test content") } };

	FProblems CheckStaticMesh(const FString& Path, UStaticMesh* Mesh)
	{
		FProblems P;
		for (const auto& C : CodeGenerated)
		{
			if (Path.StartsWith(C.Key, ESearchCase::CaseSensitive)) { P.Add(FString::Printf(TEXT("code-generated: %s; blockout only, needs a BLOCKOUT_ name or final-art replacement"), C.Value)); }
		}
		const FOCMeshAuditInfo Info = UOCAuditLibrary::GetStaticMeshAuditInfo(Mesh);
		if (Info.NumUVChannels < 1) { P.Add(TEXT("no UV channel 0")); }
		if (Info.SimpleCollisionCount == 0 && !Info.bComplexAsSimple) { P.Add(TEXT("no simple collision (no UCX_/primitives) and not complex-as-simple")); }
		if (!Info.bNanite && Info.NumLODs < 2) { P.Add(TEXT("no Nanite and only one LOD")); }
		const FVector E = Mesh->GetBounds().BoxExtent;
		const double Size = 2 * FMath::Max3(E.X, E.Y, E.Z);
		if (Size > 5000 && !Path.StartsWith(TEXT("/Game/World/Roads/"), ESearchCase::CaseSensitive)) { P.Add(FString::Printf(TEXT("very large single mesh (%.0f m): not modular"), Size / 100)); }
		if (Size < 1) { P.Add(TEXT("degenerate bounds (empty mesh?)")); }
		return P;
	}

	FProblems CheckSkeletalMesh(const FString& Path, USkeletalMesh* Mesh)
	{
		FProblems P;
		for (const auto& C : CodeGenerated)
		{
			if (Path.StartsWith(C.Key, ESearchCase::CaseSensitive)) { P.Add(FString::Printf(TEXT("code-generated: %s; blockout only"), C.Value)); }
		}
		if (!Mesh->GetPhysicsAsset()) { P.Add(TEXT("no physics asset (no collision)")); }
		return P;
	}

	FProblems CheckTexture(const FString& Path, UTexture2D* Tex)
	{
		struct FSuffix { const TCHAR* Suffix; const TCHAR* Kind; bool bSrgb; };
		const FSuffix Suffixes[] = { { TEXT("_ORM"), TEXT("masks"), false }, { TEXT("_BC"), TEXT("default"), true }, { TEXT("_N"), TEXT("normal"), false },
			{ TEXT("_M"), TEXT("masks"), false }, { TEXT("_E"), TEXT("default"), true } };   // longest first
		FProblems P;
		const FString Name = Short(FPaths::GetCleanFilename(Path));
		if (!Name.StartsWith(TEXT("T_"), ESearchCase::CaseSensitive)) { P.Add(TEXT("name lacks the T_ prefix")); }
		const FSuffix* S = nullptr;
		for (const FSuffix& X : Suffixes)
		{
			if (Name.EndsWith(X.Suffix, ESearchCase::CaseSensitive)) { S = &X; break; }
		}
		const FString Comp = StaticEnum<TextureCompressionSettings>()->GetNameStringByValue(Tex->CompressionSettings);
		if (!S)
		{
			P.Add(TEXT("no known suffix (_BC _N _ORM _E _M): role unknown, settings not checkable"));
		}
		else
		{
			if (FCString::Strcmp(S->Kind, TEXT("normal")) == 0 && Tex->CompressionSettings != TC_Normalmap) { P.Add(FString::Printf(TEXT("normal map not using Normalmap compression (%s)"), *Comp)); }
			if (FCString::Strcmp(S->Kind, TEXT("masks")) == 0 && Tex->CompressionSettings != TC_Masks) { P.Add(FString::Printf(TEXT("mask/ORM not using Masks compression (%s)"), *Comp)); }
			if (bool(Tex->SRGB) != S->bSrgb) { P.Add(FString::Printf(TEXT("sRGB is %s, should be %s"), Tex->SRGB ? TEXT("True") : TEXT("False"), S->bSrgb ? TEXT("True") : TEXT("False"))); }
		}
		const int32 Sx = Tex->GetSizeX(), Sy = Tex->GetSizeY();
		if (FMath::Max(Sx, Sy) > 2048) { P.Add(FString::Printf(TEXT("%dx%d: above the 2K in-game cap"), Sx, Sy)); }
		if (Tex->MipGenSettings == TMGS_NoMipmaps) { P.Add(TEXT("no mipmaps")); }
		return P;
	}

	UMaterial* BaseMaterial(UMaterialInterface* M)
	{
		for (int32 i = 0; i < 16 && M; ++i)
		{
			if (UMaterialInstance* Mi = Cast<UMaterialInstance>(M)) { M = Mi->Parent; }
			else { return Cast<UMaterial>(M); }
		}
		return nullptr;
	}

	FProblems CheckMaterial(const FString& Path, UMaterialInterface* Mat, TArray<FFinding>& Tiling)
	{
		FProblems P;
		UMaterial* Base = BaseMaterial(Mat);
		const bool bMaster = Path.StartsWith(MasterPrefix, ESearchCase::CaseSensitive);
		if (!Base)
		{
			P.Add(TEXT("material instance with no parent"));
			return P;
		}
		const FString BasePath = Base->GetPathName();
		if (!bMaster && !BasePath.StartsWith(MasterPrefix, ESearchCase::CaseSensitive)) { P.Add(FString::Printf(TEXT("not built from an approved master (base: %s)"), *Short(BasePath))); }
		if (UMaterial* M = Cast<UMaterial>(Mat); M && !bMaster)
		{
			if (UMaterialEditingLibrary::GetUsedTextures(M).IsEmpty()) { P.Add(TEXT("no textures: flat or procedural colour")); }
			if (!UMaterialEditingLibrary::GetMaterialPropertyInputNode(M, MP_Normal)) { P.Add(TEXT("no normal input")); }
			UMaterialExpression* Rough = UMaterialEditingLibrary::GetMaterialPropertyInputNode(M, MP_Roughness);
			if (!Rough || Rough->IsA<UMaterialExpressionConstant>()) { P.Add(TEXT("constant roughness (looks plastic)")); }
		}
		const FString Name = FPaths::GetCleanFilename(Path).ToLower();
		bool bLarge = false;
		for (const TCHAR* W : { TEXT("terrain"), TEXT("landscape"), TEXT("road"), TEXT("building"), TEXT("sidewalk"), TEXT("water"), TEXT("ground") })
		{
			bLarge |= Name.Contains(W, ESearchCase::CaseSensitive);
		}
		if (bLarge && !bMaster && !Path.StartsWith(TEXT("/Game/Vehicles/"), ESearchCase::CaseSensitive) && !Path.StartsWith(TEXT("/Game/Characters/"), ESearchCase::CaseSensitive))
		{
			TArray<FName> Scalars, Textures;
			UMaterialEditingLibrary::GetScalarParameterNames(Base, Scalars);
			UMaterialEditingLibrary::GetTextureParameterNames(Base, Textures);
			Scalars.Append(Textures);
			bool bMacro = false;
			for (const FName& N : Scalars)
			{
				const FString L = N.ToString().ToLower();
				for (const TCHAR* W : { TEXT("macro"), TEXT("variation"), TEXT("detail"), TEXT("distance") }) { bMacro |= L.Contains(W, ESearchCase::CaseSensitive); }
			}
			if (!bMacro) { Tiling.Add({ Short(Path), { TEXT("large-surface material with no macro/variation/distance-blend parameter") } }); }
		}
		return P;
	}

	FProblems CheckMap(const FString& Path, int32& OutActors)
	{
		FProblems P;
		UWorld* World = UEditorLoadingAndSavingUtils::LoadMap(Short(Path));
		if (!World)
		{
			P.Add(TEXT("could not inspect (load failed)"));
			return P;
		}
		TArray<FString> Classes;
		const TArray<FActorInfo> Descs = MapActors(World);
		for (const FActorInfo& D : Descs) { Classes.Add(D.Class); }
		TArray<AActor*> Actors = GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->GetAllLevelActors();
		for (AActor* A : Actors) { Classes.Add(A->GetClass()->GetPathName()); }
		auto Has = [&Classes](const TCHAR* Key) { return Classes.ContainsByPredicate([Key](const FString& C) { return C.Contains(Key, ESearchCase::CaseSensitive); }); };
		const TPair<const TCHAR*, const TCHAR*> Need[] = { { TEXT("SkyAtmosphere"), TEXT("no Sky Atmosphere") }, { TEXT("DirectionalLight"), TEXT("no Directional Light (sun)") },
			{ TEXT("SkyLight"), TEXT("no Sky Light") }, { TEXT("ExponentialHeightFog"), TEXT("no Exponential Height Fog") },
			{ TEXT("PostProcessVolume"), TEXT("no Post Process Volume: default exposure, no grading LUT") } };
		for (const auto& N : Need)
		{
			if (!Has(N.Key)) { P.Add(N.Value); }
		}
		if (!Has(TEXT("VolumetricCloud"))) { P.Add(TEXT("no Volumetric Cloud (ART_PIPELINE asks for cheap clouds on High+)")); }
		for (AActor* A : Actors)
		{
			if (APostProcessVolume* V = Cast<APostProcessVolume>(A))
			{
				if (!V->Settings.bOverride_AutoExposureMinBrightness && !V->Settings.bOverride_AutoExposureBias) { P.Add(V->GetActorLabel() + TEXT(": exposure not set (engine defaults)")); }
				if (!V->Settings.bOverride_ColorGradingLUT) { P.Add(V->GetActorLabel() + TEXT(": no colour-grading LUT")); }
			}
		}
		OutActors = Descs.Num() ? Descs.Num() : Actors.Num();
		return P;
	}

	/** Python's sorted() order for (asset, problems) and (folder, problems) tuples. */
	bool TupleLess(const FString& A, const FProblems& Pa, const FString& B, const FProblems& Pb)
	{
		const int32 C = A.Compare(B, ESearchCase::CaseSensitive);
		if (C != 0) { return C < 0; }
		for (int32 i = 0; i < FMath::Min(Pa.Num(), Pb.Num()); ++i)
		{
			const int32 D = Pa[i].Compare(Pb[i], ESearchCase::CaseSensitive);
			if (D != 0) { return D < 0; }
		}
		return Pa.Num() < Pb.Num();
	}
}

UOCAuditCommandlet::UOCAuditCommandlet() { InitTool(this); }

int32 UOCAuditCommandlet::Main(const FString& Params)
{
	TMap<FString, TArray<FFinding>> Findings;   // section -> findings
	TMap<FString, int32> Totals;
	auto Add = [&Findings](const TCHAR* Section, const FString& Asset, const FProblems& P)
	{
		if (!P.IsEmpty()) { Findings.FindOrAdd(Section).Add({ Asset, P }); }
	};
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	Registry.SearchAllAssets(true);
	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(TEXT("/Game"), Assets, true);
	Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.GetObjectPathString() < B.GetObjectPathString(); });
	TArray<FString> Maps;
	TArray<FFinding> Tiling;
	for (const FAssetData& D : Assets)
	{
		const FString Cls = D.AssetClassPath.GetAssetName().ToString();
		const FString Path = D.GetObjectPathString();
		if (Cls == TEXT("World"))
		{
			Maps.Add(Path);
			continue;
		}
		if (Cls != TEXT("StaticMesh") && Cls != TEXT("SkeletalMesh") && Cls != TEXT("Texture2D") && Cls != TEXT("Material") && Cls != TEXT("MaterialInstanceConstant"))
		{
			continue;
		}
		UObject* Asset = D.GetAsset();
		if (!Asset)
		{
			continue;
		}
		if (UStaticMesh* Sm = Cast<UStaticMesh>(Asset))
		{
			Totals.FindOrAdd(TEXT("meshes"))++;
			Add(TEXT("meshes"), Short(Path), CheckStaticMesh(Path, Sm));
		}
		else if (USkeletalMesh* Sk = Cast<USkeletalMesh>(Asset))
		{
			Totals.FindOrAdd(TEXT("meshes"))++;
			Add(TEXT("meshes"), Short(Path), CheckSkeletalMesh(Path, Sk));
		}
		else if (UTexture2D* T = Cast<UTexture2D>(Asset))
		{
			Totals.FindOrAdd(TEXT("textures"))++;
			const FProblems P = CheckTexture(Path, T);
			if (!P.ContainsByPredicate([](const FString& X) { return X.Contains(TEXT("prefix"), ESearchCase::CaseSensitive) || X.Contains(TEXT("suffix"), ESearchCase::CaseSensitive); }))
			{
				Totals.FindOrAdd(TEXT("named_textures"))++;
			}
			Add(TEXT("textures"), Short(Path), P);
		}
		else if (UMaterialInterface* M = Cast<UMaterialInterface>(Asset))
		{
			Totals.FindOrAdd(TEXT("materials"))++;
			if (Path.StartsWith(MasterPrefix, ESearchCase::CaseSensitive)) { Totals.FindOrAdd(TEXT("masters"))++; }
			if (Cls == TEXT("MaterialInstanceConstant")) { Totals.FindOrAdd(TEXT("instances"))++; }
			const FProblems P = CheckMaterial(Path, M, Tiling);
			if (!P.ContainsByPredicate([](const FString& X) { return X.Contains(TEXT("approved master"), ESearchCase::CaseSensitive); }) && !Path.StartsWith(MasterPrefix, ESearchCase::CaseSensitive))
			{
				Totals.FindOrAdd(TEXT("from_master"))++;
			}
			Add(TEXT("materials"), Short(Path), P);
		}
	}
	for (const FFinding& F : Tiling) { Add(TEXT("tiling"), F.Asset, F.Problems); }
	Totals.Add(TEXT("maps"), Maps.Num());
	for (const FString& Map : Maps)
	{
		int32 NumActors = 0;
		Add(TEXT("maps"), Short(Map), CheckMap(Map, NumActors));
	}

	// ---- AUDIT.md
	TArray<FString> Out = { TEXT("# OpenCity: world-building audit"), TEXT(""),
		FString::Printf(TEXT("Scanned %s by the `OCAudit` commandlet (headless). Sections 1-6 are generated; everything after them comes from `AUDIT_NOTES.md`."),
			*FDateTime::Now().ToString(TEXT("%Y-%m-%d"))), TEXT(""),
		TEXT("## 1. Summary"), TEXT(""), TEXT("| Assets | Count | With problems |"), TEXT("|---|---|---|") };
	const TPair<const TCHAR*, const TCHAR*> Rows[] = { { TEXT("meshes"), TEXT("Static and skeletal meshes") }, { TEXT("textures"), TEXT("Textures") },
		{ TEXT("materials"), TEXT("Materials and instances") }, { TEXT("maps"), TEXT("Maps") } };
	for (const auto& R : Rows)
	{
		const TArray<FFinding>* F = Findings.Find(R.Key);
		Out.Add(FString::Printf(TEXT("| %s | %d | %d |"), R.Value, Totals.FindRef(R.Key), F ? F->Num() : 0));
	}
	Out.Append({ TEXT(""), FString::Printf(TEXT("- Master materials in `/Game/Materials`: %d of 8."), Totals.FindRef(TEXT("masters"))),
		FString::Printf(TEXT("- Materials built from a master: %d (instances `MI_*`: %d)."), Totals.FindRef(TEXT("from_master")), Totals.FindRef(TEXT("instances"))),
		FString::Printf(TEXT("- Textures following the T_ naming with a role suffix: %d of %d."), Totals.FindRef(TEXT("named_textures")), Totals.FindRef(TEXT("textures"))),
		TEXT("") });
	const TPair<const TCHAR*, const TCHAR*> Sections[] = { { TEXT("meshes"), TEXT("Meshes") }, { TEXT("textures"), TEXT("Textures") },
		{ TEXT("materials"), TEXT("Materials") }, { TEXT("tiling"), TEXT("Visible-tiling risk") }, { TEXT("maps"), TEXT("Lighting and post-process (per map)") } };
	int32 Num = 2;
	for (const auto& S : Sections)
	{
		Out.Append({ FString::Printf(TEXT("## %d. %s"), Num++, S.Value), TEXT("") });
		TArray<FFinding> Items = Findings.FindRef(S.Key);
		if (Items.IsEmpty())
		{
			Out.Append({ TEXT("No problems found."), TEXT("") });
			continue;
		}
		// Five or more assets in one folder with the same problems collapse into one entry.
		Items.Sort([](const FFinding& A, const FFinding& B) { return TupleLess(A.Asset, A.Problems, B.Asset, B.Problems); });
		struct FGroup { FString Folder; FProblems Problems; TArray<FString> Assets; };
		TArray<FGroup> Groups;
		for (const FFinding& F : Items)
		{
			FString Folder, Rest;
			F.Asset.Split(TEXT("/"), &Folder, &Rest, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			FGroup* G = Groups.FindByPredicate([&](const FGroup& X) { return X.Folder == Folder && X.Problems == F.Problems; });
			if (!G) { G = &Groups.Add_GetRef({ Folder, F.Problems, {} }); }
			G->Assets.Add(F.Asset);
		}
		Groups.Sort([](const FGroup& A, const FGroup& B) { return TupleLess(A.Folder, A.Problems, B.Folder, B.Problems); });
		for (const FGroup& G : Groups)
		{
			TArray<FString> Heads;
			if (G.Assets.Num() >= 5) { Heads.Add(FString::Printf(TEXT("`%s/` (%d assets)"), *G.Folder, G.Assets.Num())); }
			else { for (const FString& A : G.Assets) { Heads.Add(FString::Printf(TEXT("`%s`"), *A)); } }
			for (const FString& H : Heads)
			{
				Out.Add(TEXT("- ") + H);
				for (const FString& Pr : G.Problems) { Out.Add(TEXT("  - ") + Pr); }
			}
		}
		Out.Add(TEXT(""));
	}
	FString Notes;
	if (FFileHelper::LoadFileToString(Notes, *(OCEd::ProjectDir() / TEXT("AUDIT_NOTES.md"))))
	{
		Out.Append({ TEXT("---"), TEXT(""), Notes });
	}
	const FString AuditPath = OCEd::ProjectDir() / TEXT("AUDIT.md");
	FFileHelper::SaveStringToFile(FString::Join(Out, TEXT("\n")) + TEXT("\n"), *AuditPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	int32 N = 0;
	for (const TPair<FString, TArray<FFinding>>& F : Findings) { N += F.Value.Num(); }
	OCEd::Log(TEXT("oc_audit"), FString::Printf(TEXT("wrote %s: %d assets with problems"), *AuditPath, N));
	return 0;
}
