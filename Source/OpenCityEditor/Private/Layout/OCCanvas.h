#pragma once

// A small RGB raster for top-down plan images (the layout preview, the network plan): thick lines,
// convex polygons, ellipses, rectangles and a 5x7 bitmap font.

#include "CoreMinimal.h"
#include <cmath>

namespace OCLayout
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

		void FillRect(double X0, double Y0, double X1, double Y1, FColor C)
		{
			for (int32 Y = int32(std::floor(Y0)); Y <= int32(std::floor(Y1)); ++Y)
			{
				for (int32 X = int32(std::floor(X0)); X <= int32(std::floor(X1)); ++X) { Set(X, Y, C); }
			}
		}

		void Rect(double X0, double Y0, double X1, double Y1, int32 Width, FColor C)
		{
			for (int32 i = 0; i < Width; ++i)
			{
				Line(X0 + i, Y0 + i, X1 - i, Y0 + i, 1, C);
				Line(X0 + i, Y1 - i, X1 - i, Y1 - i, 1, C);
				Line(X0 + i, Y0 + i, X0 + i, Y1 - i, 1, C);
				Line(X1 - i, Y0 + i, X1 - i, Y1 - i, 1, C);
			}
		}

		/** A filled ellipse in the box (X0, Y0)-(X1, Y1) with a one-pixel outline. */
		void Ellipse(double X0, double Y0, double X1, double Y1, FColor Fill, FColor Outline)
		{
			const double Cx = (X0 + X1) / 2, Cy = (Y0 + Y1) / 2, Rx = (X1 - X0) / 2, Ry = (Y1 - Y0) / 2;
			for (int32 Y = int32(std::floor(Y0)); Y <= int32(std::ceil(Y1)); ++Y)
			{
				for (int32 X = int32(std::floor(X0)); X <= int32(std::ceil(X1)); ++X)
				{
					const double D = FMath::Square((X + 0.5 - Cx) / Rx) + FMath::Square((Y + 0.5 - Cy) / Ry);
					if (D <= 1.0) { Set(X, Y, D > 0.7 ? Outline : Fill); }
				}
			}
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
				{ '/', { 1, 1, 2, 4, 8, 16, 16 } }, { '-', { 0, 0, 0, 31, 0, 0, 0 } }, { '(', { 2, 4, 8, 8, 8, 4, 2 } }, { ')', { 8, 4, 2, 2, 2, 4, 8 } }, { '_', { 0, 0, 0, 0, 0, 0, 31 } }, { ',', { 0, 0, 0, 0, 12, 4, 8 } }, { ':', { 0, 12, 12, 0, 12, 12, 0 } },
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
