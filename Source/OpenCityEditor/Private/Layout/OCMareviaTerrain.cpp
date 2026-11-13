// Water, districts and terrain for the Marevia plan (see OCMareviaLayout.h). The heightmap math keeps
// NumPy's float32/float64 split of the original generator, so no fused multiply-adds either.
#pragma clang fp contract(off)

#include "Layout/OCMareviaLayout.h"

#include "Async/ParallelFor.h"
#include "OCLog.h"
#include <cmath>

namespace OCLayout
{
	// --- Water ------------------------------------------------------------------------------------

	// Fiume Ardo centreline from the dam down to the sea: (x, y) points, south to north.
	const TArray<FPt> River = {
		{ X(1000), -40, true }, { X(990), 150, true }, { X(1010), 400, true }, { X(985), 700, true }, { X(1000), 1000, true },
		{ X(1030), 1300, true }, { X(1010), 1600, true }, { X(1050), 1850, true }, { X(1110), 2100, true }, { X(1090), 2300, true },
		{ X(1120), 2440, true } };
	const FRect Darsena = { X(260), 60, X(820), 230 };       // inner harbour basin, open to the sea at the west
	const FRect Reservoir = { X(880), 2470, X(1380), 2690 }; // behind the dam
	const FRect Molo = { X(260), -260, X(840), 60 };         // the terminal pier, reachable only by the swing bridge
	const FPt Breakwater[2] = { { X(840), -200, true }, { X(930), -240, true } };  // out to the lighthouse

	double CoastY(double XV)
	{
		// The beach curves gently; the port side sits a little further out.
		return 60.0 + 25.0 * std::sin(XV / X(400.0)) + (XV > X(1050) ? 30.0 : 0.0);
	}

	double RiverHalfWidth(double Y)
	{
		if (Y < 260)
		{
			return 25.0 + (260 - Y) * 0.45;   // the estuary widens toward the sea
		}
		return Y < 2100 ? 25.0 : 14.0;        // a narrower mountain stream above the hills
	}

	double RiverX(double Y)
	{
		for (int32 i = 0; i + 1 < River.Num(); ++i)
		{
			const FPt& A = River[i];
			const FPt& B = River[i + 1];
			if (A.Y <= Y && Y <= B.Y)
			{
				const double T = (Y - A.Y) / (B.Y - A.Y);
				return A.X + (B.X - A.X) * T;
			}
		}
		return Y < River[0].Y ? River[0].X : River.Last().X;
	}

	EWater WaterKind(double XV, double Y)
	{
		if (Y < CoastY(XV) || XV < WestCliffX)
		{
			return EWater::Sea;
		}
		if (Darsena.X0 <= XV && XV <= Darsena.X1 && Darsena.Y0 <= Y && Y <= Darsena.Y1)
		{
			return EWater::Darsena;
		}
		if (Reservoir.X0 <= XV && XV <= Reservoir.X1 && Reservoir.Y0 <= Y && Y <= Reservoir.Y1)
		{
			return EWater::Reservoir;
		}
		if (Y < DamY && std::fabs(XV - RiverX(Y)) < RiverHalfWidth(Y))
		{
			return EWater::River;
		}
		return EWater::None;
	}

	bool OnMolo(double XV, double Y) { return Molo.X0 <= XV && XV <= Molo.X1 && Molo.Y0 <= Y && Y <= Molo.Y1; }
	bool IsLand(double XV, double Y) { return OnMolo(XV, Y) || WaterKind(XV, Y) == EWater::None; }

	bool SegmentOnLand(double X0, double Y0, double X1, double Y1, double Step)
	{
		const int32 N = FMath::Max(2, int32(std::hypot(X1 - X0, Y1 - Y0) / Step));
		for (int32 T = 0; T <= N; ++T)
		{
			if (!IsLand(X0 + (X1 - X0) * T / N, Y0 + (Y1 - Y0) * T / N))
			{
				return false;
			}
		}
		return true;
	}

	// --- Districts (axis-aligned; roads inside get an organic jitter and rotation) ------------------

	const TArray<FDistrict> Districts = {
		{ TEXT("PortoVecchio"), { X(60), 230, X(975), 880 }, TEXT("Story.Mission.Funerale"), TEXT("grid90") },
		{ TEXT("MoloSalvi"), Molo, TEXT("Story.Act2.Start"), TEXT("terminal") },
		{ TEXT("CittaAlta"), { X(60), 880, X(975), 1680 }, TEXT(""), TEXT("oldtown") },
		{ TEXT("LeFornaci"), { X(1030), 900, X(1980), 1680 }, TEXT(""), TEXT("grid120") },
		{ TEXT("Lungomare"), { X(1160), 90, X(1980), 900 }, TEXT("Story.Mission.LaFornace"), TEXT("beach") },
		{ TEXT("BorgoMarino"), { X(1030), 90, X(1160), 900 }, TEXT("Story.Mission.LaFornace"), TEXT("village") },
		{ TEXT("LeColline"), { X(60), 1680, X(1980), 2160 }, TEXT("Story.Mission.StradaCostiera"), TEXT("hills") },
		{ TEXT("MonteFerrale"), { X(60), 2160, X(1980), 2724 }, TEXT("Story.Mission.LaCava"), TEXT("mountain") },
	};

	int32 DistrictIndex(const TCHAR* Name)
	{
		for (int32 i = 0; i < Districts.Num(); ++i)
		{
			if (FCString::Strcmp(Districts[i].Name, Name) == 0)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	int32 DistrictAt(double XV, double Y)
	{
		static const int32 MoloIdx = DistrictIndex(TEXT("MoloSalvi"));
		if (OnMolo(XV, Y))
		{
			return MoloIdx;
		}
		for (int32 i = 0; i < Districts.Num(); ++i)
		{
			const FRect& R = Districts[i].Rect;
			if (i != MoloIdx && R.X0 <= XV && XV <= R.X1 && R.Y0 <= Y && Y <= R.Y1)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	// --- Noise ----------------------------------------------------------------------------------

	namespace
	{
		/** Smooth value noise in [0, 1]: random lattice every `Cell` metres, smoothstep-interpolated (float64). */
		TArray<double> ValueNoise(int32 Ny, int32 Nx, int32 Cell, FNpRandom& Rng)
		{
			const int32 Gy = Ny / Cell + 3, Gx = Nx / Cell + 3;
			TArray<double> Lattice;
			Lattice.SetNumUninitialized(Gy * Gx);
			for (double& L : Lattice)
			{
				L = Rng.Random();
			}
			const double Oy = Rng.Random() * Cell, Ox = Rng.Random() * Cell;  // a random offset per octave
			TArray<int32> Y0, X0;
			TArray<double> Fy, Fx;
			Y0.SetNum(Ny); Fy.SetNum(Ny); X0.SetNum(Nx); Fx.SetNum(Nx);
			for (int32 i = 0; i < Ny; ++i)
			{
				const double Y = (double(i) + Oy) / Cell;
				Y0[i] = int32(std::floor(Y));
				double F = Y - Y0[i];
				Fy[i] = F * F * (3 - 2 * F);
			}
			for (int32 i = 0; i < Nx; ++i)
			{
				const double XV = (double(i) + Ox) / Cell;
				X0[i] = int32(std::floor(XV));
				double F = XV - X0[i];
				Fx[i] = F * F * (3 - 2 * F);
			}
			TArray<double> Out;
			Out.SetNumUninitialized(Ny * Nx);
			ParallelFor(Ny, [&](int32 r)
			{
				const double* L0 = &Lattice[Y0[r] * Gx];
				const double* L1 = &Lattice[(Y0[r] + 1) * Gx];
				for (int32 c = 0; c < Nx; ++c)
				{
					const double A = L0[X0[c]], B = L0[X0[c] + 1], C = L1[X0[c]], D = L1[X0[c] + 1];
					const double Top = A + (B - A) * Fx[c];
					const double Bot = C + (D - C) * Fx[c];
					Out[r * Nx + c] = Top + (Bot - Top) * Fy[r];
				}
			});
			return Out;
		}

		/** Fractal sum of value noise, normalised to about [-1, 1] (ridged: [0, 1], sharp crests). float32. */
		TArray<float> Fbm(int32 BaseCell, int32 Octaves, FNpRandom& Rng, bool bRidged)
		{
			TArray<float> Total;
			Total.Init(0.0f, NX * NY);
			double Amp = 1.0, Norm = 0.0, Cell = BaseCell;
			for (int32 o = 0; o < Octaves; ++o)
			{
				const TArray<double> N = ValueNoise(NY, NX, FMath::Max(4, int32(Cell)), Rng);
				ParallelFor(NY, [&](int32 r)
				{
					for (int32 c = 0; c < NX; ++c)
					{
						const int32 i = r * NX + c;
						const double V = bRidged ? std::pow(1.0 - std::fabs(2.0 * N[i] - 1.0), 1.3) : 2.0 * N[i] - 1.0;
						Total[i] = float(double(Total[i]) + Amp * V);   // float32 += float64 computes in float64
					}
				});
				Norm += Amp;
				Amp *= 0.5;
				Cell /= 2.0;
			}
			const float NormF = float(Norm);
			for (float& T : Total)
			{
				T = T / NormF;
			}
			return Total;
		}

		inline float Clip01(float V) { return V < 0.0f ? 0.0f : (V > 1.0f ? 1.0f : V); }
		inline double Clip01(double V) { return V < 0.0 ? 0.0 : (V > 1.0 ? 1.0 : V); }

		/** numpy.interp for one x (dres = slope * (x - xp[j]) + fp[j]). */
		double Interp(double XV, const TArray<double>& Xp, const TArray<double>& Fp)
		{
			const int32 N = Xp.Num();
			if (XV < Xp[0]) { return Fp[0]; }
			if (XV > Xp[N - 1]) { return Fp[N - 1]; }
			if (XV == Xp[N - 1]) { return Fp[N - 1]; }
			int32 j = 0;
			while (j + 1 < N && Xp[j + 1] <= XV) { ++j; }
			if (Xp[j] == XV) { return Fp[j]; }
			const double Slope = (Fp[j + 1] - Fp[j]) / (Xp[j + 1] - Xp[j]);
			return Slope * (XV - Xp[j]) + Fp[j];
		}
	}

	FGridD MakeHeightmap(FNpRandom& Rng, FTerrainInfo& Info)
	{
		// float32 stage: xs, ys and h are float32 arrays in NumPy until the coast mixes in float64.
		TArray<float> Hf;
		Hf.Init(4.0f, NX * NY);
		auto Ys = [](int32 r) { return float(r) + float(YMin); };
		auto Xs = [](int32 c) { return float(c); };
		// Gentle rise inland from the coast; Città Alta's rounded hill; Le Colline's ramp, then Monte Ferrale.
		ParallelFor(NY, [&](int32 r)
		{
			const float Y = Ys(r);
			for (int32 c = 0; c < NX; ++c)
			{
				float& HV = Hf[r * NX + c];
				HV = HV + Clip01((Y - 200.0f) / 700.0f) * 8.0f;
				const float D = std::hypot((Xs(c) - float(X(480))) / float(X(420.0)), (Y - 1280.0f) / 380.0f);
				HV = HV + 48.0f * std::pow(Clip01(1.0f - D), 1.6f);
				HV = HV + std::pow(Clip01((Y - 1650.0f) / 450.0f), 1.3f) * 110.0f;
				HV = HV + std::pow(Clip01((Y - 2150.0f) / 620.0f), 1.5f) * 330.0f;
			}
		});
		// Natural relief: gentle rolling noise everywhere, ridged mountains in the north.
		const TArray<float> Gentle = Fbm(400, 5, Rng, false);
		TArray<float> Ridges = Fbm(700, 4, Rng, true);
		const TArray<float> Plain = Fbm(350, 4, Rng, false);
		for (int32 i = 0; i < Ridges.Num(); ++i)
		{
			Ridges[i] = 0.75f * Ridges[i] + 0.25f * (Plain[i] * 0.5f + 0.5f);
		}
		TArray<float> Mask;
		Mask.SetNum(NY);
		for (int32 r = 0; r < NY; ++r)
		{
			Mask[r] = std::pow(Clip01((Ys(r) - 2050.0f) / 400.0f), 1.2f);
		}
		ParallelFor(NY, [&](int32 r)
		{
			const float Y = Ys(r);
			const float Roll = 2.0f + 10.0f * Clip01((Y - 1500.0f) / 600.0f);
			for (int32 c = 0; c < NX; ++c)
			{
				const int32 i = r * NX + c;
				Hf[i] = Hf[i] + Gentle[i] * Roll;   // flat-ish city, rolling hills
				Hf[i] = Hf[i] + Ridges[i] * 170.0f * Mask[r];
			}
		});
		// Weathering: soften the mountains with a box blur (two passes, 15 m), keep the lowlands sharp.
		TArray<float> Soft = Hf;
		{
			const int32 K = 15;
			TArray<float> Tmp;
			Tmp.SetNum(NX * NY);
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				ParallelFor(NY, [&](int32 r)
				{
					TArray<float> C;
					C.SetNum(NX + 2 * K);
					float Acc = 0.0f;
					for (int32 i = 0; i < NX + 2 * K; ++i)
					{
						Acc = Acc + Soft[r * NX + FMath::Clamp(i - K, 0, NX - 1)];
						C[i] = Acc;
					}
					for (int32 j = 0; j < NX; ++j)
					{
						Tmp[r * NX + j] = (C[j + 2 * K] - C[j]) / float(2 * K);
					}
				});
				ParallelFor(NX, [&](int32 c)
				{
					TArray<float> C;
					C.SetNum(NY + 2 * K);
					float Acc = 0.0f;
					for (int32 i = 0; i < NY + 2 * K; ++i)
					{
						Acc = Acc + Tmp[FMath::Clamp(i - K, 0, NY - 1) * NX + c];
						C[i] = Acc;
					}
					for (int32 j = 0; j < NY; ++j)
					{
						Soft[j * NX + c] = (C[j + 2 * K] - C[j]) / float(2 * K);
					}
				});
			}
		}
		ParallelFor(NY, [&](int32 r)
		{
			for (int32 c = 0; c < NX; ++c)
			{
				const int32 i = r * NX + c;
				Hf[i] = Hf[i] * (1.0f - Mask[r]) + Soft[i] * Mask[r];
				// West cliffs drop to the sea: a 56-degree cap rising from the shore everywhere.
				Hf[i] = FMath::Min(Hf[i], (Xs(c) - float(WestCliffX)) * 1.5f);
			}
		});

		// float64 from here: the coast term is float64 (np.where of Python floats).
		FGridD Hd(NX, NY, 0.0);
		ParallelFor(NY, [&](int32 r)
		{
			const float Y = Ys(r);
			for (int32 c = 0; c < NX; ++c)
			{
				const float S = 60.0f + 25.0f * std::sin(Xs(c) / float(X(400.0)));
				const double Coast = double(S) + (double(Xs(c)) > X(1050) ? 30.0 : 0.0);
				const double Shore = Clip01((double(Y) - Coast) / 40.0);   // a beach slope over 40 m
				Hd(r, c) = double(Hf[r * NX + c]) * Shore + (-6.0) * (1 - Shore);
				if (Xs(c) >= Darsena.X0 && Xs(c) <= Darsena.X1 && Y >= Darsena.Y0 && Y <= Darsena.Y1)
				{
					Hd(r, c) = -6.0;
				}
			}
		});
		Hf.Empty();

		// River: the bed follows the valley and must (a) always run downhill to the sea and (b) sit below
		// the land beside it. Sample the terrain along the centreline, take bed(y) = min over everything
		// upstream of (terrain - 4 m), clamp at the sea.
		TArray<double> Xp, Fp;
		for (const FPt& P : River) { Xp.Add(P.Y); Fp.Add(P.X); }
		TArray<double> RxRow, BedRow;
		RxRow.SetNum(NY);
		BedRow.SetNum(NY);
		for (int32 r = 0; r < NY; ++r)
		{
			RxRow[r] = Interp(double(r + YMin), Xp, Fp);
			const int32 Col = FMath::Clamp(int32(PyRoundInt(RxRow[r])), 0, W);
			BedRow[r] = Hd(r, Col) - 4.0;
		}
		for (int32 r = NY - 2; r >= 0; --r)
		{
			BedRow[r] = FMath::Min(BedRow[r], BedRow[r + 1]);
		}
		const double SeaRow = CoastY(1000) + 30;
		for (int32 r = 0; r < NY; ++r)
		{
			BedRow[r] = FMath::Max(BedRow[r], -3.0);
			if (double(r + YMin) < SeaRow)
			{
				BedRow[r] = -3.0;
			}
		}
		ParallelFor(NY, [&](int32 r)
		{
			const float Y = Ys(r);
			const double Rhw = Y < 260.0f ? double(25.0f + (260.0f - Y) * 0.45f) : (Y < 2100.0f ? 25.0 : 14.0);
			const double Bed = BedRow[r];
			for (int32 c = 0; c < NX; ++c)
			{
				const double D = std::fabs(double(Xs(c)) - RxRow[r]);
				if (D < Rhw + 12 && Y < float(DamY))
				{
					const double Bank = Clip01((D - Rhw) / 12.0);
					double& HV = Hd(r, c);
					HV = FMath::Min(HV, Bed + Bank * (FMath::Max(HV, Bed) - Bed));
				}
			}
		});

		// Reservoir: the water level comes from the natural terrain around the basin, so the land contains
		// the lake by itself; only the southern outlet (the river valley) is closed by the dam.
		const int32 DamRow = int32(DamY - YMin) - 2;
		const double BedAtDam = BedRow[DamRow];
		const double Cx = (Reservoir.X0 + Reservoir.X1) / 2.0, Cy = (Reservoir.Y0 + Reservoir.Y1) / 2.0;
		const double Ax = (Reservoir.X1 - Reservoir.X0) / 2.0, Ay = (Reservoir.Y1 - Reservoir.Y0) / 2.0;
		double MinShore = TNumericLimits<double>::Max();
		for (int32 Deg = 0; Deg < 360; Deg += 3)
		{
			if (std::fabs(PyMod((Deg - 270) + 180, 360) - 180) < 40)  // skip the southern outlet arc
			{
				continue;
			}
			const double Bx = Cx + Ax * 1.15 * std::cos(Radians(Deg)), By = Cy + Ay * 1.15 * std::sin(Radians(Deg));
			MinShore = FMath::Min(MinShore, Hd(int32(PyRoundInt(By - YMin)), int32(PyRoundInt(Bx))));
		}
		const double Level = FMath::Max(MinShore - 2.0, BedAtDam + 12.0);   // the dam at least 12 m high
		const float LevelBasin = float(Level - 0.5);
		const float LevelF = float(Level);
		ParallelFor(NY, [&](int32 r)
		{
			const float Y = Ys(r);
			for (int32 c = 0; c < NX; ++c)
			{
				// A bowl under the water, and valley sides rising ~80 m per oval radius beyond the shore.
				const float Rr = std::hypot((Xs(c) - float(Cx)) / float(Ax), (Y - float(Cy)) / float(Ay));
				const float Basin = LevelBasin - 12.0f * Clip01(1.0f - Rr * Rr);
				const float Sides = LevelF + 80.0f * FMath::Max(Rr - 1.0f, 0.0f);
				Hd(r, c) = FMath::Min(Hd(r, c), double(Rr < 1.0f ? Basin : Sides));
				// Molo Salvi: a flat concrete pier just above the sea.
				const float Xc = Xs(c);
				if (Xc >= Molo.X0 && Xc <= Molo.X1 && Y >= Molo.Y0 && Y <= Molo.Y1)
				{
					Hd(r, c) = 2.5;
				}
				// The breakwater out to the lighthouse: a 14 m wide causeway, and a pad round the light.
				const float Bx0 = float(Breakwater[0].X), By0 = float(Breakwater[0].Y);
				const float Bdx = float(Breakwater[1].X - Breakwater[0].X), Bdy = float(Breakwater[1].Y - Breakwater[0].Y);
				const float Den = float(Bdx * Bdx + Bdy * Bdy);
				float T = ((Xc - Bx0) * Bdx + (Y - By0) * Bdy) / Den;
				T = Clip01(T);
				const float DistBw = std::hypot(Xc - (Bx0 + T * Bdx), Y - (By0 + T * Bdy));
				if (DistBw < 7.0f && Hd(r, c) < 2.5)
				{
					Hd(r, c) = 2.5;
				}
				if (std::hypot(Xc - float(Breakwater[1].X), Y - float(Breakwater[1].Y)) < 12.0f && Hd(r, c) < 2.5)
				{
					Hd(r, c) = 2.5;
				}
			}
		});
		Info.DamHeight = PyRound(Level + 3.0 - BedAtDam, 1);
		Info.ReservoirLevel = PyRound(Level, 2);
		for (int32 i = 0; i < NY; i += 20)
		{
			if (i + YMin < DamY)
			{
				Info.RiverProfile.Add({ double(i + YMin), RxRow[i], PyRound(BedRow[i], 2) });
			}
		}
		return Hd;
	}

	// --- Grid helpers ---------------------------------------------------------------------------

	template <typename T>
	FGridF BoxBlur(const TGrid<T>& A, int32 R)
	{
		// Rows then columns: pad (r + 1, r) by edge, float64 cumulative sums, window 2r + 1, to float32.
		const int32 SX = A.SizeX, SY = A.SizeY;
		FGridF Mid(SX, SY, 0.0f), Out(SX, SY, 0.0f);
		const double Win = double(2 * R + 1);
		ParallelFor(SY, [&](int32 Row)
		{
			TArray<double> C;
			C.SetNum(SX + 2 * R + 1);
			double Acc = 0.0;
			for (int32 i = 0; i < SX + 2 * R + 1; ++i)
			{
				Acc += double(A(Row, FMath::Clamp(i - (R + 1), 0, SX - 1)));
				C[i] = Acc;
			}
			for (int32 j = 0; j < SX; ++j)
			{
				Mid(Row, j) = float((C[j + 2 * R + 1] - C[j]) / Win);
			}
		});
		ParallelFor(SX, [&](int32 Col)
		{
			TArray<double> C;
			C.SetNum(SY + 2 * R + 1);
			double Acc = 0.0;
			for (int32 i = 0; i < SY + 2 * R + 1; ++i)
			{
				Acc += double(Mid(FMath::Clamp(i - (R + 1), 0, SY - 1), Col));
				C[i] = Acc;
			}
			for (int32 j = 0; j < SY; ++j)
			{
				Out(j, Col) = float((C[j + 2 * R + 1] - C[j]) / Win);
			}
		});
		return Out;
	}
	template FGridF BoxBlur<double>(const FGridD&, int32);
	template FGridF BoxBlur<float>(const FGridF&, int32);

	namespace
	{
		/** numpy.gradient along rows (dy) and columns (dx): central inside, one-sided at the edges. */
		template <typename T>
		void Gradient(const TGrid<T>& A, TGrid<T>& Gy, TGrid<T>& Gx)
		{
			const int32 SX = A.SizeX, SY = A.SizeY;
			Gy = TGrid<T>(SX, SY, T(0));
			Gx = TGrid<T>(SX, SY, T(0));
			ParallelFor(SY, [&](int32 r)
			{
				for (int32 c = 0; c < SX; ++c)
				{
					Gy(r, c) = r == 0 ? (A(1, c) - A(0, c)) / T(1.0) : (r == SY - 1 ? (A(r, c) - A(r - 1, c)) / T(1.0) : (A(r + 1, c) - A(r - 1, c)) / T(2.0));
					Gx(r, c) = c == 0 ? (A(r, 1) - A(r, 0)) / T(1.0) : (c == SX - 1 ? (A(r, c) - A(r, c - 1)) / T(1.0) : (A(r, c + 1) - A(r, c - 1)) / T(2.0));
				}
			});
		}

		/** River corridor and reservoir box per row, as the generator's masks use them. */
		struct FRowRiver { double Rx, Rhw; };
		TArray<FRowRiver> RiverRows()
		{
			TArray<FRowRiver> Out;
			Out.SetNum(NY);
			for (int32 r = 0; r < NY; ++r)
			{
				const double Y = double(r + YMin);
				Out[r] = { RiverX(Y), RiverHalfWidth(Y) };
			}
			return Out;
		}
	}

	// --- Terrain shaping around the roads ------------------------------------------------------------

	namespace
	{
		constexpr double Shoulder = 10.0;      // m of smooth blend from a road's graded corridor back into the land (minimum)
		constexpr double ShoulderMax = 70.0;   // m: deep cuts and fills blend over up to this far (about 1:1.5)
		constexpr double CutRatio = 1.5;       // m of shoulder per m of height difference between road grade and land
		constexpr double Blend = 1.5;          // m: soft-minimum scale where road corridors meet

		struct FCore { int32 X0, X1, Y0, Y1; float Ax, Ay, Dx, Dy, L2, Half, Az, DzSeg, OA, LcA, OB, LcB, NA, NB, SAx, SAy, SBx, SBy; };   // vertical curves: OA, OB cubic terms, NA, NB node shifts, S* node planes
		constexpr double VerticalCurve = 8.0;   // m each side of a node over which grades blend (a rounded crest or dip)
	}

	FGridF FlattenRoads(FGridD& Hm, const FRoadGraph& G)
	{
		// Grade a corridor under every road (not bridges): the surface runs straight between its junction
		// heights, sidewalks included, with a smooth shoulder blending into the land. Where corridors meet,
		// grades blend with a soft minimum over the distance to each road's edge; inside corridors only the
		// roads whose grade is within 1 m of the road the cell is most inside count (a stacked hairpin leg
		// 9 m below no longer tilts the road above).
		const float Inf = std::numeric_limits<float>::infinity();
		FGridF Best(NX, NY, Inf), WSum(NX, NY, 0.0f), TSum(NX, NY, 0.0f), CSum(NX, NY, 0.0f), CNum(NX, NY, 0.0f),
			NearGrade(NX, NY, 0.0f), NearDist(NX, NY, Inf);
		// Vertical curves: the surface is straight between nodes, so a crest or dip at a node broke the grade
		// at once (30% where two 15% alleys meet on a ridge, and cars grounded on it). Each node gets a slope
		// plane fitted to the grades of its roads (least squares, a little damped), and every road out of it
		// bends from the plane's slope along it to its own grade over Lc, with a cubic that keeps the node's
		// height: a bend becomes a smooth crest or dip, and a junction has no kink between any two roads.
		// Not at bridge ends, which stay as they are.
		struct FArm { int32 Seg; FVector2D Dir; double Grade; };
		TArray<TArray<FArm>> Arms;
		TArray<double> NodeLc;
		TSet<int32> Fixed;
		Arms.SetNum(G.Nodes.Num());
		NodeLc.Init(VerticalCurve, G.Nodes.Num());
		for (int32 s = 0; s < G.Segments.Num(); ++s)
		{
			const FSeg& S = G.Segments[s];
			const FNode& A = G.Nodes[S.A];
			const FNode& B = G.Nodes[S.B];
			const double Len = std::hypot(B.X - A.X, B.Y - A.Y);
			if (!S.Bridge.IsEmpty() || Len < 1e-3)
			{
				Fixed.Add(S.A);
				Fixed.Add(S.B);
				continue;
			}
			const FVector2D D((B.X - A.X) / Len, (B.Y - A.Y) / Len);
			Arms[S.A].Add({ s, D, (B.Z - A.Z) / Len });
			Arms[S.B].Add({ s, -D, (A.Z - B.Z) / Len });
			NodeLc[S.A] = FMath::Min(NodeLc[S.A], 0.4 * Len);
			NodeLc[S.B] = FMath::Min(NodeLc[S.B], 0.4 * Len);
		}
		// Per segment end: the cubic's starting slope correction K (plane slope along the road - its grade).
		TArray<double> KA, KB, NodeDrop;
		KA.Init(0.0, G.Segments.Num());
		KB.Init(0.0, G.Segments.Num());
		NodeDrop.Init(0.0, G.Nodes.Num());
		TArray<FVector2D> NodeSlope;   // the plane's slope, which also tilts each road across near the node
		NodeSlope.Init(FVector2D::ZeroVector, G.Nodes.Num());
		for (int32 n = 0; n < G.Nodes.Num(); ++n)
		{
			if (Arms[n].Num() < 2 || Fixed.Contains(n))
			{
				continue;
			}
			// Minimise sum (s . d_i - g_i)^2 + Damp |s|^2 for the plane slope s.
			constexpr double Damp = 0.05;
			double Sxx = Damp, Sxy = 0.0, Syy = Damp, Bx = 0.0, By = 0.0;
			for (const FArm& Arm : Arms[n])
			{
				Sxx += Arm.Dir.X * Arm.Dir.X;
				Sxy += Arm.Dir.X * Arm.Dir.Y;
				Syy += Arm.Dir.Y * Arm.Dir.Y;
				Bx += Arm.Dir.X * Arm.Grade;
				By += Arm.Dir.Y * Arm.Grade;
			}
			const double Det = Sxx * Syy - Sxy * Sxy;
			if (Det < 1e-9)
			{
				continue;
			}
			const FVector2D Slope((Syy * Bx - Sxy * By) / Det, (Sxx * By - Sxy * Bx) / Det);
			// Roads tilt across to the plane only where equal roads meet (3+ arms of one kind and width, 45
			// degrees or more apart, as in the centro grid). A through road keeps its own cross-section: tilted
			// to the plane, the coast road leaned 10% at the Villa Salvi T, and the pass 12% at the quarry
			// spur's shallow fork, and cars stalled on both.
			bool bEqual = Arms[n].Num() >= 3;
			double Steepest = 0.0;
			for (int32 i = 0; i < Arms[n].Num() && bEqual; ++i)
			{
				const FSeg& Si = G.Segments[Arms[n][i].Seg];
				Steepest = FMath::Max(Steepest, std::fabs(Arms[n][i].Grade));
				for (int32 j = i + 1; j < Arms[n].Num() && bEqual; ++j)
				{
					const FSeg& Sj = G.Segments[Arms[n][j].Seg];
					bEqual = Si.Kind == Sj.Kind && Si.Width == Sj.Width
						&& FVector2D::DotProduct(Arms[n][i].Dir, Arms[n][j].Dir) <= std::cos(FMath::DegreesToRadians(45.0));
				}
			}
			if (bEqual)   // never steeper across than the steepest of its roads
			{
				NodeSlope[n] = Slope.Size() > Steepest ? Slope * (Steepest / Slope.Size()) : Slope;
			}
			// Each road's correction is O (1 - d/Lc)^2 + k d (1 - d/Lc)^2, starting at slope -2 O / Lc + k = K.
			// O, shared by the node, takes the mean of the K: at a bend k is 0 and the curve is the parabola,
			// whose slope changes steadily (with O = 0 the cubic steepened the road by K/3 before levelling it).
			double MeanK = 0.0;
			for (const FArm& Arm : Arms[n])
			{
				MeanK += FVector2D::DotProduct(Slope, Arm.Dir) - Arm.Grade;
			}
			MeanK /= Arms[n].Num();
			NodeDrop[n] = -0.5 * NodeLc[n] * MeanK;
			for (const FArm& Arm : Arms[n])
			{
				const double K = FVector2D::DotProduct(Slope, Arm.Dir) - Arm.Grade;
				(G.Segments[Arm.Seg].A == n ? KA : KB)[Arm.Seg] = K - MeanK;
			}
		}
		TArray<FCore> Cores;
		for (int32 s = 0; s < G.Segments.Num(); ++s)
		{
			const FSeg& S = G.Segments[s];
			if (!S.Bridge.IsEmpty())
			{
				continue;
			}
			const FNode& A = G.Nodes[S.A];
			const FNode& B = G.Nodes[S.B];
			const double Half = S.Width / 2.0 + (SidewalkKinds.Contains(S.Kind) ? SidewalkW : 0.0) + 1.0;
			const double Reach = Half + ShoulderMax;
			const int32 X0 = int32(FMath::Max(0.0, FMath::Min(A.X, B.X) - Reach)), X1 = int32(FMath::Min(double(W), FMath::Max(A.X, B.X) + Reach)) + 1;
			const int32 Y0 = int32(FMath::Max(0.0, FMath::Min(A.Y, B.Y) - Reach - YMin)), Y1 = int32(FMath::Min(double(H), FMath::Max(A.Y, B.Y) + Reach - YMin)) + 1;
			const double Dx = B.X - A.X, Dy = B.Y - A.Y;
			const double L2 = Dx * Dx + Dy * Dy;
			if (X1 <= X0 || Y1 <= Y0 || L2 < 1e-6)
			{
				continue;
			}
			Cores.Add({ X0, X1, Y0, Y1, float(A.X), float(A.Y), float(Dx), float(Dy), float(L2), float(Half), float(A.Z), float(B.Z - A.Z),
				float(KA[s]), float(NodeLc[S.A]), float(KB[s]), float(NodeLc[S.B]), float(NodeDrop[S.A]), float(NodeDrop[S.B]),
				float(NodeSlope[S.A].X), float(NodeSlope[S.A].Y), float(NodeSlope[S.B].X), float(NodeSlope[S.B].Y) });
		}
		auto Eval = [](const FCore& K, int32 r, int32 c, float& OutEdge, float& OutGrade)
		{
			const float YY = float(r) + float(YMin), XX = float(c);
			float T = ((XX - K.Ax) * K.Dx + (YY - K.Ay) * K.Dy) / K.L2;
			T = T < 0.0f ? 0.0f : (T > 1.0f ? 1.0f : T);
			const float Ox = XX - (K.Ax + T * K.Dx), Oy = YY - (K.Ay + T * K.Dy);   // off the centreline
			OutEdge = std::hypot(Ox, Oy) - K.Half;
			// The vertical curves round each end; 20 cm below the road surface, so distant (coarser)
			// landscape LODs never poke through.
			const float L = std::sqrt(K.L2);
			const float Da = T * L, Db = (1.0f - T) * L;
			const float Ua = FMath::Max(0.0f, 1.0f - Da / K.LcA), Ub = FMath::Max(0.0f, 1.0f - Db / K.LcB);
			OutGrade = (K.Az + T * K.DzSeg) + (K.NA + K.OA * Da) * Ua * Ua + (K.NB + K.OB * Db) * Ub * Ub - 0.2f;
			// Across the road too the surface follows the node's plane near it, fading out with the curve: each
			// road was level across, so two crossing at a junction on a slope disagreed by their width times the
			// cross slope, and the blend between them left a 0.3 m ridge across a 14.5% alley (Città Alta, 789, 1207).
			OutGrade += (K.SAx * Ox + K.SAy * Oy) * Ua * Ua + (K.SBx * Ox + K.SBy * Oy) * Ub * Ub;
		};
		for (const FCore& K : Cores)
		{
			for (int32 r = K.Y0; r < K.Y1; ++r)
			{
				for (int32 c = K.X0; c < K.X1; ++c)
				{
					float Edge, Grade;
					Eval(K, r, c, Edge, Grade);
					float Cl = Edge < -K.Half ? -K.Half : (Edge > float(ShoulderMax) ? float(ShoulderMax) : Edge);
					const float Kw = std::exp(-Cl / float(Blend));
					WSum(r, c) = WSum(r, c) + Kw;
					TSum(r, c) = TSum(r, c) + Kw * Grade;
					if (Edge + K.Half < NearDist(r, c))   // nearest centreline (a wide road must not win inside a narrow one)
					{
						NearGrade(r, c) = Grade;
						NearDist(r, c) = Edge + K.Half;
					}
					Best(r, c) = FMath::Min(Best(r, c), Edge);
				}
			}
		}
		for (const FCore& K : Cores)
		{
			for (int32 r = K.Y0; r < K.Y1; ++r)
			{
				for (int32 c = K.X0; c < K.X1; ++c)
				{
					float Edge, Grade;
					Eval(K, r, c, Edge, Grade);
					// Weighted by nearness to each centreline, 1 there and 0 at the corridor's edge: averaged evenly,
					// a steep alley's corridor pulled the first 3.75 m of a crossing one up to its own grade and left
					// a 0.5 m step where it ended.
					const bool bUse = Edge <= 0.0f && std::fabs(Grade - NearGrade(r, c)) < 1.0f;
					const float Near = FMath::Min(1.0f, -Edge / K.Half);
					const float Wn = bUse ? Near * Near + 1e-3f : 0.0f;   // the floor keeps a cell on one corridor's edge on it
					CSum(r, c) = CSum(r, c) + Wn * Grade;
					CNum(r, c) = CNum(r, c) + Wn;
				}
			}
		}
		ParallelFor(NY, [&](int32 r)
		{
			for (int32 c = 0; c < NX; ++c)
			{
				double& HV = Hm(r, c);
				double Target = WSum(r, c) > 0.0f ? double(TSum(r, c) / FMath::Max(WSum(r, c), 1e-12f)) : HV;
				if (CNum(r, c) > 1e-6f)
				{
					Target = double(CSum(r, c) / FMath::Max(CNum(r, c), 1e-6f));
				}
				// 1 inside a road, fading to 0 over a shoulder that widens with the cut or fill height.
				const double Need = FMath::Clamp(std::fabs(Target - HV) * CutRatio, Shoulder, ShoulderMax);
				double Wt = 1.0 - double(Best(r, c)) / Need;
				Wt = Wt < 0.0 ? 0.0 : (Wt > 1.0 ? 1.0 : Wt);
				Wt = Wt * Wt * (3 - 2 * Wt);
				HV = HV * (1 - Wt) + Target * Wt;
			}
		});
		return Best;
	}

	void RelaxSlopes(FGridD& Hm, const FGridF& Best)
	{
		// Ease cliffs: where the land is steeper than ~45 degrees, blend toward a blurred copy of itself, so
		// ridged crags and deep road cuts become steep slopes instead of sheer walls. Roads, their pavements
		// and the water stay exactly as graded.
		const TArray<FRowRiver> Rows = RiverRows();
		FGridF Free(NX, NY, 0.0f);
		for (int32 r = 0; r < NY; ++r)
		{
			const int32 Y = r + YMin;
			for (int32 c = 0; c < NX; ++c)
			{
				bool bFree = Best(r, c) > 1.0f && Hm(r, c) > 1.5;
				bFree &= !(std::fabs(double(c) - Rows[r].Rx) < Rows[r].Rhw + 14.0 && Y < DamY + 30);
				bFree &= !(c > Reservoir.X0 - 60 && c < Reservoir.X1 + 60 && Y > Reservoir.Y0 - 60 && Y < Reservoir.Y1 + 60);
				Free(r, c) = bFree ? 1.0f : 0.0f;
			}
		}
		// Feather the mask edge outward only (a blur into the corridors tilted roads beside drops).
		{
			const FGridF Bl = BoxBlur(Free, 3);
			for (int32 i = 0; i < Free.V.Num(); ++i)
			{
				Free.V[i] = Bl.V[i] * Free.V[i];
			}
		}
		const int32 Passes[3][2] = { { 8, 3 }, { 6, 3 }, { 4, 2 } };
		for (const auto& P : Passes)
		{
			for (int32 n = 0; n < P[1]; ++n)
			{
				FGridD Gy, Gx;
				Gradient(Hm, Gy, Gx);
				const FGridF Bl = BoxBlur(Hm, P[0]);
				ParallelFor(NY, [&](int32 r)
				{
					for (int32 c = 0; c < NX; ++c)
					{
						double Steep = (std::hypot(Gx(r, c), Gy(r, c)) - 0.9) / 0.6;
						Steep = (Steep < 0.0 ? 0.0 : (Steep > 1.0 ? 1.0 : Steep)) * double(Free(r, c));   // 0 at ~42 deg, 1 at ~56 deg
						Hm(r, c) = Hm(r, c) * (1.0 - Steep) + double(Bl(r, c)) * Steep;
					}
				});
			}
		}
	}

	int32 ClampSlopes(FGridD& Hm, const FGridF& Best, const FTerrainInfo& Info)
	{
		// Remove sheer walls relax_slopes can't reach: every unprotected cell is held within MaxSlope of its
		// eight neighbours (iterated to a fixed point), so each wall becomes a cut or fill slope that meets
		// the protected level. Roads, pavements and water keep their exact levels.
		constexpr double MaxSlope = 1.6;
		const TArray<FRowRiver> Rows = RiverRows();
		FGridB Free(NX, NY, 0);
		for (int32 r = 0; r < NY; ++r)
		{
			const int32 Y = r + YMin;
			for (int32 c = 0; c < NX; ++c)
			{
				bool bFixed = Best(r, c) <= 1.0f || Hm(r, c) <= 1.5;
				bFixed |= std::fabs(double(c) - Rows[r].Rx) < Rows[r].Rhw && Y < DamY + 30;
				bFixed |= c > Reservoir.X0 && c < Reservoir.X1 && Y > Reservoir.Y0 && Y < Reservoir.Y1 && Hm(r, c) < Info.ReservoirLevel;
				Free(r, c) = bFixed ? 0 : 1;
			}
		}
		struct FShift { int32 Dy, Dx; double D; };
		TArray<FShift> Shifts;
		for (int32 Dy = -1; Dy <= 1; ++Dy)
		{
			for (int32 Dx = -1; Dx <= 1; ++Dx)
			{
				if (Dy || Dx)
				{
					Shifts.Add({ Dy, Dx, MaxSlope * std::hypot(double(Dy), double(Dx)) });
				}
			}
		}
		FGridD New(NX, NY, 0.0);
		int32 It = 0;
		for (; It < 200; ++It)
		{
			TArray<double> RowChange;
			RowChange.Init(0.0, NY);
			ParallelFor(NY, [&](int32 r)
			{
				double Change = 0.0;
				for (int32 c = 0; c < NX; ++c)
				{
					const double HV = Hm(r, c);
					if (!Free(r, c))
					{
						New(r, c) = HV;
						continue;
					}
					double Lo = std::numeric_limits<double>::infinity(), Hi = -std::numeric_limits<double>::infinity();
					for (const FShift& S : Shifts)
					{
						const double N = Hm(FMath::Clamp(r + S.Dy, 0, NY - 1), FMath::Clamp(c + S.Dx, 0, NX - 1));
						Lo = FMath::Min(Lo, N + S.D);
						Hi = FMath::Max(Hi, N - S.D);
					}
					const double V = FMath::Max(FMath::Min(HV, Lo), FMath::Min(Hi, Lo));
					New(r, c) = V;
					Change = FMath::Max(Change, std::fabs(V - HV));
				}
				RowChange[r] = Change;
			});
			Swap(Hm.V, New.V);
			double Change = 0.0;
			for (const double C : RowChange) { Change = FMath::Max(Change, C); }
			if (Change < 0.01)
			{
				return It + 1;
			}
		}
		return It;   // 200 passes without settling
	}

	void SinkJunctions(FGridD& Hm, const FRoadGraph& G)
	{
		// Keep the land at or below each junction's height under its junction surface: the road builder
		// drapes the junction polygon over the land out to the widest road's half width, and between two
		// roads climbing apart the land rose through its corner. The land may still rise away from the node
		// at the steepest grade of the junction's own roads: held flat, every arm climbing away from a
		// junction (in a dip, or on a slope) lay on flat land for its first 1.5 m and then met its graded
		// corridor in a 0.5 m step that stopped cars. It reaches as far as buildings keep off the corners: past
		// 1.5 m the set-back corner of a crest junction stayed a steep bank, and cars cutting it climbed it.
		TArray<int32> Order;
		TMap<int32, int32> Degree;
		TMap<int32, double> Reach, Grade;
		TMap<int32, TArray<int32>> Others;
		for (const FSeg& S : G.Segments)
		{
			if (!S.Bridge.IsEmpty())
			{
				continue;
			}
			const FNode& A = G.Nodes[S.A];
			const FNode& B = G.Nodes[S.B];
			const double SegGrade = std::fabs(B.Z - A.Z) / FMath::Max(std::hypot(B.X - A.X, B.Y - A.Y), 1.0);
			for (const int32 N : { S.A, S.B })
			{
				if (!Degree.Contains(N)) { Order.Add(N); }
				Degree.FindOrAdd(N) += 1;
				double& R = Reach.FindOrAdd(N, 0.0);
				R = FMath::Max(R, S.Width / 2.0 + 3.5);   // as far as buildings keep off the corners (PlaceBuildings CornerClear)
				double& Gr = Grade.FindOrAdd(N, 0.0);
				Gr = FMath::Max(Gr, SegGrade);
				Others.FindOrAdd(N).Add(N == S.A ? S.B : S.A);
			}
		}
		int64 Sunk = 0;
		for (const int32 N : Order)
		{
			bool bCorner = Degree[N] >= 3;
			if (Degree[N] == 2)   // a bend of 45 degrees or more has a set-back corner too
			{
				const FNode& P = G.Nodes[Others[N][0]];
				const FNode& M = G.Nodes[N];
				const FNode& Q = G.Nodes[Others[N][1]];
				const double In = std::atan2(M.Y - P.Y, M.X - P.X), Out = std::atan2(Q.Y - M.Y, Q.X - M.X);
				bCorner = std::fabs(std::remainder(Out - In, 2.0 * PI)) >= FMath::DegreesToRadians(45.0);
			}
			if (!bCorner)
			{
				continue;
			}
			const double XV = G.Nodes[N].X, Y = G.Nodes[N].Y, R = Reach[N];
			const int32 Xi = int32(PyRoundInt(XV)), Yi = int32(PyRoundInt(Y - YMin));
			if (!(0 <= Xi && Xi <= W && 0 <= Yi && Yi <= H))
			{
				continue;
			}
			const double Z0 = Hm(Yi, Xi), Gr = Grade[N];
			const int32 X0 = FMath::Max(0, int32(XV - R)), X1 = FMath::Min(W, int32(XV + R) + 1);
			const int32 Y0 = FMath::Max(0, int32(Y - R - YMin)), Y1 = FMath::Min(H, int32(Y + R - YMin) + 1);
			for (int32 r = Y0; r < Y1; ++r)
			{
				for (int32 c = X0; c < X1; ++c)
				{
					const double D = std::hypot(double(c) - XV, double(r + YMin) - Y);
					const double Top = Z0 + Gr * D;
					if (D <= R && Hm(r, c) > Top)
					{
						Hm(r, c) = Top;
						++Sunk;
					}
				}
			}
		}
		UE_LOG(LogOpenCity, Display, TEXT("junction surfaces: %lld m2 of land lowered to the junction height"), (long long)Sunk);
	}

	// --- Olive terraces ---------------------------------------------------------------------------

	namespace
	{
		constexpr double TerraceStep = 3.0;   // m between olive terraces (bench to bench)
		constexpr double WallStep = 4.0;      // m: retaining-wall panels are 4 m long
		const FRect TerraceArea = { X(60), 1760.0, X(520), 2120.0 };   // Le Colline west: Villa Salvi and the lookout
	}

	TArray<FWallRun> Terraces(FGridD& Hm, const FGridF& Best, const TArray<FLandmark>& Landmarks)
	{
		// On the Le Colline slopes west of the river, the land is stepped into benches TerraceStep m apart
		// that keep a tenth of the slope, away from roads (12 m past their edge), the river, Villa Salvi and
		// the lookout, and only where the slope is 10 to 45%. Returns dry-stone wall runs along each riser
		// ("cut" walls facing downhill, the points 1.5 m in front of the contour so the wall hides the step).
		const double AX0 = TerraceArea.X0, AY0 = TerraceArea.Y0, AX1 = TerraceArea.X1, AY1 = TerraceArea.Y1;
		const int32 R0 = int32(AY0 - YMin), R1 = int32(AY1 - YMin), C0 = int32(AX0), C1 = int32(AX1);
		const int32 SW = C1 - C0 + 1, SH = R1 - R0 + 1;
		FGridD Sub(SW, SH, 0.0);
		for (int32 r = 0; r < SH; ++r) { for (int32 c = 0; c < SW; ++c) { Sub(r, c) = Hm(R0 + r, C0 + c); } }
		const FGridF Hs = BoxBlur(BoxBlur(Sub, 4), 4);   // contours of the land's shape, not its noise
		FGridF Gy, Gx;
		Gradient(Hs, Gy, Gx);
		FGridF Mask(SW, SH, 0.0f);
		for (int32 r = 0; r < SH; ++r)
		{
			const int32 Yi = R0 + r + YMin;
			const double Rx = RiverX(double(Yi)), Rhw = RiverHalfWidth(double(Yi));
			for (int32 c = 0; c < SW; ++c)
			{
				const float Slope = std::hypot(Gx(r, c), Gy(r, c));
				bool bOk = Best(R0 + r, C0 + c) > 12.0f && Slope > 0.10f && Slope < 0.45f && Sub(r, c) > 5.0;
				bOk &= std::fabs(double(float(C0 + c)) - Rx) > Rhw + 25.0;
				for (const FLandmark& L : Landmarks)
				{
					if (AX0 - 60 < L.Pos.X && L.Pos.X < AX1 + 60 && AY0 - 60 < L.Pos.Y && L.Pos.Y < AY1 + 60)
					{
						if (std::fabs(double(float(C0 + c)) - L.Pos.X) < L.FW / 2 + 20 && std::fabs(double(Yi) - L.Pos.Y) < L.FD / 2 + 20)
						{
							bOk = false;
						}
					}
				}
				Mask(r, c) = bOk ? 1.0f : 0.0f;
			}
		}
		FGridF Wt = BoxBlur(Mask, 8);
		int64 Benches = 0;
		for (int32 r = 0; r < SH; ++r)
		{
			for (int32 c = 0; c < SW; ++c)
			{
				float V = (Wt(r, c) - 0.35f) * 2.5f;
				V = V < 0.0f ? 0.0f : (V > 1.0f ? 1.0f : V);
				V = V * V * (3.0f - 2.0f * V);
				Wt(r, c) = V;
				const float Level = std::floor(Hs(r, c) / float(TerraceStep));
				const float Bench = Level * float(TerraceStep) + (Hs(r, c) - Level * float(TerraceStep)) * 0.1f;
				Hm(R0 + r, C0 + c) = Sub(r, c) + double((Bench - Hs(r, c)) * V);
				Benches += V > 0.97f ? 1 : 0;
			}
		}

		// Walls: marching squares on the 2 m grid of the smoothed land at every step level, where the
		// terraces are fully formed (w > 0.97).
		constexpr int32 Gs = 2;
		const int32 GW = (SW + Gs - 1) / Gs, GH = (SH + Gs - 1) / Gs;
		auto Hg = [&](int32 i, int32 j) { return Hs(i * Gs, j * Gs); };
		auto Wg = [&](int32 i, int32 j) { return Wt(i * Gs, j * Gs); };
		struct FCellSeg { int32 K; float Ax, Ay, Bx, By; };
		TArray<FCellSeg> Segs;
		for (int32 i = 0; i + 1 < GH; ++i)
		{
			for (int32 j = 0; j + 1 < GW; ++j)
			{
				const float C[4] = { Hg(i, j), Hg(i, j + 1), Hg(i + 1, j + 1), Hg(i + 1, j) };
				float Lv[4];
				for (int32 q = 0; q < 4; ++q) { Lv[q] = std::floor(C[q] / float(TerraceStep)); }
				const float MinL = FMath::Min(FMath::Min(Lv[0], Lv[1]), FMath::Min(Lv[2], Lv[3]));
				const float MaxL = FMath::Max(FMath::Max(Lv[0], Lv[1]), FMath::Max(Lv[2], Lv[3]));
				const float MinW = FMath::Min(FMath::Min(Wg(i, j), Wg(i, j + 1)), FMath::Min(Wg(i + 1, j + 1), Wg(i + 1, j)));
				if (MinL == MaxL || MinW < 0.97f)
				{
					continue;
				}
				const int32 Corners[4][2] = { { j, i }, { j + 1, i }, { j + 1, i + 1 }, { j, i + 1 } };
				for (int32 K = int32(MinL) + 1; K <= int32(MaxL); ++K)
				{
					const float L = float(K * TerraceStep);
					TArray<TPair<float, float>, TInlineAllocator<4>> Pts;
					for (int32 e = 0; e < 4; ++e)
					{
						const int32 a = e, b = (e + 1) % 4;
						if ((C[a] >= L) != (C[b] >= L))
						{
							const float T = (L - C[a]) / (C[b] - C[a]);
							Pts.Add({ float(Corners[a][0]) + float(Corners[b][0] - Corners[a][0]) * T,
								float(Corners[a][1]) + float(Corners[b][1] - Corners[a][1]) * T });
						}
					}
					for (int32 q = 0; q + 1 < Pts.Num(); q += 2)
					{
						Segs.Add({ K, Pts[q].Key, Pts[q].Value, Pts[q + 1].Key, Pts[q + 1].Value });
					}
				}
			}
		}
		// Chain the cell segments into polylines per level, then 4 m panels in runs of up to 4.
		auto Key = [](int32 K, float X, float Y) { return FIntVector(K, int32(PyRoundInt(double(X * 4.0f))), int32(PyRoundInt(double(Y * 4.0f)))); };
		TMap<FIntVector, TArray<int32>> ByEnd;
		for (int32 Idx = 0; Idx < Segs.Num(); ++Idx)
		{
			ByEnd.FindOrAdd(Key(Segs[Idx].K, Segs[Idx].Ax, Segs[Idx].Ay)).Add(Idx);
			ByEnd.FindOrAdd(Key(Segs[Idx].K, Segs[Idx].Bx, Segs[Idx].By)).Add(Idx);
		}
		TArray<bool> Used;
		Used.Init(false, Segs.Num());
		struct FLine { int32 K; TArray<TPair<float, float>> P; };
		TArray<FLine> Lines;
		for (int32 Idx = 0; Idx < Segs.Num(); ++Idx)
		{
			if (Used[Idx])
			{
				continue;
			}
			Used[Idx] = true;
			FLine Line{ Segs[Idx].K, { { Segs[Idx].Ax, Segs[Idx].Ay }, { Segs[Idx].Bx, Segs[Idx].By } } };
			for (const bool bForward : { true, false })
			{
				while (true)
				{
					const TPair<float, float> End = bForward ? Line.P.Last() : Line.P[0];
					const TArray<int32>* Cands = ByEnd.Find(Key(Line.K, End.Key, End.Value));
					int32 Next = INDEX_NONE;
					for (const int32 M : Cands ? *Cands : TArray<int32>())
					{
						if (!Used[M]) { Next = M; break; }
					}
					if (Next == INDEX_NONE)
					{
						break;
					}
					Used[Next] = true;
					const FCellSeg& S = Segs[Next];
					const TPair<float, float> O = Key(S.K, S.Ax, S.Ay) == Key(Line.K, End.Key, End.Value) ? TPair<float, float>(S.Bx, S.By) : TPair<float, float>(S.Ax, S.Ay);
					if (bForward) { Line.P.Add(O); } else { Line.P.Insert(O, 0); }
				}
			}
			Lines.Add(MoveTemp(Line));
		}
		TArray<FWallRun> Runs;
		double WallM = 0.0;
		for (const FLine& Line : Lines)
		{
			TArray<FPt> Pts;
			for (const TPair<float, float>& P : Line.P)
			{
				Pts.Add({ double(float(C0) + P.Key * float(Gs)), double(float(R0 + YMin) + P.Value * float(Gs)) });
			}
			TArray<FPt> Out = { Pts[0] };
			double Acc = 0.0;
			for (int32 i = 0; i + 1 < Pts.Num(); ++i)
			{
				Acc += Dist(Pts[i], Pts[i + 1]);
				if (Acc >= WallStep)
				{
					Out.Add(Pts[i + 1]);
					Acc = 0.0;
				}
			}
			if (Out.Num() < 2)
			{
				continue;
			}
			const double L = Line.K * TerraceStep;
			for (int32 S0 = 0; S0 < Out.Num() - 1; S0 += 4)
			{
				const int32 Count = FMath::Min(5, Out.Num() - S0);
				if (Count < 2)
				{
					continue;
				}
				const FPt& Mid = Out[S0 + Count / 2];
				const int32 Ii = FMath::Clamp(int32(PyRoundInt((Mid.Y - AY0) / 1.0)), 0, SH - 1);
				const int32 Jj = FMath::Clamp(int32(PyRoundInt(Mid.X - AX0)), 0, SW - 1);
				double Ux = double(Gx(Ii, Jj)), Uy = double(Gy(Ii, Jj));
				double N = std::hypot(Ux, Uy);
				N = N != 0.0 ? N : 1.0;
				Ux /= N;   // uphill
				Uy /= N;
				FWallRun Run;
				Run.Material = TEXT("Stone");
				Run.Type = TEXT("cut");
				Run.OutX = PyRound(Ux, 4);
				Run.OutY = PyRound(Uy, 4);
				Run.bTerrace = true;
				for (int32 q = 0; q < Count; ++q)
				{
					const FPt& P = Out[S0 + q];
					Run.Points.Add({ PyRound(P.X - Ux * 1.5, 2), PyRound(P.Y - Uy * 1.5, 2), PyRound(L + 0.4, 2), PyRound(L - 0.9 * TerraceStep - 0.5, 2) });
				}
				WallM += WallStep * (Run.Points.Num() - 1);
				Runs.Add(MoveTemp(Run));
			}
		}
		UE_LOG(LogOpenCity, Display, TEXT("terraces: %lld m2 of benches, %d dry-stone wall runs, %.0f m"), (long long)Benches, Runs.Num(), WallM);
		return Runs;
	}

	FRectMask RectMask(double CX, double CY, double Wd, double Dp, double Yaw, double Grow)
	{
		FRectMask M;
		const double C = std::cos(Yaw), Sn = std::sin(Yaw);
		const double Hw = Wd / 2 + Grow, Hd = Dp / 2 + Grow;
		const double Rx = std::fabs(C) * Hw + std::fabs(Sn) * Hd, Ry = std::fabs(Sn) * Hw + std::fabs(C) * Hd;
		M.X0 = FMath::Max(0, int32(std::floor(CX - Rx)));
		M.X1 = FMath::Min(W, int32(std::ceil(CX + Rx)) + 1);
		M.Y0 = FMath::Max(0, int32(std::floor(CY - Ry - YMin)));
		M.Y1 = FMath::Min(H, int32(std::ceil(CY + Ry - YMin)) + 1);
		if (!M.Valid())
		{
			return M;
		}
		M.M.SetNumZeroed((M.Y1 - M.Y0) * (M.X1 - M.X0));
		for (int32 r = M.Y0; r < M.Y1; ++r)
		{
			for (int32 c = M.X0; c < M.X1; ++c)
			{
				const double Px = double(c) - CX, Py = double(r + YMin) - CY;
				const double U = Px * C + Py * Sn, V = -Px * Sn + Py * C;
				M.M[(r - M.Y0) * (M.X1 - M.X0) + (c - M.X0)] = (std::fabs(U) <= Hw && std::fabs(V) <= Hd) ? 1 : 0;
			}
		}
		return M;
	}
}
