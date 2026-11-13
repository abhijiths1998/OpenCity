#include "Traffic/OCTrafficVehicle.h"

#include "AnimationRuntime.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Vehicles/OCVehicleData.h"

AOCTrafficVehicle::AOCTrafficVehicle()
{
	PrimaryActorTick.bCanEverTick = false;
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetBoxExtent(FVector(225.f, 90.f, 70.f));
	Box->SetCollisionProfileName(TEXT("BlockAllDynamic"));   // the player's car and people bump into it
	Box->SetCanEverAffectNavigation(false);
	Box->SetGenerateOverlapEvents(false);
	SetRootComponent(Box);
	SetCanBeDamaged(false);
}

void AOCTrafficVehicle::SetModel(UOCVehicleData* InModel, const FLinearColor& Paint, float Dirt)
{
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
			// Size the box from the chassis, and put each part where AOCVehicle attaches it: on its bone,
			// here at the bone's rest pose (wheels don't steer or bounce on traffic).
			const FBox Bounds = Model->Mesh->GetImportedBounds().GetBox();
			Box->SetBoxExtent(Bounds.GetExtent());
			const FReferenceSkeleton& Ref = Model->Mesh->GetRefSkeleton();
			// The box is the root and is centred on the chassis bounds; parts are placed relative to the chassis origin.
			CentreFromChassis = Bounds.GetCenter();
			const FVector ChassisFromBox = -Bounds.GetCenter();
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
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
				C->SetStaticMesh(Part.Mesh);
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				C->SetGenerateOverlapEvents(false);
				C->SetCanEverAffectNavigation(false);
				C->SetupAttachment(Box);
				FTransform Rel = FTransform(Part.Rotation, Part.Offset, Part.Scale) * BoneCS;
				Rel.AddToTranslation(ChassisFromBox);
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
