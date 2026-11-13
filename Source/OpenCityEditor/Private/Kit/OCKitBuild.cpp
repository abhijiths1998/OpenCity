// Kit pieces to static meshes (/Game/Environment/Meshes/Kit/SM_Kit_*), replacing the Blender FBX export and
// the FBX import. Per piece: parts merged (no welding across parts), triangulated, UV0 a world-scale cube
// projection (1 UV = 1 m), smooth normals split at edges sharper than 50 deg and weighted by face area (as
// Blender's Weighted Normal modifier did), the KIT_SPEC collision as simple shapes, Nanite on.
#include "Art/OCArt.h"
#include "Kit/OCKitPieces.h"

#include "EditorAssetLibrary.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "OCEditorHelpers.h"
#include "OCLog.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"

using namespace OCKit;
using UE::Geometry::FIndex3i;

namespace
{
	const TCHAR* KitTag = TEXT("oc_kit");

	struct FTri { int32 V[3]; int32 Mat; FVector3d N; double Area; };

	/** Fraction of triangles whose winding agrees with their vertex normals (front faces). */
	double WindingAgreement(UStaticMesh* Mesh)
	{
		const FMeshDescription* Md = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
		if (!Md) { return -1.0; }
		FStaticMeshConstAttributes Attr(*Md);
		const TVertexAttributesConstRef<FVector3f> Pos = Attr.GetVertexPositions();
		const TVertexInstanceAttributesConstRef<FVector3f> Nrm = Attr.GetVertexInstanceNormals();
		int32 Agree = 0, Total = 0;
		for (const FTriangleID T : Md->Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexInstanceID> Vi = Md->GetTriangleVertexInstances(T);
			const FVector3f P0 = Pos[Md->GetVertexInstanceVertex(Vi[0])], P1 = Pos[Md->GetVertexInstanceVertex(Vi[1])], P2 = Pos[Md->GetVertexInstanceVertex(Vi[2])];
			const FVector3f G = (P1 - P0).Cross(P2 - P0);
			if (G.SizeSquared() < 1e-12f) { continue; }
			++Total;
			Agree += G.Dot(Nrm[Vi[0]] + Nrm[Vi[1]] + Nrm[Vi[2]]) > 0 ? 1 : 0;
		}
		return Total ? double(Agree) / Total : -1.0;
	}

	/** Edges used by exactly one triangle (vertices welded by position): cracks and open ends. */
	int32 OpenEdges(UStaticMesh* Mesh)
	{
		const FMeshDescription* Md = Mesh ? Mesh->GetMeshDescription(0) : nullptr;
		if (!Md) { return -1; }
		FStaticMeshConstAttributes Attr(*Md);
		const TVertexAttributesConstRef<FVector3f> Pos = Attr.GetVertexPositions();
		auto Key = [&](FVertexID V) { const FVector3f P = Pos[V]; return FIntVector(FMath::RoundToInt(P.X * 100), FMath::RoundToInt(P.Y * 100), FMath::RoundToInt(P.Z * 100)); };
		TMap<TPair<FIntVector, FIntVector>, int32> Count;
		for (const FTriangleID T : Md->Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexInstanceID> Vi = Md->GetTriangleVertexInstances(T);
			for (int32 k = 0; k < 3; ++k)
			{
				FIntVector A = Key(Md->GetVertexInstanceVertex(Vi[k])), B = Key(Md->GetVertexInstanceVertex(Vi[(k + 1) % 3]));
				if (B.X < A.X || (B.X == A.X && (B.Y < A.Y || (B.Y == A.Y && B.Z < A.Z)))) { Swap(A, B); }
				Count.FindOrAdd(TPair<FIntVector, FIntVector>(A, B))++;
			}
		}
		int32 Open = 0;
		for (const auto& C : Count)
		{
			if (C.Value != 1) { continue; }
			if (Open++ < 12) { UE_LOG(LogOpenCity, Verbose, TEXT("open edge %s - %s (cm x100)"), *C.Key.Key.ToString(), *C.Key.Value.ToString()); }
		}
		return Open;
	}

	bool IsSolid(const FString& Name)
	{
		// Pieces placed with collision on (BuildRetainingWalls, BuildStreetFixtures): complex as simple, because
		// Chaos vehicles only collide with simple shapes and cars must stop at parapets.
		for (const TCHAR* S : { TEXT("RetainingWall_"), TEXT("WallCoping_"), TEXT("Sign_"), TEXT("Prop_Bollard"), TEXT("Prop_LedPole"),
			TEXT("Prop_ConvexMirror"), TEXT("Prop_RecyclingBin"), TEXT("Prop_Fountain"), TEXT("Prop_TrafficLight") })
		{
			if (Name.StartsWith(S, ESearchCase::CaseSensitive)) { return true; }
		}
		return false;
	}

	bool WritePiece(FPiece& P, const FString& Dest, int32& OutTris)
	{
		// ---- merge parts into one triangle list (each part keeps its own vertices) ----
		TArray<FVector3d> Pos;
		TArray<FTri> Tris;
		for (const TPair<FSoup, int32>& Part : P.Parts)
		{
			const int32 Base = Pos.Num();
			Pos.Append(Part.Key.V);
			for (const OCKit::FPoly& F : Part.Key.F)
			{
				for (const FIndex3i& T : Triangulate(Part.Key, F))
				{
					FTri Tr;
					Tr.V[0] = Base + F.I[T.A]; Tr.V[1] = Base + F.I[T.B]; Tr.V[2] = Base + F.I[T.C];
					Tr.Mat = Part.Value;
					Tris.Add(Tr);
				}
			}
		}
		// Props: pivot at the bottom centre of the bounding box (not the drainpipe, which snaps to a wall).
		if (P.Kind == TEXT("prop") && P.Name != TEXT("Drainpipe_320"))
		{
			FBox3d B(ForceInit);
			for (const FVector3d& V : Pos) { B += V; }
			const FVector3d Off(-(B.Min.X + B.Max.X) / 2, -(B.Min.Y + B.Max.Y) / 2, -B.Min.Z);
			for (FVector3d& V : Pos) { V += Off; }
			for (FUcx& U : P.Ucx)
			{
				if (U.bHull) { continue; }
				U.Box[0] += Off.X; U.Box[1] += Off.X; U.Box[2] += Off.Y; U.Box[3] += Off.Y; U.Box[4] += Off.Z; U.Box[5] += Off.Z;
			}
		}
		for (FTri& T : Tris)
		{
			const FVector3d C = (Pos[T.V[1]] - Pos[T.V[0]]).Cross(Pos[T.V[2]] - Pos[T.V[0]]);
			T.Area = C.Size() / 2;
			T.N = T.Area > 0 ? C / (2 * T.Area) : FVector3d::ZeroVector;
		}
		Tris.RemoveAll([](const FTri& T) { return T.Area < 1e-12; });   // dissolve_degenerate
		OutTris = Tris.Num();

		// ---- normals: smooth fans around each vertex, split at edges sharper than 50 deg, area weighted ----
		const double CosSharp = FMath::Cos(FMath::DegreesToRadians(50.0));
		TMap<TPair<int32, int32>, TArray<int32>> EdgeTris;
		TArray<TArray<int32>> VertTris;
		VertTris.SetNum(Pos.Num());
		for (int32 t = 0; t < Tris.Num(); ++t)
		{
			for (int32 k = 0; k < 3; ++k)
			{
				const int32 A = Tris[t].V[k], B = Tris[t].V[(k + 1) % 3];
				EdgeTris.FindOrAdd(TPair<int32, int32>(FMath::Min(A, B), FMath::Max(A, B))).Add(t);
				VertTris[A].Add(t);
			}
		}
		TArray<FVector3d> CornerN;
		CornerN.SetNumZeroed(Tris.Num() * 3);
		for (int32 v = 0; v < Pos.Num(); ++v)
		{
			const TArray<int32>& Ts = VertTris[v];
			if (Ts.IsEmpty()) { continue; }
			// union-find over the triangles around v, joined across smooth edges that contain v
			TArray<int32> Parent;
			for (int32 i = 0; i < Ts.Num(); ++i) { Parent.Add(i); }
			TFunction<int32(int32)> Find = [&](int32 i) { while (Parent[i] != i) { Parent[i] = Parent[Parent[i]]; i = Parent[i]; } return i; };
			for (int32 i = 0; i < Ts.Num(); ++i)
			{
				for (int32 k = 0; k < 3; ++k)
				{
					const int32 W = Tris[Ts[i]].V[k];
					if (W == v) { continue; }
					const TArray<int32>* Shared = EdgeTris.Find(TPair<int32, int32>(FMath::Min(v, W), FMath::Max(v, W)));
					if (!Shared || Shared->Num() != 2) { continue; }   // boundary or non-manifold: sharp
					const int32 Other = (*Shared)[0] == Ts[i] ? (*Shared)[1] : (*Shared)[0];
					if (Tris[Ts[i]].N.Dot(Tris[Other].N) < CosSharp) { continue; }   // sharp edge
					const int32 j = Ts.IndexOfByKey(Other);
					if (j != INDEX_NONE) { Parent[Find(i)] = Find(j); }
				}
			}
			TMap<int32, FVector3d> FanSum;
			for (int32 i = 0; i < Ts.Num(); ++i) { FanSum.FindOrAdd(Find(i)) += Tris[Ts[i]].N * Tris[Ts[i]].Area; }
			for (int32 i = 0; i < Ts.Num(); ++i)
			{
				const FTri& T = Tris[Ts[i]];
				const int32 Corner = T.V[0] == v ? 0 : (T.V[1] == v ? 1 : 2);
				CornerN[Ts[i] * 3 + Corner] = FanSum[Find(i)].GetSafeNormal();
			}
		}

		// ---- write the static mesh ----
		const FString Name = TEXT("SM_Kit_") + P.Name;
		UStaticMesh* Mesh = OCEd::Load<UStaticMesh>(Dest / Name);
		if (!Mesh)
		{
			Mesh = CastChecked<UStaticMesh>(OCEd::GetOrCreate(Name, Dest, UStaticMesh::StaticClass(), nullptr, KitTag));
		}
		Mesh->Modify();
		Mesh->SetNumSourceModels(1);
		FStaticMeshSourceModel& Src = Mesh->GetSourceModel(0);
		Src.BuildSettings.bRecomputeNormals = false;
		Src.BuildSettings.bRecomputeTangents = true;
		Src.BuildSettings.bUseMikkTSpace = true;
		Src.BuildSettings.bGenerateLightmapUVs = false;
		Src.BuildSettings.bRemoveDegenerates = true;
		FMeshDescription* Md = Mesh->CreateMeshDescription(0);
		Md->Empty();
		FStaticMeshAttributes Attr(*Md);
		Attr.Register();
		TVertexAttributesRef<FVector3f> VPos = Attr.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> VNrm = Attr.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector2f> VUv = Attr.GetVertexInstanceUVs();
		TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
		VUv.SetNumChannels(1);
		TArray<FVertexID> Vids;
		for (const FVector3d& V : Pos)
		{
			const FVertexID Id = Md->CreateVertex();
			VPos[Id] = FVector3f(V * 100.0);   // Unreal cm = 100 x kit metres, same axes
			Vids.Add(Id);
		}
		TArray<int32> Used;
		for (const FTri& T : Tris) { Used.AddUnique(T.Mat); }
		Used.Sort();
		TMap<int32, FPolygonGroupID> Groups;
		TArray<FStaticMaterial> Materials;
		for (int32 M : Used)
		{
			const FPolygonGroupID G = Md->CreatePolygonGroup();
			SlotNames[G] = MatOrder[M];
			Groups.Add(M, G);
			UMaterialInterface* Mi = OCEd::Load<UMaterialInterface>(FString(TEXT("/Game/Environment/Materials/MI_Kit_")) + MatOrder[M]);
			if (!Mi)
			{
				UE_LOG(LogOpenCity, Error, TEXT("kit: missing MI_Kit_%s (run the materials step first)"), MatOrder[M]);
				return false;
			}
			Materials.Add(FStaticMaterial(Mi, MatOrder[M], MatOrder[M]));
		}
		for (int32 t = 0; t < Tris.Num(); ++t)
		{
			const FTri& T = Tris[t];
			// UV0: cube projection by the face's dominant axis (Blender V up; Unreal V = 1 - v).
			const FVector3d& N = T.N;
			const int32 Ax = FMath::Abs(N.X) >= FMath::Abs(N.Y) && FMath::Abs(N.X) >= FMath::Abs(N.Z) ? 0 : (FMath::Abs(N.Y) >= FMath::Abs(N.Z) ? 1 : 2);
			TArray<FVertexInstanceID> Vi;
			for (int32 k = 0; k < 3; ++k)
			{
				const FVector3d& Pp = Pos[T.V[k]];
				double U, V;
				if (Ax == 1) { U = N.Y < 0 ? Pp.X : -Pp.X; V = Pp.Z; }
				else if (Ax == 0) { U = N.X > 0 ? Pp.Y : -Pp.Y; V = Pp.Z; }
				else { U = Pp.X; V = N.Z > 0 ? Pp.Y : -Pp.Y; }
				const FVertexInstanceID I = Md->CreateVertexInstance(Vids[T.V[k]]);
				VNrm[I] = FVector3f(CornerN[t * 3 + k]);
				VUv.Set(I, 0, FVector2f(float(U), float(1.0 - V)));
				Vi.Add(I);
			}
			Swap(Vi[1], Vi[2]);   // Unreal (left-handed) winds outward faces the other way round
			Md->CreateTriangle(Groups[T.Mat], Vi);
		}
		Mesh->CommitMeshDescription(0);
		Mesh->SetStaticMaterials(Materials);
		Mesh->GetNaniteSettings().bEnabled = true;
		if (P.Name.StartsWith(TEXT("LM_")))
		{
			// Landmarks: keep the whole mesh as the fallback (the automatic one dropped LM_Dome's shell, leaving
			// lantern-sized bounds).
			Mesh->GetNaniteSettings().FallbackTarget = ENaniteFallbackTarget::PercentTriangles;
			Mesh->GetNaniteSettings().FallbackPercentTriangles = 1.0f;
		}
		// collision: the KIT_SPEC UCX shapes as simple collision (cm)
		Mesh->CreateBodySetup();
		UBodySetup* Body = Mesh->GetBodySetup();
		Body->Modify();
		Body->AggGeom.EmptyElements();
		for (const FUcx& U : P.Ucx)
		{
			if (U.bHull)
			{
				FKConvexElem Hull;
				for (const FVector3d& V : U.Hull) { Hull.VertexData.Add(V * 100.0); }
				Hull.UpdateElemBox();
				Body->AggGeom.ConvexElems.Add(Hull);
			}
			else
			{
				FKBoxElem Bx;
				Bx.Center = FVector((U.Box[0] + U.Box[1]) * 50.0, (U.Box[2] + U.Box[3]) * 50.0, (U.Box[4] + U.Box[5]) * 50.0);
				Bx.X = (U.Box[1] - U.Box[0]) * 100.0;
				Bx.Y = (U.Box[3] - U.Box[2]) * 100.0;
				Bx.Z = (U.Box[5] - U.Box[4]) * 100.0;
				Body->AggGeom.BoxElems.Add(Bx);
			}
		}
		Body->CollisionTraceFlag = IsSolid(P.Name) ? CTF_UseComplexAsSimple : CTF_UseDefault;
		Body->InvalidatePhysicsData();
		Body->CreatePhysicsMeshes();
		Mesh->Build(false);
		Mesh->PostEditChange();
		OCEd::Save(Mesh);
		return true;
	}
}

namespace OCArt
{
	bool BuildKit(const FString& Dest, const FString& Only)
	{
		TArray<FString> OnlyNames;
		Only.ParseIntoArray(OnlyNames, TEXT(","));
		int32 Done = 0, Failed = 0;
		for (const TFunction<FPiece()>& Make : Builders())
		{
			FPiece P = Make();
			if (!OnlyNames.IsEmpty() && !OnlyNames.Contains(P.Name)) { continue; }
			const FString Path = Dest / (TEXT("SM_Kit_") + P.Name);
			const double Before = WindingAgreement(OCEd::Load<UStaticMesh>(Path));
			int32 Tris = 0;
			if (!WritePiece(P, Dest, Tris)) { ++Failed; continue; }
			UStaticMesh* Mesh = OCEd::Load<UStaticMesh>(Path);
			{
				// Bounds drive culling: a piece whose saved bounds miss part of its geometry gets them extended; NaN
				// vertices (a bad profile, as LM_Dome once had) are an error.
				FBox3d Geo(ForceInit);
				bool bNaN = false;
				for (const TPair<FSoup, int32>& Part : P.Parts) { for (const FVector3d& V : Part.Key.V) { bNaN |= V.ContainsNaN(); if (!V.ContainsNaN()) { Geo += V * 100.0; } } }
				if (bNaN)
				{
					UE_LOG(LogOpenCity, Error, TEXT("kit: %s has NaN vertices"), *P.Name);
					++Failed;
				}
				const FBox Built = Mesh->GetBounds().GetBox();
				if (Geo.IsValid && !(Built.IsInsideOrOn(FVector(Geo.Min) + FVector(1.0)) && Built.IsInsideOrOn(FVector(Geo.Max) - FVector(1.0))))
				{
					Mesh->Modify();
					Mesh->SetNegativeBoundsExtension(FVector::Max(FVector::ZeroVector, Built.Min - FVector(Geo.Min)));
					Mesh->SetPositiveBoundsExtension(FVector::Max(FVector::ZeroVector, FVector(Geo.Max) - Built.Max));
					Mesh->CalculateExtendedBounds();
					OCEd::Save(Mesh);
					OCEd::Log(KitTag, FString::Printf(TEXT("%s: bounds extended to the geometry"), *P.Name));
				}
			}
			const FVector E = Mesh->GetBounds().BoxExtent, O = Mesh->GetBounds().Origin;
			const FString Budget = Tris > (P.Kind == TEXT("facade") ? 3300 : 2200) ? TEXT("  OVER BUDGET") : TEXT("");
			OCEd::Log(KitTag, FString::Printf(TEXT("%s: %d tris, X %.1f..%.1f Y %.1f..%.1f Z %.1f..%.1f cm, slots %d, winding %.2f (was %.2f), open edges %d%s"), *P.Name, Tris,
				O.X - E.X, O.X + E.X, O.Y - E.Y, O.Y + E.Y, O.Z - E.Z, O.Z + E.Z, Mesh->GetStaticMaterials().Num(), WindingAgreement(Mesh), Before, OpenEdges(Mesh), *Budget));
			const FString RefPath = FString(TEXT("/Game/Environment/Meshes/Kit/SM_Kit_")) + P.Name;
			if (RefPath != Path)
			{
				if (UStaticMesh* Ref = OCEd::Load<UStaticMesh>(RefPath))
				{
					const FVector Re = Ref->GetBounds().BoxExtent, Ro = Ref->GetBounds().Origin;
					const FMeshDescription* Rmd = Ref->GetMeshDescription(0);
					const double Dmax = FMath::Max(((O - E) - (Ro - Re)).GetAbs().GetMax(), ((O + E) - (Ro + Re)).GetAbs().GetMax());
					OCEd::Log(KitTag, FString::Printf(TEXT("  ref %s: %d tris, slots %d, winding %.2f, open edges %d, bounds differ by %.2f cm"), *P.Name, Rmd ? Rmd->Triangles().Num() : -1,
						Ref->GetStaticMaterials().Num(), WindingAgreement(Ref), OpenEdges(Ref), Dmax));
				}
			}
			++Done;
		}
		OCEd::Log(KitTag, FString::Printf(TEXT("built %d kit pieces into %s (%d failed)"), Done, *Dest, Failed));
		return Failed == 0 && Done > 0;
	}
}
