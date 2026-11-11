#include "Kit/OCKitGeom.h"

#include "CompGeom/PolygonTriangulation.h"
#include "ConstrainedDelaunay2.h"
#include "Curve/GeneralPolygon2.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "GroupTopology.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OCLog.h"
#include "Operations/MeshBevel.h"
#include "Operations/MeshBoolean.h"
#include "MeshSimplification.h"
#include "DynamicMesh/Operations/MergeCoincidentMeshEdges.h"

using namespace UE::Geometry;

namespace OCKit
{
	// ---- Transforms ------------------------------------------------------------------------------

	FM4 FM4::Translation(const FVector3d& T)
	{
		FM4 R;
		R.M[0][3] = T.X; R.M[1][3] = T.Y; R.M[2][3] = T.Z;
		return R;
	}

	FM4 FM4::Rotation(double A, int32 Axis)
	{
		FM4 R;
		const double C = FMath::Cos(A), S = FMath::Sin(A);
		const int32 I = (Axis + 1) % 3, J = (Axis + 2) % 3;   // right-handed: X->(Y,Z), Y->(Z,X), Z->(X,Y)
		R.M[I][I] = C; R.M[I][J] = -S;
		R.M[J][I] = S; R.M[J][J] = C;
		return R;
	}

	FM4 FM4::Scale(const FVector3d& S)
	{
		FM4 R;
		R.M[0][0] = S.X; R.M[1][1] = S.Y; R.M[2][2] = S.Z;
		return R;
	}

	FM4 FM4::TrackZ(const FVector3d& Dir)
	{
		const FVector3d Z = Dir.GetSafeNormal();
		FVector3d Up(0, 0, 1);
		if (FMath::Abs(Z.Dot(Up)) > 0.999) { Up = FVector3d(0, 1, 0); }
		const FVector3d Y = (Up - Z * Up.Dot(Z)).GetSafeNormal();
		const FVector3d X = Y.Cross(Z);
		FM4 R;
		for (int32 r = 0; r < 3; ++r) { R.M[r][0] = X[r]; R.M[r][1] = Y[r]; R.M[r][2] = Z[r]; }
		return R;
	}

	FM4 FM4::operator*(const FM4& B) const
	{
		FM4 R;
		for (int32 r = 0; r < 4; ++r)
		{
			for (int32 c = 0; c < 4; ++c)
			{
				R.M[r][c] = M[r][0] * B.M[0][c] + M[r][1] * B.M[1][c] + M[r][2] * B.M[2][c] + M[r][3] * B.M[3][c];
			}
		}
		return R;
	}

	FVector3d FM4::Apply(const FVector3d& P) const
	{
		return FVector3d(M[0][0] * P.X + M[0][1] * P.Y + M[0][2] * P.Z + M[0][3],
			M[1][0] * P.X + M[1][1] * P.Y + M[1][2] * P.Z + M[1][3],
			M[2][0] * P.X + M[2][1] * P.Y + M[2][2] * P.Z + M[2][3]);
	}

	double FM4::Det3() const
	{
		return M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
			+ M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
	}

	// ---- Primitives ------------------------------------------------------------------------------

	void AddBox(FSoup& S, double X0, double X1, double Y0, double Y1, double Z0, double Z1)
	{
		const int32 Sh = S.NewShell();
		int32 C[2][2][2];
		for (int32 i = 0; i < 2; ++i)
		{
			for (int32 j = 0; j < 2; ++j)
			{
				for (int32 k = 0; k < 2; ++k) { C[i][j][k] = S.Add(FVector3d(i ? X1 : X0, j ? Y1 : Y0, k ? Z1 : Z0)); }
			}
		}
		S.Face(Sh, { C[0][0][0], C[0][0][1], C[0][1][1], C[0][1][0] });   // -X
		S.Face(Sh, { C[1][0][0], C[1][1][0], C[1][1][1], C[1][0][1] });   // +X
		S.Face(Sh, { C[0][0][0], C[1][0][0], C[1][0][1], C[0][0][1] });   // -Y
		S.Face(Sh, { C[0][1][0], C[0][1][1], C[1][1][1], C[1][1][0] });   // +Y
		S.Face(Sh, { C[0][0][0], C[0][1][0], C[1][1][0], C[1][0][0] });   // -Z
		S.Face(Sh, { C[0][0][1], C[1][0][1], C[1][1][1], C[0][1][1] });   // +Z
	}

	FSoup Box(double X0, double X1, double Y0, double Y1, double Z0, double Z1)
	{
		FSoup S;
		AddBox(S, X0, X1, Y0, Y1, Z0, Z1);
		return S;
	}

	static FVector3d AxisMap(int32 Axis, double U, double V, double A)
	{
		// right-handed (u, v, axis) frames
		if (Axis == AX) { return FVector3d(A, U, V); }
		if (Axis == AY) { return FVector3d(V, A, U); }
		return FVector3d(U, V, A);
	}

	void AddPrism(FSoup& S, TArray<FVector2d> Poly, int32 Axis, double A0, double A1, bool bCaps)
	{
		double Area = 0.0;
		for (int32 i = 0; i < Poly.Num(); ++i)
		{
			const FVector2d& P = Poly[i];
			const FVector2d& Q = Poly[(i + 1) % Poly.Num()];
			Area += P.X * Q.Y - Q.X * P.Y;
		}
		if (Area < 0) { Algo::Reverse(Poly); }
		const int32 Sh = S.NewShell();
		const int32 N = Poly.Num();
		TArray<int32> Bot, Top;
		for (const FVector2d& P : Poly) { Bot.Add(S.Add(AxisMap(Axis, P.X, P.Y, A0))); }
		for (const FVector2d& P : Poly) { Top.Add(S.Add(AxisMap(Axis, P.X, P.Y, A1))); }
		for (int32 i = 0; i < N; ++i)
		{
			const int32 j = (i + 1) % N;
			S.Face(Sh, { Bot[i], Bot[j], Top[j], Top[i] });
		}
		if (bCaps)
		{
			S.Face(Sh, Top);
			TArray<int32> B = Bot;
			Algo::Reverse(B);
			S.Face(Sh, B);
		}
	}

	FSoup PrismX(const TArray<FVector2d>& PolyYZ, double X0, double X1, bool bCaps)
	{
		FSoup S;
		AddPrism(S, PolyYZ, AX, X0, X1, bCaps);
		return S;
	}

	void AddCyl(FSoup& S, const FVector3d& Center, double R, double Length, int32 Seg, TCHAR Axis, bool bCaps, double R2)
	{
		if (R2 < 0) { R2 = R; }
		// Blender's create_cone: ring vertex k at (-r sin phi, r cos phi), phi = 2 pi k / seg, radius1 at -depth/2.
		FM4 Rot;
		FVector3d D(0, 0, 1);
		if (Axis == 'X') { Rot = FM4::Rotation(UE_PI / 2, AY); D = FVector3d(1, 0, 0); }
		else if (Axis == 'Y') { Rot = FM4::Rotation(-UE_PI / 2, AX); D = FVector3d(0, 1, 0); }
		const FM4 M = FM4::Translation(Center + D * (Length / 2)) * Rot;
		const int32 Sh = S.NewShell();
		TArray<int32> Bot, Top;
		for (int32 k = 0; k < Seg; ++k)
		{
			const double Phi = 2 * UE_PI * k / Seg;
			Bot.Add(S.Add(M.Apply(FVector3d(-R * FMath::Sin(Phi), R * FMath::Cos(Phi), -Length / 2))));
			Top.Add(S.Add(M.Apply(FVector3d(-R2 * FMath::Sin(Phi), R2 * FMath::Cos(Phi), Length / 2))));
		}
		for (int32 k = 0; k < Seg; ++k)
		{
			const int32 j = (k + 1) % Seg;
			S.Face(Sh, { Bot[k], Bot[j], Top[j], Top[k] });
		}
		if (bCaps)
		{
			S.Face(Sh, Top);
			TArray<int32> B = Bot;
			Algo::Reverse(B);
			S.Face(Sh, B);
		}
	}

	void Transform(FSoup& S, const FM4& M)
	{
		for (FVector3d& P : S.V) { P = M.Apply(P); }
		if (M.Det3() < 0)
		{
			for (FPoly& F : S.F) { Algo::Reverse(F.I); }
		}
	}

	void DelPlane(FSoup& S, int32 Axis, double Val)
	{
		S.F.RemoveAll([&](const FPoly& F)
		{
			for (int32 I : F.I) { if (FMath::Abs(S.V[I][Axis] - Val) >= Eps) { return false; } }
			return true;
		});
	}

	void Append(FSoup& Dst, const FSoup& Src)
	{
		const int32 Base = Dst.V.Num();
		const int32 ShellBase = Dst.NextShell;
		Dst.V.Append(Src.V);
		for (const FPoly& F : Src.F)
		{
			FPoly P = F;
			for (int32& I : P.I) { I += Base; }
			P.Shell += ShellBase;
			Dst.F.Add(MoveTemp(P));
		}
		Dst.NextShell += Src.NextShell;
	}

	// ---- Dynamic mesh round trip -----------------------------------------------------------------

	static FVector3d Newell(const FSoup& S, const FPoly& F)
	{
		FVector3d N(0);
		for (int32 i = 0; i < F.I.Num(); ++i)
		{
			const FVector3d& A = S.V[F.I[i]];
			const FVector3d& B = S.V[F.I[(i + 1) % F.I.Num()]];
			N.X += (A.Y - B.Y) * (A.Z + B.Z);
			N.Y += (A.Z - B.Z) * (A.X + B.X);
			N.Z += (A.X - B.X) * (A.Y + B.Y);
		}
		return N;
	}

	/** Triangles of a polygon (indices into F.I), wound like the polygon. */
	static TArray<FIndex3i> TriangulatePoly(const FSoup& S, const FPoly& F)
	{
		TArray<FIndex3i> Out;
		const int32 N = F.I.Num();
		if (N == 3) { Out.Add(FIndex3i(0, 1, 2)); return Out; }
		if (N == 4)
		{
			// Quads: the shorter diagonal (Blender's BEAUTY on planar quads).
			const double D02 = FVector3d::DistSquared(S.V[F.I[0]], S.V[F.I[2]]), D13 = FVector3d::DistSquared(S.V[F.I[1]], S.V[F.I[3]]);
			if (D02 <= D13) { Out.Add(FIndex3i(0, 1, 2)); Out.Add(FIndex3i(0, 2, 3)); }
			else { Out.Add(FIndex3i(0, 1, 3)); Out.Add(FIndex3i(1, 2, 3)); }
			return Out;
		}
		TArray<FVector3d> P;
		for (int32 I : F.I) { P.Add(S.V[I]); }
		PolygonTriangulation::TriangulateSimplePolygon<double>(P, Out, false);
		const FVector3d Want = Newell(S, F);
		for (FIndex3i& T : Out)
		{
			const FVector3d Nt = (P[T.B] - P[T.A]).Cross(P[T.C] - P[T.A]);
			if (Nt.Dot(Want) < 0) { Swap(T.B, T.C); }
		}
		return Out;
	}

	TArray<FIndex3i> Triangulate(const FSoup& S, const FPoly& F) { return TriangulatePoly(S, F); }

	static void ToDyn(const FSoup& S, FDynamicMesh3& M, int32& OutFailed)
	{
		M.EnableTriangleGroups();
		OutFailed = 0;
		TMap<TPair<int32, FIntVector>, int32> Weld;
		auto Vid = [&](int32 Shell, int32 I)
		{
			const FVector3d& P = S.V[I];
			const FIntVector Q(FMath::RoundToInt(P.X * 1e6), FMath::RoundToInt(P.Y * 1e6), FMath::RoundToInt(P.Z * 1e6));
			const TPair<int32, FIntVector> Key(Shell, Q);
			if (const int32* Found = Weld.Find(Key)) { return *Found; }
			const int32 V = M.AppendVertex(P);
			Weld.Add(Key, V);
			return V;
		};
		for (int32 f = 0; f < S.F.Num(); ++f)
		{
			const FPoly& F = S.F[f];
			for (const FIndex3i& T : TriangulatePoly(S, F))
			{
				const int32 A = Vid(F.Shell, F.I[T.A]), B = Vid(F.Shell, F.I[T.B]), C = Vid(F.Shell, F.I[T.C]);
				if (A == B || B == C || A == C) { continue; }
				// Unreal is left-handed: its outward winding is the reverse of the soups' counter-clockwise faces.
				if (M.AppendTriangle(A, C, B, f + 1) < 0) { ++OutFailed; }
			}
		}
	}

	static FSoup FromDyn(const FDynamicMesh3& M)
	{
		FSoup S;
		const int32 Sh = S.NewShell();
		TMap<int32, int32> Map;
		for (const int32 V : M.VertexIndicesItr()) { Map.Add(V, S.Add(M.GetVertex(V))); }
		for (const int32 T : M.TriangleIndicesItr())
		{
			const FIndex3i Tri = M.GetTriangle(T);
			S.Face(Sh, { Map[Tri.A], Map[Tri.C], Map[Tri.B] });   // back to counter-clockwise
		}
		return S;
	}

	/** Polygroups = maximal sets of edge-connected coplanar triangles. */
	static void PlanarGroups(FDynamicMesh3& M)
	{
		for (const int32 T : M.TriangleIndicesItr()) { M.SetTriangleGroup(T, 0); }
		int32 Next = 1;
		for (const int32 T0 : M.TriangleIndicesItr())
		{
			if (M.GetTriangleGroup(T0) != 0) { continue; }
			const FVector3d N0 = M.GetTriNormal(T0);
			const FVector3d P0 = M.GetTriCentroid(T0);
			const int32 G = Next++;
			TArray<int32> Stack = { T0 };
			M.SetTriangleGroup(T0, G);
			while (!Stack.IsEmpty())
			{
				const int32 T = Stack.Pop();
				const FIndex3i Nb = M.GetTriNeighbourTris(T);
				for (int32 k = 0; k < 3; ++k)
				{
					const int32 U = Nb[k];
					if (U < 0 || M.GetTriangleGroup(U) != 0) { continue; }
					if (M.GetTriNormal(U).Dot(N0) > 1.0 - 1e-7 && FMath::Abs((M.GetTriCentroid(U) - P0).Dot(N0)) < 1e-6)
					{
						M.SetTriangleGroup(U, G);
						Stack.Add(U);
					}
				}
			}
		}
	}

	void Bevel(FSoup& S, double Width, int32 Segs, const TArray<FKeep>& Keep, bool bConvexOnly, double MinAngle)
	{
		FDynamicMesh3 M;
		int32 Failed = 0;
		ToDyn(S, M, Failed);
		PlanarGroups(M);
		FGroupTopology Topo(&M, true);
		TMap<int32, TPair<FVector3d, FVector3d>> GroupFrame;   // group id -> (normal, centroid)
		for (const FGroupTopology::FGroup& G : Topo.Groups)
		{
			FVector3d C(0);
			for (int32 T : G.Triangles) { C += M.GetTriCentroid(T); }
			GroupFrame.Add(G.GroupID, { M.GetTriNormal(G.Triangles[0]), C / FMath::Max(1, G.Triangles.Num()) });
		}
		const double CosLim = FMath::Cos(FMath::DegreesToRadians(MinAngle));
		TArray<int32> Selected;
		double MinTurn = 90.0;   // shallowest corner among the selected edges (degrees between the face normals)
		for (int32 e = 0; e < Topo.Edges.Num(); ++e)
		{
			const FGroupTopology::FGroupEdge& E = Topo.Edges[e];
			if (E.Groups.B < 0 || E.Groups.A < 0) { continue; }
			const TPair<FVector3d, FVector3d>& A = GroupFrame[E.Groups.A];
			const TPair<FVector3d, FVector3d>& B = GroupFrame[E.Groups.B];
			if (A.Key.Dot(B.Key) > CosLim) { continue; }   // face angle below MinAngle
			const FVector3d P = M.GetVertex(E.Span.Vertices[0]);
			if (bConvexOnly && A.Key.Dot(B.Value - P) >= -1e-9) { continue; }
			// Skip edges next to faces narrower than the bevel (Blender clamps such overlaps; FMeshBevel would bulge).
			const FVector3d E0 = M.GetVertex(E.Span.Vertices[0]), E1 = M.GetVertex(E.Span.Vertices.Last());
			const FVector3d Dir = (E1 - E0).GetSafeNormal();
			auto Extent = [&](int32 GroupID)
			{
				double Far = 0.0;
				for (int32 T : Topo.FindGroupByID(GroupID)->Triangles)
				{
					const FIndex3i Tri = M.GetTriangle(T);
					for (int32 k = 0; k < 3; ++k)
					{
						const FVector3d R = M.GetVertex(Tri[k]) - E0;
						Far = FMath::Max(Far, (R - Dir * R.Dot(Dir)).Size());
					}
				}
				return Far;
			};
			if (Extent(E.Groups.A) < Width * 1.1 || Extent(E.Groups.B) < Width * 1.1) { continue; }
			bool bOnKeep = false;
			for (const FKeep& K : Keep)
			{
				bool bAll = true;
				for (int32 V : E.Span.Vertices) { bAll &= FMath::Abs(M.GetVertex(V)[K.Axis] - K.Val) < Eps; }
				bOnKeep |= bAll;
			}
			if (!bOnKeep)
			{
				Selected.Add(e);
				MinTurn = FMath::Min(MinTurn, FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(A.Key.Dot(B.Key), -1.0, 1.0))));
			}
		}
		if (Selected.IsEmpty())
		{
			S = FromDyn(M);
			return;
		}
		FMeshBevel Bev;
		Bev.InsetDistance = Width;
		Bev.NumSubdivisions = FMath::Max(0, Segs - 1);
		// FMeshBevel's round profile is sized for square corners and bulges past shallower ones (Blender's stays inside):
		// scale it down to the part's shallowest bevelled corner.
		Bev.RoundWeight = FMath::Min(1.0, FMath::Sin(FMath::DegreesToRadians(MinTurn) / 2) / FMath::Sin(UE_PI / 4));
		Bev.InitializeFromGroupTopologyEdges(M, Topo, Selected);
		const int32 TrisBefore = M.TriangleCount();
		const bool bApplied = Bev.Apply(M, nullptr);
		UE_LOG(LogOpenCity, Verbose, TEXT("kit bevel: %d groups, %d group edges, %d selected, tris %d -> %d"), Topo.Groups.Num(), Topo.Edges.Num(), Selected.Num(), TrisBefore, M.TriangleCount());
		if (!bApplied)
		{
			UE_LOG(LogOpenCity, Warning, TEXT("kit: bevel of %d edges failed (width %.3f); part left square"), Selected.Num(), Width);
		}
		S = FromDyn(M);
	}

	FSoup BoolDiff(const FSoup& A, const FSoup& Cutter)
	{
		// One cutter (shell) at a time: several thin cutters in one boolean left holes and lost cuts.
		FDynamicMesh3 Out;
		int32 FA = 0;
		ToDyn(A, Out, FA);
		TSet<int32> Shells;
		for (const FPoly& F : Cutter.F) { Shells.Add(F.Shell); }
		for (const int32 Shell : Shells)
		{
			FSoup One;
			One.V = Cutter.V;
			for (const FPoly& F : Cutter.F) { if (F.Shell == Shell) { One.F.Add(F); } }
			FDynamicMesh3 MB, Next;
			int32 FB = 0;
			ToDyn(One, MB, FB);
			FMeshBoolean B(&Out, &MB, &Next, FMeshBoolean::EBooleanOp::Difference);
			B.bSimplifyAlongNewEdges = true;
			if (!B.Compute())
			{
				UE_LOG(LogOpenCity, Warning, TEXT("kit: boolean difference reported problems (%d holes)"), B.CreatedBoundaryEdges.Num());
			}
			Out = MoveTemp(Next);
			FMergeCoincidentMeshEdges Stitch(&Out);
			Stitch.MergeVertexTolerance = 1e-5;
			Stitch.Apply();
		}
		// The cut leaves slivers along every new edge: collapse each flat region to as few triangles as its
		// outline needs, like Blender's exact boolean, which keeps n-gons.
		FQEMSimplification Simplifier(&Out);
		Simplifier.SimplifyToMinimalPlanar(0.01);
		return FromDyn(Out);
	}

	void OrientOutward(FSoup& S)
	{
		// Consistent winding by flood fill across shared edges, then outward by signed volume per component.
		TMap<TPair<int32, int32>, TArray<int32>> EdgeFaces;
		for (int32 f = 0; f < S.F.Num(); ++f)
		{
			const TArray<int32>& I = S.F[f].I;
			for (int32 k = 0; k < I.Num(); ++k)
			{
				const int32 a = I[k], b = I[(k + 1) % I.Num()];
				EdgeFaces.FindOrAdd(TPair<int32, int32>(FMath::Min(a, b), FMath::Max(a, b))).Add(f);
			}
		}
		auto HasDirected = [&](int32 f, int32 a, int32 b)
		{
			const TArray<int32>& I = S.F[f].I;
			for (int32 k = 0; k < I.Num(); ++k) { if (I[k] == a && I[(k + 1) % I.Num()] == b) { return true; } }
			return false;
		};
		TArray<int32> Comp;
		Comp.Init(-1, S.F.Num());
		int32 NumComp = 0;
		for (int32 f0 = 0; f0 < S.F.Num(); ++f0)
		{
			if (Comp[f0] >= 0) { continue; }
			const int32 C = NumComp++;
			TArray<int32> Stack = { f0 };
			Comp[f0] = C;
			while (!Stack.IsEmpty())
			{
				const int32 f = Stack.Pop();
				const TArray<int32> I = S.F[f].I;
				for (int32 k = 0; k < I.Num(); ++k)
				{
					const int32 a = I[k], b = I[(k + 1) % I.Num()];
					for (int32 g : EdgeFaces[TPair<int32, int32>(FMath::Min(a, b), FMath::Max(a, b))])
					{
						if (g == f || Comp[g] >= 0) { continue; }
						if (HasDirected(g, a, b)) { Algo::Reverse(S.F[g].I); }   // neighbours traverse a shared edge oppositely
						Comp[g] = C;
						Stack.Add(g);
					}
				}
			}
		}
		TArray<double> Vol;
		Vol.Init(0.0, NumComp);
		for (int32 f = 0; f < S.F.Num(); ++f)
		{
			const TArray<int32>& I = S.F[f].I;
			for (int32 k = 1; k + 1 < I.Num(); ++k)
			{
				Vol[Comp[f]] += S.V[I[0]].Dot(S.V[I[k]].Cross(S.V[I[k + 1]]));
			}
		}
		for (int32 f = 0; f < S.F.Num(); ++f)
		{
			if (Vol[Comp[f]] < 0) { Algo::Reverse(S.F[f].I); }
		}
	}

	// ---- Text (TrueType outlines) ----------------------------------------------------------------

	namespace
	{
		struct FFont
		{
			TArray<uint8> D;
			uint32 Glyf = 0, Loca = 0, Cmap = 0, Hmtx = 0;
			int32 UnitsPerEm = 2048, NumHMetrics = 0;
			bool bLongLoca = false;

			uint16 U16(uint32 O) const { return uint16(D[O] << 8 | D[O + 1]); }
			int16 I16(uint32 O) const { return int16(U16(O)); }
			uint32 U32(uint32 O) const { return uint32(D[O]) << 24 | uint32(D[O + 1]) << 16 | uint32(D[O + 2]) << 8 | D[O + 3]; }

			bool Load(const FString& Path)
			{
				if (!FFileHelper::LoadFileToArray(D, *Path)) { return false; }
				uint32 Head = 0, Hhea = 0;
				const uint16 NumTables = U16(4);
				for (uint16 t = 0; t < NumTables; ++t)
				{
					const uint32 R = 12 + 16 * t;
					const uint32 Tag = U32(R), Off = U32(R + 8);
					if (Tag == 0x676C7966) { Glyf = Off; }        // glyf
					else if (Tag == 0x6C6F6361) { Loca = Off; }   // loca
					else if (Tag == 0x636D6170) { Cmap = Off; }   // cmap
					else if (Tag == 0x686D7478) { Hmtx = Off; }   // hmtx
					else if (Tag == 0x68656164) { Head = Off; }   // head
					else if (Tag == 0x68686561) { Hhea = Off; }   // hhea
				}
				UnitsPerEm = U16(Head + 18);
				bLongLoca = I16(Head + 50) != 0;
				NumHMetrics = U16(Hhea + 34);
				return Glyf && Loca && Cmap && Hmtx;
			}

			int32 GlyphIndex(TCHAR Ch) const
			{
				const uint16 N = U16(Cmap + 2);
				for (uint16 i = 0; i < N; ++i)
				{
					const uint32 R = Cmap + 4 + 8 * i;
					const uint32 Sub = Cmap + U32(R + 4);
					if (U16(R) == 3 && U16(R + 2) == 1 && U16(Sub) == 4)
					{
						const uint16 SegX2 = U16(Sub + 6);
						const uint32 Ends = Sub + 14, Starts = Ends + SegX2 + 2, Deltas = Starts + SegX2, Ranges = Deltas + SegX2;
						for (uint16 s = 0; s < SegX2 / 2; ++s)
						{
							const uint16 End = U16(Ends + 2 * s), Start = U16(Starts + 2 * s);
							if (Ch < Start || Ch > End) { continue; }
							const int16 Delta = I16(Deltas + 2 * s);
							const uint16 Range = U16(Ranges + 2 * s);
							if (Range == 0) { return uint16(Ch + Delta); }
							const uint16 G = U16(Ranges + 2 * s + Range + 2 * (Ch - Start));
							return G ? uint16(G + Delta) : 0;
						}
					}
				}
				return 0;
			}

			int32 Advance(int32 G) const { return U16(Hmtx + 4 * FMath::Min(G, NumHMetrics - 1)); }

			/** Contours of a simple glyph, curves flattened with Steps segments per quadratic span. */
			TArray<TArray<FVector2d>> Outline(int32 G, int32 Steps) const
			{
				TArray<TArray<FVector2d>> Out;
				const uint32 Off = bLongLoca ? U32(Loca + 4 * G) : uint32(U16(Loca + 2 * G)) * 2;
				const uint32 Next = bLongLoca ? U32(Loca + 4 * G + 4) : uint32(U16(Loca + 2 * G + 2)) * 2;
				if (Next == Off) { return Out; }   // empty (space)
				const uint32 P = Glyf + Off;
				const int16 NumContours = I16(P);
				if (NumContours < 0)
				{
					UE_LOG(LogOpenCity, Warning, TEXT("kit text: composite glyph %d not supported"), G);
					return Out;
				}
				TArray<int32> EndPts;
				for (int16 c = 0; c < NumContours; ++c) { EndPts.Add(U16(P + 10 + 2 * c)); }
				const int32 NumPts = EndPts.IsEmpty() ? 0 : EndPts.Last() + 1;
				uint32 Q = P + 10 + 2 * NumContours;
				Q += 2 + U16(Q);   // instructions
				TArray<uint8> Flags;
				while (Flags.Num() < NumPts)
				{
					const uint8 F = D[Q++];
					Flags.Add(F);
					if (F & 8) { for (uint8 r = D[Q++]; r > 0; --r) { Flags.Add(F); } }
				}
				TArray<FVector2d> Pts;
				Pts.SetNum(NumPts);
				int32 X = 0, Y = 0;
				for (int32 i = 0; i < NumPts; ++i)
				{
					const uint8 F = Flags[i];
					if (F & 2) { const int32 Dx = D[Q++]; X += (F & 16) ? Dx : -Dx; }
					else if (!(F & 16)) { X += I16(Q); Q += 2; }
					Pts[i].X = X;
				}
				for (int32 i = 0; i < NumPts; ++i)
				{
					const uint8 F = Flags[i];
					if (F & 4) { const int32 Dy = D[Q++]; Y += (F & 32) ? Dy : -Dy; }
					else if (!(F & 32)) { Y += I16(Q); Q += 2; }
					Pts[i].Y = Y;
				}
				int32 Start = 0;
				for (int32 End : EndPts)
				{
					const int32 N = End - Start + 1;
					auto On = [&](int32 k) { return (Flags[Start + (k % N + N) % N] & 1) != 0; };
					auto At = [&](int32 k) { return Pts[Start + (k % N + N) % N]; };
					// Start on an on-curve point (or the midpoint of two off-curve points).
					int32 S0 = 0;
					while (S0 < N && !On(S0)) { ++S0; }
					TArray<FVector2d> C;
					FVector2d Cur = S0 < N ? At(S0) : (At(0) + At(1)) * 0.5;
					C.Add(Cur);
					for (int32 k = 1; k <= N; ++k)
					{
						const int32 i = S0 + k;
						if (On(i))
						{
							Cur = At(i);
							C.Add(Cur);
							continue;
						}
						const FVector2d Ctrl = At(i);
						const FVector2d End2 = On(i + 1) ? At(i + 1) : (Ctrl + At(i + 1)) * 0.5;
						for (int32 s = 1; s <= Steps; ++s)
						{
							const double T = double(s) / Steps;
							C.Add(Cur * FMath::Square(1 - T) + Ctrl * (2 * (1 - T) * T) + End2 * (T * T));
						}
						Cur = End2;
						if (On(i + 1)) { ++k; }
					}
					if (C.Num() > 1 && FVector2d::DistSquared(C[0], C.Last()) < 1e-6) { C.Pop(); }
					Out.Add(MoveTemp(C));
					Start = End + 1;
				}
				return Out;
			}
		};

		double SignedArea(const TArray<FVector2d>& P)
		{
			double A = 0;
			for (int32 i = 0; i < P.Num(); ++i) { A += P[i].X * P[(i + 1) % P.Num()].Y - P[(i + 1) % P.Num()].X * P[i].Y; }
			return A / 2;
		}

		bool Inside(const TArray<FVector2d>& Poly, const FVector2d& P)
		{
			bool bIn = false;
			for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++)
			{
				if ((Poly[i].Y > P.Y) != (Poly[j].Y > P.Y) && P.X < (Poly[j].X - Poly[i].X) * (P.Y - Poly[i].Y) / (Poly[j].Y - Poly[i].Y) + Poly[i].X) { bIn = !bIn; }
			}
			return bIn;
		}
	}

	FSoup TextMesh(const FString& Text, double Size, double Z0, double Depth, double Y)
	{
		static FFont Font;
		static bool bLoaded = Font.Load(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Regular.ttf"));
		FSoup S;
		if (!bLoaded)
		{
			UE_LOG(LogOpenCity, Error, TEXT("kit text: cannot read Roboto-Regular.ttf"));
			return S;
		}
		const double K = Size / Font.UnitsPerEm;
		double Width = 0;
		for (TCHAR Ch : Text) { Width += Font.Advance(Font.GlyphIndex(Ch)); }
		double Pen = -Width / 2;
		for (TCHAR Ch : Text)
		{
			const int32 G = Font.GlyphIndex(Ch);
			TArray<TArray<FVector2d>> Contours = Font.Outline(G, 3);   // Blender resolution_u = 3
			for (TArray<FVector2d>& C : Contours) { for (FVector2d& P : C) { P = FVector2d((P.X + Pen) * K, P.Y * K); } }
			Pen += Font.Advance(G);
			// Outer contours are the ones with the dominant orientation (TrueType: clockwise); holes go in the outer containing them.
			double Biggest = 0;
			for (const TArray<FVector2d>& C : Contours) { if (FMath::Abs(SignedArea(C)) > FMath::Abs(Biggest)) { Biggest = SignedArea(C); } }
			TArray<FGeneralPolygon2d> Polys;
			TArray<const TArray<FVector2d>*> Holes;
			for (const TArray<FVector2d>& C : Contours)
			{
				if (C.Num() < 3) { continue; }
				if ((SignedArea(C) > 0) == (Biggest > 0))
				{
					TArray<FVector2d> Ccw = C;
					if (SignedArea(Ccw) < 0) { Algo::Reverse(Ccw); }
					Polys.Add(FGeneralPolygon2d(FPolygon2d(Ccw)));
				}
				else
				{
					Holes.Add(&C);
				}
			}
			for (const TArray<FVector2d>* H : Holes)
			{
				for (FGeneralPolygon2d& P : Polys)
				{
					if (Inside(P.GetOuter().GetVertices(), (*H)[0]))
					{
						TArray<FVector2d> Cw = *H;
						if (SignedArea(Cw) > 0) { Algo::Reverse(Cw); }
						P.AddHole(FPolygon2d(Cw), false, false);
						break;
					}
				}
			}
			// Font space (x right, y up) stands in the XZ plane: font y -> Z, front face toward -Y.
			for (const FGeneralPolygon2d& P : Polys)
			{
				TArray<FVector2d> Verts;
				const TArray<FIndex3i> Tris = ConstrainedDelaunayTriangulateWithVertices<double>(P, Verts);
				const int32 Sh = S.NewShell();
				const double Yf = Y - Depth / 2, Yb = Y + Depth / 2;
				TArray<int32> Front, Back;
				for (const FVector2d& V : Verts) { Front.Add(S.Add(FVector3d(V.X, Yf, V.Y))); Back.Add(S.Add(FVector3d(V.X, Yb, V.Y))); }
				for (const FIndex3i& T : Tris)
				{
					// (x, y) CCW maps to (x, z) CCW seen from -Y: outward toward -Y for the front.
					const FVector3d A = S.V[Front[T.A]], B = S.V[Front[T.B]], C = S.V[Front[T.C]];
					const bool bFacesFront = (B - A).Cross(C - A).Y < 0;
					S.Face(Sh, bFacesFront ? std::initializer_list<int32>{ Front[T.A], Front[T.B], Front[T.C] } : std::initializer_list<int32>{ Front[T.A], Front[T.C], Front[T.B] });
					S.Face(Sh, bFacesFront ? std::initializer_list<int32>{ Back[T.A], Back[T.C], Back[T.B] } : std::initializer_list<int32>{ Back[T.A], Back[T.B], Back[T.C] });
				}
				// Side walls along every contour (outer CCW, holes CW in font space).
				auto Walls = [&](const TArray<FVector2d>& Ring)
				{
					for (int32 i = 0; i < Ring.Num(); ++i)
					{
						const FVector2d& A = Ring[i];
						const FVector2d& B = Ring[(i + 1) % Ring.Num()];
						const int32 a0 = S.Add(FVector3d(A.X, Yf, A.Y)), b0 = S.Add(FVector3d(B.X, Yf, B.Y));
						const int32 a1 = S.Add(FVector3d(A.X, Yb, A.Y)), b1 = S.Add(FVector3d(B.X, Yb, B.Y));
						S.Face(Sh, { a0, a1, b1, b0 });
					}
				};
				Walls(P.GetOuter().GetVertices());
				for (const FPolygon2d& H : P.GetHoles()) { Walls(H.GetVertices()); }
			}
		}
		double Lo = 1e18, Hi = -1e18;
		for (const FVector3d& V : S.V) { Lo = FMath::Min(Lo, V.Z); Hi = FMath::Max(Hi, V.Z); }
		Transform(S, FM4::Translation(FVector3d(0, 0, Z0 - (Lo + Hi) / 2)));
		return S;
	}
}
