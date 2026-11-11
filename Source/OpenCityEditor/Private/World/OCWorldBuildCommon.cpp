// Shared pieces of the world build: layout access, assets, shapes, the post-process look and the
// network data assets (formerly oc_look.py and oc_networks.py).
#include "World/OCWorldBuild.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"
#include "OCEditorHelpers.h"
#include "OCLog.h"
#include "World/OCInfraNetworkAssets.h"
#include "World/OCRoadGraphAsset.h"

namespace OCWorldBuild
{
	namespace
	{
		FJsonPtr Need(const FJsonPtr& J, const TCHAR* Key)
		{
			FJsonPtr V = J.IsValid() ? J->Get(Key) : nullptr;
			if (!V.IsValid())
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("layout: missing \"%s\""), Key);
			}
			return V;
		}

		TMap<FString, UObject*> Cache;
	}

	double Num(const FJsonPtr& J, const TCHAR* Key) { return Need(J, Key)->AsNumber(); }
	int32 Int(const FJsonPtr& J, const TCHAR* Key) { return int32(Need(J, Key)->AsNumber()); }
	FString Str(const FJsonPtr& J, const TCHAR* Key) { return Need(J, Key)->Str; }
	const TArray<FJsonPtr>& Arr(const FJsonPtr& J, const TCHAR* Key) { return Need(J, Key)->Items; }
	double At(const FJsonPtr& J, int32 I) { return J->Items[I]->AsNumber(); }

	bool Truthy(const FJsonPtr& J, const TCHAR* Key)
	{
		const FJsonPtr V = J->Get(Key);
		if (!V.IsValid()) { return false; }
		switch (V->Kind)
		{
		case OCLayout::FJson::EKind::Null: return false;
		case OCLayout::FJson::EKind::Bool: return V->Bool;
		case OCLayout::FJson::EKind::Int: return V->Int != 0;
		case OCLayout::FJson::EKind::Float: return V->Num != 0.0;
		case OCLayout::FJson::EKind::String: return !V->Str.IsEmpty();
		default: return !V->Items.IsEmpty();
		}
	}

	double RiverHalfWidth(double Y)
	{
		if (Y < 260.0)
		{
			return 25.0 + (260.0 - Y) * 0.45;
		}
		return Y < 2100.0 ? 25.0 : 14.0;
	}

	UObject* Asset(const FString& Path)
	{
		if (UObject** Found = Cache.Find(Path))
		{
			return *Found;
		}
		UObject* O = OCEd::Require(Path);
		Cache.Add(Path, O);
		return O;
	}

	UMaterialInterface* Art(const FString& Path, const FString& Fallback)
	{
		if (UMaterialInterface* M = OCEd::Load<UMaterialInterface>(Path))
		{
			return M;
		}
		return Cast<UMaterialInterface>(Asset(Fallback));
	}

	UMaterialInterface* BoxMaterial(const FString& Name)
	{
		// Grey-box material -> box-projected final-art instance (oc_art_materials), when built.
		static const TMap<FString, FString> ArtNames = {
			{ TEXT("Landmark"), TEXT("MI_Box_Stucco") }, { TEXT("LandmarkAccent"), TEXT("MI_Box_Roof") },
			{ TEXT("Concrete"), TEXT("MI_Box_Concrete") }, { TEXT("Bridge"), TEXT("MI_Box_Stone") },
			{ TEXT("Metal"), TEXT("MI_Box_Metal") }, { TEXT("Rock"), TEXT("MI_Box_Rock") },
			{ TEXT("Terrain"), TEXT("MI_Box_Rock") }, { TEXT("Hill"), TEXT("MI_Box_Hill") }, { TEXT("Water"), TEXT("MI_Water") } };
		const FString Dev = TEXT("/Game/Dev/Materials/M_Dev_") + Name;
		if (const FString* ArtName = ArtNames.Find(Name))
		{
			return Art(TEXT("/Game/Environment/Materials/") + *ArtName, Dev);
		}
		return Cast<UMaterialInterface>(Asset(Dev));
	}

	AStaticMeshActor* Shape(const FString& Label, const FString& Folder, const FVector& CenterM, const FVector& SizeM,
		UMaterialInterface* Material, const TCHAR* Mesh, const FRotator& Rot)
	{
		AStaticMeshActor* A = CastChecked<AStaticMeshActor>(OCEd::Spawn(AStaticMeshActor::StaticClass(), Cm(CenterM.X, CenterM.Y, CenterM.Z), Rot, Label, Folder));
		UStaticMeshComponent* C = A->GetStaticMeshComponent();
		C->SetStaticMesh(Cast<UStaticMesh>(Asset(Mesh)));
		C->SetMaterial(0, Material);
		A->SetActorScale3D(SizeM);
		return A;
	}

	void ApplyLook(FPostProcessSettings& S)
	{
		// Local exposure (M4b): with one exposure for the frame, a low sun's bright facades crushed shaded
		// streets to black at golden hour. Local exposure lowers contrast per region: shadows lift, highlights hold.
		S.bOverride_AutoExposureMinBrightness = true;            S.AutoExposureMinBrightness = -1.0f;
		S.bOverride_AutoExposureMaxBrightness = true;            S.AutoExposureMaxBrightness = 3.0f;
		S.bOverride_AutoExposureBias = true;                     S.AutoExposureBias = 0.3f;
		S.bOverride_LocalExposureShadowContrastScale = true;     S.LocalExposureShadowContrastScale = 0.6f;
		S.bOverride_LocalExposureHighlightContrastScale = true;  S.LocalExposureHighlightContrastScale = 0.8f;
		S.bOverride_MotionBlurAmount = true;                     S.MotionBlurAmount = 0.15f;
		S.bOverride_MotionBlurMax = true;                        S.MotionBlurMax = 2.0f;
		S.bOverride_BloomIntensity = true;                       S.BloomIntensity = 0.35f;
		S.bOverride_VignetteIntensity = true;                    S.VignetteIntensity = 0.25f;
		S.bOverride_WhiteTemp = true;                            S.WhiteTemp = 6200.0f;
		if (UTexture* Lut = OCEd::Load<UTexture>(TEXT("/Game/Environment/Textures/LUT/T_LUT_Marevia")))
		{
			// ART_BIBLE grade: warm highlights, cool shadows.
			S.bOverride_ColorGradingLUT = true;
			S.ColorGradingLUT = Lut;
			S.bOverride_ColorGradingIntensity = true;
			S.ColorGradingIntensity = 1.0f;
		}
		if (UObject* Rain = OCEd::LoadAsset(TEXT("/Game/Materials/M_PP_Rain")))
		{
			// Rain streaks and dimming, scaled by MPC_World.RainAmount (free when it isn't raining).
			S.WeightedBlendables.Array.Reset();
			S.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, Rain));
		}
	}

	void WriteNetworkAssets(const FJsonPtr& Layout)
	{
		const TCHAR* Folder = TEXT("/Game/World");
		const TCHAR* NetTag = TEXT("oc_networks");
		auto Wrap = [&Layout](const TCHAR* Key, const TCHAR* Empty)
		{
			FJsonPtr V = Layout->Get(Key);
			if (!V.IsValid())
			{
				V = OCLayout::FJson::MakeObject();
				V->Set(Empty, OCLayout::FJson::MakeArray());
			}
			FJsonPtr O = OCLayout::FJson::MakeObject();
			O->Set(Key, V);
			return O;
		};
		UOCRoadGraphAsset* Roads = CastChecked<UOCRoadGraphAsset>(OCEd::GetOrCreateDataAsset(TEXT("DA_MareviaRoads"), Folder, UOCRoadGraphAsset::StaticClass(), NetTag));
		Roads->LayoutJson = Wrap(TEXT("roads"), TEXT("nodes"))->WriteCompact();
		const FJsonPtr Start = Layout->Get(TEXT("start"));
		const FJsonPtr Pos = Start->Get(TEXT("pos"));
		Roads->StartLocation = Cm(At(Pos, 0), At(Pos, 1), Num(Start, TEXT("ground_z")));
		Roads->MarkPackageDirty();
		OCEd::Save(Roads);
		UOCRailNetworkAsset* Rail = CastChecked<UOCRailNetworkAsset>(OCEd::GetOrCreateDataAsset(TEXT("DA_MareviaRail"), Folder, UOCRailNetworkAsset::StaticClass(), NetTag));
		const FJsonPtr RailJson = Wrap(TEXT("rail"), TEXT("lines"));
		Rail->LayoutJson = RailJson->WriteCompact();
		Rail->MarkPackageDirty();
		OCEd::Save(Rail);
		UOCWaterNetworkAsset* Water = CastChecked<UOCWaterNetworkAsset>(OCEd::GetOrCreateDataAsset(TEXT("DA_MareviaWater"), Folder, UOCWaterNetworkAsset::StaticClass(), NetTag));
		const FJsonPtr WaterJson = Wrap(TEXT("water_network"), TEXT("bodies"));
		Water->LayoutJson = WaterJson->WriteCompact();
		Water->MarkPackageDirty();
		OCEd::Save(Water);
		OCEd::Log(NetTag, FString::Printf(TEXT("network assets: %d road segments, %d rail lines, %d water bodies"),
			Arr(Layout->Get(TEXT("roads")), TEXT("segments")).Num(),
			RailJson->Get(TEXT("rail"))->Get(TEXT("lines")).IsValid() ? RailJson->Get(TEXT("rail"))->Get(TEXT("lines"))->Items.Num() : 0,
			WaterJson->Get(TEXT("water_network"))->Get(TEXT("bodies")).IsValid() ? WaterJson->Get(TEXT("water_network"))->Get(TEXT("bodies"))->Items.Num() : 0));
	}
}
