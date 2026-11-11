// Source-art generators (formerly Tools/make_lut.py, Tools/make_bike_chassis.py and
// Tools/render_network_plan.py). The LUT and the chassis reproduce the Python output byte for byte.
#include "OCGenerateCommandlet.h"

#include "ImageCore.h"
#include "ImageUtils.h"
#include "Layout/OCCanvas.h"
#include "Layout/OCLayoutCompat.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OCEditorHelpers.h"
#include "OCLog.h"
#include <cmath>

#pragma clang fp contract(off)   // the NumPy original never fused multiply-adds

using OCLayout::FJson;
using OCLayout::FJsonPtr;

namespace
{
	bool SavePng(const FString& Path, int32 W, int32 H, const TArray<FColor>& Px)
	{
		FImage Img(W, H, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		FMemory::Memcpy(Img.RawData.GetData(), Px.GetData(), Px.Num() * sizeof(FColor));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FImageUtils::SaveImageByExtension(*Path, Img);
	}

	// ---- LUT ------------------------------------------------------------------------------------

	/** Unreal's 256 x 16 LUT strip (16 slices of 16 x 16, blue across slices). The grade: a mild S-curve,
	 *  shadows a touch cool, highlights warm (Mediterranean sun), saturation +6%. */
	bool MakeLut(const FString& Out)
	{
		constexpr int32 N = 16;
		TArray<FColor> Px;
		Px.SetNumZeroed(N * N * N);
		const double Shadows[3] = { 0.985, 1.0, 1.03 };      // cool shadows
		const double Highlights[3] = { 1.035, 1.0, 0.95 };   // warm highlights
		for (int32 Bi = 0; Bi < N; ++Bi)
		{
			for (int32 Gi = 0; Gi < N; ++Gi)
			{
				for (int32 Ri = 0; Ri < N; ++Ri)
				{
					const double Rgb[3] = { Ri / (N - 1.0), Gi / (N - 1.0), Bi / (N - 1.0) };
					const double Lum = Rgb[0] * 0.2126 + Rgb[1] * 0.7152 + Rgb[2] * 0.0722;
					const double W = FMath::Clamp(Lum, 0.0, 1.0);
					uint8 C[3];
					for (int32 k = 0; k < 3; ++k)
					{
						double S = FMath::Clamp(Rgb[k] + (Rgb[k] - Lum) * 0.06, 0.0, 1.0);       // saturation +6%
						S = S + 0.06 * (S - 0.5) * (1 - std::fabs(2 * S - 1));                   // gentle S-curve around mid-grey
						S = S * (Shadows[k] * (1 - W) + Highlights[k] * W);
						S = FMath::Clamp(S, 0.0, 1.0);
						C[k] = uint8(OCLayout::PyRoundInt(S * 255));                             // NumPy rounds half to even
					}
					Px[Gi * N * N + Bi * N + Ri] = FColor(C[0], C[1], C[2], 255);
				}
			}
		}
		return SavePng(Out, N * N, N, Px);
	}

	// ---- Bike chassis ---------------------------------------------------------------------------

	/** The grey-box motorbike physics chassis as a skinned glTF: invisible in game, it only gives Chaos Vehicles a
	 *  physics body and four wheel bones (root + PhysWheel_FL/FR/RL/RR; the "hidden four-wheel" approach,
	 *  ARCHITECTURE.md section 3). Unreal's glTF importer maps glTF (x, y, z) to Unreal (x, z, y), metres to cm. */
	bool MakeBikeChassis(const FString& Out)
	{
		constexpr double WheelRadius = 0.32, WheelbaseHalf = 0.70, TrackHalf = 0.20;
		constexpr double BodyLength = 1.50, BodyWidth = 0.30, BodyHeight = 0.45, BodyBottom = 0.30;
		auto ToGltf = [](double Forward, double Right, double Up) { return FVector(Forward, Up, Right); };
		const TPair<const TCHAR*, FVector> Joints[] = {   // Unreal convention: left wheels at -Y
			{ TEXT("root"), ToGltf(0.0, 0.0, 0.0) },
			{ TEXT("PhysWheel_FL"), ToGltf(WheelbaseHalf, -TrackHalf, WheelRadius) },
			{ TEXT("PhysWheel_FR"), ToGltf(WheelbaseHalf, TrackHalf, WheelRadius) },
			{ TEXT("PhysWheel_RL"), ToGltf(-WheelbaseHalf, -TrackHalf, WheelRadius) },
			{ TEXT("PhysWheel_RR"), ToGltf(-WheelbaseHalf, TrackHalf, WheelRadius) } };

		// Box in glTF space: width (the body length) along x, height along y from the bottom, length (the body
		// width) along z. 24 vertices (4 per face, flat normals) and 36 indices.
		const double Hx = BodyLength / 2, Hz = BodyWidth / 2, Y0 = BodyBottom, Y1 = BodyBottom + BodyHeight;
		struct FFace { FVector N; FVector C[4]; };
		const FFace Faces[] = {
			{ FVector(1, 0, 0), { FVector(Hx, Y0, Hz), FVector(Hx, Y0, -Hz), FVector(Hx, Y1, -Hz), FVector(Hx, Y1, Hz) } },
			{ FVector(-1, 0, 0), { FVector(-Hx, Y0, -Hz), FVector(-Hx, Y0, Hz), FVector(-Hx, Y1, Hz), FVector(-Hx, Y1, -Hz) } },
			{ FVector(0, 1, 0), { FVector(-Hx, Y1, Hz), FVector(Hx, Y1, Hz), FVector(Hx, Y1, -Hz), FVector(-Hx, Y1, -Hz) } },
			{ FVector(0, -1, 0), { FVector(-Hx, Y0, -Hz), FVector(Hx, Y0, -Hz), FVector(Hx, Y0, Hz), FVector(-Hx, Y0, Hz) } },
			{ FVector(0, 0, 1), { FVector(-Hx, Y0, Hz), FVector(Hx, Y0, Hz), FVector(Hx, Y1, Hz), FVector(-Hx, Y1, Hz) } },
			{ FVector(0, 0, -1), { FVector(Hx, Y0, -Hz), FVector(-Hx, Y0, -Hz), FVector(-Hx, Y1, -Hz), FVector(Hx, Y1, -Hz) } } };
		TArray<FVector> Pos, Nrm;
		TArray<uint16> Idx;
		for (const FFace& F : Faces)
		{
			const uint16 Base = uint16(Pos.Num());
			for (const FVector& C : F.C) { Pos.Add(C); Nrm.Add(F.N); }
			Idx.Append({ Base, uint16(Base + 1), uint16(Base + 2), Base, uint16(Base + 2), uint16(Base + 3) });
		}
		const int32 N = Pos.Num();

		TArray<uint8> Buf;
		FJsonPtr Views = FJson::MakeArray(), Accessors = FJson::MakeArray();
		auto Push = [&Buf](const void* Data, int32 Bytes) { Buf.Append(static_cast<const uint8*>(Data), Bytes); };
		auto F32 = [&Push](double V) { const float F = float(V); Push(&F, 4); };
		auto AddView = [&](TFunctionRef<void()> Write, int32 Target) -> int32
		{
			while (Buf.Num() % 4) { Buf.Add(0); }
			const int32 Offset = Buf.Num();
			Write();
			FJsonPtr V = FJson::MakeObject();
			V->Set(TEXT("buffer"), FJson::MakeInt(0)).Set(TEXT("byteOffset"), FJson::MakeInt(Offset)).Set(TEXT("byteLength"), FJson::MakeInt(Buf.Num() - Offset));
			if (Target) { V->Set(TEXT("target"), FJson::MakeInt(Target)); }
			Views->Add(V);
			return Views->Items.Num() - 1;
		};
		auto AddAccessor = [&](int32 View, int32 Comp, int32 Count, const TCHAR* Type, FJsonPtr Min = nullptr, FJsonPtr Max = nullptr) -> int32
		{
			FJsonPtr A = FJson::MakeObject();
			A->Set(TEXT("bufferView"), FJson::MakeInt(View)).Set(TEXT("componentType"), FJson::MakeInt(Comp)).Set(TEXT("count"), FJson::MakeInt(Count))
				.Set(TEXT("type"), FJson::MakeString(Type));
			if (Min) { A->Set(TEXT("min"), Min); A->Set(TEXT("max"), Max); }
			Accessors->Add(A);
			return Accessors->Items.Num() - 1;
		};
		constexpr int32 Float = 5126, UShort = 5123, UByte = 5121, ArrayBuf = 34962, Elements = 34963;
		FJsonPtr Min = FJson::MakeArray(), Max = FJson::MakeArray();
		for (int32 i = 0; i < 3; ++i)
		{
			double Lo = 1e18, Hi = -1e18;
			for (const FVector& P : Pos) { Lo = FMath::Min(Lo, P[i]); Hi = FMath::Max(Hi, P[i]); }
			Min->Add(FJson::MakeFloat(Lo));
			Max->Add(FJson::MakeFloat(Hi));
		}
		const int32 PosAcc = AddAccessor(AddView([&] { for (const FVector& P : Pos) { F32(P.X); F32(P.Y); F32(P.Z); } }, ArrayBuf), Float, N, TEXT("VEC3"), Min, Max);
		const int32 NrmAcc = AddAccessor(AddView([&] { for (const FVector& V : Nrm) { F32(V.X); F32(V.Y); F32(V.Z); } }, ArrayBuf), Float, N, TEXT("VEC3"));
		const int32 JntAcc = AddAccessor(AddView([&] { for (int32 i = 0; i < N; ++i) { const uint8 Z[4] = { 0, 0, 0, 0 }; Push(Z, 4); } }, ArrayBuf), UByte, N, TEXT("VEC4"));
		const int32 WgtAcc = AddAccessor(AddView([&] { for (int32 i = 0; i < N; ++i) { F32(1.0); F32(0.0); F32(0.0); F32(0.0); } }, ArrayBuf), Float, N, TEXT("VEC4"));
		const int32 IdxAcc = AddAccessor(AddView([&] { for (uint16 I : Idx) { Push(&I, 2); } }, Elements), UShort, Idx.Num(), TEXT("SCALAR"));
		// Inverse bind matrices (column-major): joints are pure translations from the root.
		const int32 IbmAcc = AddAccessor(AddView([&]
		{
			for (const auto& J : Joints)
			{
				const double M[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -J.Value.X, -J.Value.Y, -J.Value.Z, 1 };
				for (double V : M) { F32(V); }
			}
		}, 0), Float, UE_ARRAY_COUNT(Joints), TEXT("MAT4"));

		FJsonPtr Nodes = FJson::MakeArray();
		Nodes->Add(FJson::MakeObject());
		Nodes->Items[0]->Set(TEXT("name"), FJson::MakeString(TEXT("SKM_BikeChassis"))).Set(TEXT("mesh"), FJson::MakeInt(0)).Set(TEXT("skin"), FJson::MakeInt(0));
		FJsonPtr JointNodes = FJson::MakeArray();
		for (const auto& J : Joints)
		{
			FJsonPtr Node = FJson::MakeObject();
			Node->Set(TEXT("name"), FJson::MakeString(J.Key));
			if (!J.Value.IsZero())
			{
				FJsonPtr T = FJson::MakeArray();
				T->Add(FJson::MakeFloat(J.Value.X)).Add(FJson::MakeFloat(J.Value.Y)).Add(FJson::MakeFloat(J.Value.Z));
				Node->Set(TEXT("translation"), T);
			}
			Nodes->Add(Node);
			JointNodes->Add(FJson::MakeInt(Nodes->Items.Num() - 1));
		}
		FJsonPtr Children = FJson::MakeArray();
		for (int32 i = 1; i < JointNodes->Items.Num(); ++i) { Children->Add(JointNodes->Items[i]); }
		Nodes->Items[JointNodes->Items[0]->Int]->Set(TEXT("children"), Children);

		auto Obj = [](std::initializer_list<TPair<const TCHAR*, FJsonPtr>> Kv)
		{
			FJsonPtr O = FJson::MakeObject();
			for (const auto& P : Kv) { O->Set(P.Key, P.Value); }
			return O;
		};
		auto Arr = [](std::initializer_list<FJsonPtr> Items)
		{
			FJsonPtr A = FJson::MakeArray();
			for (const FJsonPtr& I : Items) { A->Add(I); }
			return A;
		};
		using J = FJson;
		FJsonPtr Gltf = Obj({
			{ TEXT("asset"), Obj({ { TEXT("version"), J::MakeString(TEXT("2.0")) }, { TEXT("generator"), J::MakeString(TEXT("OpenCity OCGenerate -what=bike")) } }) },
			{ TEXT("scene"), J::MakeInt(0) },
			{ TEXT("scenes"), Arr({ Obj({ { TEXT("nodes"), Arr({ J::MakeInt(0), J::MakeInt(JointNodes->Items[0]->Int) }) } }) }) },
			{ TEXT("nodes"), Nodes },
			{ TEXT("meshes"), Arr({ Obj({ { TEXT("name"), J::MakeString(TEXT("SKM_BikeChassis")) }, { TEXT("primitives"), Arr({ Obj({
				{ TEXT("attributes"), Obj({ { TEXT("POSITION"), J::MakeInt(PosAcc) }, { TEXT("NORMAL"), J::MakeInt(NrmAcc) },
					{ TEXT("JOINTS_0"), J::MakeInt(JntAcc) }, { TEXT("WEIGHTS_0"), J::MakeInt(WgtAcc) } }) },
				{ TEXT("indices"), J::MakeInt(IdxAcc) }, { TEXT("material"), J::MakeInt(0) } }) }) } }) }) },
			{ TEXT("materials"), Arr({ Obj({ { TEXT("name"), J::MakeString(TEXT("M_BikeChassis_Hidden")) }, { TEXT("pbrMetallicRoughness"),
				Obj({ { TEXT("baseColorFactor"), Arr({ J::MakeFloat(0.5), J::MakeFloat(0.5), J::MakeFloat(0.5), J::MakeInt(1) }) } }) } }) }) },
			{ TEXT("skins"), Arr({ Obj({ { TEXT("name"), J::MakeString(TEXT("SK_BikeChassis")) }, { TEXT("joints"), JointNodes },
				{ TEXT("skeleton"), J::MakeInt(JointNodes->Items[0]->Int) }, { TEXT("inverseBindMatrices"), J::MakeInt(IbmAcc) } }) }) },
			{ TEXT("accessors"), Accessors },
			{ TEXT("bufferViews"), Views },
			{ TEXT("buffers"), Arr({ Obj({ { TEXT("byteLength"), J::MakeInt(Buf.Num()) },
				{ TEXT("uri"), J::MakeString(TEXT("data:application/octet-stream;base64,") + FBase64::Encode(Buf)) } }) }) } });
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Out), true);
		const bool bOk = FFileHelper::SaveStringToFile(Gltf->Write(), *Out, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		OCEd::Log(TEXT("oc_gen"), FString::Printf(TEXT("wrote %s: %d vertices, %d triangles, %d joints"), *Out, N, Idx.Num() / 3, int32(UE_ARRAY_COUNT(Joints))));
		return bOk;
	}

	// ---- Network plan ---------------------------------------------------------------------------

	FJsonPtr ReadJson(const FString& Path)
	{
		FString Text, Error;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			UE_LOG(LogOpenCity, Error, TEXT("cannot read %s"), *Path);
			return nullptr;
		}
		return FJson::Parse(Text, Error);
	}

	double Nm(const FJsonPtr& J, const TCHAR* K) { return J->Get(K)->AsNumber(); }
	double Ix(const FJsonPtr& J, int32 I) { return J->Items[I]->AsNumber(); }

	/** Top-down network plan for INFRA_DESIGN.md: built roads coloured by profile, ZTL streets, junction types, the
	 *  planned tram corridor and water bodies, plus the proposed infrastructure (proposed_networks.json) dashed. */
	bool MakePlan(const FString& Out)
	{
		const FString Root = OCEd::ProjectDir();
		const FJsonPtr Lay = ReadJson(Root / TEXT("SourceArt/World/marevia_layout.json"));
		const FJsonPtr Prop = ReadJson(Root / TEXT("SourceArt/Infra/proposed_networks.json"));
		FImage Height;
		if (!Lay || !Prop || !FImageUtils::LoadImage(*(Root / TEXT("SourceArt/World/marevia_height.png")), Height))
		{
			return false;
		}
		Height.ChangeFormat(ERawImageFormat::G16, EGammaSpace::Linear);
		constexpr int32 Size = 1800;      // px for the 3024 m map
		constexpr double MarginM = 700;   // show proposals past the east edge
		const double Ox = Ix(Lay->Get(TEXT("origin")), 0), Oy = Ix(Lay->Get(TEXT("origin")), 1);
		const double Wm = Ix(Lay->Get(TEXT("size")), 0), Hm = Ix(Lay->Get(TEXT("size")), 1);
		const double Scale = Size / Wm;
		const int32 Width = int32((Wm + MarginM) * Scale);
		auto Px = [&](double X, double Y) { return FVector2D((X - Ox) * Scale, (Oy + Hm - Y) * Scale); };
		auto PxOf = [&](const FJsonPtr& P) { return Px(Ix(P, 0), Ix(P, 1)); };

		OCLayout::FCanvas Cv;
		Cv.W = Width;
		Cv.H = Size;
		Cv.Px.Init(FColor(215, 215, 210), Width * Size);   // outside the map (not sea)

		// Hillshade background from the heightmap (row 0 = north), resampled bilinearly to Size.
		const double Lo = Ix(Lay->Get(TEXT("height_range")), 0), Hi = Ix(Lay->Get(TEXT("height_range")), 1);
		const double Sea = Nm(Lay, TEXT("sea_level"));
		const TArrayView64<uint16> G16 = Height.AsG16();
		const int32 Hw = Height.SizeX, Hh = Height.SizeY;
		auto Sample = [&](double U, double V)
		{
			const double X = FMath::Clamp(U, 0.0, Hw - 1.0), Y = FMath::Clamp(V, 0.0, Hh - 1.0);
			const int32 X0 = int32(X), Y0 = int32(Y), X1 = FMath::Min(X0 + 1, Hw - 1), Y1 = FMath::Min(Y0 + 1, Hh - 1);
			const double Fx = X - X0, Fy = Y - Y0;
			auto H = [&](int32 Xi, int32 Yi) { return Lo + G16[int64(Yi) * Hw + Xi] / 65535.0 * (Hi - Lo); };
			return FMath::Lerp(FMath::Lerp(H(X0, Y0), H(X1, Y0), Fx), FMath::Lerp(H(X0, Y1), H(X1, Y1), Fx), Fy);
		};
		TArray<double> Hs;
		Hs.SetNumUninitialized(Size * Size);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				Hs[Y * Size + X] = Sample((X + 0.5) * Hw / Size - 0.5, (Y + 0.5) * Hh / Size - 0.5);
			}
		}
		const double Step = Wm / Size;
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const double H = Hs[Y * Size + X];
				const double Gx = (Hs[Y * Size + FMath::Min(X + 1, Size - 1)] - Hs[Y * Size + FMath::Max(X - 1, 0)]) / (Step * ((X > 0 && X < Size - 1) ? 2 : 1));
				const double Gy = (Hs[FMath::Min(Y + 1, Size - 1) * Size + X] - Hs[FMath::Max(Y - 1, 0) * Size + X]) / (Step * ((Y > 0 && Y < Size - 1) ? 2 : 1));
				const double Shade = FMath::Clamp(0.75 + (Gx * -0.5 + Gy * 0.5) * 0.6, 0.3, 1.0);
				const double Land = FMath::Clamp((H - Lo) / (Hi - Lo), 0.0, 1.0);
				Cv.Set(X, Y, H <= Sea + 0.3 ? FColor(150, 190, 215)
					: FColor(uint8((200 + 30 * Land) * Shade), uint8((205 + 20 * Land) * Shade), uint8((180 + 10 * Land) * Shade)));
			}
		}
		Cv.Text(Size + 20, Size - 30, TEXT("outside the playable map"), FColor(90, 90, 90));

		auto Polyline = [&](const TArray<FVector2D>& P, FColor C, int32 W)
		{
			for (int32 i = 0; i + 1 < P.Num(); ++i) { Cv.Line(P[i].X, P[i].Y, P[i + 1].X, P[i + 1].Y, W, C); }
		};
		auto Points = [&](const FJsonPtr& List)
		{
			TArray<FVector2D> P;
			for (const FJsonPtr& I : List->Items) { P.Add(PxOf(I)); }
			return P;
		};

		// Water: the river centreline as a band.
		for (const FJsonPtr& B : Lay->Get(TEXT("water_network"))->Get(TEXT("bodies"))->Items)
		{
			if (B->Get(TEXT("id"))->Str == TEXT("FiumeArdo"))
			{
				Polyline(Points(B->Get(TEXT("centerline"))), FColor(110, 160, 200), FMath::Max(2, int32(50 * Scale)));
				break;
			}
		}

		static const TArray<TPair<FString, FColor>> Profiles = {
			{ TEXT("autostrada"), FColor(40, 140, 60) }, { TEXT("viale"), FColor(200, 60, 50) }, { TEXT("lungomare"), FColor(230, 110, 40) },
			{ TEXT("urbana_quartiere"), FColor(90, 90, 95) }, { TEXT("urbana_locale"), FColor(140, 140, 145) }, { TEXT("centro_storico"), FColor(150, 90, 40) },
			{ TEXT("vicolo"), FColor(120, 70, 30) }, { TEXT("costiera"), FColor(40, 90, 170) }, { TEXT("montana_tornanti"), FColor(120, 60, 150) },
			{ TEXT("locale_f2"), FColor(70, 120, 200) }, { TEXT("strada_bianca"), FColor(235, 230, 210) }, { TEXT("banchina_porto"), FColor(60, 60, 60) } };
		auto ProfileColour = [](const FString& P)
		{
			for (const auto& Kv : Profiles) { if (Kv.Key == P) { return Kv.Value; } }
			return FColor(0, 0, 0);
		};
		static const TArray<TPair<FString, FColor>> JunctionColours = {
			{ TEXT("roundabout"), FColor(40, 140, 60) }, { TEXT("signalized"), FColor(220, 30, 30) }, { TEXT("ztl_gate"), FColor(255, 200, 0) },
			{ TEXT("piazza"), FColor(255, 255, 255) }, { TEXT("bridge_head"), FColor(30, 30, 30) } };

		// Roads by profile; ZTL outlined in yellow; tram in magenta.
		const FJsonPtr Roads = Lay->Get(TEXT("roads"));
		const TArray<FJsonPtr>& Nodes = Roads->Get(TEXT("nodes"))->Items;
		auto NodePx = [&](const FJsonPtr& S, const TCHAR* K) { return PxOf(Nodes[int32(Nm(S, K))]); };
		auto Truthy = [](const FJsonPtr& S, const TCHAR* K)
		{
			const FJsonPtr V = S->Get(K);
			return V.IsValid() && V->Kind != FJson::EKind::Null && !(V->Kind == FJson::EKind::Bool && !V->Bool) && !(V->Kind == FJson::EKind::String && V->Str.IsEmpty());
		};
		for (const FJsonPtr& S : Roads->Get(TEXT("segments"))->Items)
		{
			const FVector2D A = NodePx(S, TEXT("a")), B = NodePx(S, TEXT("b"));
			const int32 Wpx = FMath::Max(2, int32(Nm(S, TEXT("width")) * Scale * 1.6));
			if (Truthy(S, TEXT("ztl"))) { Cv.Line(A.X, A.Y, B.X, B.Y, Wpx + 3, FColor(255, 210, 0)); }
			Cv.Line(A.X, A.Y, B.X, B.Y, Wpx, ProfileColour(S->Get(TEXT("profile"))->Str));
		}
		for (const FJsonPtr& S : Roads->Get(TEXT("segments"))->Items)
		{
			if (Truthy(S, TEXT("tram")))
			{
				const FVector2D A = NodePx(S, TEXT("a")), B = NodePx(S, TEXT("b"));
				Cv.Line(A.X, A.Y, B.X, B.Y, 3, FColor(220, 0, 160));
			}
		}
		for (const FJsonPtr& Jn : Roads->Get(TEXT("junctions"))->Items)
		{
			const FString Type = Jn->Get(TEXT("type"))->Str;
			for (const auto& Kv : JunctionColours)
			{
				if (Kv.Key == Type)
				{
					const FVector2D P = PxOf(Nodes[int32(Nm(Jn, TEXT("node")))]);
					const double R = (Type == TEXT("roundabout") || Type == TEXT("piazza")) ? 6 : 4;
					Cv.Ellipse(P.X - R, P.Y - R, P.X + R, P.Y + R, Kv.Value, FColor(0, 0, 0));
				}
			}
		}

		// Proposed (dashed).
		auto Dashed = [&](const TArray<FVector2D>& Pts, FColor C, int32 Wpx, double Dash = 14, double Gap = 8)
		{
			for (int32 i = 0; i + 1 < Pts.Num(); ++i)
			{
				const FVector2D P0 = Pts[i], P1 = Pts[i + 1];
				const double Seg = FVector2D::Distance(P0, P1);
				for (double T = 0.0; T < Seg; )
				{
					const double T1 = FMath::Min(Seg, T + Dash);
					const FVector2D A = P0 + (P1 - P0) * (T / Seg), B = P0 + (P1 - P0) * (T1 / Seg);
					Cv.Line(A.X, A.Y, B.X, B.Y, Wpx, C);
					T = T1 + Gap;
				}
			}
		};
		const FJsonPtr Auto = Prop->Get(TEXT("autostrada"));
		Dashed(Points(Auto->Get(TEXT("points"))), ProfileColour(TEXT("autostrada")), 7);
		Dashed(Points(Auto->Get(TEXT("link"))), ProfileColour(TEXT("autostrada")), 4);
		for (const TPair<const TCHAR*, const TCHAR*>& K : { TPair<const TCHAR*, const TCHAR*>(TEXT("toll_plaza"), TEXT("Casello")), TPair<const TCHAR*, const TCHAR*>(TEXT("interchange"), TEXT("trumpet")) })
		{
			const FVector2D P = PxOf(Auto->Get(K.Key)->Get(TEXT("at")));
			Cv.Rect(P.X - 7, P.Y - 7, P.X + 7, P.Y + 7, 3, FColor(40, 140, 60));
			Cv.Text(int32(P.X + 9), int32(P.Y - 6), K.Value, FColor(0, 60, 0));
		}
		for (const FJsonPtr& Line : Prop->Get(TEXT("rail"))->Items)
		{
			if (!Line->Get(TEXT("points")).IsValid())
			{
				continue;
			}
			Dashed(Points(Line->Get(TEXT("points"))), FColor(30, 30, 30), 4, 10, 6);
			Dashed(Points(Line->Get(TEXT("tunnel"))), FColor(120, 120, 120), 4, 4, 6);
			for (const FJsonPtr& St : Line->Get(TEXT("stations"))->Items)
			{
				const FVector2D P = PxOf(St->Get(TEXT("at")));
				Cv.FillRect(P.X - 6, P.Y - 4, P.X + 6, P.Y + 4, FColor(30, 30, 30));
				Cv.Text(int32(P.X + 8), int32(P.Y - 12), St->Get(TEXT("name"))->Str, FColor(0, 0, 0));
			}
		}
		const FJsonPtr Torrent = Prop->Get(TEXT("torrent"));
		Dashed(Points(Torrent->Get(TEXT("points"))), FColor(150, 120, 80), 5);
		Dashed(Points(Torrent->Get(TEXT("culvert_to_sea"))), FColor(150, 120, 80), 2, 4, 8);
		const double Cx = 700.0, Cy = 1230.0, Rx = 400.0, Ry = 290.0;   // walled core (the layout generator's CORE)
		TArray<FVector2D> Ring, Wall;
		for (int32 k = 0; k <= 48; ++k) { Ring.Add(Px(Cx + (Rx + 45) * FMath::Cos(k / 48.0 * UE_TWO_PI), Cy + (Ry + 45) * FMath::Sin(k / 48.0 * UE_TWO_PI))); }
		for (int32 k = 0; k <= 64; ++k) { Wall.Add(Px(Cx + Rx * FMath::Cos(k / 64.0 * UE_TWO_PI), Cy + Ry * FMath::Sin(k / 64.0 * UE_TWO_PI))); }
		Dashed(Ring, ProfileColour(TEXT("viale")), 4);
		Polyline(Wall, FColor(110, 70, 40), 2);

		for (const FJsonPtr& D : Lay->Get(TEXT("districts"))->Items)   // district names
		{
			const FJsonPtr R = D->Get(TEXT("rect"));
			const FVector2D P = Px((Ix(R, 0) + Ix(R, 2)) / 2, (Ix(R, 1) + Ix(R, 3)) / 2);
			Cv.Text(int32(P.X - 30), int32(P.Y), D->Get(TEXT("name"))->Str, FColor(20, 20, 20));
		}

		// Legend.
		int32 Lx = Size + 20, Ly = 20;
		Cv.FillRect(Size + 10, 10, Width - 10, 470, FColor(250, 250, 245));
		Cv.Rect(Size + 10, 10, Width - 10, 470, 1, FColor(0, 0, 0));
		Cv.Text(Lx, Ly, TEXT("MAREVIA NETWORK PLAN (draft, Phase 1)"), FColor(0, 0, 0));
		Ly += 20;
		for (const auto& Kv : Profiles)
		{
			Cv.Line(Lx, Ly + 6, Lx + 30, Ly + 6, 5, Kv.Value);
			Cv.Text(Lx + 38, Ly, Kv.Key, FColor(0, 0, 0));
			Ly += 16;
		}
		const TPair<const TCHAR*, FColor> Notes[] = { { TEXT("ZTL (yellow outline)"), FColor(255, 210, 0) }, { TEXT("tram corridor (planned)"), FColor(220, 0, 160) },
			{ TEXT("dashed = proposed, not built"), FColor(0, 0, 0) }, { TEXT("rail (dashed black, grey = tunnel)"), FColor(30, 30, 30) },
			{ TEXT("walled core / ring viale"), FColor(110, 70, 40) }, { TEXT("torrent / culvert"), FColor(150, 120, 80) } };
		for (const auto& Kv : Notes)
		{
			Cv.Line(Lx, Ly + 6, Lx + 30, Ly + 6, 4, Kv.Value);
			Cv.Text(Lx + 38, Ly, Kv.Key, FColor(0, 0, 0));
			Ly += 16;
		}
		for (const auto& Kv : JunctionColours)
		{
			Cv.Ellipse(Lx + 10, Ly + 2, Lx + 20, Ly + 12, Kv.Value, FColor(0, 0, 0));
			Cv.Text(Lx + 38, Ly, Kv.Key, FColor(0, 0, 0));
			Ly += 16;
		}
		const bool bOk = SavePng(Out, Width, Size, Cv.Px);
		OCEd::Log(TEXT("oc_gen"), FString::Printf(TEXT("network plan -> %s (%dx%d)"), *Out, Width, Size));
		return bOk;
	}
}

UOCGenerateCommandlet::UOCGenerateCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UOCGenerateCommandlet::Main(const FString& Params)
{
	FString What, Out;
	FParse::Value(*Params, TEXT("what="), What);
	FParse::Value(*Params, TEXT("out="), Out);
	const FString Root = OCEd::ProjectDir();
	auto Resolve = [&](const TCHAR* Default) { return Out.IsEmpty() ? Root / Default : (FPaths::IsRelative(Out) ? Root / Out : Out); };
	bool bOk = false;
	if (What == TEXT("lut")) { bOk = MakeLut(Resolve(TEXT("SourceArt/LUT/T_LUT_Marevia.png"))); }
	else if (What == TEXT("bike")) { bOk = MakeBikeChassis(Resolve(TEXT("SourceArt/Vehicles/Bike/SKM_BikeChassis.gltf"))); }
	else if (What == TEXT("plan")) { bOk = MakePlan(Resolve(TEXT("SourceArt/Infra/network_plan.png"))); }
	else { UE_LOG(LogOpenCity, Error, TEXT("usage: -run=OCGenerate -what=lut|bike|plan [-out=<file>]")); }
	if (!bOk)
	{
		UE_LOG(LogOpenCity, Error, TEXT("OCGenerate %s FAILED"), *What);
		return 1;
	}
	UE_LOG(LogOpenCity, Display, TEXT("OCGenerate: wrote %s"), *What);
	return 0;
}
