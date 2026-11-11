// Kit pieces (formerly Tools/blender/make_kit.py). Every piece is assembled from single-material parts
// (boxes, profile prisms, cylinders). Wall slabs get their openings cut by a boolean, so openings are real
// recesses (glass 17.5 cm behind the wall face). Convex edges are bevelled (2 cm / 2 segments on masonry,
// smaller on joinery) except edges on a tiling seam plane (X=0, X=W, Z=0, Z=H, wall back), so adjacent bays
// and storeys butt without a groove. Wall slabs have no end caps at X=0/X=W (no coplanar z-fighting with the
// perpendicular facade at building corners). Frame: X along the facade, outside face at Y=0 facing -Y, Z up.
#include "Kit/OCKitPieces.h"

#include "OCLog.h"

namespace OCKit
{
	const TCHAR* const MatOrder[12] = { TEXT("Wall"), TEXT("Trim"), TEXT("Wood"), TEXT("Glass"), TEXT("Metal"), TEXT("Roof"), TEXT("Concrete"),
		TEXT("PaintRed"), TEXT("PaintWhite"), TEXT("PaintBlack"), TEXT("PaintBlue"), TEXT("Paint") };

	void FPiece::Add(FSoup S, const TCHAR* Mat)
	{
		for (int32 i = 0; i < 12; ++i)
		{
			if (FCString::Strcmp(MatOrder[i], Mat) == 0)
			{
				Parts.Add({ MoveTemp(S), i });
				return;
			}
		}
		UE_LOG(LogOpenCity, Fatal, TEXT("kit: unknown material %s"), Mat);
	}

	namespace
	{
		// ---- metrics (KIT_SPEC / WORLD_METRICS) ----
		constexpr double H = 3.20, T = 0.30;
		constexpr double WinW = 1.10, WinH = 1.50, Sill = 0.90;
		constexpr double FdW = 1.10, FdH = 2.40, DoorW = 1.10, DoorH = 2.20, ShopW = 3.40, ShopH = 2.60;
		constexpr double Recess = 0.22, GlassY = 0.175, BandH = 0.16, BandD = 0.03;
		constexpr double RwH = 2.0, RwT = 0.50;   // retaining wall panel: 2 m rows, 0.5 m thick
		using V2 = FVector2d;
		using V3 = FVector3d;

		FSoup Bev(FSoup S, double W, int32 Segs = 2, const TArray<FKeep>& Keep = {})
		{
			Bevel(S, W, Segs, Keep);
			return S;
		}

		/** A single quad facing -Y (glass planes). */
		void AddQuadY(FSoup& S, double X0, double X1, double Y, double Z0, double Z1)
		{
			const int32 Sh = S.NewShell();
			const int32 a = S.Add(V3(X0, Y, Z0)), b = S.Add(V3(X1, Y, Z0)), c = S.Add(V3(X1, Y, Z1)), d = S.Add(V3(X0, Y, Z1));
			S.Face(Sh, { a, b, c, d });
		}

		struct FCut { double X0, X1, Zb, Zt, Depth; };

		// ---- shared building elements ----

		void WallSlab(FPiece& P, double W, const TArray<FCut>& Cuts = {}, double Z0 = 0.0, double Z1 = H, double Thick = T, const TCHAR* Mat = TEXT("Wall"))
		{
			FSoup S = Box(0, W, 0, Thick, Z0, Z1);
			if (!Cuts.IsEmpty())
			{
				FSoup Cb;
				for (const FCut& C : Cuts) { AddBox(Cb, C.X0, C.X1, -0.10, C.Depth, C.Zb > Z0 + Eps ? C.Zb : Z0 - 0.10, C.Zt); }
				S = BoolDiff(S, Cb);
			}
			DelPlane(S, AX, 0.0);
			DelPlane(S, AX, W);
			Bevel(S, 0.02, 2, { { AX, 0.0 }, { AX, W }, { AZ, Z0 }, { AZ, Z1 }, { AY, Thick } });
			P.Add(MoveTemp(S), Mat);
		}

		void RenderBand(FPiece& P, double X0, double X1, bool bKeepEnds = true)
		{
			FSoup S = Box(X0, X1, -BandD, 0.0, 0.0, BandH);
			DelPlane(S, AY, 0.0);
			if (bKeepEnds) { DelPlane(S, AX, X0); DelPlane(S, AX, X1); }
			Bevel(S, 0.012, 2, { { AZ, 0.0 }, { AX, X0 }, { AX, X1 } });
			P.Add(MoveTemp(S), TEXT("Wall"));
		}

		const TArray<V2> PlinthProfile = { V2(-0.06, -3.0), V2(0.30, -3.0), V2(0.30, 0.60), V2(-0.045, 0.60), V2(-0.085, 0.565), V2(-0.085, 0.495), V2(-0.06, 0.47) };

		/** Stone base X x0..x1, Z -3.0..0.6, with sawn joints every JointStep (half joints at seams). */
		void PlinthBlock(FPiece& P, double X0, double X1, const TArray<double>& Seams, double JointStep = 1.0, double JointOrigin = 0.0)
		{
			FSoup S = PrismX(PlinthProfile, X0, X1);
			// Bevelled before the joints are cut (see MakeRetainingWall): the joint lips stay crisp.
			TArray<FKeep> Keep;
			for (double Sx : Seams) { Keep.Add({ AX, Sx }); }
			Keep.Add({ AZ, -3.0 });
			Keep.Add({ AY, 0.30 });
			Bevel(S, 0.012, 2, Keep);
			TArray<double> Xs;
			for (int32 k = FMath::CeilToInt((X0 - JointOrigin) / JointStep - Eps); JointOrigin + k * JointStep <= X1 + Eps; ++k) { Xs.Add(JointOrigin + k * JointStep); }
			if (!Xs.IsEmpty())
			{
				FSoup Cb;
				for (double Xj : Xs) { AddBox(Cb, Xj - 0.008, Xj + 0.008, -0.2, -0.047, -3.1, 0.7); }
				S = BoolDiff(S, Cb);
			}
			P.Add(MoveTemp(S), TEXT("Trim"));
		}

		void SillPart(FPiece& P, double Ox0, double Ox1, double Ztop, double Over = 0.12, double Proj = 0.08, double Back = 0.16, double Th = 0.075)
		{
			P.Add(Bev(Box(Ox0 - Over, Ox1 + Over, -Proj, Back, Ztop - Th, Ztop), 0.015, 2), TEXT("Trim"));
			// drip lip under the projecting part
			P.Add(Bev(Box(Ox0 - Over + 0.02, Ox1 + Over - 0.02, -Proj + 0.01, 0.0, Ztop - Th - 0.025, Ztop - Th + 0.002), 0.006, 1, { { AY, 0.0 } }), TEXT("Trim"));
		}

		/** Stone architrave: two jambs + head + small cap moulding. Back face on the wall face. Returns its top. */
		double Surround(FPiece& P, double Ox0, double Ox1, double Zb, double Zt, double Sw = 0.10, double Proj = 0.03, double HeadH = 0.20, bool bCap = true, bool bKeystone = false)
		{
			for (const V2 A : { V2(Ox0 - Sw, Ox0), V2(Ox1, Ox1 + Sw) })
			{
				P.Add(Bev(Box(A.X, A.Y, -Proj, 0.0, Zb, Zt), 0.01, 2, { { AY, 0.0 } }), TEXT("Trim"));
			}
			P.Add(Bev(Box(Ox0 - Sw, Ox1 + Sw, -Proj - 0.005, 0.0, Zt, Zt + HeadH), 0.012, 2, { { AY, 0.0 } }), TEXT("Trim"));
			const double Top = Zt + HeadH;
			if (bKeystone)
			{
				const double Cx = (Ox0 + Ox1) / 2;
				P.Add(Bev(Box(Cx - 0.08, Cx + 0.08, -Proj - 0.025, 0.0, Zt - 0.03, Top + 0.02), 0.01, 2, { { AY, 0.0 } }), TEXT("Trim"));
			}
			if (bCap)
			{
				const TArray<V2> Prof = { V2(0.0, Top), V2(-Proj - 0.035, Top), V2(-Proj - 0.05, Top + 0.02), V2(-Proj - 0.05, Top + 0.045),
					V2(-Proj - 0.035, Top + 0.06), V2(0.0, Top + 0.06) };
				FSoup S = PrismX(Prof, Ox0 - Sw - 0.04, Ox1 + Sw + 0.04);
				DelPlane(S, AY, 0.0);
				Bevel(S, 0.008, 2);
				P.Add(MoveTemp(S), TEXT("Trim"));
				return Top + 0.06;
			}
			return Top;
		}

		/** Fixed frame + casement leaves with glazing bars + one glass plane at GlassY. PanelH > 0: French doors. */
		void Casement(FPiece& P, double Ox0, double Ox1, double Zb, double Zt, int32 Leaves = 2, int32 Bars = 2, double PanelH = 0.0, double Fw = 0.06,
			double Y0 = 0.145, double Y1 = 0.205)
		{
			// outer fixed frame, embedded 1 cm into the reveal
			for (FSoup S : { Box(Ox0 - 0.01, Ox0 + Fw, Y0, Y1, Zb, Zt + 0.01), Box(Ox1 - Fw, Ox1 + 0.01, Y0, Y1, Zb, Zt + 0.01),
				Box(Ox0 + Fw - 0.002, Ox1 - Fw + 0.002, Y0, Y1, Zt - Fw, Zt + 0.01), Box(Ox0 + Fw - 0.002, Ox1 - Fw + 0.002, Y0 - 0.01, Y1, Zb, Zb + Fw) })
			{
				P.Add(Bev(S, 0.006, 1, { { AY, Y1 } }), TEXT("Wood"));
			}
			const double Ix0 = Ox0 + Fw, Ix1 = Ox1 - Fw, Iz0 = Zb + Fw, Iz1 = Zt - Fw;
			const double Lw = (Ix1 - Ix0) / Leaves, Lf = 0.05;
			const double Ly0 = Y0 + 0.01, Ly1 = Y1 - 0.01;
			for (int32 i = 0; i < Leaves; ++i)
			{
				const double A0 = Ix0 + i * Lw, A1 = Ix0 + (i + 1) * Lw;
				P.Add(Box(A0, A0 + Lf, Ly0, Ly1, Iz0, Iz1), TEXT("Wood"));
				P.Add(Box(A1 - Lf, A1, Ly0, Ly1, Iz0, Iz1), TEXT("Wood"));
				P.Add(Box(A0 + Lf - 0.002, A1 - Lf + 0.002, Ly0, Ly1, Iz1 - Lf, Iz1), TEXT("Wood"));
				P.Add(Box(A0 + Lf - 0.002, A1 - Lf + 0.002, Ly0, Ly1, Iz0, Iz0 + Lf + 0.02), TEXT("Wood"));
				double Gz0 = Iz0 + Lf + 0.02;
				if (PanelH > 0)
				{
					P.Add(Box(A0 + Lf - 0.002, A1 - Lf + 0.002, Ly0, Ly1, Gz0 + PanelH - 0.05, Gz0 + PanelH), TEXT("Wood"));
					// raised field on the lower panel
					P.Add(Box(A0 + Lf - 0.002, A1 - Lf + 0.002, Ly0 + 0.012, Ly1 - 0.012, Gz0 - 0.002, Gz0 + PanelH - 0.048), TEXT("Wood"));
					P.Add(Bev(Box(A0 + Lf + 0.04, A1 - Lf - 0.04, Ly0 + 0.002, Ly1 - 0.02, Gz0 + 0.05, Gz0 + PanelH - 0.10), 0.012, 1), TEXT("Wood"));
					Gz0 += PanelH;
				}
				const double Gz1 = Iz1 - Lf;   // glazing bars
				for (int32 k = 1; k <= Bars; ++k)
				{
					const double Zc = Gz0 + (Gz1 - Gz0) * k / (Bars + 1);
					P.Add(Box(A0 + Lf - 0.002, A1 - Lf + 0.002, Ly0 + 0.008, Ly1 - 0.008, Zc - 0.011, Zc + 0.011), TEXT("Wood"));
				}
			}
			FSoup G;   // glass plane (single quad facing -Y)
			AddQuadY(G, Ix0, Ix1, GlassY, Iz0, Iz1);
			P.Add(MoveTemp(G), TEXT("Glass"));
		}

		/** Louvred shutter leaf in the XZ plane, back face at y = Yb, front at Yb - Tk. */
		void LouvredLeaf(FPiece& P, double Xa, double Xb, double Z0, double Z1, double Yb, double Tk = 0.03, double Pitch = 0.062)
		{
			const double Yf = Yb - Tk, Sw = 0.045;
			P.Add(Bev(Box(Xa, Xa + Sw, Yf, Yb, Z0, Z1), 0.004, 1, { { AY, Yb } }), TEXT("Wood"));
			P.Add(Bev(Box(Xb - Sw, Xb, Yf, Yb, Z0, Z1), 0.004, 1, { { AY, Yb } }), TEXT("Wood"));
			TArray<V2> Rails = { V2(Z0, Z0 + 0.10), V2(Z1 - 0.07, Z1) };
			if (Z1 - Z0 > 1.8)
			{
				const double Zm = Z0 + (Z1 - Z0) * 0.42;
				Rails.Add(V2(Zm - 0.035, Zm + 0.035));
			}
			Rails.Sort([](const V2& A, const V2& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
			for (const V2& R : Rails)
			{
				P.Add(Bev(Box(Xa + Sw - 0.002, Xb - Sw + 0.002, Yf + 0.002, Yb, R.X, R.Y), 0.004, 1, { { AY, Yb } }), TEXT("Wood"));
			}
			// louvres between rails: thin blades at 45 deg, front edge low (sheds rain outward)
			const double Yc = (Yf + Yb) / 2, L = 0.0185, Th = 0.0065;
			const V2 D = V2(1, 1).GetSafeNormal(), N = V2(-1, 1).GetSafeNormal();   // (y, z)
			for (int32 r = 0; r + 1 < Rails.Num(); ++r)
			{
				const double Zlo = Rails[r].Y, Zhi = Rails[r + 1].X;
				const int32 Count = FMath::Max(1, int32((Zhi - Zlo) / Pitch));
				const double Step = (Zhi - Zlo) / Count;
				FSoup S;
				for (int32 k = 0; k < Count; ++k)
				{
					const V2 C(Yc, Zlo + Step * (k + 0.5));
					AddPrism(S, { C - D * L - N * Th / 2, C + D * L - N * Th / 2, C + D * L + N * Th / 2, C - D * L + N * Th / 2 }, AX, Xa + Sw - 0.004, Xb - Sw + 0.004, false);
				}
				P.Add(MoveTemp(S), TEXT("Wood"));
			}
		}

		/** Three-leaf folding shutter folded flat against the wall: two leaves under a louvred outer leaf. */
		void ShutterStack(FPiece& P, double Xa, double Xb, double Z0, double Z1, double YBack, double HingeX)
		{
			P.Add(Bev(Box(Xa + 0.006, Xb - 0.006, YBack - 0.052, YBack, Z0 + 0.004, Z1 - 0.004), 0.004, 1, { { AY, YBack } }), TEXT("Wood"));
			LouvredLeaf(P, Xa, Xb, Z0, Z1, YBack - 0.052, 0.032);
			FSoup K;
			for (const double Zz : { Z0 + 0.18, Z1 - 0.22 }) { AddCyl(K, V3(HingeX, YBack - 0.045, Zz), 0.011, 0.07, 6, 'Z', false); }
			P.Add(MoveTemp(K), TEXT("Metal"));
			// hold-back catch on the wall beside the free edge
			const double FreeX = HingeX > (Xa + Xb) / 2 ? Xa - 0.012 : Xb + 0.012;
			P.Add(Box(FreeX - 0.006, FreeX + 0.006, YBack - 0.045, YBack + 0.01, Z0 + 0.64, Z0 + 0.68), TEXT("Metal"));
		}

		/** One 1.10 x 1.50 window centred on Cx (the caller cuts the recess). Kind: shutters | plain | grille. */
		FCut WindowUnit(FPiece& P, double Cx, const TCHAR* Kind, double Zb = Sill)
		{
			const double Ox0 = Cx - WinW / 2, Ox1 = Cx + WinW / 2, Zt = Zb + WinH;
			SillPart(P, Ox0, Ox1, Zb + 0.02);
			Surround(P, Ox0, Ox1, Zb + 0.02, Zt);
			Casement(P, Ox0, Ox1, Zb + 0.015, Zt);
			if (FCString::Strcmp(Kind, TEXT("shutters")) == 0)
			{
				const double Sw = 0.195;
				ShutterStack(P, Ox0 - Sw, Ox0 - 0.005, Zb + 0.02, Zt + 0.02, -0.03, Ox0 - 0.005);
				ShutterStack(P, Ox1 + 0.005, Ox1 + Sw, Zb + 0.02, Zt + 0.02, -0.03, Ox1 + 0.005);
			}
			else if (FCString::Strcmp(Kind, TEXT("grille")) == 0)
			{
				FSoup G;
				for (int32 k = 0; k < 10; ++k)
				{
					const double Xx = Ox0 + (Ox1 - Ox0) * (k + 0.5) / 10;
					AddBox(G, Xx - 0.009, Xx + 0.009, 0.03, 0.048, Zb + 0.015, Zt + 0.012);
				}
				for (const double Zz : { Zb + 0.22, Zb + WinH / 2, Zt - 0.22 }) { AddBox(G, Ox0 - 0.01, Ox1 + 0.01, 0.034, 0.044, Zz - 0.02, Zz + 0.02); }
				P.Add(MoveTemp(G), TEXT("Metal"));
			}
			return { Ox0, Ox1, Zb, Zt, Recess };
		}

		FString Cm(double W) { return FString::FromInt(FMath::RoundToInt(W * 100)); }

		// ---- facade pieces ----

		FPiece MakeWall(double W)
		{
			FPiece P;
			P.Name = TEXT("Wall_") + Cm(W);
			WallSlab(P, W);
			RenderBand(P, 0, W);
			P.Box(0, W, 0, T, 0, H);
			return P;
		}

		FPiece MakeWindow(double W, const TCHAR* Kind)
		{
			FPiece P;
			P.Name = (FCString::Strcmp(Kind, TEXT("shutters")) == 0 ? TEXT("Window_") : TEXT("WindowPlain_")) + Cm(W);
			TArray<FCut> Cuts;
			for (const double C : (W < 3 ? TArray<double>{ 1.0 } : TArray<double>{ 1.0, 3.0 })) { Cuts.Add(WindowUnit(P, C, Kind)); }
			WallSlab(P, W, Cuts);
			RenderBand(P, 0, W);
			P.Box(0, W, 0, T, 0, H);
			return P;
		}

		FPiece MakeGroundWindow()
		{
			FPiece P;
			P.Name = TEXT("GroundWindow_400");
			const double W = 4.0;
			TArray<FCut> Cuts = { WindowUnit(P, 1.0, TEXT("grille")), WindowUnit(P, 3.0, TEXT("grille")) };
			WallSlab(P, W, Cuts);
			P.Box(0, W, 0, T, 0, H);
			return P;
		}

		/** Square bar along Z without end caps (ends are buried in rails). */
		void AddBar(FSoup& S, double X, double Y, double Z0, double Z1, double Hw)
		{
			AddPrism(S, { V2(X - Hw, Y - Hw), V2(X + Hw, Y - Hw), V2(X + Hw, Y + Hw), V2(X - Hw, Y + Hw) }, AZ, Z0, Z1, false);
		}

		/** Wrought-iron railing: front run at y = Yf from X0..X1, side returns back to the wall. */
		void Railing(FPiece& P, double X0, double X1, double Yf, double Zb, double Ht = 1.0)
		{
			const double Zt = Zb + Ht, Post = 0.03;
			FSoup M;
			for (const V2 Pp : { V2(X0, Yf), V2(X1, Yf) }) { AddBox(M, Pp.X - Post / 2, Pp.X + Post / 2, Pp.Y - Post / 2, Pp.Y + Post / 2, Zb - 0.01, Zt - 0.02); }
			const double Zr0 = Zb + 0.08, Zr1 = Zt - 0.14;
			struct FRun { bool bX; double A, B, C; };
			const FRun Runs[] = { { true, X0, X1, Yf }, { false, Yf, -0.0, X0 }, { false, Yf, -0.0, X1 } };
			for (const FRun& R : Runs)
			{
				for (const V2 Zh : { V2(Zr0, 0.025), V2(Zr1, 0.018) })
				{
					if (R.bX) { AddBox(M, R.A, R.B, R.C - 0.006, R.C + 0.006, Zh.X, Zh.X + Zh.Y); }
					else { AddBox(M, R.C - 0.006, R.C + 0.006, R.A, R.B + 0.01, Zh.X, Zh.X + Zh.Y); }
				}
			}
			const double Sp = 0.115;   // balusters + cross band
			for (const FRun& R : Runs)
			{
				const double L = FMath::Abs(R.B - R.A);
				const int32 N = FMath::Max(1, int32(FMath::RoundHalfToEven(L / Sp)));
				const double Step = (R.B - R.A) / N;
				for (int32 k = 1; k < N; ++k)
				{
					const double Pk = R.A + Step * k;
					if (R.bX) { AddBar(M, Pk, R.C, Zr0, Zt - 0.03, 0.007); }
					else { AddBar(M, R.C, Pk, Zr0, Zt - 0.03, 0.007); }
				}
				// X crosses in the band between the upper rail and the handrail
				const double Zc0 = Zr1 + 0.018, Zc1 = Zt - 0.03;
				for (int32 k = 0; k < N; ++k)
				{
					const double P0 = R.A + Step * k, P1 = R.A + Step * (k + 1);
					for (const V2 Ss : { V2(P0, P1), V2(P1, P0) })
					{
						const double Len = FMath::Sqrt(FMath::Square(Ss.Y - Ss.X) + FMath::Square(Zc1 - Zc0));
						FSoup Bar;
						AddBar(Bar, 0.0, 0.0, 0.0, Len, 0.005);
						const double Ang = FMath::Atan2(Ss.Y - Ss.X, Zc1 - Zc0);
						const FM4 Rot = R.bX ? FM4::Rotation(Ang, AY) : FM4::Rotation(-Ang, AX);
						const V3 Pos = R.bX ? V3(Ss.X, R.C, Zc0) : V3(R.C, Ss.X, Zc0);
						Transform(Bar, FM4::Translation(Pos) * Rot);
						Append(M, Bar);
					}
				}
			}
			P.Add(MoveTemp(M), TEXT("Metal"));
			// handrail (moulded flat bar), separate so it can be bevelled
			for (const FRun& R : Runs)
			{
				FSoup Hb = R.bX ? Box(R.A - 0.025, R.B + 0.025, R.C - 0.025, R.C + 0.025, Zt - 0.03, Zt)
					: Box(R.C - 0.025, R.C + 0.025, R.A < R.B ? R.A - 0.025 : R.B, 0.02, Zt - 0.03, Zt);
				P.Add(Bev(Hb, 0.008, 1), TEXT("Metal"));
			}
		}

		FPiece MakeBalcony()
		{
			FPiece P;
			P.Name = TEXT("Balcony_400");
			const double W = 4.0, Cx = 2.0;
			const double Ox0 = Cx - FdW / 2, Ox1 = Cx + FdW / 2;
			const double Zs = 0.17;   // slab top = door threshold level
			const double Zb = Zs + 0.02, Zt = Zs + 0.02 + FdH - 0.02;
			const double Sx0 = 0.80, Sx1 = 3.20, Sd = 0.80;   // slab (concrete/stone), moulded edge, above Z=0
			P.Add(Bev(Box(Sx0, Sx1, -Sd, 0.01, 0.035, Zs), 0.02, 2, { { AY, 0.01 } }), TEXT("Concrete"));
			P.Add(Bev(Box(Sx0 + 0.05, Sx1 - 0.05, -Sd + 0.05, 0.01, 0.0, 0.045), 0.015, 2, { { AY, 0.01 } }), TEXT("Concrete"));
			P.Add(Bev(Box(Ox0 - 0.02, Ox1 + 0.02, -0.02, 0.17, Zs - 0.02, Zb), 0.008, 1), TEXT("Trim"));   // threshold
			Surround(P, Ox0, Ox1, Zb, Zt + 0.02, 0.10, 0.03, 0.20, true, true);
			Casement(P, Ox0, Ox1, Zb - 0.005, Zt + 0.02, 2, 2, 0.55);
			const double Sw = 0.195;
			ShutterStack(P, Ox0 - Sw, Ox0 - 0.005, Zb, Zt + 0.04, -0.03, Ox0 - 0.005);
			ShutterStack(P, Ox1 + 0.005, Ox1 + Sw, Zb, Zt + 0.04, -0.03, Ox1 + 0.005);
			Railing(P, Sx0 + 0.04, Sx1 - 0.04, -Sd + 0.04, Zs, 1.0);
			WallSlab(P, W, { { Ox0, Ox1, Zs - 0.02, Zt + 0.02, Recess } });
			RenderBand(P, 0, W);
			P.Box(0, W, 0, T, 0, H);
			return P;
		}

		FPiece MakeDoor()
		{
			FPiece P;
			P.Name = TEXT("Door_200");
			const double W = 2.0, Cx = 1.0;
			const double Ox0 = Cx - DoorW / 2, Ox1 = Cx + DoorW / 2;
			const double StepH = 0.17, Zb = StepH, Zt = StepH + DoorH, Sw = 0.18;
			// step + below-ground skirt (so slopes never show a gap)
			P.Add(Bev(Box(Ox0 - Sw, Ox1 + Sw, -0.32, 0.17, 0.0, StepH), 0.02, 2, { { AZ, 0.0 } }), TEXT("Trim"));
			P.Add(Box(Ox0 - Sw, Ox1 + Sw, -0.30, T, -3.0, 0.0), TEXT("Trim"));
			PlinthBlock(P, 0.0, Ox0 - Sw, { 0.0 });   // plinth stubs either side of the surround
			PlinthBlock(P, Ox1 + Sw, W, { W });
			Surround(P, Ox0, Ox1, Zb, Zt, Sw, 0.05, 0.24, true, true);
			const double Fw = 0.07;
			for (FSoup S : { Box(Ox0 - 0.01, Ox0 + Fw, 0.13, 0.21, Zb, Zt + 0.01), Box(Ox1 - Fw, Ox1 + 0.01, 0.13, 0.21, Zb, Zt + 0.01),
				Box(Ox0 + Fw - 0.002, Ox1 - Fw + 0.002, 0.13, 0.21, Zt - Fw, Zt + 0.01) })
			{
				P.Add(Bev(S, 0.006, 1, { { AY, 0.21 } }), TEXT("Wood"));
			}
			// two panelled leaves
			const double Lx0 = Ox0 + Fw, Lx1 = Ox1 - Fw, Lz0 = Zb + 0.005, Lz1 = Zt - Fw;
			const double Mid = (Lx0 + Lx1) / 2;
			for (const V2 A : { V2(Lx0, Mid - 0.003), V2(Mid + 0.003, Lx1) })
			{
				P.Add(Bev(Box(A.X, A.Y, 0.15, 0.20, Lz0, Lz1), 0.006, 1, { { AY, 0.20 } }), TEXT("Wood"));
				const double Sw2 = 0.085;   // three raised fields: bottom, tall middle, top
				for (const V2 F : { V2(Lz0 + 0.12, Lz0 + 0.55), V2(Lz0 + 0.67, Lz1 - 0.42), V2(Lz1 - 0.32, Lz1 - 0.10) })
				{
					P.Add(Bev(Box(A.X + Sw2, A.Y - Sw2, 0.138, 0.151, F.X, F.Y), 0.006, 1, { { AY, 0.151 } }), TEXT("Wood"));
					P.Add(Bev(Box(A.X + Sw2 + 0.035, A.Y - Sw2 - 0.035, 0.130, 0.140, F.X + 0.035, F.Y - 0.035), 0.012, 1, { { AY, 0.140 } }), TEXT("Wood"));
				}
			}
			P.Add(Bev(Box(Mid - 0.018, Mid + 0.018, 0.138, 0.152, Lz0, Lz1), 0.006, 1, { { AY, 0.152 } }), TEXT("Wood"));   // meeting-stile astragal
			FSoup M;   // ironmongery: knob, keyhole plate, knocker
			AddCyl(M, V3(Mid + 0.07, 0.15, Zb + 1.02), 0.025, 0.05, 10, 'Y', true, 0.025);
			AddBox(M, Mid - 0.06, Mid - 0.035, 0.143, 0.15, Zb + 0.95, Zb + 1.07);
			AddCyl(M, V3(Mid, 0.13, Zb + 1.45), 0.035, 0.02, 10, 'Y');
			P.Add(MoveTemp(M), TEXT("Metal"));
			P.Add(Bev(Box(Ox1 + Sw + 0.06, Ox1 + Sw + 0.20, -0.012, 0.0, Zb + 1.65, Zb + 1.77), 0.004, 1, { { AY, 0.0 } }), TEXT("Trim"));   // house number plate
			WallSlab(P, W, { { Ox0, Ox1, Zb - 0.02, Zt, Recess } });
			P.Box(0, W, 0, T, 0, H);
			return P;
		}

		FPiece MakeShopfront()
		{
			FPiece P;
			P.Name = TEXT("Shopfront_400");
			const double W = 4.0;
			const double Ox0 = (W - ShopW) / 2, Ox1 = (W + ShopW) / 2;   // 0.30 .. 3.70
			const double Zt = ShopH, Jw = 0.10;
			for (const V2 A : { V2(Ox0 - Jw, Ox0), V2(Ox1, Ox1 + Jw) })   // stone frame: jambs + lintel
			{
				P.Add(Bev(Box(A.X, A.Y, -0.045, 0.0, 0.0, Zt + 0.10), 0.012, 2, { { AY, 0.0 }, { AZ, 0.0 } }), TEXT("Trim"));
			}
			P.Add(Bev(Box(Ox0 - Jw, Ox1 + Jw, -0.05, 0.0, Zt, Zt + 0.10), 0.012, 2, { { AY, 0.0 } }), TEXT("Trim"));
			PlinthBlock(P, 0.0, Ox0 - Jw, { 0.0 });   // plinth stubs + skirt below the opening
			PlinthBlock(P, Ox1 + Jw, W, { W });
			P.Add(Box(Ox0 - Jw, Ox1 + Jw, -0.045, T, -3.0, 0.0), TEXT("Trim"));
			P.Add(Bev(Box(Ox0, Ox1, -0.03, Recess + 0.02, 0.0, 0.035), 0.01, 1, { { AZ, 0.0 } }), TEXT("Trim"));   // threshold
			// sign board (blank, painted wood) with raised border and a small cap
			const double Sz0 = Zt + 0.10, Sz1 = 3.12, Sx0 = Ox0, Sx1 = Ox1;
			P.Add(Box(Sx0, Sx1, -0.04, 0.0, Sz0, Sz1), TEXT("Wood"));
			for (FSoup B : { Box(Sx0, Sx0 + 0.05, -0.06, 0.0, Sz0, Sz1), Box(Sx1 - 0.05, Sx1, -0.06, 0.0, Sz0, Sz1),
				Box(Sx0 + 0.05 - 0.002, Sx1 - 0.05 + 0.002, -0.06, 0.0, Sz0, Sz0 + 0.05), Box(Sx0 + 0.05 - 0.002, Sx1 - 0.05 + 0.002, -0.06, 0.0, Sz1 - 0.05, Sz1) })
			{
				P.Add(Bev(B, 0.008, 1, { { AY, 0.0 } }), TEXT("Wood"));
			}
			{
				FSoup S = PrismX({ V2(0.0, Sz1), V2(-0.075, Sz1), V2(-0.09, Sz1 + 0.02), V2(-0.09, Sz1 + 0.04), V2(-0.07, Sz1 + 0.06), V2(0.0, Sz1 + 0.06) }, Sx0 - 0.08, Sx1 + 0.08);
				DelPlane(S, AY, 0.0);
				Bevel(S, 0.008, 2);
				P.Add(MoveTemp(S), TEXT("Trim"));
			}
			// shopfront joinery deep in the recess
			const double Fy0 = 0.17, Fy1 = 0.235, Fw = 0.065;
			const double Dx0 = Ox1 - Fw - 1.0, Dx1 = Ox1 - Fw;   // door 1.0 wide
			const double Mx = (Ox0 + Fw + Dx0 - Fw) / 2;
			const TArray<FSoup> Wood = {
				Box(Ox0 - 0.01, Ox0 + Fw, Fy0, Fy1, 0.03, Zt + 0.01), Box(Ox1 - Fw, Ox1 + 0.01, Fy0, Fy1, 0.03, Zt + 0.01),   // posts
				Box(Ox0 + Fw, Ox1 - Fw, Fy0, Fy1, Zt - Fw, Zt + 0.01),                                                       // head
				Box(Dx0 - Fw, Dx0, Fy0, Fy1, 0.03, Zt - Fw),                                                                  // door post
				Box(Ox0 + Fw, Ox1 - Fw, Fy0, Fy1, 2.12, 2.12 + 0.06),                                                         // transom
				Box(Ox0 + Fw, Dx0 - Fw, Fy0, Fy1, 0.48, 0.48 + 0.06),                                                         // stallriser top rail
				Box(Mx - 0.03, Mx + 0.03, Fy0, Fy1, 0.54, 2.12),                                                              // display mullion
				Box(Ox0 + Fw, Dx0 - Fw, Fy0 + 0.02, Fy1, 0.03, 0.48),                                                         // stallriser panel
				Box(Dx0, Dx0 + 0.09, Fy0 + 0.01, Fy1 - 0.01, 0.035, 2.12), Box(Dx1 - 0.09, Dx1, Fy0 + 0.01, Fy1 - 0.01, 0.035, 2.12),   // door stiles
				Box(Dx0 + 0.09, Dx1 - 0.09, Fy0 + 0.01, Fy1 - 0.01, 0.035, 0.30), Box(Dx0 + 0.09, Dx1 - 0.09, Fy0 + 0.01, Fy1 - 0.01, 2.02, 2.12) };   // rails
			for (const FSoup& B : Wood) { P.Add(Bev(B, 0.007, 1), TEXT("Wood")); }
			for (const V2 A : { V2(Ox0 + Fw + 0.08, Mx - 0.10), V2(Mx + 0.10, Dx0 - Fw - 0.08) })   // raised fields on the stallriser
			{
				P.Add(Bev(Box(A.X, A.Y, Fy0 + 0.008, Fy0 + 0.022, 0.12, 0.40), 0.012, 1), TEXT("Wood"));
			}
			FSoup M;   // door handle bar
			AddBox(M, Dx0 + 0.13, Dx0 + 0.15, Fy0 - 0.05, Fy0 - 0.03, 0.85, 1.35);
			for (const double Zz : { 0.88, 1.32 }) { AddBox(M, Dx0 + 0.13, Dx0 + 0.15, Fy0 - 0.03, Fy0 + 0.01, Zz - 0.01, Zz + 0.01); }
			P.Add(MoveTemp(M), TEXT("Metal"));
			FSoup G;   // glass
			AddQuadY(G, Ox0 + Fw, Dx0 - Fw, 0.205, 0.54, 2.12);
			AddQuadY(G, Ox0 + Fw, Ox1 - Fw, 0.205, 2.18, Zt - Fw);
			AddQuadY(G, Dx0 + 0.09, Dx1 - 0.09, 0.205, 0.30, 2.02);
			P.Add(MoveTemp(G), TEXT("Glass"));
			// rolling shutter: guides + half-raised corrugated curtain + bottom bar with handles
			FSoup Guides;
			for (const V2 A : { V2(Ox0 - 0.01, Ox0 + 0.05), V2(Ox1 - 0.05, Ox1 + 0.01) }) { AddBox(Guides, A.X, A.Y, 0.005, 0.065, 0.035, Zt + 0.01); }
			P.Add(MoveTemp(Guides), TEXT("Metal"));
			const double Cz0 = 1.38, Cz1 = Zt + 0.01, Cx0 = Ox0 + 0.02, Cx1 = Ox1 - 0.02, Pitch = 0.07;
			const int32 N = int32((Cz1 - (Cz0 + 0.07)) / Pitch);
			const double Zb2 = Cz0 + 0.07;
			TArray<V2> Prof;   // front zig-zag (small y = outside), back flat
			for (int32 k = 0; k <= N; ++k)
			{
				const double Z = Zb2 + k * Pitch;
				Prof.Add(V2(0.026, Z));
				if (k < N) { Prof.Add(V2(0.016, Z + Pitch * 0.35)); Prof.Add(V2(0.016, Z + Pitch * 0.65)); }
			}
			Prof.Add(V2(0.04, Zb2 + N * Pitch));
			Prof.Add(V2(0.04, Zb2));
			P.Add(PrismX(Prof, Cx0, Cx1), TEXT("Metal"));
			P.Add(Bev(Box(Cx0, Cx1, 0.008, 0.045, Cz0, Zb2 + 0.002), 0.006, 1), TEXT("Metal"));
			FSoup Hd;
			for (const double Hx : { Cx0 + 0.5, Cx1 - 0.5 })
			{
				AddBox(Hd, Hx - 0.06, Hx + 0.06, -0.005, 0.008, Cz0 + 0.02, Cz0 + 0.035);
				AddBox(Hd, Hx - 0.06, Hx - 0.045, -0.005, 0.01, Cz0 + 0.02, Cz0 + 0.05);
				AddBox(Hd, Hx + 0.045, Hx + 0.06, -0.005, 0.01, Cz0 + 0.02, Cz0 + 0.05);
			}
			P.Add(MoveTemp(Hd), TEXT("Metal"));
			WallSlab(P, W, { { Ox0, Ox1, 0.0, Zt, Recess + 0.02 } });
			P.Box(0, W, 0, T, 0, H);
			return P;
		}

		FPiece MakeCorner()
		{
			FPiece P;
			P.Name = TEXT("Corner_320");
			const int32 N = 8;
			const double Ch = H / N;
			for (int32 k = 0; k < N; ++k)
			{
				const double R = k % 2 == 0 ? 0.2 : 0.175;
				P.Add(Bev(Box(-R, 0.2, -R, 0.2, k * Ch, (k + 1) * Ch), 0.02, 2, { { AX, 0.2 }, { AY, 0.2 } }), TEXT("Trim"));
			}
			P.Box(-0.2, 0.2, -0.2, 0.2, 0, H);
			return P;
		}

		FPiece MakePlinth(double W)
		{
			FPiece P;
			P.Name = TEXT("Plinth_") + Cm(W);
			PlinthBlock(P, 0.0, W, { 0.0, W });
			P.Box(0, W, -0.06, T, -3.0, 0.6);
			return P;
		}

		TArray<V2> CorniceOutline()
		{
			// (y, z) from the wall face at the bottom, outward and up, back to the wall at the top
			TArray<V2> Pts = { V2(0.0, 0.0), V2(-0.04, 0.0), V2(-0.04, 0.07), V2(-0.06, 0.075), V2(-0.06, 0.16), V2(-0.12, 0.16) };
			for (int32 i = 1; i < 7; ++i)   // ovolo: from (-0.12, 0.16) to (-0.22, 0.25), centre (-0.12, 0.25)
			{
				const double Tt = (UE_PI / 2) * i / 6;
				Pts.Add(V2(-0.12 - 0.10 * FMath::Sin(Tt), 0.25 - 0.09 * FMath::Cos(Tt)));
			}
			Pts.Append({ V2(-0.275, 0.25), V2(-0.275, 0.243), V2(-0.285, 0.243), V2(-0.285, 0.31), V2(-0.30, 0.315), V2(-0.30, 0.335), V2(-0.285, 0.35), V2(0.0, 0.35) });
			return Pts;
		}

		FPiece MakeCornice(double W)
		{
			FPiece P;
			P.Name = TEXT("Cornice_") + Cm(W);
			TArray<V2> Prof = CorniceOutline();
			Prof.Append({ V2(T, 0.35), V2(T, 0.0) });
			FSoup S = PrismX(Prof, 0.0, W);
			Bevel(S, 0.008, 2, { { AX, 0.0 }, { AX, W }, { AY, T } });
			P.Add(MoveTemp(S), TEXT("Trim"));
			FSoup D;   // dentils every 0.20 m, offset 0.10 so bays continue the rhythm
			for (int32 k = 0; 0.10 + 0.20 * k < W; ++k)
			{
				const double Xc = 0.10 + 0.20 * k;
				AddBox(D, Xc - 0.045, Xc + 0.045, -0.11, -0.055, 0.085, 0.16);
			}
			P.Add(MoveTemp(D), TEXT("Trim"));
			P.Box(0, W, -0.30, T, 0, 0.35);
			return P;
		}

		/** Weld coincident vertices and drop collapsed faces (remove_doubles + dissolve_degenerate). */
		void Weld(FSoup& S, double Dist)
		{
			TMap<FIntVector, int32> Map;
			TArray<int32> Remap;
			TArray<V3> Out;
			for (const V3& P : S.V)
			{
				const FIntVector Q(FMath::RoundToInt(P.X / Dist), FMath::RoundToInt(P.Y / Dist), FMath::RoundToInt(P.Z / Dist));
				if (const int32* F = Map.Find(Q)) { Remap.Add(*F); continue; }
				Map.Add(Q, Out.Num());
				Remap.Add(Out.Add(P));
			}
			S.V = Out;
			for (FPoly& F : S.F)
			{
				TArray<int32> I;
				for (const int32 X : F.I)
				{
					const int32 Y = Remap[X];
					if (I.IsEmpty() || I.Last() != Y) { I.Add(Y); }
				}
				while (I.Num() > 1 && I.Last() == I[0]) { I.Pop(); }
				F.I = I;
				F.Shell = 0;
			}
			S.F.RemoveAll([](const FPoly& F) { TSet<int32> U(F.I); return U.Num() < 3; });
		}

		FPiece MakeCorniceCorner()
		{
			FPiece P;
			P.Name = TEXT("CorniceCorner");
			const TArray<V2> Outer = CorniceOutline();
			FSoup S;
			TArray<int32> A, M, B;
			for (const V2& Pt : Outer)
			{
				const double Dd = FMath::Max(0.0, -Pt.X);
				A.Add(S.Add(V3(0.0, -Dd, Pt.Y)));
				M.Add(S.Add(V3(-Dd, -Dd, Pt.Y)));
				B.Add(S.Add(V3(-Dd, 0.0, Pt.Y)));
			}
			for (int32 i = 0; i + 1 < Outer.Num(); ++i)
			{
				S.Face(0, { A[i], A[i + 1], M[i + 1], M[i] });
				S.Face(0, { M[i], M[i + 1], B[i + 1], B[i] });
			}
			S.Face(0, A);
			S.Face(0, B);
			S.NextShell = 1;
			Weld(S, 1e-5);
			OrientOutward(S);
			Bevel(S, 0.008, 2, { { AX, 0.0 }, { AY, 0.0 } });
			DelPlane(S, AX, 0.0);
			DelPlane(S, AY, 0.0);
			P.Add(MoveTemp(S), TEXT("Trim"));
			P.Add(Box(-0.11, -0.02, -0.11, -0.02, 0.085, 0.16), TEXT("Trim"));
			P.Box(-0.30, 0.0, -0.30, 0.0, 0, 0.35);
			return P;
		}

		FPiece MakeParapet(double W)
		{
			FPiece P;
			P.Name = TEXT("Parapet_") + Cm(W);
			const double Pt = 0.25;
			FSoup S = Box(0, W, 0, Pt, 0, 0.80);
			DelPlane(S, AX, 0.0);
			DelPlane(S, AX, W);
			DelPlane(S, AZ, 0.80);
			P.Add(MoveTemp(S), TEXT("Wall"));
			FSoup C = PrismX({ V2(-0.045, 0.77), V2(-0.045, 0.86), V2(-0.025, 0.90), V2(0.275, 0.885), V2(0.295, 0.865), V2(0.295, 0.77),
				V2(0.27, 0.77), V2(0.27, 0.785), V2(-0.02, 0.785), V2(-0.02, 0.77) }, 0.0, W);
			Bevel(C, 0.01, 2, { { AX, 0.0 }, { AX, W } });
			P.Add(MoveTemp(C), TEXT("Concrete"));
			P.Box(0, W, -0.045, 0.295, 0, 0.90);
			return P;
		}

		// ---- roofs ----

		FPiece MakeRoofHip()
		{
			FPiece P;
			P.Name = TEXT("RoofHip");
			P.Kind = TEXT("roof");
			const double Tf = 0.04, R = 0.15;   // eave fascia height; half ridge length (ridge along X)
			const double Rad = 0.012, Zr = 1.0 - 0.65 * Rad;   // the capping tiles top out at exactly 1.0
			FSoup S;
			TArray<int32> C, E;
			for (const V2 Q : { V2(-0.5, -0.5), V2(0.5, -0.5), V2(0.5, 0.5), V2(-0.5, 0.5) }) { C.Add(S.Add(V3(Q.X, Q.Y, 0))); }
			for (const V2 Q : { V2(-0.5, -0.5), V2(0.5, -0.5), V2(0.5, 0.5), V2(-0.5, 0.5) }) { E.Add(S.Add(V3(Q.X, Q.Y, Tf))); }
			const int32 Rl = S.Add(V3(-R, 0, Zr)), Rr = S.Add(V3(R, 0, Zr));
			S.Face(0, { C[3], C[2], C[1], C[0] });                                       // soffit
			for (int32 i = 0; i < 4; ++i) { S.Face(0, { C[i], C[(i + 1) % 4], E[(i + 1) % 4], E[i] }); }   // fascia
			S.Face(0, { E[0], E[1], Rr, Rl });   // front slope
			S.Face(0, { E[1], E[2], Rr });       // +X hip end
			S.Face(0, { E[2], E[3], Rl, Rr });   // back slope
			S.Face(0, { E[3], E[0], Rl });       // -X hip end
			S.NextShell = 1;
			OrientOutward(S);
			P.Add(MoveTemp(S), TEXT("Roof"));
			// rounded ridge/hip capping tiles (half-round, slightly sunk)
			FSoup Caps;
			TArray<TPair<V3, V3>> Lines = { { V3(-R, 0, Zr), V3(R, 0, Zr) } };
			for (const V2 Sgn : { V2(1, 1), V2(1, -1), V2(-1, 1), V2(-1, -1) })
			{
				const V3 A(Sgn.X * 0.5, Sgn.Y * 0.5, Tf), B(Sgn.X * R, 0, Zr);
				Lines.Add({ A + (B - A) * 0.04, B });   // start inside the eave corner
			}
			for (const TPair<V3, V3>& L : Lines)
			{
				const V3 A = L.Key, B = L.Value;
				const V3 D = (B - A).GetSafeNormal();
				const V3 Side = D.Cross(V3(0, 0, 1)).GetSafeNormal();
				V3 Up2 = Side.Cross(D).GetSafeNormal();
				if (Up2.Z < 0) { Up2 = -Up2; }
				const int32 Seg = 6;
				const int32 Sh = Caps.NewShell();
				TArray<int32> Ra, Rb;
				for (int32 k = 0; k <= Seg; ++k)
				{
					const double Ang = UE_PI * k / Seg;
					const V3 Off = Side * FMath::Cos(Ang) * Rad + Up2 * (FMath::Sin(Ang) * Rad - Rad * 0.35);
					Ra.Add(Caps.Add(A + Off));
					Rb.Add(Caps.Add(B + Off));
				}
				const int32 First = Caps.F.Num();
				for (int32 k = 0; k < Seg; ++k) { Caps.Face(Sh, { Ra[k], Rb[k], Rb[k + 1], Ra[k + 1] }); }
				Caps.Face(Sh, Ra);
				TArray<int32> RbRev = Rb;
				Algo::Reverse(RbRev);
				Caps.Face(Sh, RbRev);
				// Orient: arc strips away from the tile axis, the ends along -D / +D.
				const V3 Axis0 = A + Up2 * (-Rad * 0.35);
				for (int32 f = First; f < Caps.F.Num(); ++f)
				{
					FPoly& F = Caps.F[f];
					V3 N(0), Cn(0);
					for (int32 i = 0; i < F.I.Num(); ++i)
					{
						const V3& P0 = Caps.V[F.I[i]];
						const V3& P1 = Caps.V[F.I[(i + 1) % F.I.Num()]];
						N += V3((P0.Y - P1.Y) * (P0.Z + P1.Z), (P0.Z - P1.Z) * (P0.X + P1.X), (P0.X - P1.X) * (P0.Y + P1.Y));
						Cn += P0;
					}
					Cn /= F.I.Num();
					V3 Want;
					if (f == Caps.F.Num() - 2) { Want = -D; }
					else if (f == Caps.F.Num() - 1) { Want = D; }
					else { const V3 Rel = Cn - Axis0; Want = Rel - D * Rel.Dot(D); }
					if (N.Dot(Want) < 0) { Algo::Reverse(F.I); }
				}
			}
			P.Add(MoveTemp(Caps), TEXT("Roof"));
			FUcx U;
			U.bHull = true;
			U.Hull = { V3(-0.5, -0.5, 0), V3(0.5, -0.5, 0), V3(0.5, 0.5, 0), V3(-0.5, 0.5, 0), V3(-0.5, -0.5, Tf), V3(0.5, -0.5, Tf), V3(0.5, 0.5, Tf),
				V3(-0.5, 0.5, Tf), V3(-R, 0, Zr), V3(R, 0, Zr) };
			P.Ucx.Add(U);
			return P;
		}

		FPiece MakeRoofFlat()
		{
			FPiece P;
			P.Name = TEXT("RoofFlat");
			P.Kind = TEXT("roof");
			P.Add(Box(-0.5, 0.5, -0.5, 0.5, 0, 0.25), TEXT("Concrete"));
			P.Box(-0.5, 0.5, -0.5, 0.5, 0, 0.25);
			return P;
		}

		// ---- roof and wall props ----

		FPiece MakeDrainpipe()
		{
			FPiece P;
			P.Name = TEXT("Drainpipe_320");
			P.Kind = TEXT("prop");
			FSoup M;
			const double Yc = -0.09;
			AddCyl(M, V3(0, Yc, 0.0), 0.06, H - 0.16, 12, 'Z', false);
			AddCyl(M, V3(0, Yc, H - 0.17), 0.07, 0.17, 12, 'Z', true);              // socket for the next segment
			AddCyl(M, V3(0, Yc, H - 0.20), 0.06, 0.03, 12, 'Z', false, 0.07);       // socket taper
			for (const double Zz : { 0.55, 2.15 })
			{
				AddCyl(M, V3(0, Yc, Zz), 0.068, 0.035, 12);
				AddBox(M, -0.01, 0.01, Yc + 0.06, 0.0, Zz + 0.005, Zz + 0.03);
				AddBox(M, -0.04, 0.04, -0.006, 0.0, Zz - 0.02, Zz + 0.055);
			}
			P.Add(MoveTemp(M), TEXT("Metal"));
			return P;
		}

		FPiece MakeWaterTank()
		{
			FPiece P;
			P.Name = TEXT("Prop_WaterTank");
			P.Kind = TEXT("prop");
			FSoup M;   // stand: 4 legs + top frame
			for (const double Sx : { -1.0, 1.0 })
			{
				for (const double Sy : { -1.0, 1.0 })
				{
					AddBox(M, Sx * 0.40 - 0.025, Sx * 0.40 + 0.025, Sy * 0.40 - 0.025, Sy * 0.40 + 0.025, 0, 0.40);
					AddBox(M, Sx * 0.40 - 0.06, Sx * 0.40 + 0.06, Sy * 0.40 - 0.06, Sy * 0.40 + 0.06, 0, 0.01);
				}
			}
			for (const double Sy : { -1.0, 1.0 })
			{
				AddBox(M, -0.45, 0.45, Sy * 0.40 - 0.025, Sy * 0.40 + 0.025, 0.36, 0.40);
				AddBox(M, Sy * 0.40 - 0.025, Sy * 0.40 + 0.025, -0.45, 0.45, 0.36, 0.40);
			}
			P.Add(MoveTemp(M), TEXT("Metal"));
			FSoup Tk;
			AddCyl(Tk, V3(0, 0, 0.40), 0.50, 1.05, 24);
			for (const double Zz : { 0.62, 1.05 }) { AddCyl(Tk, V3(0, 0, Zz), 0.515, 0.05, 24); }
			AddCyl(Tk, V3(0, 0, 1.45), 0.50, 0.10, 24, 'Z', true, 0.30);   // domed shoulder
			AddCyl(Tk, V3(0, 0, 1.55), 0.22, 0.07, 16);                     // lid
			AddCyl(Tk, V3(0.38, 0, 0.55), 0.03, 0.25, 8, 'X');              // outlet
			AddCyl(Tk, V3(0, 0, 1.62), 0.05, 0.04, 8);
			P.Add(MoveTemp(Tk), TEXT("Metal"));
			P.Box(-0.52, 0.52, -0.52, 0.52, 0, 1.66);
			return P;
		}

		FPiece MakeAcUnit()
		{
			FPiece P;
			P.Name = TEXT("Prop_ACUnit");
			P.Kind = TEXT("prop");
			const double W = 0.84, D = 0.32, Hh = 0.58, Z0 = 0.10;
			FSoup Cut;
			AddCyl(Cut, V3(-0.10, -D / 2 - 0.05, Z0 + Hh / 2), 0.22, 0.08, 24, 'Y');
			FSoup Body = BoolDiff(Box(-W / 2, W / 2, -D / 2, D / 2, Z0, Z0 + Hh), Cut);
			Bevel(Body, 0.015, 2);
			P.Add(MoveTemp(Body), TEXT("Metal"));
			FSoup M;   // fan grille: rings + spokes inside the recess
			for (const double Rr : { 0.08, 0.15, 0.21 })
			{
				for (int32 k = 0; k < 16; ++k)
				{
					const double A0 = 2 * UE_PI * k / 16, A1 = 2 * UE_PI * (k + 1) / 16;
					const V2 P0(-0.10 + Rr * FMath::Cos(A0), Z0 + Hh / 2 + Rr * FMath::Sin(A0));
					const V2 P1(-0.10 + Rr * FMath::Cos(A1), Z0 + Hh / 2 + Rr * FMath::Sin(A1));
					const V2 Mid = (P0 + P1) / 2;
					const double L = (P1 - P0).Size();
					FSoup Bar = Box(-L / 2 - 0.002, L / 2 + 0.002, -0.003, 0.003, -0.003, 0.003);
					const double Ang = FMath::Atan2(P1.Y - P0.Y, P1.X - P0.X);
					Transform(Bar, FM4::Translation(V3(Mid.X, -D / 2 - 0.004, Mid.Y)) * FM4::Rotation(-Ang, AY));
					Append(M, Bar);
				}
			}
			for (int32 k = 0; k < 4; ++k)
			{
				const double Ang = UE_PI / 4 + k * UE_PI / 2;
				FSoup Bar = Box(0, 0.215, -0.003, 0.003, -0.004, 0.004);
				Transform(Bar, FM4::Translation(V3(-0.10, -D / 2 - 0.004, Z0 + Hh / 2)) * FM4::Rotation(-Ang, AY));
				Append(M, Bar);
			}
			AddCyl(M, V3(-0.10, -D / 2 + 0.02, Z0 + Hh / 2), 0.04, 0.03, 12, 'Y');
			for (int32 k = 0; k < 9; ++k)   // side louvres on the right of the front
			{
				const double Zz = Z0 + 0.10 + k * 0.042;
				AddBox(M, 0.18, 0.37, -D / 2 - 0.012, -D / 2 + 0.002, Zz, Zz + 0.012);
			}
			for (const double Sx : { -1.0, 1.0 }) { AddBox(M, Sx * 0.30 - 0.03, Sx * 0.30 + 0.03, -D / 2 - 0.02, D / 2 + 0.02, 0.0, Z0); }   // feet
			AddCyl(M, V3(W / 2, 0.05, Z0 + 0.12), 0.012, 0.10, 8, 'X');   // refrigerant pipes leaving the right side
			AddCyl(M, V3(W / 2, 0.09, Z0 + 0.12), 0.009, 0.10, 8, 'X');
			P.Add(MoveTemp(M), TEXT("Metal"));
			P.Box(-W / 2, W / 2, -D / 2 - 0.02, D / 2 + 0.02, 0, Z0 + Hh);
			return P;
		}

		FPiece MakeDish()
		{
			FPiece P;
			P.Name = TEXT("Prop_Dish");
			P.Kind = TEXT("prop");
			FSoup M;
			AddBox(M, -0.12, 0.12, -0.12, 0.12, 0.0, 0.012);
			AddCyl(M, V3(0, 0, 0.012), 0.025, 0.80, 10);
			// dish: shallow paraboloid shell 12 mm thick, opening toward -Y
			const double R = 0.40, Depth = 0.08, Th = 0.012;
			const int32 Rings = 5, Seg = 20;
			FSoup Shell;
			{
				// Rebuild as one closed shell: front surface, back surface (offset +Y), rim quads.
				const int32 Sh = Shell.NewShell();
				TArray<TArray<int32>> Fr, Bk;
				for (int32 s = 0; s < 2; ++s)
				{
					TArray<TArray<int32>>& G = s == 0 ? Fr : Bk;
					for (int32 i = 0; i <= Rings; ++i)
					{
						TArray<int32>& Row = G.AddDefaulted_GetRef();
						for (int32 k = 0; k < Seg; ++k)
						{
							const double Rr = R * i / Rings, A = 2 * UE_PI * k / Seg;
							if (i == 0 && k > 0) { const int32 Centre = Row[0]; Row.Add(Centre); continue; }
							Row.Add(Shell.Add(V3(Rr * FMath::Cos(A), -Depth * FMath::Square(Rr / R) + (s ? Th : 0.0), Rr * FMath::Sin(A))));
						}
					}
				}
				for (int32 i = 0; i < Rings; ++i)
				{
					for (int32 k = 0; k < Seg; ++k)
					{
						const int32 K2 = (k + 1) % Seg;
						if (i == 0)
						{
							Shell.Face(Sh, { Fr[0][0], Fr[1][K2], Fr[1][k] });
							Shell.Face(Sh, { Bk[0][0], Bk[1][k], Bk[1][K2] });
						}
						else
						{
							Shell.Face(Sh, { Fr[i][k], Fr[i][K2], Fr[i + 1][K2], Fr[i + 1][k] });
							Shell.Face(Sh, { Bk[i][k], Bk[i + 1][k], Bk[i + 1][K2], Bk[i][K2] });
						}
					}
				}
				for (int32 k = 0; k < Seg; ++k)
				{
					const int32 K2 = (k + 1) % Seg;
					Shell.Face(Sh, { Fr[Rings][k], Fr[Rings][K2], Bk[Rings][K2], Bk[Rings][k] });
				}
				OrientOutward(Shell);
			}
			FSoup Dish = MoveTemp(Shell);
			FSoup Arm;   // LNB arm + LNB
			const V3 A0(0, -0.07, -0.37), A1(0, -0.40, -0.03);
			AddBox(Arm, -0.012, 0.012, -0.012, 0.012, 0.0, (A1 - A0).Size());
			Transform(Arm, FM4::Translation(A0) * FM4::TrackZ(A1 - A0));
			AddCyl(Arm, V3(0, -0.47, -0.03), 0.032, 0.11, 10, 'Y');
			Append(Dish, Arm);
			AddBox(Dish, -0.06, 0.06, 0.02, 0.10, -0.06, 0.06);   // bracket behind the dish
			Transform(Dish, FM4::Translation(V3(0, -0.02, 0.80)) * FM4::Rotation(FMath::DegreesToRadians(-25.0), AX));
			Append(M, Dish);
			P.Add(MoveTemp(M), TEXT("Metal"));
			P.Box(-0.42, 0.42, -0.45, 0.15, 0, 1.18);
			return P;
		}

		FPiece MakeChimney()
		{
			FPiece P;
			P.Name = TEXT("Prop_Chimney");
			P.Kind = TEXT("prop");
			const double S = 0.30;
			{
				FSoup B = Box(-S, S, -S, S, 0, 1.0);
				DelPlane(B, AZ, 0.0);
				Bevel(B, 0.02, 2, { { AZ, 0.0 } });
				P.Add(MoveTemp(B), TEXT("Wall"));
			}
			P.Add(Bev(Box(-S - 0.03, S + 0.03, -S - 0.03, S + 0.03, 1.0, 1.06), 0.01, 2), TEXT("Trim"));
			for (const double Sx : { -1.0, 1.0 })
			{
				for (const double Sy : { -1.0, 1.0 })
				{
					const double Px0 = Sx > 0 ? S - 0.12 : -S, Px1 = Sx > 0 ? S : -S + 0.12;
					const double Py0 = Sy > 0 ? S - 0.12 : -S, Py1 = Sy > 0 ? S : -S + 0.12;
					P.Add(Bev(Box(Px0, Px1, Py0, Py1, 1.06, 1.40), 0.01, 2), TEXT("Wall"));
				}
			}
			P.Add(Bev(Box(-S - 0.06, S + 0.06, -S - 0.06, S + 0.06, 1.40, 1.46), 0.01, 2), TEXT("Concrete"));
			FSoup Hood;   // small hipped tile hood
			TArray<int32> B;
			for (const V2 Q : { V2(-1, -1), V2(1, -1), V2(1, 1), V2(-1, 1) }) { B.Add(Hood.Add(V3(Q.X * (S + 0.08), Q.Y * (S + 0.08), 1.46))); }
			const int32 Top = Hood.Add(V3(0, 0, 1.68));
			for (int32 i = 0; i < 4; ++i) { Hood.Face(0, { B[i], B[(i + 1) % 4], Top }); }
			Hood.Face(0, { B[3], B[2], B[1], B[0] });
			Hood.NextShell = 1;
			OrientOutward(Hood);
			P.Add(MoveTemp(Hood), TEXT("Roof"));
			P.Add(Box(-S + 0.02, S - 0.02, -S + 0.02, S - 0.02, 1.0, 1.08), TEXT("Metal"));   // dark flue (soot) top inside the piers
			P.Box(-S - 0.08, S + 0.08, -S - 0.08, S + 0.08, 0, 1.68);
			return P;
		}

		FPiece MakeRoofHut()
		{
			FPiece P;
			P.Name = TEXT("Prop_RoofHut");
			P.Kind = TEXT("prop");
			const double Hw = 1.20, Hd = 1.00, Hh = 2.40, Dx0 = -0.45, Dx1 = 0.45, Dz1 = 2.05;
			{
				FSoup Cut;
				AddBox(Cut, Dx0, Dx1, -Hd - 0.1, -Hd + 0.12, -0.1, Dz1);
				AddBox(Cut, Hw - 0.10, Hw + 0.1, -0.35, 0.35, 1.45, 1.95);   // small side window
				FSoup Wall = BoolDiff(Box(-Hw, Hw, -Hd, Hd, 0, Hh), Cut);
				Bevel(Wall, 0.02, 2, { { AZ, 0.0 } });
				P.Add(MoveTemp(Wall), TEXT("Wall"));
			}
			{
				FSoup Band = Box(-Hw - 0.03, Hw + 0.03, -Hd - 0.03, Hd + 0.03, 0, 0.18);   // base band
				DelPlane(Band, AZ, 0.0);
				FSoup Cut;
				AddBox(Cut, Dx0, Dx1, -Hd - 0.2, -Hd + 0.2, -0.1, Dz1);
				// The band is open at the bottom; close it for the boolean, then remove the bottom again.
				FSoup Closed = Box(-Hw - 0.03, Hw + 0.03, -Hd - 0.03, Hd + 0.03, 0, 0.18);
				Band = BoolDiff(Closed, Cut);
				DelPlane(Band, AZ, 0.0);
				Bevel(Band, 0.01, 2, { { AZ, 0.0 } });
				P.Add(MoveTemp(Band), TEXT("Wall"));
			}
			P.Add(Bev(Box(Dx0 - 0.1, Dx1 + 0.1, -Hd - 0.25, -Hd + 0.10, 0, 0.12), 0.015, 2, { { AZ, 0.0 } }), TEXT("Concrete"));   // step
			P.Add(Box(Dx0 + 0.01, Dx1 - 0.01, -Hd + 0.06, -Hd + 0.10, 0.12, Dz1), TEXT("Wood"));   // door
			for (int32 k = 0; k < 7; ++k)
			{
				const double Xx = Dx0 + 0.01 + (Dx1 - Dx0 - 0.02) * (k + 0.5) / 7;
				P.Add(Bev(Box(Xx - 0.055, Xx + 0.055, -Hd + 0.045, -Hd + 0.065, 0.13, Dz1 - 0.01), 0.005, 1), TEXT("Wood"));
			}
			for (const double Zz : { 0.35, 1.10, 1.80 }) { P.Add(Bev(Box(Dx0 + 0.04, Dx1 - 0.04, -Hd + 0.03, -Hd + 0.046, Zz, Zz + 0.10), 0.005, 1), TEXT("Wood")); }
			P.Add(Box(Dx1 - 0.14, Dx1 - 0.10, -Hd + 0.015, -Hd + 0.03, 1.0, 1.06), TEXT("Metal"));
			P.Add(Bev(Box(Dx0 - 0.08, Dx1 + 0.08, -Hd - 0.02, -Hd + 0.05, Dz1, Dz1 + 0.15), 0.01, 2), TEXT("Concrete"));   // door head
			FSoup G;   // side window: louvre vent
			for (int32 k = 0; k < 6; ++k) { const double Zz = 1.47 + k * 0.08; AddBox(G, Hw - 0.08, Hw - 0.05, -0.35, 0.35, Zz, Zz + 0.03); }
			P.Add(MoveTemp(G), TEXT("Metal"));
			P.Add(Bev(Box(-Hw - 0.12, Hw + 0.12, -Hd - 0.12, Hd + 0.12, Hh, Hh + 0.14), 0.02, 2), TEXT("Concrete"));   // roof slab with drip
			P.Box(-Hw, Hw, -Hd, Hd, 0, Hh + 0.14);
			return P;
		}

		// ---- retaining walls ----

		FPiece MakeRetainingWall(const TCHAR* Kind)
		{
			// 4 x 2 m panel, face at Y=0. Stone: coursed blocks with sawn joints (old roads, the 1930s pass);
			// concrete: smooth pours with lift joints every metre (post-war roads). Stacked by BuildRetainingWalls.
			const double W = 4.0;
			FPiece P;
			P.Name = FString::Printf(TEXT("RetainingWall_%s_400"), Kind);
			FSoup Cb;
			const bool bStone = FCString::Strcmp(Kind, TEXT("Stone")) == 0;
			if (bStone)
			{
				const int32 Rows = 5;
				for (int32 r = 1; r < Rows; ++r) { const double Z = RwH * r / Rows; AddBox(Cb, -0.1, W + 0.1, -0.1, 0.015, Z - 0.008, Z + 0.008); }   // bed joints
				const double Lens[4] = { 0.55, 0.72, 0.63, 0.8 };
				for (int32 r = 0; r < Rows; ++r)   // staggered head joints, irregular block lengths
				{
					const double Z0 = RwH * r / Rows, Z1 = RwH * (r + 1) / Rows;
					double X = 0.25 + 0.31 * (r % 3);
					for (int32 k = 0; X < W - 0.15; ++k)
					{
						// stop short of the bed joints: overlapping cutters make booleans fragile
						AddBox(Cb, X - 0.008, X + 0.008, -0.1, 0.015, Z0 + (r ? 0.009 : -0.1), Z1 - (r < Rows - 1 ? 0.009 : -0.1));
						X += Lens[(r + k) % 4];
					}
				}
			}
			else
			{
				AddBox(Cb, -0.1, W + 0.1, -0.1, 0.01, 0.995, 1.005);   // pour lift
				// formwork panel seams, through the lift joint (the cuts are made one cutter at a time)
				for (const double X : { 1.0, 2.0, 3.0 }) { AddBox(Cb, X - 0.004, X + 0.004, -0.1, 0.006, -0.1, RwH + 0.1); }
			}
			// Blender bevelled after the cut, clamping overlaps on the narrow bridges between joints; Unreal's bevel
			// cannot clamp, so the panel is bevelled first and the joints are cut with crisp lips.
			FSoup S = Box(0.0, W, 0.0, RwT, 0.0, RwH);
			Bevel(S, 0.012, 2, { { AX, 0.0 }, { AX, W }, { AZ, 0.0 }, { AZ, RwH }, { AY, RwT } });
			S = BoolDiff(S, Cb);
			P.Add(MoveTemp(S), bStone ? TEXT("Trim") : TEXT("Concrete"));
			P.Box(0, W, 0, RwT, 0, RwH);
			return P;
		}

		FPiece MakeWallCoping()
		{
			const double W = 4.0;   // stone coping capping the top row of a retaining wall (5 cm overhang both sides)
			FPiece P;
			P.Name = TEXT("WallCoping_400");
			P.Add(Bev(Box(0.0, W, -0.05, RwT + 0.05, 0.0, 0.16), 0.015, 2, { { AX, 0.0 }, { AX, W }, { AZ, 0.0 } }), TEXT("Trim"));
			P.Box(0, W, -0.05, RwT + 0.05, 0, 0.16);
			return P;
		}

		// ---- street fixtures: pivot at the base of a post, or at the wall plate of a bracket (reaching -Y) ----

		FSoup DiscY(double R, double Y0, double Y1, double Z, int32 Seg = 32, double RIn = -1.0)
		{
			FSoup S;
			if (RIn < 0)
			{
				AddCyl(S, V3(0, Y0, Z), R, Y1 - Y0, Seg, 'Y');
				return S;
			}
			for (int32 k = 0; k < Seg; ++k)   // ring as Seg closed blocks
			{
				const double C0 = FMath::Cos(2 * UE_PI * k / Seg), S0 = FMath::Sin(2 * UE_PI * k / Seg);
				const double C1 = FMath::Cos(2 * UE_PI * (k + 1) / Seg), S1 = FMath::Sin(2 * UE_PI * (k + 1) / Seg);
				AddPrism(S, { V2(Z + R * S0, R * C0), V2(Z + R * S1, R * C1), V2(Z + RIn * S1, RIn * C1), V2(Z + RIn * S0, RIn * C0) }, AY, Y0, Y1);
			}
			return S;
		}

		/** Convex polygon in XZ extruded along Y (Y0 front .. Y1 back). */
		FSoup PolyY(const TArray<V2>& PointsXZ, double Y0, double Y1)
		{
			TArray<V2> ZX;   // AddPrism along Y takes (z, x)
			for (const V2& P : PointsXZ) { ZX.Add(V2(P.Y, P.X)); }
			FSoup S;
			AddPrism(S, ZX, AY, Y0, Y1);
			return S;
		}

		void SignPost(FPiece& P, double Height = 2.90)
		{
			FSoup M;
			AddCyl(M, V3(0, 0.03, 0), 0.03, Height, 12);   // galvanised post, plate in front of it
			AddCyl(M, V3(0, 0.03, Height), 0.035, 0.03, 12);
			P.Add(MoveTemp(M), TEXT("Metal"));
		}

		constexpr double SignZ = 2.25, SignR = 0.30;   // plate centre height and radius (urban 60 cm discs)

		/** Codice della Strada sign on a post (original artwork: shapes, colours and text only). */
		FPiece MakeSign(const FString& Kind)
		{
			FPiece P;
			P.Name = TEXT("Sign_") + Kind;
			P.Kind = TEXT("fixture");
			SignPost(P);
			const double Y0 = -0.02, Y1 = 0.0;
			if (Kind.StartsWith(TEXT("Speed")))
			{
				P.Add(DiscY(SignR, Y0, Y1, SignZ, 32, SignR * 0.78), TEXT("PaintRed"));
				P.Add(DiscY(SignR * 0.78, Y0 + 0.002, Y1, SignZ), TEXT("PaintWhite"));
				P.Add(TextMesh(Kind.Mid(5), 0.26, SignZ, 0.004, Y0 - 0.001), TEXT("PaintBlack"));
			}
			else if (Kind == TEXT("Stop"))
			{
				TArray<V2> Octo;
				for (int32 k = 0; k < 8; ++k) { Octo.Add(V2(SignR * FMath::Cos(UE_PI / 8 + k * UE_PI / 4), SignZ + SignR * FMath::Sin(UE_PI / 8 + k * UE_PI / 4))); }
				P.Add(PolyY(Octo, Y0, Y1), TEXT("PaintRed"));
				P.Add(TextMesh(TEXT("STOP"), 0.15, SignZ, 0.004, Y0 - 0.001), TEXT("PaintWhite"));
			}
			else if (Kind == TEXT("GiveWay"))
			{
				const double Hh = 0.78;
				P.Add(PolyY({ V2(-0.45, SignZ + Hh * 0.4), V2(0.45, SignZ + Hh * 0.4), V2(0.0, SignZ - Hh * 0.6) }, Y0, Y1), TEXT("PaintRed"));
				P.Add(PolyY({ V2(-0.32, SignZ + Hh * 0.4 - 0.08), V2(0.32, SignZ + Hh * 0.4 - 0.08), V2(0.0, SignZ - Hh * 0.6 + 0.15) }, Y0 - 0.002, Y1), TEXT("PaintWhite"));
			}
			else if (Kind == TEXT("NoEntry"))
			{
				P.Add(DiscY(SignR, Y0, Y1, SignZ), TEXT("PaintRed"));
				P.Add(Box(-0.21, 0.21, Y0 - 0.003, Y1, SignZ - 0.05, SignZ + 0.05), TEXT("PaintWhite"));
			}
			else if (Kind == TEXT("ZTL"))
			{
				P.Add(Box(-0.40, 0.40, Y0, Y1, 1.75, 2.75), TEXT("PaintWhite"));
				P.Add(DiscY(0.22, Y0 - 0.002, Y1, 2.45, 32, 0.17), TEXT("PaintRed"));
				P.Add(TextMesh(TEXT("ZTL"), 0.16, 1.95, 0.004, Y0 - 0.003), TEXT("PaintBlack"));
				P.Add(TextMesh(TEXT("ZONA TRAFFICO LIMITATO"), 0.045, 2.12, 0.004, Y0 - 0.003), TEXT("PaintBlack"));
			}
			P.Box(-0.1, 0.1, -0.05, 0.1, 0, 2.9);
			return P;
		}

		FPiece MakeConvexMirror()
		{
			FPiece P;
			P.Name = TEXT("Prop_ConvexMirror");
			P.Kind = TEXT("fixture");
			SignPost(P, 2.6);
			P.Add(DiscY(0.33, -0.08, -0.02, 2.45, 32, 0.29), TEXT("PaintRed"));
			FSoup Face;
			AddCyl(Face, V3(0, -0.07, 2.45), 0.29, 0.05, 28, 'Y', true, 0.24);   // domed mirror
			P.Add(MoveTemp(Face), TEXT("Glass"));
			P.Box(-0.1, 0.1, -0.05, 0.1, 0, 2.6);
			return P;
		}

		FPiece MakeBollard(const FString& Kind)
		{
			FPiece P;
			P.Name = TEXT("Prop_Bollard") + Kind;
			P.Kind = TEXT("fixture");
			FSoup M;
			if (Kind == TEXT("Stone"))
			{
				AddCyl(M, V3(0, 0, 0), 0.15, 0.48, 16, 'Z', true, 0.13);
				AddCyl(M, V3(0, 0, 0.48), 0.13, 0.10, 16, 'Z', true, 0.05);   // rounded crown
				Bevel(M, 0.01, 2);
				P.Add(MoveTemp(M), TEXT("Trim"));
			}
			else   // cast iron
			{
				AddCyl(M, V3(0, 0, 0), 0.09, 0.08, 14);
				AddCyl(M, V3(0, 0, 0.08), 0.06, 0.70, 14, 'Z', true, 0.05);
				for (const double Z : { 0.12, 0.72 }) { AddCyl(M, V3(0, 0, Z), 0.075, 0.04, 14); }
				AddCyl(M, V3(0, 0, 0.78), 0.07, 0.07, 14, 'Z', true, 0.03);   // knob
				P.Add(MoveTemp(M), TEXT("Metal"));
			}
			P.Box(-0.15, 0.15, -0.15, 0.15, 0, 0.9);
			return P;
		}

		void ShiftParts(FPiece& P, double Dz)
		{
			for (TPair<FSoup, int32>& Part : P.Parts) { Transform(Part.Key, FM4::Translation(V3(0, 0, Dz))); }
		}

		/** Wall lantern on an iron bracket; pivot at the wall plate's foot, reaching out along -Y. */
		FPiece MakeWallLantern()
		{
			FPiece P;
			P.Name = TEXT("Prop_WallLantern");
			P.Kind = TEXT("fixture");
			FSoup M;
			AddBox(M, -0.06, 0.06, -0.02, 0.0, 0.0, 0.30);    // wall plate
			AddCyl(M, V3(0, 0.0, 0.24), 0.018, 0.62, 8, 'Y');   // arm, built along +Y and mirrored
			Transform(M, FM4::Scale(V3(1, -1, 1)));
			AddCyl(M, V3(0, -0.55, 0.12), 0.012, 0.12, 8);     // drop rod
			FSoup Lantern;
			for (const double Sx : { -1.0, 1.0 })
			{
				for (const double Sy : { -1.0, 1.0 })
				{
					AddBox(Lantern, Sx * 0.11 - 0.012, Sx * 0.11 + 0.012, -0.55 + Sy * 0.11 - 0.012, -0.55 + Sy * 0.11 + 0.012, -0.30, 0.04);
				}
			}
			AddCyl(Lantern, V3(0, -0.55, 0.04), 0.17, 0.10, 4, 'Z', true, 0.02);   // roof
			AddCyl(Lantern, V3(0, -0.55, -0.34), 0.08, 0.04, 8);                    // base
			P.Add(MoveTemp(M), TEXT("Metal"));
			P.Add(MoveTemp(Lantern), TEXT("Metal"));
			P.Add(Box(-0.10, 0.10, -0.65, -0.45, -0.28, 0.02), TEXT("Glass"));
			ShiftParts(P, 0.34);
			P.Box(-0.15, 0.15, -0.7, 0.0, 0, 0.7);
			return P;
		}

		/** Galvanised LED street light: 8 m tapered pole, 1.6 m arm toward -Y, flat head. */
		FPiece MakeLedPole()
		{
			FPiece P;
			P.Name = TEXT("Prop_LedPole");
			P.Kind = TEXT("fixture");
			FSoup M;
			AddCyl(M, V3(0, 0, 0), 0.10, 0.30, 12);   // base collar
			AddCyl(M, V3(0, 0, 0.30), 0.075, 7.70, 12, 'Z', true, 0.045);
			FSoup Arm;
			AddCyl(Arm, V3(0, 0, 7.85), 0.035, 1.60, 8, 'Y');
			Transform(Arm, FM4::Scale(V3(1, -1, 1)));
			P.Add(MoveTemp(M), TEXT("Metal"));
			P.Add(MoveTemp(Arm), TEXT("Metal"));
			P.Add(Box(-0.16, 0.16, -1.85, -1.30, 7.80, 7.92), TEXT("Metal"));
			P.Add(Box(-0.13, 0.13, -1.80, -1.35, 7.785, 7.80), TEXT("Glass"));
			P.Box(-0.12, 0.12, -0.12, 0.12, 0, 8.0);
			return P;
		}

		/** Wheeled kerbside recycling bin (cassonetto); colour per instance (Paint slot). */
		FPiece MakeBin()
		{
			FPiece P;
			P.Name = TEXT("Prop_RecyclingBin");
			P.Kind = TEXT("fixture");
			P.Add(Bev(Box(-0.65, 0.65, -0.42, 0.42, 0.18, 1.20), 0.04, 2), TEXT("Paint"));
			P.Add(Bev(Box(-0.68, 0.68, -0.46, 0.46, 1.20, 1.30), 0.03, 2), TEXT("Paint"));
			FSoup Wheels;
			for (const double Sx : { -0.5, 0.5 })
			{
				// a 10-sided wheel's lowest edge is r cos 18 deg below its centre: sit it on z = 0
				for (const double Sy : { -0.3, 0.3 }) { AddCyl(Wheels, V3(Sx - 0.04, Sy, 0.09 * FMath::Cos(UE_PI / 10)), 0.09, 0.08, 10, 'X'); }
			}
			P.Add(MoveTemp(Wheels), TEXT("Metal"));
			P.Add(Box(-0.15, 0.15, -0.55, -0.42, 0.15, 0.20), TEXT("Metal"));   // pedal
			P.Box(-0.68, 0.68, -0.46, 0.46, 0, 1.3);
			return P;
		}

		/** Cast-iron drinking fountain (original design): fluted column, spout, grate. */
		FPiece MakeFountain()
		{
			FPiece P;
			P.Name = TEXT("Prop_Fountain");
			P.Kind = TEXT("fixture");
			FSoup M;
			AddBox(M, -0.30, 0.30, -0.45, 0.15, 0.0, 0.05);                 // grate base
			AddCyl(M, V3(0, 0, 0.05), 0.17, 0.10, 8);                        // plinth
			AddCyl(M, V3(0, 0, 0.15), 0.13, 0.85, 8, 'Z', true, 0.11);      // octagonal column
			AddCyl(M, V3(0, 0, 1.00), 0.15, 0.06, 8);                        // cap
			AddCyl(M, V3(0, 0, 1.06), 0.09, 0.08, 8, 'Z', true, 0.02);
			FSoup Spout;
			AddCyl(Spout, V3(0, 0.0, 0.70), 0.02, 0.22, 8, 'Y');
			Transform(Spout, FM4::Scale(V3(1, -1, 1)));
			P.Add(MoveTemp(M), TEXT("Metal"));
			P.Add(MoveTemp(Spout), TEXT("Metal"));
			P.Box(-0.3, 0.3, -0.45, 0.17, 0, 1.15);
			return P;
		}

		/** Three-aspect traffic signal on a 3.2 m post (original design), face toward -Y, with a pedestrian
		 *  push-button box. Lenses are unlit: signal states come with traffic (INFRA_DESIGN Phase 11). */
		FPiece MakeTrafficLight()
		{
			FPiece P;
			P.Name = TEXT("Prop_TrafficLight");
			P.Kind = TEXT("fixture");
			FSoup Post;
			AddCyl(Post, V3(0, 0.05, 0), 0.07, 0.25, 12);   // base collar
			AddCyl(Post, V3(0, 0.05, 0.25), 0.055, 2.95, 12);
			AddCyl(Post, V3(0, 0.05, 3.20), 0.06, 0.03, 12);
			P.Add(MoveTemp(Post), TEXT("Metal"));
			FSoup Head = Box(-0.15, 0.15, -0.12, 0.10, 2.25, 3.15);   // housing
			Bevel(Head, 0.02, 1);
			for (const double Z : { 2.40, 2.70, 3.00 }) { AddBox(Head, -0.13, 0.13, -0.30, -0.12, Z + 0.11, Z + 0.13); }   // visors
			AddBox(Head, -0.26, 0.26, 0.10, 0.12, 2.18, 3.22);   // backplate
			P.Add(MoveTemp(Head), TEXT("PaintBlack"));
			FSoup Red;
			AddCyl(Red, V3(0, -0.13, 3.00), 0.10, 0.01, 16, 'Y');
			P.Add(MoveTemp(Red), TEXT("PaintRed"));
			FSoup Lens;
			for (const double Z : { 2.40, 2.70 }) { AddCyl(Lens, V3(0, -0.13, Z), 0.10, 0.01, 16, 'Y'); }
			P.Add(MoveTemp(Lens), TEXT("Glass"));
			P.Add(Box(-0.07, 0.07, -0.06, 0.0, 1.00, 1.22), TEXT("Paint"));   // pedestrian push button
			P.Box(-0.26, 0.26, -0.3, 0.12, 0, 3.25);
			return P;
		}

		/** Lamp hung on a cable across a narrow street; pivot at the bottom of the glass bowl, hook 0.62 m above. */
		FPiece MakeCableLamp()
		{
			FPiece P;
			P.Name = TEXT("Prop_CableLamp");
			P.Kind = TEXT("fixture");
			FSoup M;
			AddCyl(M, V3(0, 0, 0.44), 0.012, 0.18, 8);                       // hanger up to the hook
			AddCyl(M, V3(0, 0, 0.20), 0.32, 0.24, 16, 'Z', true, 0.05);      // shade, wide at the bottom
			P.Add(MoveTemp(M), TEXT("Metal"));
			FSoup Bowl;
			AddCyl(Bowl, V3(0, 0, 0.0), 0.12, 0.20, 16, 'Z', true, 0.24);    // glass bowl
			P.Add(MoveTemp(Bowl), TEXT("Glass"));
			P.Box(-0.33, 0.33, -0.33, 0.33, 0, 0.62);
			return P;
		}

		/** 1 m of 8 mm cable along +X from the origin, its axis 8 mm up; stretched per instance (scale X = span). */
		FPiece MakeCable()
		{
			FPiece P;
			P.Name = TEXT("Prop_Cable");
			P.Kind = TEXT("fixture");
			FSoup M;
			AddCyl(M, V3(0, 0, 0.008 * FMath::Cos(UE_PI / 6)), 0.008, 1.0, 6, 'X');   // a hexagon resting on z = 0
			P.Add(MoveTemp(M), TEXT("Metal"));
			P.Box(0, 1, -0.01, 0.01, 0, 0.016);
			return P;
		}
	}

	TArray<TFunction<FPiece()>> Builders()
	{
		return {
			[] { return MakeWall(2.0); }, [] { return MakeWall(4.0); },
			[] { return MakeWindow(2.0, TEXT("shutters")); }, [] { return MakeWindow(4.0, TEXT("shutters")); },
			[] { return MakeWindow(2.0, TEXT("plain")); }, [] { return MakeWindow(4.0, TEXT("plain")); },
			MakeBalcony, MakeShopfront, MakeDoor, MakeGroundWindow, MakeCorner,
			[] { return MakePlinth(2.0); }, [] { return MakePlinth(4.0); },
			[] { return MakeCornice(2.0); }, [] { return MakeCornice(4.0); }, MakeCorniceCorner,
			[] { return MakeParapet(2.0); }, [] { return MakeParapet(4.0); },
			MakeRoofHip, MakeRoofFlat,
			MakeWaterTank, MakeAcUnit, MakeDish, MakeChimney, MakeRoofHut, MakeDrainpipe,
			[] { return MakeRetainingWall(TEXT("Stone")); }, [] { return MakeRetainingWall(TEXT("Concrete")); }, MakeWallCoping,
			[] { return MakeSign(TEXT("Speed30")); }, [] { return MakeSign(TEXT("Speed50")); }, [] { return MakeSign(TEXT("Stop")); },
			[] { return MakeSign(TEXT("GiveWay")); }, [] { return MakeSign(TEXT("NoEntry")); }, [] { return MakeSign(TEXT("ZTL")); }, MakeConvexMirror,
			[] { return MakeBollard(TEXT("Stone")); }, [] { return MakeBollard(TEXT("Iron")); }, MakeWallLantern, MakeLedPole, MakeBin, MakeFountain,
			MakeTrafficLight, MakeCableLamp, MakeCable };
	}
}
