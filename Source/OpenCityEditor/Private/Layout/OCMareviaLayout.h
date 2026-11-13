#pragma once

// The Marevia world plan generator (UOCLayoutCommandlet): districts, water, landmarks, gates, roads,
// terrain, retaining walls, terraces and building footprints, written to SourceArt/World/.
// Unreal builds the landscape, roads, blockout and gates from these files; traffic (M5), police (M7)
// and the minimap (M9) read the road graph.
//
// Coordinates: metres, x = east (0..3024), y = north (-300..2724; the coast is near y = 60). The sea is
// south (y < coast) and west of the cliffs. Heights are metres above sea level.
//
// Ported from Tools/the layout generator (Python/NumPy) and kept bit-compatible with it: the same
// random streams (OCLayoutCompat.h), the same float32/float64 split as NumPy's arrays, the same rounding
// and the same iteration orders, so the port reproduces the committed city.

#include "CoreMinimal.h"
#include "Layout/OCLayoutCompat.h"

namespace OCLayout
{
	constexpr int32 DefaultSeed = 1974;   // scripts/make_layout.sh -seed=N picks another city
	constexpr int32 W = 3024, H = 3024;   // metres; the landscape is 24 x 24 components of 126 quads at 1 m
	constexpr int32 YMin = -300;          // the map starts 300 m out to sea, so the Molo Salvi pier is on it
	constexpr int32 NX = W + 1, NY = H + 1;
	constexpr double SeaLevel = 0.0;
	// The plan was first laid out for a 2016 m wide map: every east-west plan coordinate is written in
	// those units and widened by XScale through X(); only x is stretched.
	constexpr double XScale = 1.5;
	constexpr double HeightMin = -100.0, HeightMax = 700.0;  // heightmap 0..65535 maps to this range
	inline double X(double V) { return V * XScale; }

	/** A plan point; IntY records whether Python held y as an int (written without ".0"). */
	struct FPt
	{
		double X = 0.0, Y = 0.0;
		bool IntY = false;
		FPt() = default;
		FPt(double InX, double InY, bool bIntY = false) : X(InX), Y(InY), IntY(bIntY) {}
		bool operator==(const FPt& O) const { return X == O.X && Y == O.Y; }
	};

	inline double Dist(double AX, double AY, double BX, double BY) { return std::hypot(AX - BX, AY - BY); }
	inline double Dist(const FPt& A, const FPt& B) { return std::hypot(A.X - B.X, A.Y - B.Y); }
	/** Python's float % (result takes the divisor's sign). */
	inline double PyMod(double A, double B)
	{
		double M = std::fmod(A, B);
		if (M != 0.0 && ((B < 0.0) != (M < 0.0)))
		{
			M += B;
		}
		return M;
	}
	constexpr double Pi = 3.141592653589793;
	inline double Radians(double D) { return D * (Pi / 180.0); }
	inline double Degrees(double R) { return R * (180.0 / Pi); }

	/** Row-major grid over the map: row r is y = r + YMin (row 0 = south edge), column c is x = c. */
	template <typename T>
	struct TGrid
	{
		int32 SizeX = 0, SizeY = 0;
		TArray<T> V;
		TGrid() = default;
		TGrid(int32 InX, int32 InY, T Init) : SizeX(InX), SizeY(InY) { V.Init(Init, InX * InY); }
		T& operator()(int32 Row, int32 Col) { return V[int64(Row) * SizeX + Col]; }
		const T& operator()(int32 Row, int32 Col) const { return V[int64(Row) * SizeX + Col]; }
	};
	using FGridD = TGrid<double>;
	using FGridF = TGrid<float>;
	using FGridB = TGrid<uint8>;

	// --- Water and districts --------------------------------------------------------------------
	enum class EWater : uint8 { None, Sea, Darsena, Reservoir, River };
	struct FRect { double X0, Y0, X1, Y1; };
	extern const TArray<FPt> River;
	extern const FRect Darsena, Reservoir, Molo;
	extern const FPt Breakwater[2];
	constexpr int32 DamY = 2455;
	constexpr double WestCliffX = 60.0;
	double CoastY(double X);
	double RiverHalfWidth(double Y);
	double RiverX(double Y);
	EWater WaterKind(double X, double Y);
	bool OnMolo(double X, double Y);
	bool IsLand(double X, double Y);
	bool SegmentOnLand(double X0, double Y0, double X1, double Y1, double Step = 4.0);

	struct FDistrict { const TCHAR* Name; FRect Rect; const TCHAR* Unlock; const TCHAR* Style; };
	extern const TArray<FDistrict> Districts;
	/** Index into Districts, or INDEX_NONE. */
	int32 DistrictAt(double X, double Y);
	int32 DistrictIndex(const TCHAR* Name);

	// --- Terrain --------------------------------------------------------------------------------
	struct FTerrainInfo
	{
		double ReservoirLevel = 0.0, DamHeight = 0.0;
		struct FProfilePt { double Y, X, Bed; };
		TArray<FProfilePt> RiverProfile;
	};
	FGridD MakeHeightmap(FNpRandom& Rng, FTerrainInfo& Info);
	/** Mean over a (2r+1)^2 window, edges clamped; float64 sums, float32 result (NumPy box_blur). */
	template <typename T> FGridF BoxBlur(const TGrid<T>& A, int32 R);

	// --- Roads ----------------------------------------------------------------------------------
	struct FKind { const TCHAR* Name; int32 Lanes; double Width; int32 Speed; int32 Rank; };
	const FKind& RoadKind(const FString& Name);
	extern const TArray<FKind> RoadKinds;
	extern const TSet<FString> SidewalkKinds;
	constexpr double SidewalkW = 2.5;

	struct FNode { double X, Y, Z; };
	struct FSeg
	{
		int32 A = 0, B = 0;
		FString Kind;
		int32 Lanes = 1;
		double Width = 0.0;
		int32 Speed = 0;
		FString Bridge, Gate;   // empty = None
		bool bWidening = false;
		double Widening = 0.0;
		// INFRA_DESIGN.md metadata (annotate_networks)
		bool bAnnotated = false;
		FString Profile, Paving, Era, Parking;
		bool bZtl = false, bPedestrian = false, bTram = false;
		double Wear = 0.0;
		int32 Uid = 0;          // identity (Python "is"), unique per segment object
	};

	struct FRoadGraph
	{
		TArray<FNode> Nodes;
		TArray<FSeg> Segments;
		const FGridD* Terrain = nullptr;
		int32 NextUid = 1;
		TSet<int32> LoopNodes;   // hairpin loops and turning circles: no other road joins there
		/** Hairpin-loop necks: where the loop leaves one leg and where it joins the other (close in plan),
		 *  and the most their heights may differ so the land between them is no steeper than NeckSlope. */
		struct FNeck { FPt A, B; double Rise; };
		TArray<FNeck> Necks;

		double Height(double X, double Y) const;
		int32 Node(double X, double Y, double Merge = 6.0);
		void Connect(int32 A, int32 B, const FString& Kind, const FString& Bridge = FString(), const FString& Gate = FString());
		TArray<int32> Path(const TArray<FPt>& Points, const FString& Kind, double Merge = 6.0, const FString& Bridge = FString(),
			const FString& Gate = FString());
		FSeg Copy(const FSeg& S) { FSeg C = S; C.Uid = NextUid++; return C; }
	};
	void BuildRoads(FRoadGraph& G, const FGridD& Terrain, const FTerrainInfo& Info, FPyRandom& Rng);
	int32 LimitGrades(FRoadGraph& G, const FTerrainInfo& Info);
	void DropOrphanNodes(FRoadGraph& G);

	// --- Landmarks, walls, buildings --------------------------------------------------------------
	struct FLandmark
	{
		FString Name, District;
		FPt Pos;                 // IntY as planned (moved ones are rounded floats)
		bool bMoved = false;
		double FW, FD;           // footprint (FWInt/FDInt: written as ints)
		bool FWInt = true, FDInt = true;
		int32 Height;
		FString Kind;
	};
	TArray<FLandmark> PlannedLandmarks();
	TArray<FLandmark> SettleLandmarks(const FGridD& Terrain, FRoadGraph& G);

	struct FWallRun
	{
		FString Material, Type;
		double OutX, OutY;
		bool bTerrace = false;
		TArray<TArray<double>> Points;   // [x, y, top, bottom]
	};
	struct FBuilding
	{
		double PX, PY, SW, SD, Yaw, Base, Top;
		int32 Storeys, Style;
		FString District, Landmark;
		// BUILDINGS_PLAN.md phase 2: what the assembler builds here.
		FString Archetype, LmKind;            // e.g. townhouse, palazzo, slabhotel; landmark kind (church, civic...)
		double Age = 0.5, Wealth = 0.5, Upkeep = 0.5;   // city fields at the lot, 0-1
		int32 Street = 0;                     // character seed of the street it fronts
		int32 Lot = -1;                       // volumes of one building share a lot
		int32 On = -1;                        // a recessed top storey: index of the volume it stands on
		int32 Chamfer = 0;                    // 1: the front corner at the start of the width is cut 2 m (corner building)
	};
	extern const TArray<FString> BuildingStyles;

	// Terrain shaping around the roads (OCMareviaTerrain.cpp)
	FGridF FlattenRoads(FGridD& H, const FRoadGraph& G);
	void RelaxSlopes(FGridD& H, const FGridF& Best);
	int32 ClampSlopes(FGridD& H, const FGridF& Best, const FTerrainInfo& Info);
	void SinkJunctions(FGridD& H, const FRoadGraph& G);
	TArray<FWallRun> Terraces(FGridD& H, const FGridF& Best, const TArray<FLandmark>& Landmarks);

	// Placement and metadata (OCMareviaPlace.cpp)
	TArray<FWallRun> RetainingWalls(const FGridD& Terrain, const FRoadGraph& G);
	TArray<FBuilding> PlaceBuildings(const FGridD& Terrain, const FGridF& Best, const FRoadGraph& G, FPyRandom& Rng,
		const TArray<FLandmark>& Landmarks, FString& OutError, int32 Seed);
	TArray<FBuilding> LandmarkBuildings(const FGridD& Terrain, const TArray<FLandmark>& Landmarks);
	TArray<FWallRun> DropWallsInBuildings(const TArray<FWallRun>& Runs, const TArray<FBuilding>& Buildings, double Grow = 2.0);
	struct FJunction { int32 Node; FString Type; };
	bool AnnotateNetworks(FRoadGraph& G, const TArray<FLandmark>& Landmarks, FPyRandom& Rng, const FJsonPtr& Profiles,
		TArray<FJunction>& OutJunctions, FString& OutError);
	void SightTriangles(const FRoadGraph& G, const TArray<FBuilding>& Buildings, TArray<FJunction>& Junctions);

	/** Mask of the grid cells (centres at integer metres) inside a rotated rectangle. */
	struct FRectMask
	{
		int32 Y0 = 0, Y1 = 0, X0 = 0, X1 = 0;
		TArray<uint8> M;   // (Y1 - Y0) x (X1 - X0)
		bool Valid() const { return X1 > X0 && Y1 > Y0; }
		bool At(int32 Row, int32 Col) const { return M[(Row - Y0) * (X1 - X0) + (Col - X0)] != 0; }
	};
	FRectMask RectMask(double CX, double CY, double W, double D, double Yaw, double Grow = 0.0);

	/** The whole pipeline: writes marevia_layout.json, marevia_height.png and marevia_preview.png to OutDir. */
	bool GenerateMarevia(const FString& OutDir, const FString& ProfilesPath, FString& OutError, int32 Seed = DefaultSeed);
	/** Preview picture (OCMareviaPreview.cpp). */
	bool RenderPreview(const FString& Path, const FGridD& Terrain, const FRoadGraph& G, const TArray<FBuilding>& Buildings,
		const TArray<FLandmark>& Landmarks);
}
