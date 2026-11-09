#include "World/OCKitBuildingChunk.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"

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
	AddInstanceComponent(C);
	C->RegisterComponent();
	return C;
}
#endif
