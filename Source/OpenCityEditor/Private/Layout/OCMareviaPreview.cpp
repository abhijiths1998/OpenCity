// Top-down preview of the Marevia plan (0.5 px per metre, north up): district colours shaded by height,
// water, roads (gated red, bridges white), building footprints and labelled landmarks.
#include "Layout/OCMareviaLayout.h"

#include "ImageCore.h"
#include "ImageUtils.h"
#include "Layout/OCCanvas.h"
#include <cmath>

namespace OCLayout
{

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
