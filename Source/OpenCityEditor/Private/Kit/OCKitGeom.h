#pragma once

// Geometry for the modular kit generator (Kit/OCKitPieces.cpp). Parts are polygon soups in the kit's
// piece frame (metres, X along the facade, Y into the wall, Z up; Unreal cm = 100 x these numbers),
// faces wound counter-clockwise seen from outside. Bevels and booleans run on Unreal's dynamic mesh
// (FMeshBevel, FMeshBoolean). Formerly done in Blender (Tools/blender/make_kit.py, bmesh).

#include "CoreMinimal.h"
#include "IndexTypes.h"

namespace OCKit
{
	enum EAxis : int32 { AX = 0, AY = 1, AZ = 2 };
	constexpr double Eps = 1e-4;

	struct FPoly
	{
		TArray<int32> I;
		int32 Shell = 0;   // vertices are welded only within a shell (one primitive or one operation result)
	};

	struct FSoup
	{
		TArray<FVector3d> V;
		TArray<FPoly> F;
		int32 NextShell = 0;

		int32 Add(const FVector3d& P) { return V.Add(P); }
		int32 NewShell() { return NextShell++; }
		void Face(int32 Shell, std::initializer_list<int32> Idx) { FPoly P; P.I = Idx; P.Shell = Shell; F.Add(MoveTemp(P)); }
		void Face(int32 Shell, const TArray<int32>& Idx) { FPoly P; P.I = Idx; P.Shell = Shell; F.Add(MoveTemp(P)); }
		bool IsEmpty() const { return F.IsEmpty(); }
	};

	/** A 4x4 affine transform acting on column vectors (row-major storage). */
	struct FM4
	{
		double M[4][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };
		static FM4 Translation(const FVector3d& T);
		/** Rotation by Angle (radians) about axis 0/1/2, right-handed (Blender's Matrix.Rotation). */
		static FM4 Rotation(double Angle, int32 Axis);
		static FM4 Scale(const FVector3d& S);
		/** Rotation taking +Z to Dir with +Y as up hint (Blender's to_track_quat('Z', 'Y')). */
		static FM4 TrackZ(const FVector3d& Dir);
		FM4 operator*(const FM4& B) const;
		FVector3d Apply(const FVector3d& P) const;
		double Det3() const;
	};

	void AddBox(FSoup& S, double X0, double X1, double Y0, double Y1, double Z0, double Z1);
	FSoup Box(double X0, double X1, double Y0, double Y1, double Z0, double Z1);
	/** Extrude a 2D polygon along an axis: axis X takes (y, z), Y takes (z, x), Z takes (x, y). */
	void AddPrism(FSoup& S, TArray<FVector2d> Poly, int32 Axis, double A0, double A1, bool bCaps = true);
	FSoup PrismX(const TArray<FVector2d>& PolyYZ, double X0, double X1, bool bCaps = true);
	/** Cylinder or cone with its base centre at Center, Length along +axis ('X', 'Y' or 'Z'); R2 < 0 = R. */
	void AddCyl(FSoup& S, const FVector3d& Center, double R, double Length, int32 Seg = 12, TCHAR Axis = 'Z', bool bCaps = true, double R2 = -1.0);
	/** Transform every vertex; a mirroring transform also flips the winding so faces stay outward. */
	void Transform(FSoup& S, const FM4& M);
	/** Delete faces lying on the plane Axis = Val. */
	void DelPlane(FSoup& S, int32 Axis, double Val);
	void Append(FSoup& Dst, const FSoup& Src);
	/** Triangles of a polygon (indices into F.I), wound like the polygon. */
	TArray<UE::Geometry::FIndex3i> Triangulate(const FSoup& S, const FPoly& F);

	struct FKeep { int32 Axis; double Val; };
	/** Bevel convex edges sharper than MinAngle degrees, except edges on a Keep plane (tiling seams). */
	void Bevel(FSoup& S, double Width, int32 Segs = 2, const TArray<FKeep>& Keep = {}, bool bConvexOnly = true, double MinAngle = 30.0);
	/** Boolean difference A - Cutter (both closed). */
	FSoup BoolDiff(const FSoup& A, const FSoup& Cutter);
	/** Faces of a closed shell oriented outward (recalc_face_normals). */
	void OrientOutward(FSoup& S);

	/** Extruded text (Roboto from the engine's fonts) centred on X, standing in the XZ plane facing -Y,
	 *  vertically centred at Z0, between Y - Depth/2 and Y + Depth/2. Size is the em height in metres. */
	FSoup TextMesh(const FString& Text, double Size, double Z0, double Depth = 0.004, double Y = 0.0);
}
