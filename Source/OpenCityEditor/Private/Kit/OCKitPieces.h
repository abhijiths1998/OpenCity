#pragma once

// The Marevia modular kit pieces (Tools/kit/KIT_SPEC.md: names, sizes, pivots, slots, collision).
// Formerly Tools/blender/make_kit.py; built here as polygon soups (Kit/OCKitGeom.h) and written
// straight to SM_Kit_* static meshes by OCArt::BuildKit.

#include "Kit/OCKitGeom.h"

namespace OCKit
{
	/** Material slots, in slot order (the paint slots are street fixtures; Paint = colour per instance). */
	extern const TCHAR* const MatOrder[12];

	struct FUcx
	{
		bool bHull = false;
		double Box[6] = { 0, 0, 0, 0, 0, 0 };   // x0, x1, y0, y1, z0, z1
		TArray<FVector3d> Hull;
	};

	struct FPiece
	{
		FString Name;
		FString Kind = TEXT("facade");   // facade | roof | prop | fixture
		TArray<TPair<FSoup, int32>> Parts;
		TArray<FUcx> Ucx;
		void Add(FSoup S, const TCHAR* Mat);
		void Box(double X0, double X1, double Y0, double Y1, double Z0, double Z1)
		{
			FUcx U;
			U.Box[0] = X0; U.Box[1] = X1; U.Box[2] = Y0; U.Box[3] = Y1; U.Box[4] = Z0; U.Box[5] = Z1;
			Ucx.Add(U);
		}
	};

	/** Every kit piece, in the generator's order. */
	TArray<TFunction<FPiece()>> Builders();
}
