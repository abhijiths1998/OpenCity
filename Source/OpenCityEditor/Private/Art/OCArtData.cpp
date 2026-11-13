// Weather presets, physical materials and material statistics (formerly OCArtData.cpp (weather),
// OCArtData.cpp (physical materials) and OCBuildArt -steps=stats).
#include "Art/OCArt.h"

#include "Factories/PhysicalMaterialFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "OCEditorHelpers.h"
#include "OCLog.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "World/OCWeatherPreset.h"

namespace OCArt
{
	void BuildWeather()
	{
		// Multipliers on the map's lighting (sun, sky light, fog), the sky atmosphere's Rayleigh/Mie scales
		// (grey, hazy skies), RainAmount and the wetness the weather drives toward. Materials read RainAmount
		// and Wetness from MPC_World.
		struct FPreset { const TCHAR* Name; float Sun, Sky, Fog, Rayleigh, Mie, Rain, Wet, Exposure, Wind; };
		const FPreset Presets[] = {
			{ TEXT("Clear"), 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.25f },
			// Overcast: the sun barely casts shadows, the sky light carries the scene, the sky goes grey.
			{ TEXT("Cloudy"), 0.15f, 1.15f, 1.8f, 0.4f, 4.0f, 0.0f, 0.0f, 0.0f, 0.55f },
			// The sky light captures the sky in real time, and the sky is lit by the (dimmed) sun, so its scale has
			// to rise as the sun falls; ExposureBias (EV, day only) stops the bright fog at the end of a street from
			// exposing the shaded walls down to black (QA 2026-11-09).
			{ TEXT("Rain"), 0.04f, 4.5f, 3.5f, 0.3f, 5.0f, 1.0f, 1.0f, 1.0f, 0.9f } };
		for (const FPreset& P : Presets)
		{
			UObject* Da = OCEd::GetOrCreateDataAsset(FString(TEXT("DA_Weather_")) + P.Name, TEXT("/Game/Core/Weather"), UOCWeatherPreset::StaticClass(), TEXT("oc_weather"));
			const TPair<const TCHAR*, float> Props[] = { { TEXT("SunScale"), P.Sun }, { TEXT("SkyLightScale"), P.Sky }, { TEXT("FogScale"), P.Fog },
				{ TEXT("RayleighScale"), P.Rayleigh }, { TEXT("MieScale"), P.Mie }, { TEXT("RainAmount"), P.Rain }, { TEXT("WetnessTarget"), P.Wet },
				{ TEXT("ExposureBias"), P.Exposure }, { TEXT("WindStrength"), P.Wind } };
			for (const auto& Kv : Props)
			{
				OCEd::SetProp(Da, Kv.Key, FString::SanitizeFloat(Kv.Value));
			}
			OCEd::Save(Da);
			OCEd::Log(TEXT("oc_weather"), FString::Printf(TEXT("DA_Weather_%s: sun %.2f, sky %.2f, fog %.2f, rain %.2f"), P.Name, P.Sun, P.Sky, P.Fog, P.Rain));
		}
	}

	void BuildPhysicalMaterials()
	{
		// Friction and restitution per surface (INFRA_DESIGN.md Phase 4). Chaos vehicle tyres scale their grip by
		// the surface's friction; footstep and tyre audio (M10) and wet grip build on the same assets. Values are
		// game tuning (dry): asphalt is the reference grip.
		struct FSurface { const TCHAR* Name; float Friction, Restitution; };
		const FSurface Surfaces[] = {
			{ TEXT("Asphalt"), 1.0f, 0.1f },
			{ TEXT("Cobbles"), 0.85f, 0.1f },      // sampietrini, porfido: rougher ride, a little less grip
			{ TEXT("StoneSlab"), 0.78f, 0.1f },    // lastricato: polished by centuries of feet
			{ TEXT("Gravel"), 0.62f, 0.05f },      // strade bianche
			{ TEXT("Concrete"), 0.95f, 0.1f },     // quays
			{ TEXT("Dirt"), 0.6f, 0.05f },
			{ TEXT("Sand"), 0.45f, 0.02f },
			{ TEXT("MetalGrate"), 0.7f, 0.2f },
			{ TEXT("TramRail"), 0.55f, 0.2f } };   // slippery for bikes
		const TCHAR* PhysTag = TEXT("oc_physmat");
		TMap<FString, UPhysicalMaterial*> Made;
		for (const FSurface& S : Surfaces)
		{
			UPhysicalMaterial* Pm = CastChecked<UPhysicalMaterial>(OCEd::GetOrCreate(FString(TEXT("PM_")) + S.Name, TEXT("/Game/Core/Physics"),
				UPhysicalMaterial::StaticClass(), NewObject<UPhysicalMaterialFactoryNew>(), PhysTag));
			Pm->Modify();
			Pm->Friction = S.Friction;
			Pm->StaticFriction = S.Friction;
			Pm->Restitution = S.Restitution;
			Pm->PostEditChange();
			OCEd::Save(Pm);
			Made.Add(S.Name, Pm);
		}
		const TPair<const TCHAR*, const TCHAR*> Paving[] = { { TEXT("asphalt"), TEXT("Asphalt") }, { TEXT("sampietrini"), TEXT("Cobbles") },
			{ TEXT("porfido"), TEXT("Cobbles") }, { TEXT("lastricato"), TEXT("StoneSlab") }, { TEXT("stone_slab"), TEXT("StoneSlab") },
			{ TEXT("gravel_white"), TEXT("Gravel") }, { TEXT("concrete"), TEXT("Concrete") } };
		TArray<FString> Assigned;
		for (const auto& P : Paving)
		{
			UMaterialInstanceConstant* Mi = OCEd::Load<UMaterialInstanceConstant>(FString(TEXT("/Game/Environment/Materials/Paving/MI_Paving_")) + P.Key);
			if (!Mi)
			{
				continue;
			}
			Mi->Modify();
			Mi->PhysMaterial = Made.FindChecked(P.Value);
			Mi->PostEditChange();
			OCEd::Save(Mi);
			Assigned.Add(FString::Printf(TEXT("%s->%s"), P.Key, P.Value));
		}
		OCEd::Log(PhysTag, FString::Printf(TEXT("physical materials: %d; paving: %s"), Made.Num(), *FString::Join(Assigned, TEXT(", "))));
	}

	void MaterialStats()
	{
		// Base-pass pixel shader targets (ART_PIPELINE.md, "Master materials", raised 2026-11-09). 0 = no budget.
		const TCHAR* StatTag = TEXT("oc_matstats");
		const TPair<const TCHAR*, int32> Budgets[] = {
			{ TEXT("M_Master_Surface"), 450 }, { TEXT("M_Master_SurfaceTwoSided"), 450 }, { TEXT("M_Master_Road"), 550 },
			{ TEXT("M_Master_Landscape"), 600 }, { TEXT("M_Master_Water"), 440 }, { TEXT("M_Master_KitGlass"), 400 },
			{ TEXT("M_Master_CarGlass"), 400 }, { TEXT("M_Master_CarPaint"), 450 }, { TEXT("M_Master_Foliage"), 400 },
			{ TEXT("M_Master_Character"), 450 }, { TEXT("M_Master_LampGlow"), 0 }, { TEXT("M_Master_Paint"), 350 },
			{ TEXT("M_Master_Decal"), 400 }, { TEXT("M_PP_Rain"), 0 } };
		TArray<FString> Over;
		for (const auto& B : Budgets)
		{
			UMaterial* M = OCEd::Load<UMaterial>(FString(TEXT("/Game/Materials/")) + B.Key);
			if (!M)
			{
				OCEd::Log(StatTag, FString::Printf(TEXT("MATSTAT %s: missing"), B.Key));
				continue;
			}
			UMaterialEditingLibrary::RecompileMaterial(M);
			const FMaterialStatistics St = UMaterialEditingLibrary::GetStatistics(M);
			OCEd::Log(StatTag, FString::Printf(TEXT("MATSTAT %s: ps %d vs %d samplers %d budget %s"), B.Key, St.NumPixelShaderInstructions,
				St.NumVertexShaderInstructions, St.NumSamplers, B.Value ? *FString::FromInt(B.Value) : TEXT("-")));
			if (B.Value && St.NumPixelShaderInstructions > B.Value)
			{
				Over.Add(FString::Printf(TEXT("%s %d/%d"), B.Key, St.NumPixelShaderInstructions, B.Value));
			}
		}
		// Static switches make one shader per instance permutation: report the ones that cover the city.
		const TPair<const TCHAR*, int32> InstanceBudgets[] = { { TEXT("MI_Kit_Wall"), 450 }, { TEXT("MI_Kit_Roof"), 450 }, { TEXT("MI_Sidewalk"), 550 }, { TEXT("MI_Box_Stone"), 550 },
			{ TEXT("MI_Kit_PaintRed"), 350 }, { TEXT("MI_Kit_Paint"), 350 } };   // retroreflective sign paint, bins by instance colour
		for (const auto& B : InstanceBudgets)
		{
			if (UMaterialInterface* Mi = OCEd::Load<UMaterialInterface>(FString(TEXT("/Game/Environment/Materials/")) + B.Key))
			{
				const int32 Ps = UMaterialEditingLibrary::GetStatistics(Mi).NumPixelShaderInstructions;
				OCEd::Log(StatTag, FString::Printf(TEXT("MATSTAT   %s: ps %d budget %d"), B.Key, Ps, B.Value));
				if (Ps > B.Value)
				{
					Over.Add(FString::Printf(TEXT("%s %d/%d"), B.Key, Ps, B.Value));
				}
			}
		}
		OCEd::Log(StatTag, TEXT("MATSTAT over budget: ") + (Over.IsEmpty() ? FString(TEXT("none")) : FString::Join(Over, TEXT(", "))));
	}
}
