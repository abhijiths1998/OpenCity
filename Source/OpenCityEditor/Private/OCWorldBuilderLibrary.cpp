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
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "OCLog.h"
#include "WorldPartition/RuntimeHashSet/RuntimePartition.h"
#include "WorldPartition/WorldPartition.h"

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

namespace OCRoadMesh
{
	/** Geometry for one chunk, in world space until it is built. */
	struct FChunk
	{
		struct FPoly { TArray<FVector> Points; int32 Group; };
		TArray<FPoly> Polys;
		FBox Bounds = FBox(ForceInit);

		void Add(TArray<FVector> Points, int32 Group)
		{
			// Normalise winding so every polygon faces up (measured: front faces have a
			// negative Z cross product here); a down-facing one renders black when two-sided.
			if (Points.Num() >= 3 && FVector::CrossProduct(Points[1] - Points[0], Points[2] - Points[0]).Z > 0.0)
			{
				Algo::Reverse(Points);
			}
			for (const FVector& P : Points) { Bounds += P; }
			Polys.Add({ MoveTemp(Points), Group });
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
		TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();

		const FPolygonGroupID Groups[2] = { Desc.CreatePolygonGroup(), Desc.CreatePolygonGroup() };
		SlotNames[Groups[0]] = TEXT("Road");
		SlotNames[Groups[1]] = TEXT("Sidewalk");

		for (const FChunk::FPoly& Poly : Chunk.Polys)
		{
			TArray<FVertexInstanceID> Instances;
			for (const FVector& World : Poly.Points)
			{
				const FVertexID V = Desc.CreateVertex();
				Positions[V] = FVector3f(World - Origin);
				const FVertexInstanceID I = Desc.CreateVertexInstance(V);
				Normals[I] = FVector3f::UpVector;
				Tangents[I] = FVector3f::ForwardVector;
				BinormalSigns[I] = 1.f;
				UVs.Set(I, 0, FVector2f(World.X / 400.f, World.Y / 400.f)); // 4 m texture tiles
				Instances.Add(I);
			}
			Desc.CreatePolygon(Groups[Poly.Group], Instances);
		}

		UPackage* Package = CreatePackage(*(PackagePath / Name));
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Package, *Name, RF_Public | RF_Standalone);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(RoadMat, TEXT("Road"), TEXT("Road")));
		Mesh->GetStaticMaterials().Add(FStaticMaterial(SidewalkMat, TEXT("Sidewalk"), TEXT("Sidewalk")));
		Mesh->SetNumSourceModels(1);
		Mesh->GetSourceModel(0).BuildSettings.bRecomputeNormals = false;
		Mesh->GetSourceModel(0).BuildSettings.bRecomputeTangents = false;
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

int32 UOCWorldBuilderLibrary::BuildRoadMeshes(UWorld* World, const FString& LayoutJsonPath, const FString& AssetFolder,
	UMaterialInterface* RoadMaterial, UMaterialInterface* SidewalkMaterial, const TArray<FName>& SidewalkKinds,
	float SidewalkWidthCm, float ChunkSizeCm)
{
	using namespace OCRoadMesh;
	FString Text, Error;
	FOCRoadGraph Graph;
	if (!World || !FFileHelper::LoadFileToString(Text, *LayoutJsonPath) || !Graph.LoadFromLayoutJson(Text, Error))
	{
		UE_LOG(LogOpenCity, Error, TEXT("BuildRoadMeshes: cannot load %s %s"), *LayoutJsonPath, *Error);
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
		const FVector Lift(0.f, 0.f, KindLift(S.Kind));
		FChunk& C = Chunks.FindOrAdd(ChunkOf((A + B) * 0.5));
		// Run each strip half its width past junctions, so neighbouring strips always overlap
		// (no wedge gaps where streets meet at an angle).
		const FVector EA = Degree[S.A] > 1 ? A - Dir * Half : A;
		const FVector EB = Degree[S.B] > 1 ? B + Dir * Half : B;
		C.Add({ EA - Side * Half + Lift, EA + Side * Half + Lift, EB + Side * Half + Lift, EB - Side * Half + Lift }, 0);

		if (SidewalkKinds.Contains(S.Kind))
		{
			const float Len = FVector::Dist2D(A, B);
			const float TrimA = Degree[S.A] > 1 ? OuterHalf[S.A] : 0.f;
			const float TrimB = Degree[S.B] > 1 ? OuterHalf[S.B] : 0.f;
			if (Len > TrimA + TrimB + 100.f)
			{
				const FVector SA = A + (B - A) * (TrimA / Len);
				const FVector SB = B + (A - B) * (TrimB / Len);
				const FVector Kerb(0.f, 0.f, 12.f);
				for (const float Sign : { -1.f, 1.f })
				{
					const FVector In = Side * (Sign * Half), Out = Side * (Sign * (Half + SidewalkWidthCm));
					C.Add({ SA + In + Kerb, SA + Out + Kerb, SB + Out + Kerb, SB + In + Kerb }, 1);
				}
			}
		}
	}

	// Junction discs (12-sided) over the overlapping road ends.
	for (int32 N = 0; N < Nodes.Num(); ++N)
	{
		if (Degree[N] < 2 || RoadHalf[N] <= 0.f)
		{
			continue;
		}
		TArray<FVector> Ring;
		for (int32 i = 0; i < 12; ++i)
		{
			const float Ang = 2.f * PI * i / 12.f;
			Ring.Add(Nodes[N].Location + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.f) * RoadHalf[N] + FVector(0.f, 0.f, 7.f));
		}
		Chunks.FindOrAdd(ChunkOf(Nodes[N].Location)).Add(MoveTemp(Ring), 0);
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
