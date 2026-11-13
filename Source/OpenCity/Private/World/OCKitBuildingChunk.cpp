#include "World/OCKitBuildingChunk.h"

#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"

AOCKitBuildingChunk::AOCKitBuildingChunk()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);
}

#if WITH_EDITOR
UInstancedStaticMeshComponent* AOCKitBuildingChunk::GetOrAddPiece(UStaticMesh* Mesh)
{
	TArray<UInstancedStaticMeshComponent*> Existing;
	GetComponents(Existing);
	for (UInstancedStaticMeshComponent* C : Existing)
	{
		if (C->GetStaticMesh() == Mesh)
		{
			return C;
		}
	}
	UInstancedStaticMeshComponent* C = NewObject<UInstancedStaticMeshComponent>(this, *FString::Printf(TEXT("ISM_%s"), *Mesh->GetName()));
	C->SetMobility(EComponentMobility::Static);
	C->SetupAttachment(RootComponent);
	C->SetStaticMesh(Mesh);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCanEverAffectNavigation(false);
	C->NumCustomDataFloats = NumCustomData;
	// Small clutter (pots, crates, roof props, pipes) is invisible from afar: cull it. Unit meshes that are
	// stretched per instance (roofs, gable ends, pediments, brace arches, bridge rooms) are as big as the
	// building: never cull them by their unscaled size (every roof used to vanish beyond 120 m).
	const FString MeshName = Mesh->GetName();
	static const TCHAR* Stretched[] = { TEXT("SM_Kit_Roof"), TEXT("SM_Kit_GableEnd"), TEXT("SM_Kit_Pediment"), TEXT("SM_Kit_Archetto"), TEXT("SM_Kit_Sottoportico"), TEXT("SM_Kit_LM_") };
	bool bStretched = false;
	for (const TCHAR* Prefix : Stretched) { bStretched |= MeshName.StartsWith(Prefix); }
	const double Size = Mesh->GetBounds().BoxExtent.GetMax() * 2.0;
	if (!bStretched && Size < 150.0)
	{
		C->SetCullDistances(0, 12000);
	}
	else if (!bStretched && Size < 400.0 && !MeshName.Contains(TEXT("Wall")) && !MeshName.Contains(TEXT("Window")))
	{
		C->SetCullDistances(0, 40000);
	}
	// Foliage sways in the wind (world position offset, M_Master_Foliage): only near the camera,
	// because WPO makes Nanite rasterise the mesh every frame.
	bool bFoliage = false;
	for (int32 i = 0; i < Mesh->GetStaticMaterials().Num(); ++i)
	{
		UMaterialInterface* M = Mesh->GetMaterial(i);
		const UMaterial* Base = M ? M->GetBaseMaterial() : nullptr;
		bFoliage |= Base && Base->GetName() == TEXT("M_Master_Foliage");
	}
	C->SetEvaluateWorldPositionOffset(bFoliage);
	if (bFoliage)
	{
		C->SetWorldPositionOffsetDisableDistance(6000);
	}
	AddInstanceComponent(C);
	C->RegisterComponent();
	return C;
}

UDecalComponent* AOCKitBuildingChunk::AddDecal(UMaterialInterface* Material, const FTransform& WorldTransform, const FVector& HalfSize)
{
	UDecalComponent* D = NewObject<UDecalComponent>(this);
	D->SetupAttachment(RootComponent);
	D->SetDecalMaterial(Material);
	D->DecalSize = HalfSize;
	D->SetFadeScreenSize(0.002f);  // fade small, distant decals out
	AddInstanceComponent(D);
	D->RegisterComponent();
	D->SetWorldTransform(WorldTransform);
	return D;
}
#endif
