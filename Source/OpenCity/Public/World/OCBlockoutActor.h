#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OCBlockoutActor.generated.h"

class UInstancedStaticMeshComponent;

/**
 * One World Partition chunk of grey-box buildings: a single instanced mesh, one instance per
 * building. Per-instance custom data: [0] district style index, [1] random 0-1 (colour).
 * Built by UOCWorldBuilderLibrary::BuildBuildings; replaced by real buildings later.
 */
UCLASS()
class OPENCITY_API AOCBlockoutActor : public AActor
{
	GENERATED_BODY()

public:
	AOCBlockoutActor();

	UInstancedStaticMeshComponent* GetInstances() const { return Instances; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Blockout")
	TObjectPtr<UInstancedStaticMeshComponent> Instances;
};
