#include "OCKitPreviewCommandlet.h"

#include "EditorAssetLibrary.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Layout/OCCanvas.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "OCEditorHelpers.h"
#include "OCLog.h"
#include "StaticMeshAttributes.h"

namespace
{
	FColor SlotColour(const FString& Slot)
	{
		static const TMap<FString, FColor> Colours = {
			{ TEXT("Wall"), FColor(204, 148, 77) }, { TEXT("Trim"), FColor(191, 179, 148) }, { TEXT("Wood"), FColor(13, 36, 20) },
			{ TEXT("Glass"), FColor(10, 12, 15) }, { TEXT("Metal"), FColor(30, 30, 33) }, { TEXT("Roof"), FColor(117, 23, 10) },
			{ TEXT("Concrete"), FColor(97, 89, 79) }, { TEXT("PaintRed"), FColor(153, 8, 8) }, { TEXT("PaintWhite"), FColor(217, 217, 217) },
			{ TEXT("PaintBlack"), FColor(8, 8, 8) }, { TEXT("PaintBlue"), FColor(5, 31, 115) }, { TEXT("Paint"), FColor(51, 102, 51) } };
		const FColor* C = Colours.Find(Slot);
		return C ? *C : FColor(255, 0, 255);
	}

	/** A 3/4 view (front-left, from above) of a mesh, orthographic, fitted to Size px. */
	void Render(UStaticMesh* Mesh, OCLayout::FCanvas& Cv, int32 Ox, int32 Oy, int32 Size)
	{
		const FMeshDescription* Md = Mesh->GetMeshDescription(0);
		if (!Md) { return; }
		FStaticMeshConstAttributes Attr(*Md);
		const TVertexAttributesConstRef<FVector3f> Pos = Attr.GetVertexPositions();
		const TVertexInstanceAttributesConstRef<FVector3f> Nrm = Attr.GetVertexInstanceNormals();
		const TPolygonGroupAttributesConstRef<FName> Slots = Attr.GetPolygonGroupMaterialSlotNames();
		// Camera: looking at the facade (-Y side) from front-left and above.
		const FVector3d Fwd = FVector3d(0.55, 1.0, -0.45).GetSafeNormal();   // view direction
		const FVector3d Right = FVector3d(0, 0, 1).Cross(Fwd).GetSafeNormal() * -1.0;
		const FVector3d Up = Fwd.Cross(Right).GetSafeNormal() * -1.0;
		const FVector3d Light = FVector3d(-0.4, -0.8, 0.6).GetSafeNormal();
		FBox2d B(ForceInit);
		for (const FVertexID V : Md->Vertices().GetElementIDs())
		{
			const FVector3d P(Pos[V]);
			B += FVector2d(P.Dot(Right), P.Dot(Up));
		}
		const double Scale = 0.9 * Size / FMath::Max(B.GetSize().X, B.GetSize().Y);
		const FVector2d C = B.GetCenter();
		TArray<double> Zb;
		Zb.Init(1e30, Size * Size);
		for (const FTriangleID T : Md->Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexInstanceID> Vi = Md->GetTriangleVertexInstances(T);
			FVector3d P[3], N[3];
			FVector2d S[3];
			double D[3];
			for (int32 k = 0; k < 3; ++k)
			{
				P[k] = FVector3d(Pos[Md->GetVertexInstanceVertex(Vi[k])]);
				N[k] = FVector3d(Nrm[Vi[k]]);
				S[k] = FVector2d(Size / 2 + (P[k].Dot(Right) - C.X) * Scale, Size / 2 - (P[k].Dot(Up) - C.Y) * Scale);
				D[k] = P[k].Dot(Fwd);
			}
			if ((P[2] - P[0]).Cross(P[1] - P[0]).Dot(Fwd) >= 0) { continue; }   // back face (or inside out): culled (Unreal winding)
			const FColor Base = SlotColour(Slots[Md->GetTrianglePolygonGroup(T)].ToString());
			const int32 X0 = FMath::Max(0, FMath::FloorToInt(FMath::Min3(S[0].X, S[1].X, S[2].X)));
			const int32 X1 = FMath::Min(Size - 1, FMath::CeilToInt(FMath::Max3(S[0].X, S[1].X, S[2].X)));
			const int32 Y0 = FMath::Max(0, FMath::FloorToInt(FMath::Min3(S[0].Y, S[1].Y, S[2].Y)));
			const int32 Y1 = FMath::Min(Size - 1, FMath::CeilToInt(FMath::Max3(S[0].Y, S[1].Y, S[2].Y)));
			const double Area = (S[1].X - S[0].X) * (S[2].Y - S[0].Y) - (S[2].X - S[0].X) * (S[1].Y - S[0].Y);
			if (FMath::Abs(Area) < 1e-12) { continue; }
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 X = X0; X <= X1; ++X)
				{
					const FVector2d Q(X + 0.5, Y + 0.5);
					const double W0 = ((S[1].X - Q.X) * (S[2].Y - Q.Y) - (S[2].X - Q.X) * (S[1].Y - Q.Y)) / Area;
					const double W1 = ((S[2].X - Q.X) * (S[0].Y - Q.Y) - (S[0].X - Q.X) * (S[2].Y - Q.Y)) / Area;
					const double W2 = 1 - W0 - W1;
					if (W0 < 0 || W1 < 0 || W2 < 0) { continue; }
					const double Depth = W0 * D[0] + W1 * D[1] + W2 * D[2];
					double& Zp = Zb[Y * Size + X];
					if (Depth >= Zp) { continue; }
					Zp = Depth;
					const FVector3d Nn = (N[0] * W0 + N[1] * W1 + N[2] * W2).GetSafeNormal();
					const double L = 0.35 + 0.65 * FMath::Max(0.0, Nn.Dot(Light));
					Cv.Set(Ox + X, Oy + Y, FColor(uint8(FMath::Min(255.0, Base.R * L + 18)), uint8(FMath::Min(255.0, Base.G * L + 18)), uint8(FMath::Min(255.0, Base.B * L + 18))));
				}
			}
		}
	}

	bool Save(const FString& Path, const OCLayout::FCanvas& Cv)
	{
		FImage Img(Cv.W, Cv.H, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
		FMemory::Memcpy(Img.RawData.GetData(), Cv.Px.GetData(), Cv.Px.Num() * sizeof(FColor));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FImageUtils::SaveImageByExtension(*Path, Img);
	}
}

UOCKitPreviewCommandlet::UOCKitPreviewCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UOCKitPreviewCommandlet::Main(const FString& Params)
{
	FString Src = TEXT("/Game/Environment/Meshes/Kit"), Ref, Out = TEXT("SourceArt/Kit/Previews"), Only;
	FParse::Value(*Params, TEXT("src="), Src);
	FParse::Value(*Params, TEXT("ref="), Ref);
	FParse::Value(*Params, TEXT("out="), Out);
	FParse::Value(*Params, TEXT("only="), Only, false);
	FString Dump;
	if (FParse::Value(*Params, TEXT("dump="), Dump))
	{
		UStaticMesh* M = OCEd::Load<UStaticMesh>(Src / (TEXT("SM_Kit_") + Dump));
		const FMeshDescription* Md = M ? M->GetMeshDescription(0) : nullptr;
		if (!Md) { return 1; }
		FStaticMeshConstAttributes Attr(*Md);
		const TVertexAttributesConstRef<FVector3f> Pos = Attr.GetVertexPositions();
		const TVertexInstanceAttributesConstRef<FVector3f> Nrm = Attr.GetVertexInstanceNormals();
		const TPolygonGroupAttributesConstRef<FName> Slots = Attr.GetPolygonGroupMaterialSlotNames();
		for (const FTriangleID T : Md->Triangles().GetElementIDs())
		{
			const TArrayView<const FVertexInstanceID> Vi = Md->GetTriangleVertexInstances(T);
			const FVector3f P0 = Pos[Md->GetVertexInstanceVertex(Vi[0])], P1 = Pos[Md->GetVertexInstanceVertex(Vi[1])], P2 = Pos[Md->GetVertexInstanceVertex(Vi[2])];
			const FVector3f G = (P2 - P0).Cross(P1 - P0).GetSafeNormal();
			UE_LOG(LogOpenCity, Display, TEXT("TRI %s p0 %s p1 %s p2 %s geo %s n0 %s"), *Slots[Md->GetTrianglePolygonGroup(T)].ToString(), *P0.ToString(), *P1.ToString(), *P2.ToString(), *G.ToString(), *Nrm[Vi[0]].ToString());
		}
		return 0;
	}
	if (FPaths::IsRelative(Out)) { Out = OCEd::ProjectDir() / Out; }
	TArray<FString> OnlyNames;
	Only.ParseIntoArray(OnlyNames, TEXT(","));
	TArray<FString> Names;
	for (const FString& Path : UEditorAssetLibrary::ListAssets(Src, false))
	{
		const FString Name = FPaths::GetBaseFilename(Path);
		if (Name.StartsWith(TEXT("SM_Kit_")) && (OnlyNames.IsEmpty() || OnlyNames.Contains(Name.Mid(7)))) { Names.AddUnique(Name); }
	}
	Names.Sort();
	const int32 Size = 384, Pad = 16, Label = 14;
	const int32 Cols = Ref.IsEmpty() ? 6 : 4;
	const int32 CellW = Ref.IsEmpty() ? Size : 2 * Size + Pad;
	const int32 Rows = (Names.Num() + Cols - 1) / Cols;
	OCLayout::FCanvas Sheet;
	Sheet.W = Cols * (CellW + Pad) + Pad;
	Sheet.H = Rows * (Size + Label + Pad) + Pad;
	Sheet.Px.Init(FColor(232, 230, 224), Sheet.W * Sheet.H);
	for (int32 i = 0; i < Names.Num(); ++i)
	{
		UStaticMesh* Mesh = OCEd::Load<UStaticMesh>(Src / Names[i]);
		if (!Mesh) { continue; }
		OCLayout::FCanvas One;
		One.W = CellW;
		One.H = Size;
		One.Px.Init(FColor(232, 230, 224), One.W * One.H);
		if (!Ref.IsEmpty())
		{
			if (UStaticMesh* R = OCEd::Load<UStaticMesh>(Ref / Names[i])) { Render(R, One, 0, 0, Size); }
			Render(Mesh, One, Size + Pad, 0, Size);
		}
		else
		{
			Render(Mesh, One, 0, 0, Size);
		}
		Save(Out / TEXT("pieces") / (Names[i].Mid(7) + TEXT(".png")), One);
		const int32 Cx = Pad + (i % Cols) * (CellW + Pad), Cy = Pad + (i / Cols) * (Size + Label + Pad);
		for (int32 y = 0; y < Size; ++y) { for (int32 x = 0; x < CellW; ++x) { Sheet.Set(Cx + x, Cy + y, One.Px[y * CellW + x]); } }
		Sheet.Text(Cx, Cy + Size + 4, Names[i].Mid(7) + (Ref.IsEmpty() ? FString() : FString(TEXT("  (left: ref, right: new)"))), FColor(20, 20, 20));
	}
	Save(Out / TEXT("contact_sheet.png"), Sheet);
	OCEd::Log(TEXT("oc_kit"), FString::Printf(TEXT("previews: %d pieces -> %s"), Names.Num(), *Out));
	return 0;
}
