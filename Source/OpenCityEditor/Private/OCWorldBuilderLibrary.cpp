#include "OCWorldBuilderLibrary.h"

#include "DynamicRHI.h"
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
