// HLSL for the Custom nodes of the art masters (OCArtMaterials.cpp, OCArtDecals.cpp). Moved verbatim
// from the former Content/Python/oc_art_materials.py and oc_decals.py. Included once, inside
// namespace OCArt::Hlsl.
#pragma once

const FString DECAL_HLSL = FString(R"HLSL(
float4 c = Texture2DSample(BC, BCSampler, UV);
float lum = dot(c.rgb, float3(0.3, 0.59, 0.11));
float op = FromColor > 0.5 ? saturate((ColorKey - lum) * ColorGain) : Texture2DSample(OP, OPSampler, UV).r;
Opacity = op * Strength;
float2 nxy = Texture2DSample(NM, NMSampler, UV).rg * 2.0 - 1.0;
Normal = float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
Rough = RoughOverride >= 0.0 ? RoughOverride : Texture2DSample(RO, ROSampler, UV).r;
// Rain (MPC_World.Wetness): decals darken and gloss with the ground under them, or they read dry on a wet road.
Rough = lerp(Rough, 0.12, Wet * 0.8);
return (FromColor > 0.5 ? c.rgb * 0.6 : c.rgb) * lerp(1.0, 0.7, Wet);
)HLSL");

const FString SURFACE_HLSL = FString(R"HLSL(
// Projection and tangent frame. Mode comes from two static switches (ProjectionBit0/1), so it is a
// compile-time constant and each instance compiles one projection path.
float Mode = ProjA + 2.0 * ProjB;
float3 n = normalize(VN);
float3 T, B;
float2 uv;
float tileCm = max(Tile, 0.01) * 100.0;
if (Mode < 0.5)
{
    uv = UV / max(Tile, 0.01);
    T = Parameters.TangentToWorld[0];
    B = Parameters.TangentToWorld[1];
}
else if (Mode < 1.5)
{
    // Roof planes: U along the eave (horizontal), V down the slope; flat parts fall back to X/Y.
    float3 h = float3(n.x, n.y, 0.0);
    T = length(h) > 0.05 ? normalize(cross(float3(0.0, 0.0, 1.0), h)) : float3(1.0, 0.0, 0.0);
    B = normalize(cross(n, T));
    uv = float2(dot(WP, T), dot(WP, B)) / tileCm;
}
else if (Mode < 2.5)
{
    T = float3(1.0, 0.0, 0.0);
    B = float3(0.0, 1.0, 0.0);
    uv = WP.xy / tileCm;
}
else
{
    // World box projection from the dominant axis (grey-box landmarks, bridges, scenery: their
    // engine-cube UVs would stretch one texture over a whole face).
    float3 a = abs(n);
    if (a.z >= a.x && a.z >= a.y) { T = float3(1, 0, 0); B = float3(0, 1, 0); uv = WP.xy / tileCm; }
    else if (a.x >= a.y)          { T = float3(0, 1, 0); B = float3(0, 0, -1); uv = WP.yz * float2(1, -1) / tileCm; }
    else                          { T = float3(1, 0, 0); B = float3(0, 0, -1); uv = WP.xz * float2(1, -1) / tileCm; }
}

// Base colour with macro variation: the same texture 7x larger, relative to its mean colour.
float3 bc = Texture2DSample(BCTex, BCTexSampler, uv).rgb;
float3 big = Texture2DSample(BCTex, BCTexSampler, uv * 0.143 + float2(0.37, 0.71)).rgb;
float3 mean = Texture2DSampleLevel(BCTex, BCTexSampler, float2(0.5, 0.5), 12.0).rgb;
float lumBig = dot(big, float3(0.3, 0.59, 0.11));
float lumMean = max(dot(mean, float3(0.3, 0.59, 0.11)), 0.02);
bc *= lerp(1.0, clamp(lumBig / lumMean, 0.65, 1.35), Macro);

// Tints: material tint x per-instance tint (slot A = custom data 0-2, slot B = 3-5, slot C = the stone
// trim palette picked by custom data 9: pale limestone, grey stone, painted white, brick red, and the
// Ligurian painted architecture: cream, or a darker shade of the wall colour (BUILDINGS_PLAN.md 4.4).
const float3 TrimPal[5] = { float3(1.0, 1.0, 1.0), float3(0.74, 0.76, 0.78), float3(1.28, 1.26, 1.2), float3(0.92, 0.55, 0.44), float3(1.3, 1.22, 1.0) };
int ti = clamp((int)(I9 + 0.5), 0, 5);
float3 trim = ti == 5 ? float3(I0, I1, I2) * float3(0.78, 0.74, 0.7) * 1.25 : TrimPal[min(ti, 4)];
float3 inst = Slot < -0.5 ? float3(1.0, 1.0, 1.0) : (Slot < 0.5 ? float3(I0, I1, I2) : (Slot < 1.5 ? float3(I3, I4, I5) : trim));
bc *= Tint * inst;
// Kit walls on the ground floor (custom data 8 = 1): a darker, warmer base band and rain splash over
// the lowest metre of the piece.
if (Slot > -0.5 && Slot < 0.5 && I8 > 0.5)
{
    bc *= float3(0.86, 0.82, 0.78) * (1.0 - 0.22 * saturate(1.0 - LP.z / 110.0) * (0.4 + 0.6 * I6));
}

// Grime: scanned leak stains (ambientCG, oc_decals.py), one per 4 m bay and storey on kit walls,
// shifted and scaled by the building's wear (custom data 6). No decal actors on buildings.
if (GrimeAmt > 0.001 && Mode < 0.5)
{
    // Each bay and storey (one instance) reads its own part of the stain scan.
    float2 g = UV / float2(4.0, 3.2) + float2(I6 * 0.37 + Rand * 0.71, frac(Rand * 7.31) * 0.6);
    float stain = 1.0 - Texture2DSample(GrimeT, GrimeTSampler, frac(g)).r;
    bc *= 1.0 - saturate(stain * 1.4) * GrimeAmt * (0.35 + 0.65 * I6);
}

// Normal (DirectX, BC5): rebuild Z, scale, to world.
float2 nxy = (Texture2DSample(NTex, NTexSampler, uv).rg * 2.0 - 1.0) * NStrength;
float3 tn = float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
WorldNormal = normalize(T * tn.x + B * tn.y + n * tn.z);

float3 orm = Texture2DSample(ORMTex, ORMTexSampler, uv).rgb;
AO = orm.r;
Rough = saturate(orm.g * RoughMul + RoughAdd);
Metal = orm.b * MetalMul;
return bc;
)HLSL");

const FString GLASS_HLSL = FString(R"HLSL(
// Kit glass, per window (LP = position in the piece; windows sit on a 2 m pitch, so floor(LP.x / 200)
// numbers them; Rand = the instance): about 30% lit at night in warm or cool white, and by day a blind
// or curtain drawn part way down from the head, so glass is not uniformly black.
float win = floor(LP.x / 200.0);
float h = frac(sin(Rand * 91.7 + win * 12.9898) * 43758.5453);
float h2 = frac(h * 13.37 + 0.17);
float lit = step(h, 0.3);
Emis = lerp(float3(1.0, 0.72, 0.42), float3(0.78, 0.86, 1.0), step(0.75, h2)) * lit * Night * Strength;
float blind = step(240.0 - (15.0 + 95.0 * h2), LP.z) * step(h2, 0.8);
float3 cloth = lerp(float3(0.62, 0.56, 0.46), float3(0.42, 0.44, 0.48), step(0.5, frac(h * 5.1)));
return lerp(float3(0.02, 0.025, 0.03), cloth, blind * (1.0 - Night * lit));
)HLSL");

const FString CHARACTER_HLSL = FString(R"HLSL(
// Characters (M4b): skin, cloth and simple hair cards from one subsurface master. Skin = 1 scatters
// light under the surface (the Subsurface model's opacity) and tints it blood-red; cloth and hair use
// Skin = 0 and shade as plain lit surfaces. Rain (MPC_World.Wetness) soaks cloth and hair darker and
// glossier, and leaves skin with a wet sheen.
float3 bc = Texture2DSample(BCTex, BCTexSampler, UV).rgb * Tint;
float2 nxy = Texture2DSample(NTex, NTexSampler, UV).rg * 2.0 - 1.0;
TangentNormal = float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
float soak = Wet * (1.0 - Skin);
bc *= lerp(1.0, 0.55, soak);
Rough = lerp(Roughness, lerp(0.3, 0.22, Skin), Wet * 0.75);
Sss = float3(0.8, 0.25, 0.15) * Skin;
SssAmount = Skin * 0.85;
return bc;
)HLSL");
const FString LAMP_HLSL = FString(R"HLSL(
// Street lamp glass and bulbs: dark by day, warm sodium glow at night (MPC_World.NightAmount).
Emis = float3(1.0, 0.62, 0.30) * Night * Strength;
return float3(0.35, 0.33, 0.30);
)HLSL");

const FString WET_HLSL = FString(R"HLSL(
// --- Wetness and rain (MPC_World.Wetness / RainAmount, one value each; ROADMAP M4b) ---
// Porous surfaces darken, everything turns glossy, flat hollows (pn = 0..1 puddle noise) fill
// with standing water that shows drifting rain ripples. A uniform branch: free when dry.
if (Wet > 0.001)
{
    float wetv = saturate(Wet);
    float upv = saturate((nGeo.z - 0.75) / 0.2);
    // WetGround is a compile-time constant: walls, roofs and trim compile none of the ground code.
    float pool = WetGround > 0.5 ? saturate((pn - 1.0 + wetv * PuddleAmt) * 5.0) * upv * wetv : 0.0;
    bc *= lerp(1.0, 0.6, wetv * (1.0 - pool));
    bc = lerp(bc, bc * 0.3 + float3(0.01, 0.012, 0.014), pool);
    Rough = lerp(Rough, 0.15, wetv * 0.85);
    Rough = lerp(Rough, 0.03, pool);
    if (WetGround > 0.5)
    {
        float2 ruv = WP.xy / 220.0;
        float2 r1 = Texture2DSample(RipN, RipNSampler, ruv + float2(0.031, 0.017) * Time).rg * 2.0 - 1.0;
        float2 r2 = Texture2DSample(RipN, RipNSampler, ruv * 1.7 - float2(0.023, 0.041) * Time).rg * 2.0 - 1.0;
        float2 rip = (r1 + r2) * (0.12 + 0.4 * Rain);
        WorldNormal = normalize(lerp(WorldNormal, normalize(float3(rip, 1.0)), pool));
        // Rain splashes: one impact per 22 cm cell at a random time, a ring that grows to ~10 cm over
        // 0.3 s on up-facing ground: a bent, glossy, slightly bright crown that glints like a splash.
        float2 sp = WP.xy / 22.0;
        float sh = frac(sin(dot(floor(sp), float2(41.3, 289.1))) * 15731.743);
        float per = 0.5 + sh;
        float age = frac(Time / per + sh * 7.0) * per;
        float2 sd = (frac(sp) - 0.5 - (float2(frac(sh * 21.7), frac(sh * 57.3)) - 0.5) * 0.5) * 22.0;
        float ring = saturate(1.0 - abs(length(sd) - age * 33.0) / 1.6) * saturate(1.0 - age / 0.3) * Rain * upv;
        WorldNormal = normalize(WorldNormal + float3(normalize(sd + 1e-4) * ring * 1.6, 0.0));
        Rough = lerp(Rough, 0.02, ring);
        bc += ring * 0.22;  // normal and gloss alone are invisible on overcast asphalt (QA 2026-11-09)
    }
}
)HLSL");

const FString ROAD_HLSL = FString(R"HLSL(
// Road meshes (OCWorldBuilderLibrary::BuildRoadMeshes): UV0 = (lateral m, along m); UV1 = (half width m,
// marking code). One texture set per instance: the paving (MI_Paving_*: asphalt, sampietrini,
// lastricato, gravel, concrete), chosen per segment by the road builder.
// Marking code = lanes per direction + 10 x style (Codice della Strada, DPR 495/1992 artt. 138, 141):
// style 0 urban F (10 cm lines, 3 m dash / 3 m gap), 1 urban D/E (12 cm, 3/3), 2 extra-urban C/F
// (12 cm, 3/4.5), 3 A/B (15 cm, 4.5/7.5). Edge lines 12 / 15 / 15 / 25 cm. Code 0: no markings.
float hw = R1.x;
float code = R1.y + 0.01;
float parking = floor(code / 100.0);
code -= parking * 100.0;
float style = floor(code / 10.0);
float lanes = floor(code - style * 10.0);
float2 uv0 = R0;
float2 uv = uv0 / TileA;
float2 dx = ddx(uv), dy = ddy(uv);
float3 bc = Texture2DSampleGrad(ABC, ABCSampler, uv, dx, dy).rgb * Tint;
float2 nxy = Texture2DSampleGrad(AN, ANSampler, uv, dx, dy).rg;
float3 orm = Texture2DSampleGrad(AORM, AORMSampler, uv, dx, dy).rgb;
// Macro variation across the city: a large, slow sample of the same paving.
float big = dot(Texture2DSample(ABC, ABCSampler, WP.xy / 4100.0).rgb, float3(0.3, 0.59, 0.11));
bc *= lerp(0.82, 1.18, saturate(big * 2.2 - 0.3));

// Lane markings (white paint), worn by the paving's own texture.
float au = abs(uv0.x);
float mk = 0.0;
if (lanes > 0.5)
{
    float lw = style < 0.5 ? 0.10 : (style < 2.5 ? 0.12 : 0.15);
    float ew = style < 0.5 ? 0.12 : (style < 2.5 ? 0.15 : 0.25);
    float dashL = style > 2.5 ? 4.5 : 3.0;
    float gapL = style > 2.5 ? 7.5 : (style > 1.5 ? 4.5 : 3.0);
    float dash = step(frac(uv0.y / (dashL + gapL)), dashL / (dashL + gapL));
    mk += step(hw - 0.40 - ew, au) * step(au, hw - 0.40);                 // edge lines
    if (lanes > 1.5)
    {
        mk += step(0.06, au) * step(au, 0.06 + lw);                        // double centre line
        float laneW = (hw - 0.5) / lanes;
        mk += step(abs(au - laneW), lw * 0.5) * dash;                       // lane lines
    }
    else
    {
        mk += step(au, lw * 0.5) * dash;                                    // dashed centre line
    }
}
// Pedestrian crossings (zebra, 50 cm stripes and gaps, 2.5 m long on E/F roads and 4 m on the
// others) 1 m out from each urban junction edge, and a 50 cm stop line just before each one on the
// approaching half (right-hand traffic: the A end is approached on the left half, uv0.x < 0).
// R2 = distance along the strip of the junction edge at A and at B (0: no junction at that end).
if (lanes > 0.5)
{
    float zl = style < 1.5 ? 2.5 : 4.0;
    float inside = step(au, hw - 0.4);
    float stripes = step(frac(au / 1.0), 0.5) * inside;
    if (R2.x > 0.01)
    {
        float d = uv0.y - R2.x;
        mk += stripes * step(1.0, d) * step(d, 1.0 + zl);
        mk += step(1.0 + zl + 0.6, d) * step(d, 1.0 + zl + 1.1) * step(uv0.x, 0.0) * inside;
    }
    if (R2.y > 0.01)
    {
        float d = R2.y - uv0.y;
        mk += stripes * step(1.0, d) * step(d, 1.0 + zl);
        mk += step(1.0 + zl + 0.6, d) * step(d, 1.0 + zl + 1.1) * step(0.0, uv0.x) * inside;
    }
}
// Parking bays (art. 149): the profile's 2 m strip along the segment's right-hand edge, 12 cm lines,
// 5 m bays; white free, blue paid, yellow reserved.
float pk = 0.0;
if (parking > 0.5)
{
    float outer = hw - 0.40, inner = hw - 2.40;
    float inStrip = step(inner, uv0.x) * step(uv0.x, outer);  // one side (the profile's 2 m strip)
    pk = step(abs(uv0.x - inner), 0.06) + step(frac(uv0.y / 5.0), 0.024) * inStrip;
    pk = saturate(pk) * step(0.1, uv0.y);
}
float3 pkColour = parking < 1.5 ? float3(0.72, 0.71, 0.66) : (parking < 2.5 ? float3(0.05, 0.22, 0.55) : float3(0.75, 0.6, 0.05));
float wear = saturate(dot(bc, float3(0.3, 0.59, 0.11)) * 3.0);
mk = saturate(mk) * lerp(0.55, 1.0, wear);
pk *= lerp(0.55, 1.0, wear);
bc = lerp(bc, float3(0.72, 0.71, 0.66), mk);
bc = lerp(bc, pkColour, pk);
mk = max(mk, pk);

float2 n2 = (nxy * 2.0 - 1.0) * (1.0 - mk * 0.8);
float3 tn = float3(n2, sqrt(saturate(1.0 - dot(n2, n2))));
WorldNormal = normalize(mul(tn, (float3x3)Parameters.TangentToWorld));
Rough = lerp(saturate(orm.g), 0.55, mk);
AO = orm.r;
float3 nGeo = normalize(VN);
// Stone paving (GapWater > 0): water stands in the joints between setts and slabs first (they are the
// dark, occluded part of the ORM's AO); asphalt keeps only the macro puddle pattern.
float gapw = saturate((0.72 - orm.r) * 6.0) * GapWater;
float pn = max(saturate(big * 2.2 - 0.3), gapw) * (1.0 - mk);
)HLSL") + WET_HLSL + FString(R"HLSL(
return bc;
)HLSL");

const FString LAND_HLSL = FString(R"HLSL(
// Landscape auto-material: beach sand near the sea, grass, dirt on slopes, rock on steep or high
// ground. World top-down UVs in metres; transitions broken up by a large noise-free texture sample;
// far away each layer is sampled 4x larger so tiling doesn't read from the hills.
float3 n = normalize(VN);
// Face normal from screen derivatives: on a one-quad cut between two graded roads the vertex
// normals sit near 45 degrees (they share the flat road), so the wall smeared a top-down texel
// into a pale streak (QA 2026-11-09). Rock weight and projection follow the steeper of the two.
float3 nf = normalize(cross(ddy(WP), ddx(WP)));
nf *= sign(nf.z + 1e-4);
float3 ns = nf.z < n.z ? nf : n;
float h = WP.z / 100.0;
float2 m = WP.xy / 100.0;
float vary = dot(Texture2DSample(GBC, GBCSampler, m / 61.0).rgb, float3(0.3, 0.59, 0.11)) * 2.0 - 0.5;
float wSand = saturate((1.8 - h + vary * 0.8) / 1.2);
float wRock = max(saturate((0.78 - ns.z + vary * 0.05) / 0.10), saturate((h - 230.0 + vary * 40.0) / 70.0));
float wDirt = saturate((0.94 - n.z + vary * 0.04) / 0.07) * (1.0 - wRock);
wSand *= (1.0 - wRock);
float wGrass = saturate(1.0 - wSand - wRock - wDirt);
float total = wSand + wRock + wDirt + wGrass + 1e-4;
float4 w = float4(wGrass, wDirt, wRock, wSand) / total;

float dist = length(WP - Cam);
float far = saturate((dist - 4000.0) / 12000.0);
float3 bc = 0; float2 nn = 0; float3 orm = 0;
// Steep rock: project from the side (X or Y) so cliffs don't smear; everything else top-down.
float3 an = abs(ns);
float2 mRock = an.z > 0.7 ? m : (an.x > an.y ? float2(WP.y, -WP.z) / 100.0 : float2(WP.x, -WP.z) / 100.0);
#define OC_LAYER(i, T, TN, TO, TILE) if (w[i] > 0.01) { float2 uv = (i == 2 ? mRock : m) / TILE; float2 dx = ddx(uv), dy = ddy(uv); \
    float3 c = lerp(Texture2DSampleGrad(T, T##Sampler, uv, dx, dy).rgb, Texture2DSampleGrad(T, T##Sampler, uv * 0.25, dx * 0.25, dy * 0.25).rgb, far); \
    bc += c * w[i]; nn += (Texture2DSampleGrad(TN, TN##Sampler, uv, dx, dy).rg * 2.0 - 1.0) * w[i] * (1.0 - far * 0.7); \
    orm += Texture2DSampleGrad(TO, TO##Sampler, uv, dx, dy).rgb * w[i]; }
OC_LAYER(0, GBC, GN, GORM, TileG)
OC_LAYER(1, DBC, DN, DORM, TileD)
OC_LAYER(2, RBC, RN, RORM, TileR)
OC_LAYER(3, SBC, SN, SORM, TileS)
float3 tn = float3(nn, sqrt(saturate(1.0 - dot(nn, nn))));
WorldNormal = normalize(mul(tn, (float3x3)Parameters.TangentToWorld));
Rough = saturate(orm.g);
AO = orm.r;
float3 nGeo = n;
float pn = saturate(vary + 0.5) * (1.0 - w.z);
)HLSL") + WET_HLSL + FString(R"HLSL(
// Dry Mediterranean grass: less saturated, a touch darker than the raw scan.
float g = dot(bc, float3(0.3, 0.59, 0.11));
bc = lerp(bc, lerp(float3(g, g, g), bc, 0.65) * 0.9, w.x);
return bc * lerp(0.9, 1.1, saturate(vary + 0.5));
)HLSL");

const FString WATER_HLSL = FString(R"HLSL(
// Sea, river and reservoir: layered drifting water normals (two scales plus a slow swell), rain
// roughens the surface. Opaque (cheap); Lumen reflects the sky and the coast.
float2 uv = WP.xy / 100.0;
float2 a = Texture2DSample(WN, WNSampler, uv / 18.0 + float2(0.010, 0.006) * Time).rg * 2.0 - 1.0;
float2 b = Texture2DSample(WN, WNSampler, uv / 7.0 - float2(0.008, 0.013) * Time).rg * 2.0 - 1.0;
float2 c = Texture2DSample(LF, LFSampler, uv / 60.0 + float2(0.003, 0.002) * Time).rg * 2.0 - 1.0;
float2 r = Texture2DSample(WN, WNSampler, uv / 1.5 + float2(0.05, 0.03) * Time).rg * 2.0 - 1.0;
float2 nn = (a * 0.6 + b * 0.4 + c * 0.5 + r * 0.5 * Rain) * Strength;
WorldNormal = normalize(float3(nn, 1.0));
float facing = saturate(dot(normalize(Cam - WP), float3(0.0, 0.0, 1.0)));
Rough = 0.03 + 0.06 * Rain;
return lerp(Deep, Shallow, facing * 0.6) * lerp(1.0, 0.7, Rain);
)HLSL");

const FString RAIN_PP_HLSL = FString(R"HLSL(
// Rain over the screen (MPC_World.RainAmount): three layers of falling streaks, slightly
// lightened and blurred-looking, and the scene dimmed under the overcast.
float3 col = Scene;
if (Rain > 0.001)
{
    float streak = 0.0;
    for (int i = 0; i < 3; i++)
    {
        float s = 38.0 + i * 29.0;
        float2 p = float2(UV.x * s * 1.6 + i * 7.3, UV.y * s * 0.11 + Time * (2.3 + i * 0.8));
        float2 cell = floor(p);
        float h = frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453);
        float2 f = frac(p);
        streak += step(0.93, h) * smoothstep(0.0, 0.2, f.y) * smoothstep(1.0, 0.55, f.y) * saturate(1.0 - abs(f.x - 0.5) * 7.0);
    }
    col = lerp(col, col * 0.8 + 0.1, saturate(streak) * 0.45 * Rain);
    col *= lerp(1.0, 0.9, Rain);
}
return col;
)HLSL");

const FString CAR_GLASS_HLSL = FString(R"HLSL(
// Car glass (M4b): opaque, near-black and glossy (the vehicle bodies are shells with no interior,
// so see-through glass would show an empty car). In rain, drops bead on it in the car's own space
// (they ride along with the car), from MPC_World.RainAmount. LP is in cm: 3.5 cm cells give drops
// of roughly 0.4 to 1.1 cm, which read at chase-camera distance as bright domed highlights.
float2 q = (LP.xy + LP.yz * 0.7) / 3.5;
float2 c = floor(q);
float h = frac(sin(dot(c, float2(127.1, 311.7))) * 43758.5453);
float2 f = frac(q) - 0.5 - (float2(frac(h * 13.7), frac(h * 7.3)) - 0.5) * 0.4;
float r = 0.12 + 0.2 * frac(h * 3.1);
float d = saturate(1.0 - length(f) / r);
float on = step(0.45, h) * step(0.001, d) * Rain;
// A film of water roughens the glass slightly; each drop is a smooth dome.
Rough = lerp(0.04, 0.12, Rain) * (1.0 - on) + 0.02 * on;
float3 n = normalize(N);
float3 t = normalize(abs(n.z) < 0.95 ? cross(float3(0, 0, 1), n) : cross(float3(1, 0, 0), n));
float3 b = cross(n, t);
float2 g = (f / max(r, 1e-3)) * on;
WorldNormal = normalize(n + (t * g.x + b * g.y) * 1.2);
return lerp(float3(0.012, 0.014, 0.017), float3(0.05, 0.055, 0.06), on * d);
)HLSL");

const FString FOLIAGE_HLSL = FString(R"HLSL(
// Foliage (M4b): Poly Haven tree and plant scans on the two-sided foliage shading model. Leaves
// let light through (Subsurface 1, tinted by their own colour); trunks don't (0). Each instance
// gets a slight hue and brightness shift so a row of the same tree doesn't repeat.
float4 c = Texture2DSample(BCTex, BCTexSampler, UV);
float3 bc = c.rgb * lerp(0.85, 1.12, Rand) * lerp(float3(1.0, 1.0, 1.0), float3(1.04, 1.0, 0.9), frac(Rand * 7.31));
float2 nxy = Texture2DSample(NTex, NTexSampler, UV).rg * 2.0 - 1.0;
TangentNormal = float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
float3 orm = Texture2DSample(ORMTex, ORMTexSampler, UV).rgb;
AO = orm.r;
Rough = saturate(orm.g);
Sss = bc * float3(0.9, 1.0, 0.55) * Subsurface;
// Wet leaves turn darker and glossier (MPC_World.Wetness).
bc *= lerp(1.0, 0.75, Wet);
Rough = lerp(Rough, 0.35, Wet * 0.8);
return bc;
)HLSL");

const FString FOLIAGE_WPO_HLSL = FString(R"HLSL(
// Cheap wind (MPC_World.Wind, set per weather preset): the whole plant leans and sways with height
// above its pivot (the trunk base stays put), plus a fast flutter on leaves. World-space cm.
float h = max(LP.z, 0.0) / 100.0;                       // metres above the pivot
float phase = dot(WP.xy, float2(0.0011, 0.0007));       // neighbours sway out of step
float gust = 0.6 + 0.4 * sin(Time * 0.37 + phase * 3.0);
float sway = sin(Time * 1.3 + phase * 6.2832) * gust;
float bend = Wind * h * h * 0.35;                       // cm: ~22 cm at 8 m in a strong wind
float2 dir = float2(0.94, 0.34);
float flutter = sin(Time * 7.0 + dot(WP.xyz, float3(0.05, 0.07, 0.09))) * Wind * Subsurface * min(h, 4.0) * 0.6;
return float3(dir * bend * (0.4 + sway), 0.0) + VN * flutter;
)HLSL");

const FString PAINT_HLSL = FString(R"HLSL(
// Painted street fixtures (INFRA_DESIGN.md Phase 5): sign faces, bins, railings. Flat paint colour
// (or a palette entry picked by the instance's custom data 0: recycling streams and the signal button box;
// one value, not three, keeps the per-instance fetches cheap), roughened and dulled
// by a scanned grime map; sign faces are retroreflective (a faint night glow). Wet paint is glossier.
const float3 Pal[6] = { float3(0.05, 0.25, 0.06), float3(0.75, 0.6, 0.02), float3(0.03, 0.12, 0.45),   // glass, plastic, paper
                        float3(0.22, 0.12, 0.05), float3(0.25, 0.25, 0.25), float3(0.85, 0.62, 0.03) };  // organic, unsorted, button box
float3 col = UseInstance > 0.5 ? Pal[clamp((int)(I0 + 0.5), 0, 5)] : Colour;
float g = Texture2DSample(Grime, GrimeSampler, frac(WP.xz / 180.0 + WP.yz / 260.0)).r;
float dirt = saturate((1.0 - g) * 1.6) * Dirt;
float3 bc = lerp(col, col * float3(0.55, 0.5, 0.45), dirt);
Rough = lerp(0.42, 0.75, dirt);
Rough = lerp(Rough, 0.12, Wet * 0.8);
Emis = col * Reflect * Night * 0.6;
return bc;
)HLSL");

const FString CAR_PAINT_HLSL = FString(R"HLSL(
// Car paint (M4b): clear coat over the vehicle's own body maps. Paint colour, dirt and damage come
// from custom primitive data (AOCVehicle::ApplyPaint), so every car differs without a dynamic
// material instance. Dirt collects low on the body and in crevices, streaked by a scanned leak
// pattern in the car's side space; damage dulls the coat and scrapes paint to grey primer.
float3 bt = Texture2DSample(BaseTex, BaseTexSampler, UV).rgb;
float3 mra = Texture2DSample(MRATex, MRATexSampler, UV).rgb;     // metallic, roughness, AO
float2 nxy = Texture2DSample(NTex, NTexSampler, UV).rg * 2.0 - 1.0;
TangentNormal = float3(nxy, sqrt(saturate(1.0 - dot(nxy, nxy))));
float3 paint = bt * Paint;
float low = saturate(1.0 - (LP.z - 25.0) / 70.0);                 // cm above the car's base
float g = 1.0 - Texture2DSample(Grime, GrimeSampler, frac(float2(LP.x + LP.y, LP.z) / 140.0)).r;
float dirt = saturate(Dirt * ((0.35 + 1.3 * low) * (0.55 + 0.9 * g) + (1.0 - mra.b) * 0.6));
float scrape = saturate((g - 0.3) * 4.0) * saturate(Damage * 1.5 - 0.15);
float3 bc = lerp(paint, float3(0.30, 0.26, 0.20), dirt * 0.85);
bc = lerp(bc, float3(0.16, 0.16, 0.17), scrape);
bc *= lerp(1.0, 0.5, Damage * Damage);                             // sooty and faded when wrecked
Metal = saturate(mra.r * (1.0 - dirt) * (1.0 - scrape));
Rough = lerp(mra.g, 0.8, max(dirt, scrape));
AO = mra.b;
Coat = (1.0 - dirt * 0.75) * (1.0 - scrape) * (1.0 - Damage * 0.4);
CoatRough = lerp(0.04, 0.4, max(dirt, Damage * 0.7));
// Rain: a wet coat is glossier, and wet dirt darker.
Coat = lerp(Coat, 1.0, Wet * 0.5);
CoatRough = lerp(CoatRough, 0.02, Wet * 0.8);
bc *= lerp(1.0, 0.85, Wet * dirt);
return bc;
)HLSL");
