#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "ImageCore.h"
#include "ImageUtils.h"

namespace OCWorld
{
	/** The layout heightmap, sampled in world cm: roads drape over exactly what the landscape imported. */
	struct FHeightField
	{
		FImage Image;
		double OriginX = 0.0, NorthEdge = 0.0, MinM = 0.0, RangeM = 1.0;

		bool Load(const FString& Png, const TSharedPtr<FJsonObject>& Root)
		{
			const TArray<TSharedPtr<FJsonValue>>* Origin;
			const TArray<TSharedPtr<FJsonValue>>* Size;
			const TArray<TSharedPtr<FJsonValue>>* Range;
			if (!Root || !Root->TryGetArrayField(TEXT("origin"), Origin) || !Root->TryGetArrayField(TEXT("size"), Size)
				|| !Root->TryGetArrayField(TEXT("height_range"), Range) || !FImageUtils::LoadImage(*Png, Image))
			{
				return false;
			}
			Image.ChangeFormat(ERawImageFormat::G16, EGammaSpace::Linear);
			OriginX = (*Origin)[0]->AsNumber();
			NorthEdge = (*Origin)[1]->AsNumber() + (*Size)[1]->AsNumber(); // PNG row 0 is the north edge
			MinM = (*Range)[0]->AsNumber();
			RangeM = (*Range)[1]->AsNumber() - MinM;
			return true;
		}

		double Pixel(int32 Col, int32 Row) const
		{
			Col = FMath::Clamp(Col, 0, Image.SizeX - 1);
			Row = FMath::Clamp(Row, 0, Image.SizeY - 1);
			return Image.AsG16()[static_cast<int64>(Row) * Image.SizeX + Col];
		}

		/** Bilinear height in cm at world X, Y (cm). Layout metres: x = X/100, y = -Y/100. */
		double HeightCm(double X, double Y) const
		{
			const double Col = X / 100.0 - OriginX;
			const double Row = NorthEdge + Y / 100.0;
			const int32 C0 = FMath::FloorToInt32(Col), R0 = FMath::FloorToInt32(Row);
			const double FX = Col - C0, FY = Row - R0;
			const double V = FMath::Lerp(FMath::Lerp(Pixel(C0, R0), Pixel(C0 + 1, R0), FX),
				FMath::Lerp(Pixel(C0, R0 + 1), Pixel(C0 + 1, R0 + 1), FX), FY);
			return 100.0 * (MinM + V / 65535.0 * RangeM);
		}

		FVector3f NormalAt(double X, double Y) const
		{
			const double DX = HeightCm(X + 100.0, Y) - HeightCm(X - 100.0, Y);
			const double DY = HeightCm(X, Y + 100.0) - HeightCm(X, Y - 100.0);
			return FVector3f(FVector(-DX, -DY, 200.0).GetSafeNormal());
		}
	};
}
