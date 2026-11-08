#include "World/OCBlockoutActor.h"

#include "Components/InstancedStaticMeshComponent.h"

AOCBlockoutActor::AOCBlockoutActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Instances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Instances"));
	Instances->SetMobility(EComponentMobility::Static);
	Instances->SetCollisionProfileName(TEXT("BlockAll"));
	Instances->NumCustomDataFloats = 2;
	RootComponent = Instances;
}
