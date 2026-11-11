// Top-down preview of the Marevia plan (0.5 px per metre, north up): district colours shaded by height,
// water, roads (gated red, bridges white), building footprints and labelled landmarks.
#include "Layout/OCMareviaLayout.h"

#include "ImageCore.h"
#include "ImageUtils.h"
#include <cmath>

namespace OCLayout
{
	namespace
	{
		struct FCanvas
		{
			int32 W = 0, H = 0;
			TArray<FColor> Px;
			void Set(int32 X, int32 Y, FColor C) { if (X >= 0 && Y >= 0 && X < W && Y < H) { Px[Y * W + X] = C; } }

			void Line(double X0, double Y0, double X1, double Y1, int32 Width, FColor C)
			{
				const double Len = FMath::Max(std::hypot(X1 - X0, Y1 - Y0), 1e-6);
				const int32 Steps = FMath::Max(1, int32(Len * 2));
				const double R = Width / 2.0;
				for (int32 s = 0; s <= Steps; ++s)
				{
					const double X = X0 + (X1 - X0) * s / Steps, Y = Y0 + (Y1 - Y0) * s / Steps;
					if (Width <= 1)
					{
						Set(int32(std::floor(X)), int32(std::floor(Y)), C);
						continue;
					}
					for (int32 Dy = int32(std::floor(-R)); Dy <= int32(std::ceil(R)); ++Dy)
					{
						for (int32 Dx = int32(std::floor(-R)); Dx <= int32(std::ceil(R)); ++Dx)
						{
							if (Dx * Dx + Dy * Dy <= R * R) { Set(int32(std::floor(X)) + Dx, int32(std::floor(Y)) + Dy, C); }
						}
					}
				}
			}

			void Polygon(const TArray<FVector2D>& P, FColor Fill, FColor Outline)
			{
				double MinX = 1e9, MinY = 1e9, MaxX = -1e9, MaxY = -1e9;
				for (const FVector2D& V : P) { MinX = FMath::Min(MinX, V.X); MinY = FMath::Min(MinY, V.Y); MaxX = FMath::Max(MaxX, V.X); MaxY = FMath::Max(MaxY, V.Y); }
				for (int32 Y = int32(std::floor(MinY)); Y <= int32(std::ceil(MaxY)); ++Y)
				{
					for (int32 X = int32(std::floor(MinX)); X <= int32(std::ceil(MaxX)); ++X)
					{
						// Inside a convex polygon: on the same side of every edge.
						int32 Pos = 0, Neg = 0;
						for (int32 i = 0; i < P.Num(); ++i)
						{
							const FVector2D& A = P[i];
							const FVector2D& B = P[(i + 1) % P.Num()];
							const double Cr = (B.X - A.X) * (Y + 0.5 - A.Y) - (B.Y - A.Y) * (X + 0.5 - A.X);
							(Cr >= 0 ? Pos : Neg) += 1;
						}
						if (Pos == 0 || Neg == 0) { Set(X, Y, Fill); }
					}
				}
				for (int32 i = 0; i < P.Num(); ++i) { Line(P[i].X, P[i].Y, P[(i + 1) % P.Num()].X, P[(i + 1) % P.Num()].Y, 1, Outline); }
			}

			void Text(int32 X, int32 Y, const FString& S, FColor C)
			{
				// 5x7 capitals, digits and a little punctuation (lower case is drawn as capitals).
				static const TMap<TCHAR, TArray<uint8>> Font = {
					{ 'A', { 14, 17, 17, 31, 17, 17, 17 } }, { 'B', { 30, 17, 17, 30, 17, 17, 30 } }, { 'C', { 14, 17, 16, 16, 16, 17, 14 } },
					{ 'D', { 30, 17, 17, 17, 17, 17, 30 } }, { 'E', { 31, 16, 16, 30, 16, 16, 31 } }, { 'F', { 31, 16, 16, 30, 16, 16, 16 } },
					{ 'G', { 14, 17, 16, 23, 17, 17, 15 } }, { 'H', { 17, 17, 17, 31, 17, 17, 17 } }, { 'I', { 14, 4, 4, 4, 4, 4, 14 } },
					{ 'J', { 7, 2, 2, 2, 2, 18, 12 } }, { 'K', { 17, 18, 20, 24, 20, 18, 17 } }, { 'L', { 16, 16, 16, 16, 16, 16, 31 } },
					{ 'M', { 17, 27, 21, 21, 17, 17, 17 } }, { 'N', { 17, 17, 25, 21, 19, 17, 17 } }, { 'O', { 14, 17, 17, 17, 17, 17, 14 } },
					{ 'P', { 30, 17, 17, 30, 16, 16, 16 } }, { 'Q', { 14, 17, 17, 17, 21, 18, 13 } }, { 'R', { 30, 17, 17, 30, 20, 18, 17 } },
					{ 'S', { 15, 16, 16, 14, 1, 1, 30 } }, { 'T', { 31, 4, 4, 4, 4, 4, 4 } }, { 'U', { 17, 17, 17, 17, 17, 17, 14 } },
					{ 'V', { 17, 17, 17, 17, 17, 10, 4 } }, { 'W', { 17, 17, 17, 21, 21, 21, 10 } }, { 'X', { 17, 17, 10, 4, 10, 17, 17 } },
					{ 'Y', { 17, 17, 10, 4, 4, 4, 4 } }, { 'Z', { 31, 1, 2, 4, 8, 16, 31 } },
					{ '0', { 14, 17, 19, 21, 25, 17, 14 } }, { '1', { 4, 12, 4, 4, 4, 4, 14 } }, { '2', { 14, 17, 1, 2, 4, 8, 31 } },
					{ '3', { 31, 2, 4, 2, 1, 17, 14 } }, { '4', { 2, 6, 10, 18, 31, 2, 2 } }, { '5', { 31, 16, 30, 1, 1, 17, 14 } },
					{ '6', { 6, 8, 16, 30, 17, 17, 14 } }, { '7', { 31, 1, 2, 4, 8, 8, 8 } }, { '8', { 14, 17, 17, 14, 17, 17, 14 } },
					{ '9', { 14, 17, 17, 15, 1, 2, 12 } }, { '.', { 0, 0, 0, 0, 0, 12, 12 } }, { '=', { 0, 0, 31, 0, 31, 0, 0 } },
					{ '/', { 1, 1, 2, 4, 8, 16, 16 } }, { '-', { 0, 0, 0, 31, 0, 0, 0 } }, { '(', { 2, 4, 8, 8, 8, 4, 2 } }, { ')', { 8, 4, 2, 2, 2, 4, 8 } },
				};
				int32 Cx = X;
				for (const TCHAR Ch : S.ToUpper())
				{
					if (const TArray<uint8>* G = Font.Find(Ch))
					{
						for (int32 Row = 0; Row < 7; ++Row)
						{
							for (int32 Col = 0; Col < 5; ++Col)
							{
								if ((*G)[Row] & (16 >> Col)) { Set(Cx + Col, Y + Row, C); }
							}
						}
					}
					Cx += 6;
				}
			}
		};
	}

	bool RenderPreview(const FString& Path, const FGridD& Terrain, const FRoadGraph& G, const TArray<FBuilding>& Buildings, const TArray<FLandmark>& Landmarks)
	{
		const double Scale = 0.5;   // px per metre
		FCanvas Cv;
		Cv.W = int32(W * Scale);
		Cv.H = int32(H * Scale);
		Cv.Px.SetNumZeroed(Cv.W * Cv.H);
		double TMin = 1e18, TMax = -1e18;
		for (int32 Py = 0; Py < Cv.H; ++Py)
		{
			for (int32 Px = 0; Px < Cv.W; ++Px)
			{
				const double V = Terrain(Py * 2, Px * 2);
				TMin = FMath::Min(TMin, V);
				TMax = FMath::Max(TMax, V);
			}
		}
		static const TMap<FString, FColor> DistrictColors = {
			{ TEXT("PortoVecchio"), FColor(205, 170, 120) }, { TEXT("MoloSalvi"), FColor(160, 160, 170) }, { TEXT("CittaAlta"), FColor(220, 190, 150) },
			{ TEXT("LeFornaci"), FColor(170, 120, 95) }, { TEXT("Lungomare"), FColor(235, 215, 160) }, { TEXT("BorgoMarino"), FColor(210, 200, 175) },
			{ TEXT("LeColline"), FColor(150, 175, 110) }, { TEXT("MonteFerrale"), FColor(125, 135, 120) } };
		for (int32 Py = 0; Py < Cv.H; ++Py)   // Py counts north from the south edge; the image is flipped below
		{
			for (int32 Px = 0; Px < Cv.W; ++Px)
			{
				const double X = Px / Scale, Y = Py / Scale + YMin;
				const EWater Wk = OnMolo(X, Y) ? EWater::None : WaterKind(X, Y);
				FColor C;
				if (Wk != EWater::None)
				{
					C = Wk != EWater::River ? FColor(60, 110, 170) : FColor(70, 130, 185);
				}
				else
				{
					const int32 D = DistrictAt(X, Y);
					const FColor Base = D == INDEX_NONE ? FColor(180, 180, 180) : DistrictColors[Districts[D].Name];
					const double K = 0.65 + 0.35 * FMath::Clamp((Terrain(Py * 2, Px * 2) - TMin) / FMath::Max(1e-6, TMax - TMin), 0.0, 1.0);
					C = FColor(uint8(Base.R * K), uint8(Base.G * K), uint8(Base.B * K));
				}
				Cv.Px[(Cv.H - 1 - Py) * Cv.W + Px] = C;
			}
		}
		auto ToPx = [&](double X, double Y) { return FVector2D(X * Scale, (YMin + H - Y) * Scale); };
		for (const FSeg& S : G.Segments)
		{
			const FNode& A = G.Nodes[S.A];
			const FNode& B = G.Nodes[S.B];
			FColor Col(40, 40, 40);
			if (!S.Bridge.IsEmpty()) { Col = FColor(250, 250, 250); }
			if (!S.Gate.IsEmpty()) { Col = FColor(220, 30, 30); }
			const FVector2D Pa = ToPx(A.X, A.Y), Pb = ToPx(B.X, B.Y);
			Cv.Line(Pa.X, Pa.Y, Pb.X, Pb.Y, FMath::Max(1, int32(S.Width * Scale * 0.6)), Col);
		}
		for (const FBuilding& B : Buildings)
		{
			const double C = std::cos(Radians(B.Yaw)), Sn = std::sin(Radians(B.Yaw));
			const double Hw = B.SW / 2, Hd = B.SD / 2;
			TArray<FVector2D> Poly;
			for (const FVector2D& UV : { FVector2D(-Hw, -Hd), FVector2D(Hw, -Hd), FVector2D(Hw, Hd), FVector2D(-Hw, Hd) })
			{
				Poly.Add(ToPx(B.PX + UV.X * C - UV.Y * Sn, B.PY + UV.X * Sn + UV.Y * C));
			}
			Cv.Polygon(Poly, FColor(150, 150, 155), FColor(105, 105, 110));
		}
		for (const FLandmark& L : Landmarks)
		{
			const FVector2D P = ToPx(L.Pos.X, L.Pos.Y);
			const double Hw = L.FW * Scale / 2, Hd = L.FD * Scale / 2;
			Cv.Polygon({ FVector2D(P.X - Hw, P.Y - Hd), FVector2D(P.X + Hw, P.Y - Hd), FVector2D(P.X + Hw, P.Y + Hd), FVector2D(P.X - Hw, P.Y + Hd) },
				FColor(255, 230, 0), FColor(0, 0, 0));
			Cv.Text(int32(P.X) + 6, int32(P.Y) - 6, L.Name, FColor(0, 0, 0));
		}
		for (const FDistrict& D : Districts)
		{
			const FVector2D P = ToPx((D.Rect.X0 + D.Rect.X1) / 2, D.Rect.Y1 - 25);
			Cv.Text(int32(P.X) - 30, int32(P.Y), D.Name, FColor(255, 255, 255));
		}
		Cv.Text(8, 8, TEXT("MAREVIA LAYOUT  N UP  0.5 PX/M   RED = GATED ROAD   WHITE = BRIDGE   YELLOW = LANDMARK"), FColor(255, 255, 255));
		FImage Img(Cv.W, Cv.H, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		FMemory::Memcpy(Img.RawData.GetData(), Cv.Px.GetData(), Cv.Px.Num() * sizeof(FColor));
		return FImageUtils::SaveImageByExtension(*Path, Img);
	}
}
