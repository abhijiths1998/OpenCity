#include "Traffic/OCTrafficVehicle.h"

#include "AnimationRuntime.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Traffic/OCTrafficSettings.h"
#include "Traffic/OCTrafficSubsystem.h"
#include "Vehicles/OCVehicle.h"
#include "Vehicles/OCVehicleData.h"
#include "Engine/World.h"
#include "OCLog.h"
#include "GameFramework/Pawn.h"

AOCTrafficVehicle::AOCTrafficVehicle()
{
	PrimaryActorTick.bCanEverTick = false;
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetBoxExtent(FVector(225.f, 90.f, 70.f));
	Box->SetCollisionProfileName(TEXT("BlockAllDynamic"));   // the player's car and people bump into it
	Box->SetCanEverAffectNavigation(false);
	Box->SetGenerateOverlapEvents(false);
	Box->SetNotifyRigidBodyCollision(true);   // a physics body (a driven car) knocks it out of traffic
	Box->OnComponentHit.AddDynamic(this, &AOCTrafficVehicle::HandleHit);
	SetRootComponent(Box);
	SetCanBeDamaged(false);
}

void AOCTrafficVehicle::SetModel(UOCVehicleData* InModel, const FLinearColor& InPaint, float Dirt)
{
	Paint = InPaint;
	DirtAmount = Dirt;
	if (InModel != Model)
	{
		for (UStaticMeshComponent* Old : Parts)
		{
			if (Old)
			{
				Old->DestroyComponent();
			}
		}
		Parts.Reset();
		Model = InModel;
		if (Model && Model->Mesh)
		{
			// Put each part where AOCVehicle attaches it: on its bone, here at the bone's rest pose (wheels
			// don't steer or bounce on traffic). The box is sized from the parts themselves: the coupe
			// skeletal mesh's imported bounds are 2.5 cm across, and a driven car went straight through it.
			const FReferenceSkeleton& Ref = Model->Mesh->GetRefSkeleton();
			TArray<TPair<const FOCVehiclePart*, FTransform>> Placed;   // part, transform in the chassis frame
			FBox Bounds(ForceInit);
			for (const FOCVehiclePart& Part : Model->VisualParts)
			{
				if (!Part.Mesh)
				{
					continue;
				}
				FTransform BoneCS = FTransform::Identity;
				const int32 Bone = Part.Bone.IsNone() ? INDEX_NONE : Ref.FindBoneIndex(Part.Bone);
				if (Bone != INDEX_NONE)
				{
					BoneCS = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, Bone);
				}
				const FTransform Rel = FTransform(Part.Rotation, Part.Offset, Part.Scale) * BoneCS;
				Bounds += Part.Mesh->GetBoundingBox().TransformBy(Rel);
				Placed.Add({ &Part, Rel });
			}
			if (Bounds.IsValid)
			{
				Box->SetBoxExtent(Bounds.GetExtent());
				CentreFromChassis = Bounds.GetCenter();
			}
			for (const TPair<const FOCVehiclePart*, FTransform>& P : Placed)
			{
				const FOCVehiclePart& Part = *P.Key;
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
				C->SetStaticMesh(Part.Mesh);
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				C->SetGenerateOverlapEvents(false);
				C->SetCanEverAffectNavigation(false);
				C->SetupAttachment(Box);
				FTransform Rel = P.Value;
				Rel.AddToTranslation(-CentreFromChassis);   // the box (root) sits at the parts' centre
				C->SetRelativeTransform(Rel);
				if (Part.MaterialOverride)
				{
					for (int32 i = 0; i < C->GetNumMaterials(); ++i)
					{
						C->SetMaterial(i, Part.MaterialOverride);
					}
				}
				C->RegisterComponent();
				Parts.Add(C);
			}
		}
	}
	for (UStaticMeshComponent* C : Parts)
	{
		C->SetCustomPrimitiveDataVector4(0, FVector4(Paint.R, Paint.G, Paint.B, 1.0));
		C->SetCustomPrimitiveDataFloat(4, Dirt);
		C->SetCustomPrimitiveDataFloat(5, 0.f);
	}
}

float AOCTrafficVehicle::GetHalfLength() const
{
	return Box->GetUnscaledBoxExtent().X;
}

void AOCTrafficVehicle::SetActive(bool bActive)
{
	if (bActive == bInWorld)
	{
		return;
	}
	bInWorld = bActive;
	SetActorHiddenInGame(!bActive);
	SetActorEnableCollision(bActive);
	if (!bActive)
	{
		SimId = INDEX_NONE;
		SetActorLocation(FVector(0.0, 0.0, -100000.0));   // out of every trace and overlap
	}
}

bool AOCTrafficVehicle::CanInteract(const APawn* Instigator) const
{
	return bInWorld && SimId != INDEX_NONE && Instigator && Instigator->IsPlayerControlled();
}

bool AOCTrafficVehicle::Interact(APawn* Instigator)
{
	UOCTrafficSubsystem* Traffic = GetWorld() ? GetWorld()->GetSubsystem<UOCTrafficSubsystem>() : nullptr;
	AOCVehicle* Car = (Traffic && CanInteract(Instigator)) ? Traffic->Physicalize(this, FVector::ZeroVector, /*bStop*/ true) : nullptr;
	return Car && Car->Interact(Instigator);
}

FText AOCTrafficVehicle::GetInteractionText() const
{
	return NSLOCTEXT("OpenCity", "TakeCar", "Take car");
}

void AOCTrafficVehicle::HandleHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bInWorld || SimId == INDEX_NONE || !OtherComp || !OtherComp->IsSimulatingPhysics())
	{
		return;
	}
	const float KnockCms = GetDefault<UOCTrafficSettings>()->KnockKmh / 0.036f;
	// The change in the other body's speed this hit caused (its speed after the hit can be near zero: a car
	// that ran into a traffic car at 40 km/h stopped against it, and read as too slow to knock it).
	const float Mass = OtherComp->GetMass();
	const float DeltaV = Mass > KINDA_SMALL_NUMBER ? NormalImpulse.Size() / Mass : 0.f;
	if (FMath::Max(DeltaV, float(OtherComp->GetPhysicsLinearVelocity().Size())) < KnockCms)
	{
		return;
	}
	if (UOCTrafficSubsystem* Traffic = GetWorld() ? GetWorld()->GetSubsystem<UOCTrafficSubsystem>() : nullptr)
	{
		// The kinematic box took the blow without moving; hand the impulse on to the physical car.
		Traffic->RequestKnock(this, -NormalImpulse);   // next tick, not inside the physics callback
	}
}
