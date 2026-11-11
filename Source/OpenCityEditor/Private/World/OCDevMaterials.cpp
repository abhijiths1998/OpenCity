// Grey-box debug materials, generated so they can be rebuilt from clean (the world build falls back to
// them when the final-art masters are missing). Formerly OCDevMaterials.cpp.
//   M_Dev_Terrain   colour by height (sand, green hills, grey rock) with soft variation
//   M_Dev_Road      asphalt with lane markings from the road meshes' UVs
//   M_Dev_Sidewalk  60 cm paving slabs
//   M_Dev_Building  instanced grey-box buildings by style
//   M_Dev_Water     opaque grey-box water, and flat colours for landmarks, gates and props
#include "OCEditorHelpers.h"
#include "World/OCWorldBuild.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"

namespace OCWorldBuild
{
	namespace
	{
		const TCHAR* Tag = TEXT("oc_dev");
		const TCHAR* Folder = TEXT("/Game/Dev/Materials");

		// Hash and smooth value noise for Custom nodes (no textures in grey-box), float-safe at world scale.
		const FString HlslNoise = TEXT("\n#define OC_HASH(c) frac((frac((c).x * 0.1031 + (c).y * 0.11369) + 33.33) * frac((c).y * 0.13787 + (c).x * 0.0973) * 43.7585)\n");

		FString ValueNoise(const FString& Var, const FString& P)
		{
			return FString::Printf(TEXT("\nfloat2 %s_i = floor(%s); float2 %s_f = frac(%s); %s_f = %s_f * %s_f * (3.0 - 2.0 * %s_f);\n"
				"float %s = lerp(lerp(OC_HASH(%s_i), OC_HASH(%s_i + float2(1, 0)), %s_f.x),\n"
				"                   lerp(OC_HASH(%s_i + float2(0, 1)), OC_HASH(%s_i + float2(1, 1)), %s_f.x), %s_f.y);\n"),
				*Var, *P, *Var, *P, *Var, *Var, *Var, *Var, *Var, *Var, *Var, *Var, *Var, *Var, *Var, *Var);
		}

		UMaterialExpression* Ramp(OCEd::FMat& Mt, UMaterialExpression* Z, float Start, float Span, int32 X, int32 Y)
		{
			// saturate((z - start) / span)
			UMaterialExpression* Sub = Mt.Expr(UMaterialExpressionSubtract::StaticClass(), X, Y);
			Mt.Connect(Z, TEXT(""), Sub, TEXT("A"));
			Mt.Connect(Mt.Const(Start, X - 150, Y + 60), TEXT(""), Sub, TEXT("B"));
			UMaterialExpression* Div = Mt.Expr(UMaterialExpressionDivide::StaticClass(), X + 150, Y);
			Mt.Connect(Sub, TEXT(""), Div, TEXT("A"));
			Mt.Connect(Mt.Const(Span, X, Y + 120), TEXT(""), Div, TEXT("B"));
			UMaterialExpression* Sat = Mt.Expr(UMaterialExpressionSaturate::StaticClass(), X + 300, Y);
			Mt.Connect(Div, TEXT(""), Sat, TEXT(""));
			return Sat;
		}

		void MakeTerrain()
		{
			OCEd::FMat Mt = OCEd::FMat::Fresh(TEXT("M_Dev_Terrain"), Folder, Tag);
			UMaterialExpression* Wp = Mt.WorldPos(-1200, 0);
			UMaterialExpressionComponentMask* Z = Cast<UMaterialExpressionComponentMask>(Mt.Expr(UMaterialExpressionComponentMask::StaticClass(), -1000, 0));
			Z->R = false; Z->G = false; Z->B = true; Z->A = false;
			Mt.Connect(Wp, TEXT(""), Z, TEXT(""));
			UMaterialExpression* Shore = Ramp(Mt, Z, 30.0f, 120.0f, -800, -200);       // sand below ~0.3 m, green by 1.5 m
			UMaterialExpression* High = Ramp(Mt, Z, 15000.0f, 15000.0f, -800, 200);    // green to rock from 150 m to 300 m
			UMaterialExpression* L1 = Mt.Expr(UMaterialExpressionLinearInterpolate::StaticClass(), -200, -100);
			Mt.Connect(Mt.Color(FLinearColor(0.62f, 0.55f, 0.40f), -400, -300), TEXT(""), L1, TEXT("A"));
			Mt.Connect(Mt.Color(FLinearColor(0.30f, 0.38f, 0.18f), -400, -150), TEXT(""), L1, TEXT("B"));
			Mt.Connect(Shore, TEXT(""), L1, TEXT("Alpha"));
			UMaterialExpression* L2 = Mt.Expr(UMaterialExpressionLinearInterpolate::StaticClass(), 0, 0);
			Mt.Connect(L1, TEXT(""), L2, TEXT("A"));
			Mt.Connect(Mt.Color(FLinearColor(0.42f, 0.40f, 0.38f), -400, 100), TEXT(""), L2, TEXT("B"));
			Mt.Connect(High, TEXT(""), L2, TEXT("Alpha"));
			// Break up the flat colours: broad patches (40 m) and fine mottling (3 m).
			UMaterialExpressionCustom* Vary = Mt.Custom(HlslNoise + ValueNoise(TEXT("big"), TEXT("WP.xy / 4000.0")) + ValueNoise(TEXT("fine"), TEXT("WP.xy / 300.0"))
				+ TEXT("return Col * (0.78 + 0.32 * big) * (0.92 + 0.14 * fine);"), { { TEXT("Col"), L2 }, { TEXT("WP"), Wp } }, 200, 0);
			Mt.Output(Vary, TEXT(""), MP_BaseColor);
			Mt.Output(Mt.Const(0.9f, 0, 200), TEXT(""), MP_Roughness);
			Mt.Finish(Tag);
		}

		void Prepare(OCEd::FMat& Mt, bool bTwoSided)
		{
			Mt.M->TwoSided = bTwoSided;
			OCEd::Usage(Mt.M, MATUSAGE_InstancedStaticMeshes);   // instanced HLODs
			OCEd::Usage(Mt.M, MATUSAGE_Nanite);                  // road chunks are Nanite
		}

		void MakeFlat(const TCHAR* Name, const FLinearColor& Rgb, float Roughness)
		{
			OCEd::FMat Mt = OCEd::FMat::Fresh(Name, Folder, Tag);
			Prepare(Mt, true);
			Mt.Output(Mt.Color(Rgb, -300, 0), TEXT(""), MP_BaseColor);
			Mt.Output(Mt.Const(Roughness, -300, 150), TEXT(""), MP_Roughness);
			Mt.Finish(Tag);
		}

		void MakeCustomSurface(const TCHAR* Name, const FString& Code, TFunctionRef<TArray<TPair<FString, UMaterialExpression*>>(OCEd::FMat&)> Inputs, float Roughness)
		{
			OCEd::FMat Mt = OCEd::FMat::Fresh(Name, Folder, Tag);
			Prepare(Mt, true);
			UMaterialExpressionCustom* Node = Mt.Custom(Code, Inputs(Mt), -300, 0);
			Mt.Output(Node, TEXT(""), MP_BaseColor);
			Mt.Output(Mt.Const(Roughness, -300, 200), TEXT(""), MP_Roughness);
			Mt.Finish(Tag);
		}

		const TCHAR* RoadHlsl = TEXT(R"(
float3 asphalt = float3(0.055, 0.055, 0.058) * (0.85 + 0.3 * patch) * (0.9 + 0.2 * grain);
float hw = Road1.x;            // half width, m (0 on skirts)
float lanes = Road1.y;         // marked lanes per direction (0 = no markings)
float3 surf = asphalt;
if (lanes < 0.5 && hw > 0.01 && hw < 3.5)
{
    // Alleys: grey stone setts, 25 cm, rows offset by half a stone.
    float2 q = WP.xy / 25.0;
    q.x += 0.5 * fmod(floor(q.y), 2.0);
    float2 g = frac(q);
    float joint = max(step(g.x, 0.08), step(g.y, 0.08));
    float3 stone = float3(0.20, 0.19, 0.175) * (0.8 + 0.4 * OC_HASH(floor(q))) * (0.9 + 0.2 * patch);
    surf = lerp(stone, stone * 0.45, joint);
}
else if (lanes < 0.5 && hw > 4.9)
{
    // Piers: 3 m concrete slabs.
    float2 g = frac(WP.xy / 300.0);
    float joint = max(step(g.x, 0.008), step(g.y, 0.008));
    float3 conc = float3(0.33, 0.32, 0.30) * (0.88 + 0.2 * OC_HASH(floor(WP.xy / 300.0))) * (0.9 + 0.15 * grain);
    surf = lerp(conc, conc * 0.6, joint);
}
float au = abs(Road0.x);       // distance from the centre line, m
float mk = 0.0;
if (lanes > 0.5)
{
    float dash = step(frac(Road0.y / 12.0), 0.375);         // 4.5 m line, 7.5 m gap
    mk += step(hw - 0.55, au) * step(au, hw - 0.40);         // solid edge lines
    if (lanes > 1.5)
    {
        mk += step(0.06, au) * step(au, 0.18);               // double centre line
        float laneW = (hw - 0.5) / lanes;
        mk += step(abs(au - laneW), 0.07) * dash;            // dashed lane dividers
    }
    else
    {
        mk += step(au, 0.07) * dash;                         // dashed centre line
    }
}
return lerp(surf, float3(0.70, 0.69, 0.64), saturate(mk));
)");

		const TCHAR* SidewalkHlsl = TEXT(R"(
float2 cell = UV.xy / 0.6;                                   // 60 cm slabs
float2 g = frac(cell);
float joint = max(step(g.x, 0.04), step(g.y, 0.04));
float n = OC_HASH(floor(cell));
float3 stone = float3(0.50, 0.47, 0.42) * (0.9 + 0.16 * n);
return lerp(stone, stone * 0.62, joint);
)");

		const TCHAR* BuildingHlsl = TEXT(R"(
// Grey-box buildings (instanced cubes). Style = building_styles index from the layout:
// 0 oldtown, 1 grid90, 2 grid120, 3 beach, 4 village, 5 terminal, 6 hills. Rnd = 0-1 per building.
float3 n = normalize(N);
float s = round(Style);
float3 pal[6] = { float3(0.80, 0.73, 0.60), float3(0.79, 0.58, 0.33), float3(0.72, 0.42, 0.30),
                  float3(0.83, 0.67, 0.60), float3(0.86, 0.85, 0.80), float3(0.86, 0.78, 0.52) };
float3 wall = pal[min((int)floor(Rnd * 6.0), 5)];
bool industrial = (s == 2.0 || s == 5.0);
if (industrial)
{
    wall = lerp(float3(0.46, 0.48, 0.49), float3(0.58, 0.52, 0.44), Rnd);
}
if (n.z > 0.6)
{
    bool tiled = (s == 0.0 || s == 4.0 || s == 6.0);
    return tiled ? float3(0.55, 0.27, 0.18) * (0.85 + 0.3 * Rnd) : float3(0.42, 0.41, 0.39);
}
float u = dot(WP.xy, float2(-n.y, n.x)) / 100.0;    // metres along the wall
float v = WP.z / 100.0;
float storey = industrial ? 6.0 : 3.2;
float win = step(abs(frac(u / 3.0) - 0.5), industrial ? 0.42 : 0.19) * step(abs(frac(v / storey) - 0.6), industrial ? 0.08 : 0.22);
float shutter = industrial ? 0.0 : step(abs(abs(frac(u / 3.0) - 0.5) - 0.25), 0.05) * step(abs(frac(v / storey) - 0.6), 0.22);
float3 shut = (Rnd > 0.5) ? float3(0.20, 0.33, 0.24) : float3(0.30, 0.38, 0.45);
float3 c = lerp(wall, float3(0.06, 0.07, 0.08), win);
return lerp(c, shut, shutter);
)");

		const TCHAR* StripesHlsl = TEXT(R"(
float t = frac((WP.x + WP.y + WP.z) / 100.0);
return t < 0.5 ? float3(0.75, 0.08, 0.06) : float3(0.85, 0.85, 0.82);
)");

		void MakeBuilding()
		{
			OCEd::FMat Mt = OCEd::FMat::Fresh(TEXT("M_Dev_Building"), Folder, Tag);
			OCEd::Usage(Mt.M, MATUSAGE_InstancedStaticMeshes);
			UMaterialExpression* Style = Mt.InstanceData(0, 0.0f, -700, 100);
			UMaterialExpression* Rnd = Mt.InstanceData(1, 0.0f, -700, 250);
			UMaterialExpression* Normal = Mt.Expr(UMaterialExpressionVertexNormalWS::StaticClass(), -700, -100);
			UMaterialExpressionCustom* Node = Mt.Custom(HlslNoise + BuildingHlsl,
				{ { TEXT("WP"), Mt.WorldPos(-700, -250) }, { TEXT("N"), Normal }, { TEXT("Style"), Style }, { TEXT("Rnd"), Rnd } }, -300, 0);
			Mt.Output(Node, TEXT(""), MP_BaseColor);
			Mt.Output(Mt.Const(0.85f, -300, 200), TEXT(""), MP_Roughness);
			Mt.Finish(Tag);
		}
	}

	void BuildDevMaterials()
	{
		MakeTerrain();
		MakeBuilding();
		MakeCustomSurface(TEXT("M_Dev_Barrier"), StripesHlsl, [](OCEd::FMat& M) { return TArray<TPair<FString, UMaterialExpression*>>{ { TEXT("WP"), M.WorldPos(-700, 0) } }; }, 0.6f);
		MakeFlat(TEXT("M_Dev_Landmark"), FLinearColor(0.78f, 0.74f, 0.66f), 0.8f);        // pale stone: story landmarks stand out
		MakeFlat(TEXT("M_Dev_LandmarkAccent"), FLinearColor(0.55f, 0.26f, 0.17f), 0.7f);  // terracotta roofs, domes
		MakeFlat(TEXT("M_Dev_Burnt"), FLinearColor(0.07f, 0.06f, 0.055f), 0.95f);        // La Cala, the burned house
		MakeFlat(TEXT("M_Dev_Metal"), FLinearColor(0.30f, 0.32f, 0.34f), 0.5f);          // masts, cranes, rails
		MakeFlat(TEXT("M_Dev_Glass"), FLinearColor(0.25f, 0.42f, 0.55f), 0.2f);          // lantern, pool, windows on landmarks
		MakeFlat(TEXT("M_Dev_Police"), FLinearColor(0.10f, 0.18f, 0.45f), 0.5f);         // cordon cars
		MakeFlat(TEXT("M_Dev_Rock"), FLinearColor(0.20f, 0.17f, 0.14f), 0.95f);          // rockfall
		MakeFlat(TEXT("M_Dev_Train"), FLinearColor(0.38f, 0.14f, 0.10f), 0.7f);          // parked freight wagons
		MakeFlat(TEXT("M_Dev_Concrete"), FLinearColor(0.58f, 0.57f, 0.54f), 0.9f);       // the dam
		MakeCustomSurface(TEXT("M_Dev_Road"), HlslNoise + ValueNoise(TEXT("patch"), TEXT("WP.xy / 350.0")) + ValueNoise(TEXT("grain"), TEXT("WP.xy / 12.0")) + RoadHlsl,
			[](OCEd::FMat& M) { return TArray<TPair<FString, UMaterialExpression*>>{ { TEXT("Road0"), M.UV(0, -700, -100) }, { TEXT("Road1"), M.UV(1, -700, 50) }, { TEXT("WP"), M.WorldPos(-700, 200) } }; }, 0.8f);
		MakeCustomSurface(TEXT("M_Dev_Sidewalk"), HlslNoise + SidewalkHlsl, [](OCEd::FMat& M) { return TArray<TPair<FString, UMaterialExpression*>>{ { TEXT("UV"), M.UV(0, -700, 0) } }; }, 0.85f);
		MakeFlat(TEXT("M_Dev_Bridge"), FLinearColor(0.45f, 0.44f, 0.42f), 0.8f);
		// Grey-box water: deep blue-green with slow broad patches; low roughness, so it reflects the sky.
		MakeCustomSurface(TEXT("M_Dev_Water"), HlslNoise + ValueNoise(TEXT("big"), TEXT("WP.xy / 9000.0")) + ValueNoise(TEXT("ripple"), TEXT("WP.xy / 700.0"))
			+ TEXT("\nfloat3 deep = float3(0.010, 0.045, 0.060);\nfloat3 teal = float3(0.020, 0.085, 0.090);\nreturn lerp(deep, teal, 0.6 * big + 0.25 * ripple);\n"),
			[](OCEd::FMat& M) { return TArray<TPair<FString, UMaterialExpression*>>{ { TEXT("WP"), M.WorldPos(-700, 0) } }; }, 0.12f);
	}
}
