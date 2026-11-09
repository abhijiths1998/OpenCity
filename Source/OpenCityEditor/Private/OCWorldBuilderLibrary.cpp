#include "OCWorldBuilderLibrary.h"

#include "Algo/Reverse.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DynamicRHI.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/FileHelper.h"
#include "StaticMeshAttributes.h"
#include "World/OCRoadGraph.h"
#include "Engine/World.h"
#include "RHIGlobals.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "OCLog.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/RuntimeHashSet/RuntimePartition.h"
#include "WorldPartition/RuntimeHashSet/WorldPartitionRuntimeHashSet.h"
#include "WorldPartition/WorldPartition.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "World/OCBlockoutActor.h"

ALandscape* UOCWorldBuilderLibrary::CreateLandscapeFromHeightmap(UWorld* World, const FString& HeightmapPng,
	FVector Location, FVector Scale, int32 SectionsPerComponent, int32 QuadsPerSection, int32 GridSizeInComponents,
	UMaterialInterface* Material)
{
	if (!World)
	{
		UE_LOG(LogOpenCity, Error, TEXT("CreateLandscapeFromHeightmap: no world"));
		return nullptr;
	}

	FImage Image;
	if (!FImageUtils::LoadImage(*HeightmapPng, Image))
	{
		UE_LOG(LogOpenCity, Error, TEXT("CreateLandscapeFromHeightmap: could not read %s"), *HeightmapPng);
		return nullptr;
	}
	if (Image.Format != ERawImageFormat::G16)
	{
		UE_LOG(LogOpenCity, Warning, TEXT("CreateLandscapeFromHeightmap: %s is not 16-bit grayscale; converting (precision may be lost)"), *HeightmapPng);
		Image.ChangeFormat(ERawImageFormat::G16, EGammaSpace::Linear);
	}

	const int32 SizeX = Image.SizeX;
	const int32 SizeY = Image.SizeY;
	const int32 QuadsPerComponent = SectionsPerComponent * QuadsPerSection;
	if ((SizeX - 1) % QuadsPerComponent != 0 || (SizeY - 1) % QuadsPerComponent != 0)
	{
		UE_LOG(LogOpenCity, Error, TEXT("CreateLandscapeFromHeightmap: %dx%d is not (n * %d + 1) on both axes"),
			SizeX, SizeY, QuadsPerComponent);
		return nullptr;
	}

	const TArrayView64<uint16> Pixels = Image.AsG16();
	TArray<uint16> Heights;
	Heights.SetNumUninitialized(SizeX * SizeY);
	FMemory::Memcpy(Heights.GetData(), Pixels.GetData(), Heights.Num() * sizeof(uint16));

	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	HeightDataPerLayers.Add(FGuid(), MoveTemp(Heights));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
	MaterialLayerDataPerLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	// Landscape heights live in edit layers; the visible heightmap is merged from them on the
	// GPU. Without a renderer (a NullRHI commandlet) collision gets the heights but the terrain
	// renders flat. Run commandlets that build landscapes with -AllowCommandletRendering.
	if (!GDynamicRHI || GUsingNullRHI)
	{
		UE_LOG(LogOpenCity, Error, TEXT("CreateLandscapeFromHeightmap: needs a real RHI (run with -AllowCommandletRendering)"));
		return nullptr;
	}

	ALandscape* Landscape = World->SpawnActor<ALandscape>(Location, FRotator::ZeroRotator);
	if (!Landscape)
	{
		UE_LOG(LogOpenCity, Error, TEXT("CreateLandscapeFromHeightmap: spawn failed"));
		return nullptr;
	}
	Landscape->LandscapeMaterial = Material;
	Landscape->SetActorRelativeScale3D(Scale);
	Landscape->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((SizeX * SizeY) / (2048 * 2048) + 1), (uint32)2);
	Landscape->Import(FGuid::NewGuid(), 0, 0, SizeX - 1, SizeY - 1, SectionsPerComponent, QuadsPerSection,
		HeightDataPerLayers, *HeightmapPng, MaterialLayerDataPerLayers, ELandscapeImportAlphamapType::Additive,
		TArrayView<const FLandscapeLayer>());

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info)
	{
		UE_LOG(LogOpenCity, Error, TEXT("CreateLandscapeFromHeightmap: import produced no landscape info"));
		return Landscape;
	}
	Info->UpdateLayerInfoMap(Landscape);

	// Merge the edit layers into the render heightmaps now (normally done on editor ticks).
	Landscape->ForceLayersFullUpdate();
	Landscape->ForceUpdateLayersContent();

	// World Partition: split into streaming proxies, as the New Landscape tool does.
	if (ULandscapeSubsystem* Subsystem = World->GetSubsystem<ULandscapeSubsystem>())
	{
		if (Subsystem->IsGridBased() && GridSizeInComponents > 0)
		{
			Subsystem->ChangeGridSize(Info, static_cast<uint32>(GridSizeInComponents));
		}
	}
	UE_LOG(LogOpenCity, Display, TEXT("CreateLandscapeFromHeightmap: %dx%d vertices, %dx%d components, grid %d"),
		SizeX, SizeY, (SizeX - 1) / QuadsPerComponent, (SizeY - 1) / QuadsPerComponent, GridSizeInComponents);
	return Landscape;
}

int32 UOCWorldBuilderLibrary::ConfigureStreaming(UWorld* World, int32 CellSizeCm, int32 LoadingRangeCm)
{
	UWorldPartition* WP = World ? World->GetWorldPartition() : nullptr;
	if (!WP || !WP->RuntimeHash)
	{
		UE_LOG(LogOpenCity, Error, TEXT("ConfigureStreaming: not a World Partition world"));
		return 0;
	}
	// Only each partition's MAIN layer: HLOD layers need far longer ranges to show distant
	// terrain. RuntimePartitions is private, so walk it through reflection.
	UObject* Hash = WP->RuntimeHash;
	FArrayProperty* PartitionsProp = FindFProperty<FArrayProperty>(Hash->GetClass(), TEXT("RuntimePartitions"));
	FStructProperty* DescProp = PartitionsProp ? CastField<FStructProperty>(PartitionsProp->Inner) : nullptr;
	FObjectProperty* MainProp = DescProp ? FindFProperty<FObjectProperty>(DescProp->Struct, TEXT("MainLayer")) : nullptr;
	FNameProperty* NameProp = DescProp ? FindFProperty<FNameProperty>(DescProp->Struct, TEXT("Name")) : nullptr;
	if (!MainProp || !NameProp)
	{
		UE_LOG(LogOpenCity, Error, TEXT("ConfigureStreaming: %s has no RuntimePartitions/MainLayer (engine changed?)"), *Hash->GetClass()->GetName());
		return 0;
	}

	int32 Changed = 0;
	FScriptArrayHelper Partitions(PartitionsProp, PartitionsProp->ContainerPtrToValuePtr<void>(Hash));
	for (int32 i = 0; i < Partitions.Num(); ++i)
	{
		void* Desc = Partitions.GetRawPtr(i);
		URuntimePartition* Main = Cast<URuntimePartition>(MainProp->GetObjectPropertyValue_InContainer(Desc));
		if (!Main)
		{
			continue;
		}
		Main->Modify();
		Main->LoadingRange = LoadingRangeCm;
		// CellSize is private on grid partitions; set it through reflection where it exists.
		if (FIntProperty* CellProp = FindFProperty<FIntProperty>(Main->GetClass(), TEXT("CellSize")))
		{
			CellProp->SetPropertyValue_InContainer(Main, CellSizeCm);
		}
		UE_LOG(LogOpenCity, Display, TEXT("ConfigureStreaming: partition '%s' main layer %s: loading range %d cm, cell size %d cm"),
			*NameProp->GetPropertyValue_InContainer(Desc).ToString(), *Main->GetClass()->GetName(), LoadingRangeCm, CellSizeCm);
		++Changed;
	}
	return Changed;
}

int32 UOCWorldBuilderLibrary::ConfigureHLOD(UWorld* World, UHLODLayer* RootLayer, const TArray<int32>& CellSizesCm, const TArray<int32>& LoadingRangesCm)
{
	UWorldPartition* WP = World ? World->GetWorldPartition() : nullptr;
	if (!WP || !WP->RuntimeHash || !RootLayer)
	{
		UE_LOG(LogOpenCity, Error, TEXT("ConfigureHLOD: needs a World Partition world and an HLOD layer"));
		return 0;
	}
	int32 Levels = 0;
	for (const UHLODLayer* L = RootLayer; L; L = L->GetParentLayer())
	{
		++Levels;
	}
	if (CellSizesCm.Num() < Levels || LoadingRangesCm.Num() < Levels)
	{
		UE_LOG(LogOpenCity, Error, TEXT("ConfigureHLOD: %s has %d levels but %d cell sizes and %d loading ranges were given"),
			*RootLayer->GetName(), Levels, CellSizesCm.Num(), LoadingRangesCm.Num());
		return 0;
	}

	// RuntimePartitions is private on the hash set; walk it through reflection (as ConfigureStreaming does).
	UObject* Hash = WP->RuntimeHash;
	FArrayProperty* PartitionsProp = FindFProperty<FArrayProperty>(Hash->GetClass(), TEXT("RuntimePartitions"));
	FStructProperty* DescProp = PartitionsProp ? CastField<FStructProperty>(PartitionsProp->Inner) : nullptr;
	if (!DescProp || DescProp->Struct != FRuntimePartitionDesc::StaticStruct())
	{
		UE_LOG(LogOpenCity, Error, TEXT("ConfigureHLOD: %s has no RuntimePartitions of FRuntimePartitionDesc (engine changed?)"), *Hash->GetClass()->GetName());
		return 0;
	}

	WP->Modify();
	WP->SetDefaultHLODLayer(RootLayer);
	Hash->Modify();
	FScriptArrayHelper Partitions(PartitionsProp, PartitionsProp->ContainerPtrToValuePtr<void>(Hash));
	for (int32 i = 0; i < Partitions.Num(); ++i)
	{
		FRuntimePartitionDesc& Desc = *reinterpret_cast<FRuntimePartitionDesc*>(Partitions.GetRawPtr(i));
		if (!Desc.MainLayer)
		{
			continue;
		}
		// Same steps as UWorldPartitionRuntimeHashSet::SetDefaultValues, with our sizes.
		Desc.HLODSetups.Reset();
		int32 Index = 0;
		for (const UHLODLayer* L = RootLayer; L; L = L->GetParentLayer(), ++Index)
		{
			FRuntimePartitionHLODSetup& Setup = Desc.HLODSetups.AddDefaulted_GetRef();
			Setup.Name = L->GetFName();
			Setup.HLODLayers = { L };
			URuntimePartition* Layer = DuplicateObject<URuntimePartition>(Desc.MainLayer, Hash);
			Layer->Name = Setup.Name;
			Layer->bBlockOnSlowStreaming = false;
			Layer->bClientOnlyVisible = true;
			Layer->Priority = 0;
			Layer->HLODIndex = Index;
			Layer->LoadingRange = LoadingRangesCm[Index];
			if (FIntProperty* CellProp = FindFProperty<FIntProperty>(Layer->GetClass(), TEXT("CellSize")))
			{
				CellProp->SetPropertyValue_InContainer(Layer, CellSizesCm[Index]);
			}
			Setup.PartitionLayer = Layer;
			UE_LOG(LogOpenCity, Display, TEXT("ConfigureHLOD: partition '%s' HLOD %d = %s (%s): cell %d cm, loading range %d cm"),
				*Desc.Name.ToString(), Index, *L->GetName(), *UEnum::GetValueAsString(L->GetLayerType()), CellSizesCm[Index], LoadingRangesCm[Index]);
		}
	}
	return Levels;
}

namespace OCRoadMesh
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

	/** One polygon: positions (world cm), per-vertex normals and UV0, and a per-polygon UV1. */
	struct FRoadPoly
	{
		TArray<FVector> P;
		TArray<FVector3f> N;
		TArray<FVector2f> UV0;
		FVector2f UV1 = FVector2f::ZeroVector;
		int32 Group = 0;
	};

	/** Geometry for one chunk, in world space until it is built. */
	struct FChunk
	{
		TArray<FRoadPoly> Polys;
		FBox Bounds = FBox(ForceInit);

		/** Facing: the side the polygon's front should face (Up for surfaces). Two-sided materials
		 * shade back faces with the flipped normal, so the winding must match it. */
		void Add(FRoadPoly Poly, const FVector& Facing)
		{
			// Measured: front faces have a negative cross product of the first two edges here.
			if (Poly.P.Num() >= 3 && FVector::DotProduct(-FVector::CrossProduct(Poly.P[1] - Poly.P[0], Poly.P[2] - Poly.P[0]), Facing) < 0.0)
			{
				Algo::Reverse(Poly.P);
				Algo::Reverse(Poly.N);
				Algo::Reverse(Poly.UV0);
			}
			for (const FVector& V : Poly.P) { Bounds += V; }
			Polys.Add(MoveTemp(Poly));
		}
	};

	float KindLift(FName Kind)
	{
		// Different kinds sit at slightly different heights so overlaps never z-fight.
		if (Kind == TEXT("avenue")) { return 6.f; }
		if (Kind == TEXT("street") || Kind == TEXT("coast") || Kind == TEXT("pass")) { return 5.f; }
		if (Kind == TEXT("pier")) { return 3.f; }
		return 4.f; // alley
	}

	/** Lanes per direction for the road material's markings (0 = none: alleys, piers, junctions). */
	float MarkedLanes(const FOCRoadSegment& S)
	{
		return (S.Kind == TEXT("alley") || S.Kind == TEXT("pier")) ? 0.f : static_cast<float>(FMath::Max(S.LanesPerDirection, 1));
	}

	constexpr double StepCm = 200.0;     // drape resolution along roads
	constexpr double ColumnCm = 150.0;   // drape resolution across roads and junctions
	// Everything sits BaseLift above the heightmap: the generator grades the ground 20 cm under the
	// road, and distant landscape LODs drift by a few cm. Kinds add 3-6 cm so overlaps never z-fight.
	constexpr float BaseLift = 15.f;
	constexpr float SidewalkLift = BaseLift + 15.f; // 9-12 cm curbs above the road
	constexpr float SkirtDepth = 40.f;              // road edges without sidewalks drop this far below the surface

	UStaticMesh* BuildMesh(const FChunk& Chunk, const FVector& Origin, const FString& PackagePath, const FString& Name,
		UMaterialInterface* RoadMat, UMaterialInterface* SidewalkMat)
	{
		FMeshDescription Desc;
		FStaticMeshAttributes Attr(Desc);
		Attr.Register();
		TVertexAttributesRef<FVector3f> Positions = Attr.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attr.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tangents = Attr.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> BinormalSigns = Attr.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attr.GetVertexInstanceUVs();
		Desc.SetNumUVChannels(2); // the description's own channel count; the build trims UVs to it
		UVs.SetNumChannels(2);
		TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();

		const FPolygonGroupID Groups[2] = { Desc.CreatePolygonGroup(), Desc.CreatePolygonGroup() };
		SlotNames[Groups[0]] = TEXT("Road");
		SlotNames[Groups[1]] = TEXT("Sidewalk");

		for (const FRoadPoly& Poly : Chunk.Polys)
		{
			TArray<FVertexInstanceID> Instances;
			for (int32 i = 0; i < Poly.P.Num(); ++i)
			{
				const FVertexID V = Desc.CreateVertex();
				Positions[V] = FVector3f(Poly.P[i] - Origin);
				const FVertexInstanceID I = Desc.CreateVertexInstance(V);
				Normals[I] = Poly.N[i];
				const FVector3f T = FVector3f::CrossProduct(FVector3f::RightVector, Poly.N[i]).GetSafeNormal();
				Tangents[I] = T.IsNearlyZero() ? FVector3f::ForwardVector : T;
				BinormalSigns[I] = 1.f;
				UVs.Set(I, 0, Poly.UV0[i]);
				UVs.Set(I, 1, Poly.UV1);
				Instances.Add(I);
			}
			Desc.CreatePolygon(Groups[Poly.Group], Instances);
		}

		UPackage* Package = CreatePackage(*(PackagePath / Name));
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Package, *Name, RF_Public | RF_Standalone);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(RoadMat, TEXT("Road"), TEXT("Road")));
		Mesh->GetStaticMaterials().Add(FStaticMaterial(SidewalkMat, TEXT("Sidewalk"), TEXT("Sidewalk")));
		Mesh->SetNumSourceModels(1);
		FMeshBuildSettings& Build = Mesh->GetSourceModel(0).BuildSettings;
		Build.bRecomputeNormals = false;
		Build.bRecomputeTangents = false;
		Build.bGenerateLightmapUVs = false; // UV1 carries road data for the material; Lumen needs no lightmaps
		Mesh->CreateMeshDescription(0, MoveTemp(Desc));
		Mesh->CommitMeshDescription(0);
		FMeshNaniteSettings Nanite = Mesh->GetNaniteSettings();
		Nanite.bEnabled = true;
		Mesh->SetNaniteSettings(Nanite);
		Mesh->Build(/*bInSilent*/ true);
		Mesh->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Mesh);
		return Mesh;
	}
}

int32 UOCWorldBuilderLibrary::BuildRoadMeshes(UWorld* World, const FString& LayoutJsonPath, const FString& HeightmapPng,
	const FString& AssetFolder, UMaterialInterface* RoadMaterial, UMaterialInterface* SidewalkMaterial,
	const TArray<FName>& SidewalkKinds, float SidewalkWidthCm, float ChunkSizeCm)
{
	using namespace OCRoadMesh;
	FString Text, Error;
	FOCRoadGraph Graph;
	TSharedPtr<FJsonObject> Root;
	FHeightField Ground;
	if (!World || !FFileHelper::LoadFileToString(Text, *LayoutJsonPath) || !Graph.LoadFromLayoutJson(Text, Error)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Ground.Load(HeightmapPng, Root))
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildRoadMeshes: cannot load %s / %s %s"), *LayoutJsonPath, *HeightmapPng, *Error);
		return 0;
	}
	const TArray<FOCRoadNode>& Nodes = Graph.GetNodes();
	const TArray<FOCRoadSegment>& Segments = Graph.GetSegments();

	// Per junction: widest road half-width meeting there (disc size, sidewalk trim).
	TArray<float> RoadHalf, OuterHalf;
	RoadHalf.Init(0.f, Nodes.Num());
	OuterHalf.Init(0.f, Nodes.Num());
	TArray<int32> Degree;
	Degree.Init(0, Nodes.Num());
	for (const FOCRoadSegment& S : Segments)
	{
		const bool bWalk = SidewalkKinds.Contains(S.Kind);
		for (const int32 N : { S.A, S.B })
		{
			RoadHalf[N] = FMath::Max(RoadHalf[N], S.WidthCm * 0.5f);
			OuterHalf[N] = FMath::Max(OuterHalf[N], S.WidthCm * 0.5f + (bWalk ? SidewalkWidthCm : 0.f));
			++Degree[N];
		}
	}

	TMap<FIntPoint, FChunk> Chunks;
	auto ChunkOf = [ChunkSizeCm](const FVector& P)
	{
		return FIntPoint(FMath::FloorToInt32(P.X / ChunkSizeCm), FMath::FloorToInt32(P.Y / ChunkSizeCm));
	};
	auto Drape = [&Ground](const FVector& P, float Lift)
	{
		return FVector(P.X, P.Y, Ground.HeightCm(P.X, P.Y) + Lift);
	};

	/**
	 * A strip from P0 to P1 between lateral offsets In and Out (cm, along Side), split every
	 * StepCm and draped on the ground. Road strips get UV0 = (lateral m, along m) for markings;
	 * sidewalks get world-planar metres.
	 */
	auto AddStrip = [&](FChunk& C, const FVector& P0, const FVector& P1, const FVector& Side, float In, float Out,
		float Lift, int32 Group, double AlongStartM, const FVector2f& UV1)
	{
		const double Len = FVector::Dist2D(P0, P1);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt32(Len / StepCm));
		const int32 Cols = FMath::Max(1, FMath::CeilToInt32(FMath::Abs(Out - In) / ColumnCm));
		for (int32 i = 0; i < Steps; ++i)
		{
			const double T0 = double(i) / Steps, T1 = double(i + 1) / Steps;
			const FVector A = FMath::Lerp(P0, P1, T0), B = FMath::Lerp(P0, P1, T1);
			const float AlongA = static_cast<float>(AlongStartM + T0 * Len / 100.0);
			const float AlongB = static_cast<float>(AlongStartM + T1 * Len / 100.0);
			for (int32 c = 0; c < Cols; ++c)
			{
				const float L0 = FMath::Lerp(In, Out, float(c) / Cols), L1 = FMath::Lerp(In, Out, float(c + 1) / Cols);
				FRoadPoly Poly;
				Poly.Group = Group;
				Poly.UV1 = UV1;
				for (const FVector& Corner : { A + Side * L0, A + Side * L1, B + Side * L1, B + Side * L0 })
				{
					const FVector V = Drape(Corner, Lift);
					Poly.P.Add(V);
					Poly.N.Add(Ground.NormalAt(V.X, V.Y));
					Poly.UV0.Add(FVector2f(static_cast<float>(V.X / 100.0), static_cast<float>(V.Y / 100.0)));
				}
				if (Group == 0)
				{
					Poly.UV0 = { FVector2f(L0 / 100.f, AlongA), FVector2f(L1 / 100.f, AlongA), FVector2f(L1 / 100.f, AlongB), FVector2f(L0 / 100.f, AlongB) };
				}
				C.Add(MoveTemp(Poly), FVector::UpVector);
			}
		}
	};

	/** Vertical face along a strip edge from Bottom to Top lift, facing Facing (curbs, skirts). */
	auto AddWall = [&](FChunk& C, const FVector& P0, const FVector& P1, const FVector& Edge, const FVector& Facing,
		float Bottom, float Top, int32 Group)
	{
		const double Len = FVector::Dist2D(P0, P1);
		const int32 Steps = FMath::Max(1, FMath::CeilToInt32(Len / StepCm));
		for (int32 i = 0; i < Steps; ++i)
		{
			const FVector A = FMath::Lerp(P0, P1, double(i) / Steps) + Edge, B = FMath::Lerp(P0, P1, double(i + 1) / Steps) + Edge;
			FRoadPoly Poly;
			Poly.Group = Group;
			Poly.P = { Drape(A, Bottom), Drape(A, Top), Drape(B, Top), Drape(B, Bottom) };
			Poly.N.Init(FVector3f(Facing), 4);
			const float UA = static_cast<float>(i * Len / Steps / 100.0), UB = static_cast<float>((i + 1) * Len / Steps / 100.0);
			Poly.UV0 = { FVector2f(UA, 0.3f), FVector2f(UA, 0.4f), FVector2f(UB, 0.4f), FVector2f(UB, 0.3f) };
			C.Add(MoveTemp(Poly), Facing);
		}
	};

	for (const FOCRoadSegment& S : Segments)
	{
		if (!S.Bridge.IsNone())
		{
			continue; // bridges are built as decks with collision
		}
		const FVector A = Nodes[S.A].Location;
		const FVector B = Nodes[S.B].Location;
		const FVector Dir = (B - A).GetSafeNormal2D();
		if (Dir.IsNearlyZero())
		{
			continue;
		}
		const FVector Side(-Dir.Y, Dir.X, 0.f);
		const float Half = S.WidthCm * 0.5f;
		const float Lift = BaseLift + KindLift(S.Kind);
		FChunk& C = Chunks.FindOrAdd(ChunkOf((A + B) * 0.5));
		// Junctions (3+ roads): the strip stops at the junction polygon, which is built below.
		// Bends (2 roads): run the strip half its width past the node so the two overlap and the
		// markings continue; an underlay fills the outer corner.
		auto EndOffset = [&](int32 N) { return Degree[N] >= 3 ? -RoadHalf[N] : Degree[N] == 2 ? Half : 0.f; };
		const float ExtA = EndOffset(S.A), ExtB = EndOffset(S.B);
		AddStrip(C, A - Dir * ExtA, B + Dir * ExtB, Side, -Half, Half, Lift, 0, -ExtA / 100.0,
			FVector2f(Half / 100.f, MarkedLanes(S)));

		if (!SidewalkKinds.Contains(S.Kind))
		{
			for (const float Sign : { -1.f, 1.f }) // skirts hide the gap under a road's edge on uneven ground
			{
				AddWall(C, A - Dir * ExtA, B + Dir * ExtB, Side * (Sign * Half), Side * Sign, Lift - SkirtDepth, Lift, 0);
			}
		}
		else
		{
			const float Len = FVector::Dist2D(A, B);
			const float TrimA = Degree[S.A] > 1 ? OuterHalf[S.A] : 0.f;
			const float TrimB = Degree[S.B] > 1 ? OuterHalf[S.B] : 0.f;
			if (Len > TrimA + TrimB + 100.f)
			{
				const FVector SA = A + Dir * TrimA;
				const FVector SB = B - Dir * TrimB;
				for (const float Sign : { -1.f, 1.f })
				{
					AddStrip(C, SA, SB, Side, Sign * Half, Sign * (Half + SidewalkWidthCm), SidewalkLift, 1, 0.0, FVector2f::ZeroVector);
					AddWall(C, SA, SB, Side * (Sign * Half), Side * -Sign, Lift, SidewalkLift, 1);
					AddWall(C, SA, SB, Side * (Sign * (Half + SidewalkWidthCm)), Side * Sign, SidewalkLift - SkirtDepth, SidewalkLift, 1);
				}
			}
		}
	}

	// Junction polygons: the convex hull of every incident road's corners at the junction edge
	// (RoadHalf from the node), draped and fanned from the centre. At 3+ roads it sits above the
	// strips (no markings inside the junction); at bends it is an underlay filling the outer corner.
	TArray<TArray<TPair<FVector, float>>> Incident; // per node: (direction away from node, half width)
	Incident.SetNum(Nodes.Num());
	for (const FOCRoadSegment& S : Segments)
	{
		const FVector D = (Nodes[S.B].Location - Nodes[S.A].Location).GetSafeNormal2D();
		if (S.Bridge.IsNone() && !D.IsNearlyZero())
		{
			Incident[S.A].Add({ D, S.WidthCm * 0.5f });
			Incident[S.B].Add({ -D, S.WidthCm * 0.5f });
		}
	}
	for (int32 N = 0; N < Nodes.Num(); ++N)
	{
		if (Incident[N].Num() < 2)
		{
			continue;
		}
		const FVector Centre = Nodes[N].Location;
		TArray<FVector2D> Pts;
		for (const TPair<FVector, float>& R : Incident[N])
		{
			const FVector Side(-R.Key.Y, R.Key.X, 0.f);
			for (const float Sign : { -1.f, 1.f })
			{
				const FVector P = Centre + R.Key * RoadHalf[N] + Side * (Sign * R.Value);
				Pts.Add(FVector2D(P.X, P.Y));
			}
		}
		// Andrew's monotone chain.
		Pts.Sort([](const FVector2D& L, const FVector2D& R) { return L.X < R.X || (L.X == R.X && L.Y < R.Y); });
		auto Cross = [](const FVector2D& O, const FVector2D& A, const FVector2D& B) { return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X); };
		TArray<FVector2D> Hull;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const int32 Start = Hull.Num();
			for (int32 k = 0; k < Pts.Num(); ++k)
			{
				const FVector2D& P = Pass == 0 ? Pts[k] : Pts[Pts.Num() - 1 - k];
				while (Hull.Num() >= Start + 2 && Cross(Hull[Hull.Num() - 2], Hull.Last(), P) <= 0.0)
				{
					Hull.Pop();
				}
				Hull.Add(P);
			}
			Hull.Pop();
		}
		if (Hull.Num() < 3)
		{
			continue;
		}
		const float JunctionLift = BaseLift + (Incident[N].Num() >= 3 ? 7.f : 2.5f); // bends: an underlay below every road kind
		const FVector2f JunctionUV1(RoadHalf[N] / 100.f, 0.f); // surface as the widest road, no markings
		FChunk& C = Chunks.FindOrAdd(ChunkOf(Centre));
		// Fan from the centre, each triangle split into a grid of small ones so it follows the ground.
		for (int32 i = 0; i < Hull.Num(); ++i)
		{
			const FVector P0 = Centre, P1(Hull[i].X, Hull[i].Y, 0.0), P2(Hull[(i + 1) % Hull.Num()].X, Hull[(i + 1) % Hull.Num()].Y, 0.0);
			const double Edge = FMath::Max3(FVector::Dist2D(P0, P1), FVector::Dist2D(P1, P2), FVector::Dist2D(P2, P0));
			const int32 Div = FMath::Max(1, FMath::CeilToInt32(Edge / ColumnCm));
			auto At = [&](int32 a, int32 b) // barycentric grid point: a steps towards P1, b towards P2
			{
				return P0 + (P1 - P0) * (double(a) / Div) + (P2 - P0) * (double(b) / Div);
			};
			auto Tri = [&](const FVector& X, const FVector& Y, const FVector& Z)
			{
				FRoadPoly T;
				T.UV1 = JunctionUV1;
				for (const FVector& Q : { X, Y, Z })
				{
					const FVector V = Drape(Q, JunctionLift);
					T.P.Add(V);
					T.N.Add(Ground.NormalAt(V.X, V.Y));
					T.UV0.Add(FVector2f(static_cast<float>(V.X / 100.0), static_cast<float>(V.Y / 100.0)));
				}
				C.Add(MoveTemp(T), FVector::UpVector);
			};
			for (int32 a = 0; a < Div; ++a)
			{
				for (int32 b = 0; a + b < Div; ++b)
				{
					Tri(At(a, b), At(a + 1, b), At(a, b + 1));
					if (a + b + 2 <= Div)
					{
						Tri(At(a + 1, b), At(a + 1, b + 1), At(a, b + 1));
					}
				}
			}
		}
	}

	int32 Built = 0;
	for (const TPair<FIntPoint, FChunk>& Pair : Chunks)
	{
		const FChunk& C = Pair.Value;
		const FVector Origin(C.Bounds.GetCenter().X, C.Bounds.GetCenter().Y, C.Bounds.Min.Z);
		const FString Name = FString::Printf(TEXT("SM_Roads_%d_%d"), Pair.Key.X, Pair.Key.Y < 0 ? 1000 - Pair.Key.Y : Pair.Key.Y);
		UStaticMesh* Mesh = BuildMesh(C, Origin, AssetFolder, Name, RoadMaterial, SidewalkMaterial);
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Origin, FRotator::ZeroRotator);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		Actor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision); // drive on the flattened landscape
		Actor->GetStaticMeshComponent()->SetCanEverAffectNavigation(false);
		Actor->SetActorLabel(Name.RightChop(3));
		Actor->SetFolderPath(TEXT("Roads"));
		++Built;
	}
	UE_LOG(LogOpenCity, Display, TEXT("BuildRoadMeshes: %d segments, %d junctions -> %d chunks"), Segments.Num(), Nodes.Num(), Built);
	return Built;
}

constexpr float BlockoutGridCm = 100.f;   // WORLD_METRICS.md: modular grid 100 / 200 / 400 cm
constexpr float StoreyHeightCm = 320.f;   // WORLD_METRICS.md: floor-to-floor height

int32 UOCWorldBuilderLibrary::BuildBuildings(UWorld* World, const FString& LayoutJsonPath, UStaticMesh* Mesh, UMaterialInterface* Material, float ChunkSizeCm)
{
	FString Text;
	TSharedPtr<FJsonObject> Root;
	const TArray<TSharedPtr<FJsonValue>>* Buildings = nullptr;
	if (!World || !Mesh || !FFileHelper::LoadFileToString(Text, *LayoutJsonPath)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root->TryGetArrayField(TEXT("buildings"), Buildings))
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildBuildings: no buildings in %s (run Tools/make_marevia_layout.py)"), *LayoutJsonPath);
		return 0;
	}

	// Group per chunk first, so each chunk actor sits at its buildings' centre.
	TMap<FIntPoint, TArray<TPair<FTransform, FVector2f>>> Chunks;
	FRandomStream Random(20261108);
	for (const TSharedPtr<FJsonValue>& V : *Buildings)
	{
		const TSharedPtr<FJsonObject> B = V.IsValid() ? V->AsObject() : nullptr;
		const TArray<TSharedPtr<FJsonValue>>* PosPtr = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* SizePtr = nullptr;
		if (!B || !B->TryGetArrayField(TEXT("pos"), PosPtr) || !B->TryGetArrayField(TEXT("size"), SizePtr) || PosPtr->Num() < 2 || SizePtr->Num() < 2)
		{
			UE_LOG(LogOpenCity, Warning, TEXT("BuildBuildings: skipping a malformed building entry"));
			continue;
		}
		const TArray<TSharedPtr<FJsonValue>>& Pos = *PosPtr;
		const TArray<TSharedPtr<FJsonValue>>& Size = *SizePtr;
		const double Base = B->GetNumberField(TEXT("base")), Top = B->GetNumberField(TEXT("top"));
		// Layout metres (x east, y north, yaw counter-clockwise) to Unreal cm (Y south, yaw clockwise).
		const double CentreX = Pos[0]->AsNumber() * 100.0;
		const double CentreY = -Pos[1]->AsNumber() * 100.0;
		// Snap centre and half-extents to the 100 cm modular grid (WORLD_METRICS.md).
		const double SnapX = FMath::RoundToDouble(CentreX / BlockoutGridCm) * BlockoutGridCm;
		const double SnapY = FMath::RoundToDouble(CentreY / BlockoutGridCm) * BlockoutGridCm;
		const float SnapW = FMath::RoundToFloat(Size[0]->AsNumber() * 100.0 / BlockoutGridCm) * BlockoutGridCm;
		const float SnapD = FMath::RoundToFloat(Size[1]->AsNumber() * 100.0 / BlockoutGridCm) * BlockoutGridCm;
		// Floor-to-floor 320 cm: snap total height to whole storeys.
		const float TotalH = FMath::Max(static_cast<float>(Top - Base), 1.0f);
		const int32 Storeys = FMath::Max(1, FMath::RoundToInt(TotalH / StoreyHeightCm));
		const float SnapH = Storeys * StoreyHeightCm;
		const FTransform T(FRotator(0.0, -B->GetNumberField(TEXT("yaw")), 0.0),
			FVector(static_cast<float>(SnapX), static_cast<float>(SnapY), static_cast<float>(Base * 100.0) + SnapH * 0.5f),
			FVector(SnapW, SnapD, SnapH));
		const FIntPoint Key(FMath::FloorToInt32(SnapX / ChunkSizeCm), FMath::FloorToInt32(SnapY / ChunkSizeCm));
		Chunks.FindOrAdd(Key).Add({ T, FVector2f(static_cast<float>(B->GetNumberField(TEXT("style"))), Random.FRand()) });
	}

	int32 Count = 0;
	for (const TPair<FIntPoint, TArray<TPair<FTransform, FVector2f>>>& Pair : Chunks)
	{
		FBox Bounds(ForceInit);
		for (const TPair<FTransform, FVector2f>& I : Pair.Value)
		{
			Bounds += I.Key.GetLocation();
		}
		const FVector Origin(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z);
		AOCBlockoutActor* Actor = World->SpawnActor<AOCBlockoutActor>(Origin, FRotator::ZeroRotator);
		if (!Actor)
		{
			UE_LOG(LogOpenCity, Error, TEXT("BuildBuildings: could not spawn chunk %s"), *Pair.Key.ToString());
			continue;
		}
		UInstancedStaticMeshComponent* ISM = Actor->GetInstances();
		ISM->SetStaticMesh(Mesh);
		ISM->SetMaterial(0, Material);
		for (const TPair<FTransform, FVector2f>& I : Pair.Value)
		{
			const int32 Index = ISM->AddInstance(I.Key, /*bWorldSpace*/ true);
			ISM->SetCustomDataValue(Index, 0, I.Value.X);
			ISM->SetCustomDataValue(Index, 1, I.Value.Y);
			++Count;
		}
		Actor->SetActorLabel(FString::Printf(TEXT("BLOCKOUT_Buildings_%d_%d"), Pair.Key.X, Pair.Key.Y));
		Actor->SetFolderPath(TEXT("Buildings"));
	}
	UE_LOG(LogOpenCity, Display, TEXT("BuildBuildings: %d buildings in %d chunks"), Count, Chunks.Num());
	return Count;
}

UDataLayerAsset* UOCWorldBuilderLibrary::CreateRuntimeDataLayer(UWorld* World, const FString& PackagePath, const FString& Name, bool bInitiallyActivated)
{
	UDataLayerEditorSubsystem* Layers = UDataLayerEditorSubsystem::Get();
	if (!World || !Layers)
	{
		return nullptr;
	}
	const FString ObjectPath = PackagePath / Name + TEXT(".") + Name;
	UDataLayerAsset* Asset = LoadObject<UDataLayerAsset>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Asset)
	{
		UPackage* Package = CreatePackage(*(PackagePath / Name));
		Asset = NewObject<UDataLayerAsset>(Package, *Name, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Asset);
	}
	Asset->SetType(EDataLayerType::Runtime);
	Asset->MarkPackageDirty();

	UDataLayerInstance* Instance = Layers->GetDataLayerInstance(Asset);
	if (!Instance)
	{
		FDataLayerCreationParameters Params;
		Params.DataLayerAsset = Asset;
		Instance = Layers->CreateDataLayerInstance(Params);
	}
	if (!Instance)
	{
		UE_LOG(LogOpenCity, Error, TEXT("CreateRuntimeDataLayer: no instance for %s"), *Name);
		return nullptr;
	}
	Instance->SetInitialRuntimeState(bInitiallyActivated ? EDataLayerRuntimeState::Activated : EDataLayerRuntimeState::Unloaded);
	return Asset;
}

bool UOCWorldBuilderLibrary::AddActorToDataLayer(AActor* Actor, UDataLayerAsset* DataLayer)
{
	UDataLayerEditorSubsystem* Layers = UDataLayerEditorSubsystem::Get();
	UDataLayerInstance* Instance = Layers && DataLayer ? Layers->GetDataLayerInstance(DataLayer) : nullptr;
	return Actor && Instance && Layers->AddActorToDataLayer(Actor, Instance);
}

FGameplayTag UOCWorldBuilderLibrary::MakeTag(FName TagName)
{
	return FGameplayTag::RequestGameplayTag(TagName, /*ErrorIfNotFound*/ true);
}
