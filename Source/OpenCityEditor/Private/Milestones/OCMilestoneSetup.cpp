// Milestone assets and test maps (formerly OCMilestone -steps=m0, OCMilestone -steps=m1, OCMilestone -steps=m2,
// OCMilestone -steps=verify-m1 and OCMilestone -steps=verify-m2).
#include "Milestones/OCMilestoneCommon.h"
#include "OCMilestoneCommandlet.h"

#include "Animation/AnimBlueprint.h"
#include "BlueprintEditorLibrary.h"
#include "ChaosVehicleWheel.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/PointLight.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EnhancedActionKeyMapping.h"
#include "Game/OCGameMode.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerStart.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LevelEditorSubsystem.h"
#include "OCEditorHelpers.h"
#include "OCLog.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "StaticMeshAttributes.h"
#include "Player/OCCharacter.h"
#include "Player/OCCharacterAnimInstance.h"
#include "Player/OCInputConfig.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Vehicles/OCMotorbike.h"
#include "Vehicles/OCVehicle.h"
#include "Vehicles/OCVehicleData.h"

namespace OCMilestone
{
	namespace
	{
		const TCHAR* InputDir = TEXT("/Game/Core/Input");
		const TCHAR* VehDir = TEXT("/Game/Vehicles");

		UClass* PerfPathClass()
		{
			UClass* C = StaticLoadClass(AActor::StaticClass(), nullptr, TEXT("/Script/OpenCity.OCPerfCameraPath"));
			if (!C)
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("OCPerfCameraPath class not found"));
			}
			return C;
		}

		// ---- M0 --------------------------------------------------------------------------------

		/** Sky, sun, fog, clouds, sky light, ground and the perf camera path. */
		void M0Environment()
		{
			Daylight(FString(), -35.0, 40.0);   // sun 35 deg up; default fog
			OCEd::Spawn(StaticLoadClass(AActor::StaticClass(), nullptr, TEXT("/Script/Engine.VolumetricCloud")), FVector::ZeroVector, FRotator::ZeroRotator, TEXT("VolumetricCloud"));
			// A 400 m slab of the engine cube (top at z=0). The flat Plane mesh has no collision, so the walking
			// player (default game mode since M1) fell through it.
			Block(TEXT("Ground"), FVector(0, 0, -50), FVector(40000, 40000, 100), FRotator::ZeroRotator, FString());
			OCEd::Spawn(PerfPathClass(), FVector::ZeroVector, FRotator::ZeroRotator, TEXT("PerfCameraPath"));
		}

		/** A 30 x 30 field of Nanite spheres and 24 shadow-casting point lights. */
		void M0Stress(UStaticMesh* Mesh)
		{
			const double Spacing = 600.0;
			const int32 Grid = 30;
			const double Offset = (Grid - 1) * Spacing / 2;
			for (int32 X = 0; X < Grid; ++X)
			{
				for (int32 Y = 0; Y < Grid; ++Y)
				{
					AStaticMeshActor* A = CastChecked<AStaticMeshActor>(OCEd::Spawn(AStaticMeshActor::StaticClass(), FVector(X * Spacing - Offset, Y * Spacing - Offset, 150),
						FRotator::ZeroRotator, FString(), TEXT("StressMeshes")));
					A->GetStaticMeshComponent()->SetStaticMesh(Mesh);
					const double Scale = 1.5 + 2.0 * (((X * 7 + Y * 13) % 10) / 10.0);
					A->SetActorScale3D(FVector(Scale));
				}
			}
			for (int32 i = 0; i < 24; ++i)
			{
				const double Angle = 2 * PI * i / 24;
				AActor* L = OCEd::Spawn(APointLight::StaticClass(), FVector(4000 * FMath::Cos(Angle), 4000 * FMath::Sin(Angle), 400), FRotator::ZeroRotator,
					FString::Printf(TEXT("StressLight_%02d"), i), TEXT("StressLights"));
				UPointLightComponent* C = L->FindComponentByClass<UPointLightComponent>();
				OCEd::SetProp(C, TEXT("Mobility"), TEXT("Movable"));
				OCEd::SetProp(C, TEXT("Intensity"), TEXT("5000.0"));
				OCEd::SetProp(C, TEXT("AttenuationRadius"), TEXT("2500.0"));
				OCEd::SetProp(C, TEXT("CastShadows"), TEXT("True"));
			}
			OCEd::Log(TEXT("oc_m0"), FString::Printf(TEXT("stress content: %d Nanite meshes, 24 shadowed lights"), Grid * Grid));
		}

		// ---- M2 helpers ------------------------------------------------------------------------

		/** (radius, width) in cm from a wheel mesh's bounds: the largest extent is the diameter. */
		TPair<float, float> WheelSize(const FString& Path)
		{
			const FVector E = CastChecked<UStaticMesh>(OCEd::Require(Path))->GetBounds().BoxExtent;
			TArray<double> Dims = { E.X * 2, E.Y * 2, E.Z * 2 };
			Dims.Sort();
			return { float(Dims[2] / 2.0), float(Dims[0]) };
		}

		struct FWheel { float Friction = 3.0f, Spring = 250.0f, Raise = 10.0f, Drop = 10.0f, Brake = 1500.0f; };

		UClass* MakeWheel(const FString& Name, EAxleType Axle, float Radius, float Width, float SteerDeg, bool bEngine, bool bHandbrake, const FWheel& W = FWheel())
		{
			UBlueprint* Bp = Blueprint(Name, FString(VehDir) / TEXT("Wheels"), UChaosVehicleWheel::StaticClass(), TEXT("oc_m2"));
			UChaosVehicleWheel* C = CastChecked<UChaosVehicleWheel>(Cdo(Bp));
			C->Modify();
			C->AxleType = Axle;
			C->WheelRadius = Radius;
			C->WheelWidth = Width;
			C->MaxSteerAngle = SteerDeg;
			C->bAffectedBySteering = SteerDeg > 0.0f;
			C->bAffectedByEngine = bEngine;
			C->bAffectedByHandbrake = bHandbrake;
			C->bAffectedByBrake = true;
			C->FrictionForceMultiplier = W.Friction;
			C->SpringRate = W.Spring;
			C->SuspensionMaxRaise = W.Raise;
			C->SuspensionMaxDrop = W.Drop;
			C->MaxBrakeTorque = W.Brake;
			C->MaxHandBrakeTorque = bHandbrake ? W.Brake * 2.0f : 0.0f;
			C->PostEditChange();
			OCEd::Save(Bp);
			return Bp->GeneratedClass;
		}

		TArray<FChaosWheelSetup> WheelSetups(const TArray<TPair<UClass*, const TCHAR*>>& In)
		{
			TArray<FChaosWheelSetup> Out;
			for (const TPair<UClass*, const TCHAR*>& W : In)
			{
				FChaosWheelSetup S;
				S.WheelClass = W.Key;
				S.BoneName = W.Value;
				Out.Add(S);
			}
			return Out;
		}

		FVehicleEngineConfig Engine(float Torque, float MaxRpm, float IdleRpm)
		{
			FVehicleEngineConfig E;
			E.MaxTorque = Torque;
			E.MaxRPM = MaxRpm;
			E.EngineIdleRPM = IdleRpm;
			return E;
		}

		FVehicleTransmissionConfig Transmission(const TArray<float>& Forward, float Final, float Up, float Down)
		{
			FVehicleTransmissionConfig T;
			T.bUseAutomaticGears = true;
			T.bUseAutoReverse = true;
			T.ForwardGearRatios = Forward;
			T.ReverseGearRatios = { 3.0f };
			T.FinalRatio = Final;
			T.ChangeUpRPM = Up;
			T.ChangeDownRPM = Down;
			T.GearChangeTime = 0.3f;
			return T;
		}

		FVehicleDifferentialConfig Differential(EVehicleDifferential Kind, float Split = 0.5f)
		{
			FVehicleDifferentialConfig D;
			D.DifferentialType = Kind;
			D.FrontRearSplit = Split;
			return D;
		}

		FOCVehicleCameraSettings Camera(float Arm, float Height, float FovRest = 85.0f, float FovSpeed = 100.0f, float FullKmh = 180.0f)
		{
			FOCVehicleCameraSettings C;
			C.ArmLength = Arm;
			C.SocketOffset = FVector(0, 0, Height);
			C.Pitch = -10.0f;
			C.FovAtRest = FovRest;
			C.FovAtSpeed = FovSpeed;
			C.FovFullSpeedKmh = FullKmh;
			return C;
		}

		TArray<FVector2D> Points(std::initializer_list<FVector2D> P) { return TArray<FVector2D>(P); }

		/** A visual part; Rot as an FRotator. */
		FOCVehiclePart Part(const FString& MeshPath, const TCHAR* Bone = TEXT("None"), const FRotator& Rot = FRotator::ZeroRotator)
		{
			FOCVehiclePart P;
			P.Mesh = CastChecked<UStaticMesh>(OCEd::Require(MeshPath));
			P.Bone = Bone;
			P.Rotation = Rot;
			return P;
		}

		/** Four wheels; right-hand ones turned 180 degrees so the rim faces out. Roll/Yaw turn the wheel mesh onto
		 *  the bone's axle: the coupe's Phys_Wheel_* bones have Z forward, Y up and X across the car, so its wheels
		 *  need yaw -90, rim out (without it the tyres lay flat, axle up, under the body). */
		TArray<FOCVehiclePart> WheelParts(const FString& MeshPath, const TArray<const TCHAR*>& Bones, double Roll = 0.0, double Yaw = 0.0)
		{
			TArray<FOCVehiclePart> Out;
			for (const TCHAR* B : Bones)
			{
				const FString S(B);
				const bool bRight = S.EndsWith(TEXT("FR"), ESearchCase::CaseSensitive) || S.EndsWith(TEXT("BR"), ESearchCase::CaseSensitive) || S.EndsWith(TEXT("RR"), ESearchCase::CaseSensitive);
				Out.Add(Part(MeshPath, B, FRotator(0.0, bRight ? Yaw + 180.0 : Yaw, Roll)));
			}
			return Out;
		}

		UOCVehicleData* VehicleData(const FString& Name, const FString& Display)
		{
			UOCVehicleData* D = CastChecked<UOCVehicleData>(OCEd::GetOrCreateDataAsset(Name, FString(VehDir) / TEXT("Data"), UOCVehicleData::StaticClass(), TEXT("oc_m2")));
			D->Modify();
			D->DisplayName = FText::FromString(Display);
			return D;
		}

		UClass* AnimClass(const FString& Path)
		{
			return CastChecked<UBlueprint>(OCEd::Require(Path))->GeneratedClass;
		}

		USkeletalMesh* ImportBikeChassis()
		{
			const FString Gltf = OCEd::ProjectDir() / TEXT("SourceArt/Vehicles/Bike/SKM_BikeChassis.gltf");
			if (!FPaths::FileExists(Gltf))
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("missing %s: generate the bike chassis first"), *Gltf);
			}
			USkeletalMesh* Mesh = nullptr;
			for (UObject* O : OCEd::ImportFile(Gltf, FString(VehDir) / TEXT("Bike")))
			{
				if (USkeletalMesh* S = Cast<USkeletalMesh>(O)) { Mesh = S; }
				OCEd::Save(O);
			}
			if (!Mesh)
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("bike chassis import produced no skeletal mesh"));
			}
			UPhysicsAsset* Pa = Mesh->GetPhysicsAsset();
			OCEd::Log(TEXT("oc_m2"), FString::Printf(TEXT("bike chassis mesh %s, physics asset %s"), *Mesh->GetPathName(), Pa ? *Pa->GetPathName() : TEXT("NONE")));
			if (!Pa)
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("bike chassis has no physics asset; Chaos needs one"));
			}
			return Mesh;
		}

		/** SM_SportsCar_Body: a copy of the template's SM_SportsCar whose windows, UV islands of the body-paint slot
		 *  that the paint texture colours dark, sit in their own Glass slot, so they get the rain-aware
		 *  M_Master_CarGlass (M4b). No geometry is added or moved; the template stays untouched (formerly
		 *  Tools/blender/split_car_glass.py, which did the same through an FBX round trip). The islands are found
		 *  by their UV centre, checked against a render: the windshield, both side windows and the rear window. */
		UStaticMesh* MakeCoupeBody()
		{
			const FString Src = FString(VehDir) / TEXT("SportsCar/SM_SportsCar");
			const FString Dst = FString(VehDir) / TEXT("SportsCar/SM_SportsCar_Body");
			UStaticMesh* Template = CastChecked<UStaticMesh>(OCEd::Require(Src));
			UStaticMesh* Mesh = OCEd::Load<UStaticMesh>(Dst);
			if (!Mesh)
			{
				Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::DuplicateAsset(Src, Dst));
			}
			else
			{
				// Rebuild in place (the coupe data references it): the template's mesh, LODs and slots.
				Mesh->Modify();
				Mesh->SetNumSourceModels(Template->GetNumSourceModels());
				for (int32 Lod = 0; Lod < Template->GetNumSourceModels(); ++Lod)
				{
					Mesh->GetSourceModel(Lod).BuildSettings = Template->GetSourceModel(Lod).BuildSettings;
					Mesh->GetSourceModel(Lod).ReductionSettings = Template->GetSourceModel(Lod).ReductionSettings;
					*Mesh->CreateMeshDescription(Lod) = *Template->GetMeshDescription(Lod);
					Mesh->CommitMeshDescription(Lod);
				}
				Mesh->SetStaticMaterials(Template->GetStaticMaterials());
			}
			if (!Mesh)
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("could not copy %s"), *Src);
			}
			FMeshDescription* Md = Mesh->GetMeshDescription(0);
			FStaticMeshAttributes Attr(*Md);
			TPolygonGroupAttributesRef<FName> SlotNames = Attr.GetPolygonGroupMaterialSlotNames();
			TVertexInstanceAttributesConstRef<FVector2f> Uvs = Attr.GetVertexInstanceUVs();
			FPolygonGroupID Body = INDEX_NONE;
			for (const FPolygonGroupID G : Md->PolygonGroups().GetElementIDs())
			{
				if (SlotNames[G].ToString().StartsWith(TEXT("MI_SportsCarBody"), ESearchCase::CaseSensitive)) { Body = G; }
			}
			if (Body == INDEX_NONE)
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("SM_SportsCar has no MI_SportsCarBody slot"));
			}
			// UV-space centres of the glass islands (Blender's V up; Unreal's V is 1 - v).
			const TPair<const TCHAR*, FVector2D> GlassUv[] = { { TEXT("windshield"), FVector2D(0.155, 0.949) }, { TEXT("rear"), FVector2D(0.272, 0.946) },
				{ TEXT("side_right"), FVector2D(0.034, 0.917) }, { TEXT("side_left"), FVector2D(0.072, 0.919) } };
			constexpr double Tol = 0.02;
			auto Key = [&Uvs](FVertexInstanceID V) { const FVector2f T = Uvs.Get(V, 0); return FIntPoint(FMath::RoundToInt(T.X * 1e5), FMath::RoundToInt(T.Y * 1e5)); };
			// The vertex instance a triangle uses at a vertex.
			auto InstanceAt = [Md](FTriangleID T, FVertexID V)
			{
				for (const FVertexInstanceID I : Md->GetTriangleVertexInstances(T)) { if (Md->GetVertexInstanceVertex(I) == V) { return I; } }
				return FVertexInstanceID(INDEX_NONE);
			};
			// Triangles of the body slot grouped by UV island: neighbours across an edge whose two UVs match.
			TSet<FTriangleID> Seen;
			TArray<TArray<FTriangleID>> Islands;
			for (const FTriangleID T0 : Md->GetPolygonGroupTriangles(Body))
			{
				if (Seen.Contains(T0)) { continue; }
				TArray<FTriangleID>& Comp = Islands.AddDefaulted_GetRef();
				TArray<FTriangleID> Stack = { T0 };
				Seen.Add(T0);
				while (!Stack.IsEmpty())
				{
					const FTriangleID T = Stack.Pop();
					Comp.Add(T);
					for (const FEdgeID E : Md->GetTriangleEdges(T))
					{
						const FVertexID A = Md->GetEdgeVertex(E, 0), B = Md->GetEdgeVertex(E, 1);
						for (const FTriangleID N : Md->GetEdgeConnectedTriangleIDs(E))
						{
							if (Seen.Contains(N) || Md->GetTrianglePolygonGroup(N) != Body) { continue; }
							const TSet<FIntPoint> Mine = { Key(InstanceAt(T, A)), Key(InstanceAt(T, B)) };
							const TSet<FIntPoint> Theirs = { Key(InstanceAt(N, A)), Key(InstanceAt(N, B)) };
							if (Mine.Num() == Theirs.Num() && Mine.Includes(Theirs))
							{
								Seen.Add(N);
								Stack.Add(N);
							}
						}
					}
				}
			}
			const FPolygonGroupID Glass = Md->CreatePolygonGroup();
			SlotNames[Glass] = TEXT("Glass");
			TMap<FString, int32> Found;
			for (const TArray<FTriangleID>& Comp : Islands)
			{
				FBox2D Box(ForceInit);
				for (const FTriangleID T : Comp)
				{
					for (const FVertexInstanceID I : Md->GetTriangleVertexInstances(T)) { const FVector2f Uv = Uvs.Get(I, 0); Box += FVector2D(Uv.X, 1.0 - Uv.Y); }
				}
				const FVector2D C = Box.GetCenter();
				for (const auto& G : GlassUv)
				{
					if (FMath::Abs(C.X - G.Value.X) < Tol && FMath::Abs(C.Y - G.Value.Y) < Tol)
					{
						Found.Add(G.Key, Comp.Num());
						for (const FTriangleID T : Comp) { Md->SetPolygonPolygonGroup(Md->GetTrianglePolygon(T), Glass); }
					}
				}
			}
			if (Found.Num() != UE_ARRAY_COUNT(GlassUv))
			{
				UE_LOG(LogOpenCity, Fatal, TEXT("coupe body: found %d of 4 glass islands"), Found.Num());
			}
			Mesh->Modify();
			Mesh->GetStaticMaterials().Add(FStaticMaterial(CastChecked<UMaterialInterface>(OCEd::Require(TEXT("/Game/Materials/M_Master_CarGlass"))), TEXT("Glass"), TEXT("Glass")));
			Mesh->CommitMeshDescription(0);
			Mesh->GetNaniteSettings().bEnabled = true;
			// Body paint: M_Master_CarPaint's instance; the chassis keeps the template material.
			const TArray<FStaticMaterial>& Slots = Mesh->GetStaticMaterials();
			for (int32 i = 0; i < Slots.Num(); ++i)
			{
				if (Slots[i].MaterialSlotName.ToString().StartsWith(TEXT("MI_SportsCarBody"), ESearchCase::CaseSensitive))
				{
					Mesh->SetMaterial(i, CastChecked<UMaterialInterface>(OCEd::Require(FString(VehDir) / TEXT("Materials/MI_CarPaint_SportsCar"))));
				}
			}
			Mesh->PostEditChange();
			OCEd::Save(Mesh);
			TArray<FString> Parts;
			for (const TPair<FString, int32>& F : Found) { Parts.Add(FString::Printf(TEXT("%s %d"), *F.Key, F.Value)); }
			const FVector E = Mesh->GetBounds().BoxExtent;
			OCEd::Log(TEXT("oc_m2"), FString::Printf(TEXT("coupe body %s: glass islands (triangles) %s; extent %.1f x %.1f x %.1f cm"), *Mesh->GetPathName(),
				*FString::Join(Parts, TEXT(", ")), E.X, E.Y, E.Z));
			return Mesh;
		}

		void BuildDrivingMap(const TArray<TPair<FString, UOCVehicleData*>>& Datas)
		{
			const FString MapPath = TEXT("/Game/Maps/Dev/L_DevDriving");
			NewOrClearLevel(MapPath);
			Daylight(TEXT("Lighting"), -40.0, 35.0, 0.004f, 0.05f);
			Block(TEXT("Floor"), FVector(0, 0, -50), FVector(40000, 40000, 100), FRotator::ZeroRotator, TEXT("Ground"));
			OCEd::Spawn(APlayerStart::StaticClass(), FVector(0, 0, 100), FRotator::ZeroRotator, TEXT("PlayerStart"));
			// Vehicles parked side by side in front of the player, facing +X. The data asset is set on each instance
			// explicitly: right after a Blueprint save in the same session, the class can still carry stale (empty)
			// defaults, and placed actors would be saved without data.
			for (int32 i = 0; i < Datas.Num(); ++i)
			{
				UClass* Cls = UEditorAssetLibrary::LoadBlueprintClass(FString(VehDir) / TEXT("Blueprints") / Datas[i].Key);
				AActor* A = OCEd::Spawn(Cls, FVector(600, -500 + i * 500, 60), FRotator::ZeroRotator, Datas[i].Key.Replace(TEXT("BP_OCVehicle_"), TEXT("Vehicle_")), TEXT("Vehicles"));
				OCEd::SetProp(A, TEXT("VehicleData"), Ref(Datas[i].Value));
				if (!CastChecked<AOCVehicle>(A)->GetVehicleData())
				{
					UE_LOG(LogOpenCity, Fatal, TEXT("%s: VehicleData did not stick on the placed actor"), *Datas[i].Key);
				}
			}
			// A clear runway along +X (y -1500..1500) from the start to a crash wall at 60 m. Everything else sits
			// beside it, so scripted runs don't clip props by accident.
			Block(TEXT("CrashWall"), FVector(6000, 0, 150), FVector(100, 3000, 300));
			for (int32 i = 0; i < 6; ++i)   // crash blocks in their own lane at y = -7000
			{
				Block(FString::Printf(TEXT("CrashBlock_%d"), i), FVector(2000 + i * 800, -7000, 75), FVector(150, 150, 150));
			}
			Block(TEXT("Ramp_10deg"), FVector(2000, 3000, 60), FVector(1400, 600, 20), FRotator(-10, 0, 0));   // jump ramps
			Block(TEXT("Ramp_18deg"), FVector(2000, 4500, 110), FVector(1400, 600, 20), FRotator(-18, 0, 0));
			for (int32 i = 0; i < 8; ++i)   // slalom posts every 15 m in the lane at y = -4000
			{
				Block(FString::Printf(TEXT("Slalom_%d"), i), FVector(1500 + i * 1500, -4000 + (i % 2) * 400, 100), FVector(40, 40, 200));
			}
			Block(TEXT("Alley_Left"), FVector(-3000, -150 - 25, 150), FVector(2000, 50, 300));   // a 3 m alley for exit-spot tests
			Block(TEXT("Alley_Right"), FVector(-3000, 150 + 25, 150), FVector(2000, 50, 300));
			// Perf camera: a 25 m circle around the parked vehicles (runs only with -ocperf).
			AActor* Perf = OCEd::Spawn(PerfPathClass(), FVector(600, 0, 0), FRotator::ZeroRotator, TEXT("PerfCameraPath"));
			OCEd::SetProp(Perf, TEXT("CircleRadius"), TEXT("2500.0"));
			OCEd::SetProp(Perf, TEXT("CircleHeight"), TEXT("300.0"));
			SaveCurrentLevel(MapPath, TEXT("oc_m2"));
		}

		struct FVerify
		{
			int32 Fails = 0;
			void Check(const FString& Label, bool bOk, const FString& Detail = FString())
			{
				if (bOk)
				{
					UE_LOG(LogOpenCity, Display, TEXT("OCVERIFY OK   %s %s"), *Label, *Detail);
				}
				else
				{
					++Fails;
					UE_LOG(LogOpenCity, Error, TEXT("OCVERIFY FAIL %s %s"), *Label, *Detail);
				}
			}
		};

		UObject* GetObjectProp(UObject* Obj, const TCHAR* Name)
		{
			FObjectPropertyBase* P = CastField<FObjectPropertyBase>(Obj->GetClass()->FindPropertyByName(Name));
			return P ? P->GetObjectPropertyValue_InContainer(Obj) : nullptr;
		}
	}

	bool SetupM0()
	{
		// An engine sphere copied into /Game with Nanite on (higher poly than BasicShapes).
		const TCHAR* NaniteMesh = TEXT("/Game/Dev/Meshes/SM_Dev_NaniteSphere");
		if (!UEditorAssetLibrary::DoesAssetExist(NaniteMesh) && !UEditorAssetLibrary::DuplicateAsset(TEXT("/Engine/EngineMeshes/Sphere"), NaniteMesh))
		{
			UE_LOG(LogOpenCity, Error, TEXT("could not duplicate /Engine/EngineMeshes/Sphere"));
			return false;
		}
		UStaticMesh* Mesh = CastChecked<UStaticMesh>(OCEd::Require(NaniteMesh));
		if (!Mesh->GetNaniteSettings().bEnabled)
		{
			Mesh->Modify();
			Mesh->GetNaniteSettings().bEnabled = true;
			Mesh->PostEditChange();
			OCEd::Log(TEXT("oc_m0"), TEXT("enabled Nanite on SM_Dev_NaniteSphere"));
		}
		OCEd::Save(Mesh);
		for (const bool bStress : { false, true })
		{
			const FString Path = bStress ? TEXT("/Game/Maps/Test/L_Stress") : TEXT("/Game/Maps/Test/L_PerfBaseline");
			NewOrClearLevel(Path);
			M0Environment();
			if (bStress)
			{
				M0Stress(Mesh);
			}
			SaveCurrentLevel(Path, TEXT("oc_m0"));
		}
		return true;
	}

	bool SetupM1()
	{
		const TCHAR* Tag = TEXT("oc_m1");
		const TPair<const TCHAR*, EInputActionValueType> Names[] = {
			{ TEXT("IA_Move"), EInputActionValueType::Axis2D }, { TEXT("IA_Look"), EInputActionValueType::Axis2D },
			{ TEXT("IA_Jump"), EInputActionValueType::Boolean }, { TEXT("IA_Sprint"), EInputActionValueType::Boolean },
			{ TEXT("IA_SprintToggle"), EInputActionValueType::Boolean }, { TEXT("IA_Crouch"), EInputActionValueType::Boolean },
			{ TEXT("IA_Aim"), EInputActionValueType::Boolean }, { TEXT("IA_SwapShoulder"), EInputActionValueType::Boolean },
			{ TEXT("IA_Interact"), EInputActionValueType::Boolean } };
		TMap<FString, UInputAction*> A;
		for (const auto& N : Names)
		{
			A.Add(N.Key, Action(N.Key, InputDir, N.Value, Tag));
		}
		UInputMappingContext* OnFoot = Context(TEXT("IMC_OnFoot"), InputDir, [&A](UInputMappingContext* Imc) -> TArray<FRow>
		{
			return {
				// Move: X = right, Y = forward.
				{ A[TEXT("IA_Move")], TEXT("W"), { Swizzle(Imc) } },
				{ A[TEXT("IA_Move")], TEXT("S"), { Swizzle(Imc), Negate(Imc) } },
				{ A[TEXT("IA_Move")], TEXT("A"), { Negate(Imc) } },
				{ A[TEXT("IA_Move")], TEXT("D"), {} },
				{ A[TEXT("IA_Move")], TEXT("Gamepad_Left2D"), { DeadZone(Imc) } },
				// Look, X = yaw, Y = pitch. Same scheme as Epic's tested template: legacy controller scales ON
				// (DefaultInput.ini pins it; PlayerController yaw x2.5, pitch x-2.5), so Y is negated. Enhanced Input
				// reads raw mouse deltas, so the mouse needs no scalar. Stick is a rate: scaled by delta time, then
				// x2.5 by legacy = 150 deg/s yaw, 100 pitch.
				{ A[TEXT("IA_Look")], TEXT("Mouse2D"), { Negate(Imc, false, true, false) } },
				{ A[TEXT("IA_Look")], TEXT("Gamepad_Right2D"), { DeadZone(Imc), Negate(Imc, false, true, false), Scalar(Imc, 60.0, 40.0), ScaleByDt(Imc) } },
				{ A[TEXT("IA_Jump")], TEXT("SpaceBar"), {} },
				{ A[TEXT("IA_Jump")], TEXT("Gamepad_FaceButton_Bottom"), {} },
				{ A[TEXT("IA_Sprint")], TEXT("LeftShift"), {} },
				{ A[TEXT("IA_SprintToggle")], TEXT("Gamepad_LeftThumbstick"), {} },
				{ A[TEXT("IA_Crouch")], TEXT("C"), {} },
				{ A[TEXT("IA_Crouch")], TEXT("Gamepad_FaceButton_Right"), {} },
				{ A[TEXT("IA_Aim")], TEXT("RightMouseButton"), {} },
				{ A[TEXT("IA_Aim")], TEXT("Gamepad_LeftTrigger"), {}, { Down(Imc, 0.35f) } },
				{ A[TEXT("IA_Interact")], TEXT("F"), {} },
				{ A[TEXT("IA_Interact")], TEXT("Gamepad_FaceButton_Top"), {} } };
		}, Tag);
		UInputMappingContext* Aiming = Context(TEXT("IMC_Aiming"), InputDir, [&A](UInputMappingContext*) -> TArray<FRow>
		{
			return { { A[TEXT("IA_SwapShoulder")], TEXT("Q"), {} }, { A[TEXT("IA_SwapShoulder")], TEXT("Gamepad_RightThumbstick"), {} } };
		}, Tag);

		UOCInputConfig* Cfg = CastChecked<UOCInputConfig>(OCEd::GetOrCreateDataAsset(TEXT("DA_OCInputConfig"), InputDir, UOCInputConfig::StaticClass(), Tag));
		OCEd::SetProp(Cfg, TEXT("OnFootContext"), Ref(OnFoot));
		OCEd::SetProp(Cfg, TEXT("AimingContext"), Ref(Aiming));
		const TPair<const TCHAR*, const TCHAR*> Props[] = { { TEXT("Move"), TEXT("IA_Move") }, { TEXT("Look"), TEXT("IA_Look") }, { TEXT("Jump"), TEXT("IA_Jump") },
			{ TEXT("Sprint"), TEXT("IA_Sprint") }, { TEXT("SprintToggle"), TEXT("IA_SprintToggle") }, { TEXT("Crouch"), TEXT("IA_Crouch") },
			{ TEXT("Aim"), TEXT("IA_Aim") }, { TEXT("SwapShoulder"), TEXT("IA_SwapShoulder") }, { TEXT("Interact"), TEXT("IA_Interact") } };
		for (const auto& P : Props)
		{
			OCEd::SetProp(Cfg, P.Key, Ref(A[P.Value]));
		}
		OCEd::Save(Cfg);

		// Give ABP_Unarmed our C++ base so its graph can read crouch/aim/sprint state.
		UBlueprint* Abp = CastChecked<UBlueprint>(OCEd::Require(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed")));
		UClass* Target = UOCCharacterAnimInstance::StaticClass();
		UClass* Current = UBlueprintEditorLibrary::GetBlueprintParentClass(Abp);
		if (Current != Target)
		{
			UBlueprintEditorLibrary::ReparentBlueprint(Abp, Target);
			OCEd::Log(Tag, FString::Printf(TEXT("reparented ABP_Unarmed: %s -> %s"), *GetNameSafe(Current), *Target->GetName()));
		}
		FKismetEditorUtilities::CompileBlueprint(Abp);
		OCEd::Save(Abp);

		UBlueprint* Player = Blueprint(TEXT("BP_OCPlayerCharacter"), TEXT("/Game/Core/Player"), AOCCharacter::StaticClass(), Tag);
		OCEd::SetProp(Cdo(Player), TEXT("InputConfig"), Ref(Cfg));
		OCEd::SetProp(Cdo(Player), TEXT("BodyMesh"), TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
		OCEd::SetProp(Cdo(Player), TEXT("BodyAnimClass"), Ref(Abp->GeneratedClass));
		OCEd::Save(Player);
		UBlueprint* Mode = Blueprint(TEXT("BP_OCGameMode"), TEXT("/Game/Core"), AOCGameMode::StaticClass(), Tag);
		OCEd::SetProp(Cdo(Mode), TEXT("DefaultPawnClass"), Ref(Player->GeneratedClass));
		OCEd::Save(Mode);

		// Movement and camera test course.
		const FString MapPath = TEXT("/Game/Maps/Dev/L_DevPlayground");
		NewOrClearLevel(MapPath);
		Daylight(TEXT("Lighting"));
		Block(TEXT("Floor"), FVector(0, 0, -50), FVector(10000, 10000, 100), FRotator::ZeroRotator, TEXT("Ground"));
		OCEd::Spawn(APlayerStart::StaticClass(), FVector(0, 0, 100), FRotator::ZeroRotator, TEXT("PlayerStart"));
		// Stairs: 10 steps of 20 cm rise, 35 cm run (character step height is 45 cm). Each step is a box from its
		// front edge to the top of the flight, so there are no gaps.
		for (int32 i = 0; i < 10; ++i)
		{
			const double Length = 35 * (10 - i), Height = 20 * (i + 1);
			Block(FString::Printf(TEXT("Step_%02d"), i), FVector(800 + i * 35 + Length / 2, -600, Height / 2), FVector(Length, 300, Height));
		}
		Block(TEXT("Ramp_20deg"), FVector(800, 0, 120), FVector(800, 300, 20), FRotator(-20, 0, 0));   // walkable limit is about 44.8
		Block(TEXT("Ramp_40deg"), FVector(800, 600, 220), FVector(800, 300, 20), FRotator(-40, 0, 0));
		// Crouch tunnel: 120 cm clearance; standing capsule is 192 cm, crouched 120 cm.
		Block(TEXT("Tunnel_Left"), FVector(-800, -175, 100), FVector(800, 50, 200));
		Block(TEXT("Tunnel_Right"), FVector(-800, 175, 100), FVector(800, 50, 200));
		Block(TEXT("Tunnel_Roof"), FVector(-800, 0, 175), FVector(800, 400, 30));
		// Jump test: boxes 50 / 100 / 150 cm tall, and a 300 cm gap between platforms.
		const int32 Heights[] = { 50, 100, 150 };
		for (int32 i = 0; i < 3; ++i)
		{
			Block(FString::Printf(TEXT("JumpBox_%dcm"), Heights[i]), FVector(0, 800 + i * 300, Heights[i] / 2.0), FVector(200, 200, Heights[i]));
		}
		Block(TEXT("Gap_PlatformA"), FVector(-800, 800, 100), FVector(400, 400, 200));
		Block(TEXT("Gap_PlatformB"), FVector(-800, 1500, 100), FVector(400, 400, 200));
		// Camera collision test: a corridor 200 cm wide (walls at y -1200 and -1450, 50 cm thick).
		Block(TEXT("Corridor_Left"), FVector(0, -1200, 200), FVector(1500, 50, 400));
		Block(TEXT("Corridor_Right"), FVector(0, -1450, 200), FVector(1500, 50, 400));
		SaveCurrentLevel(MapPath, Tag);
		return true;
	}

	bool SetupM2()
	{
		const TCHAR* Tag = TEXT("oc_m2");
		// ---- Vehicle input
		const TPair<const TCHAR*, EInputActionValueType> Names[] = {
			{ TEXT("IA_Throttle"), EInputActionValueType::Axis1D }, { TEXT("IA_Brake"), EInputActionValueType::Axis1D },
			{ TEXT("IA_Steer"), EInputActionValueType::Axis1D }, { TEXT("IA_Handbrake"), EInputActionValueType::Boolean },
			{ TEXT("IA_VehicleLook"), EInputActionValueType::Axis2D }, { TEXT("IA_ExitVehicle"), EInputActionValueType::Boolean },
			{ TEXT("IA_RadioNext"), EInputActionValueType::Boolean }, { TEXT("IA_RadioPrevious"), EInputActionValueType::Boolean },
			{ TEXT("IA_ResetVehicle"), EInputActionValueType::Boolean } };
		TMap<FString, UInputAction*> A;
		for (const auto& N : Names)
		{
			A.Add(N.Key, Action(N.Key, InputDir, N.Value, Tag));
		}
		UInputMappingContext* Imc = Context(TEXT("IMC_Vehicle"), InputDir, [&A](UInputMappingContext* C) -> TArray<FRow>
		{
			return {
				{ A[TEXT("IA_Throttle")], TEXT("W"), {} },
				{ A[TEXT("IA_Throttle")], TEXT("Gamepad_RightTriggerAxis"), {} },
				{ A[TEXT("IA_Brake")], TEXT("S"), {} },
				{ A[TEXT("IA_Brake")], TEXT("Gamepad_LeftTriggerAxis"), {} },
				{ A[TEXT("IA_Steer")], TEXT("A"), { Negate(C) } },
				{ A[TEXT("IA_Steer")], TEXT("D"), {} },
				{ A[TEXT("IA_Steer")], TEXT("Gamepad_LeftX"), { DeadZone(C, 0.15f, false) } },
				{ A[TEXT("IA_Handbrake")], TEXT("SpaceBar"), {} },
				{ A[TEXT("IA_Handbrake")], TEXT("Gamepad_RightShoulder"), {} },
				// Free look adds degrees to the camera directly (no controller scales here): mouse delta x 0.25 deg;
				// stick 150 deg/s yaw, 90 deg/s pitch.
				{ A[TEXT("IA_VehicleLook")], TEXT("Mouse2D"), { Scalar(C, 0.25, 0.25) } },
				{ A[TEXT("IA_VehicleLook")], TEXT("Gamepad_Right2D"), { DeadZone(C), Scalar(C, 150.0, 90.0), ScaleByDt(C) } },
				{ A[TEXT("IA_ExitVehicle")], TEXT("F"), {} },
				{ A[TEXT("IA_ExitVehicle")], TEXT("Gamepad_FaceButton_Top"), {} },
				{ A[TEXT("IA_RadioNext")], TEXT("E"), {} },
				{ A[TEXT("IA_RadioNext")], TEXT("Gamepad_DPad_Right"), {} },
				{ A[TEXT("IA_RadioPrevious")], TEXT("Q"), {} },
				{ A[TEXT("IA_RadioPrevious")], TEXT("Gamepad_DPad_Left"), {} },
				{ A[TEXT("IA_ResetVehicle")], TEXT("R"), {} },
				{ A[TEXT("IA_ResetVehicle")], TEXT("Gamepad_DPad_Down"), {} } };
		}, Tag);
		UOCVehicleInputConfig* Vin = CastChecked<UOCVehicleInputConfig>(OCEd::GetOrCreateDataAsset(TEXT("DA_OCVehicleInputConfig"), InputDir, UOCVehicleInputConfig::StaticClass(), Tag));
		Vin->Modify();
		Vin->VehicleContext = Imc;
		Vin->Throttle = A[TEXT("IA_Throttle")];
		Vin->Brake = A[TEXT("IA_Brake")];
		Vin->Steer = A[TEXT("IA_Steer")];
		Vin->Handbrake = A[TEXT("IA_Handbrake")];
		Vin->Look = A[TEXT("IA_VehicleLook")];
		Vin->Exit = A[TEXT("IA_ExitVehicle")];
		Vin->RadioNext = A[TEXT("IA_RadioNext")];
		Vin->RadioPrevious = A[TEXT("IA_RadioPrevious")];
		Vin->ResetVehicle = A[TEXT("IA_ResetVehicle")];
		OCEd::Save(Vin);

		// ---- Radio: original stations for Marevia (no real brands or music).
		struct FStation { const TCHAR* Asset; const TCHAR* Display; const TCHAR* Genre; float Length; };
		const FStation Stations[] = { { TEXT("DA_Radio_HarborBreeze"), TEXT("Harbor Breeze FM"), TEXT("Coastal pop"), 1500.0f },
			{ TEXT("DA_Radio_Underground"), TEXT("Marevia Underground"), TEXT("Electronic"), 1800.0f },
			{ TEXT("DA_Radio_OldTown"), TEXT("Old Town Classics"), TEXT("Classics"), 2100.0f } };
		TArray<TObjectPtr<UOCRadioStationData>> Radio;
		for (const FStation& S : Stations)
		{
			UOCRadioStationData* St = CastChecked<UOCRadioStationData>(OCEd::GetOrCreateDataAsset(S.Asset, FString(VehDir) / TEXT("Radio"), UOCRadioStationData::StaticClass(), Tag));
			St->Modify();
			St->DisplayName = FText::FromString(S.Display);
			St->Genre = FText::FromString(S.Genre);
			St->LengthSeconds = S.Length;
			OCEd::Save(St);
			Radio.Add(St);
		}
		USkeletalMesh* BikeMesh = ImportBikeChassis();

		// ---- Wheels. Springs carry the car about a third into its travel: rate (N/cm) ~ mass * g / 4 / (0.35 * raise).
		// 250 N/cm couldn't hold the 1400 kg coupe within 10 cm: it bottomed out and sat on its body.
		TPair<float, float> Rw = WheelSize(FString(VehDir) / TEXT("SportsCar/SM_SportsCar_Wheel"));
		OCEd::Log(Tag, FString::Printf(TEXT("coupe wheel radius %.1f cm, width %.1f cm"), Rw.Key, Rw.Value));
		FWheel Coupe; Coupe.Spring = 900.0f;
		UClass* CoupeF = MakeWheel(TEXT("BP_Wheel_Coupe_Front"), EAxleType::Front, Rw.Key, Rw.Value, 40.0f, false, false, Coupe);
		UClass* CoupeR = MakeWheel(TEXT("BP_Wheel_Coupe_Rear"), EAxleType::Rear, Rw.Key, Rw.Value, 0.0f, true, true, Coupe);
		Rw = WheelSize(FString(VehDir) / TEXT("OffroadCar/SM_Offroad_Tire"));
		OCEd::Log(Tag, FString::Printf(TEXT("offroad wheel radius %.1f cm, width %.1f cm"), Rw.Key, Rw.Value));
		FWheel Off; Off.Friction = 3.5f; Off.Spring = 650.0f; Off.Raise = 20.0f; Off.Drop = 20.0f;
		UClass* OffF = MakeWheel(TEXT("BP_Wheel_Offroad_Front"), EAxleType::Front, Rw.Key, Rw.Value, 35.0f, true, false, Off);
		UClass* OffR = MakeWheel(TEXT("BP_Wheel_Offroad_Rear"), EAxleType::Rear, Rw.Key, Rw.Value, 0.0f, true, true, Off);
		FWheel Bk; Bk.Friction = 3.0f; Bk.Spring = 120.0f; Bk.Raise = 8.0f; Bk.Drop = 8.0f; Bk.Brake = 600.0f;
		UClass* BikeF = MakeWheel(TEXT("BP_Wheel_Bike_Front"), EAxleType::Front, 32.0f, 12.0f, 30.0f, false, false, Bk);
		UClass* BikeR = MakeWheel(TEXT("BP_Wheel_Bike_Rear"), EAxleType::Rear, 32.0f, 12.0f, 0.0f, true, true, Bk);

		// ---- Vehicle data (only the listed fields; damage, steering and motorbike tuning stay as edited).
		UOCVehicleData* CoupeData = VehicleData(TEXT("DA_Vehicle_Coupe"), TEXT("Strada Coupe"));
		CoupeData->Mesh = CastChecked<USkeletalMesh>(OCEd::Require(FString(VehDir) / TEXT("SportsCar/SKM_SportsCar")));
		CoupeData->AnimClass = AnimClass(FString(VehDir) / TEXT("SportsCar/ABP_SportsCar"));
		CoupeData->VisualParts = { Part(MakeCoupeBody()->GetPathName()), Part(FString(VehDir) / TEXT("SportsCar/SM_SportsCar_Glass")) };
		CoupeData->VisualParts.Append(WheelParts(FString(VehDir) / TEXT("SportsCar/SM_SportsCar_Wheel"),
			{ TEXT("Phys_Wheel_FL"), TEXT("Phys_Wheel_FR"), TEXT("Phys_Wheel_BL"), TEXT("Phys_Wheel_BR") }, 0.0, -90.0));
		CoupeData->Mass = 1400.0f;
		CoupeData->DragCoefficient = 0.3f;
		CoupeData->ChassisWidth = 180.0f;
		CoupeData->ChassisHeight = 120.0f;
		// Linear paint colours a sports coupe on the Marevia coast might wear (M_Master_CarPaint).
		CoupeData->PaintColors = { FLinearColor(0.80f, 0.05f, 0.02f, 1.0f), FLinearColor(0.85f, 0.32f, 0.02f, 1.0f), FLinearColor(0.90f, 0.62f, 0.02f, 1.0f),
			FLinearColor(0.02f, 0.02f, 0.025f, 1.0f), FLinearColor(0.75f, 0.75f, 0.73f, 1.0f), FLinearColor(0.30f, 0.31f, 0.33f, 1.0f),
			FLinearColor(0.02f, 0.09f, 0.32f, 1.0f), FLinearColor(0.03f, 0.18f, 0.08f, 1.0f) };
		CoupeData->DirtRange = FVector2D(0.05, 0.4);
		CoupeData->WheelSetups = WheelSetups({ { CoupeF, TEXT("Phys_Wheel_FL") }, { CoupeF, TEXT("Phys_Wheel_FR") }, { CoupeR, TEXT("Phys_Wheel_BL") }, { CoupeR, TEXT("Phys_Wheel_BR") } });
		CoupeData->TorqueCurvePoints = Points({ { 0, 0.5 }, { 2500, 0.8 }, { 5000, 1.0 }, { 7000, 0.85 } });
		CoupeData->EngineSetup = Engine(650.0f, 7000.0f, 900.0f);
		CoupeData->TransmissionSetup = Transmission({ 4.0f, 2.6f, 1.8f, 1.4f, 1.1f, 0.9f }, 3.4f, 6200.0f, 3000.0f);
		CoupeData->DifferentialSetup = Differential(EVehicleDifferential::RearWheelDrive);
		CoupeData->SteeringCurvePoints = Points({ { 0, 1.0 }, { 60, 0.7 }, { 120, 0.45 }, { 200, 0.3 } });
		CoupeData->Camera = Camera(650.0f, 150.0f);
		CoupeData->RadioStations = Radio;
		OCEd::Save(CoupeData);

		UOCVehicleData* OffData = VehicleData(TEXT("DA_Vehicle_Offroad"), TEXT("Terra 4x4"));
		OffData->Mesh = CastChecked<USkeletalMesh>(OCEd::Require(FString(VehDir) / TEXT("OffroadCar/SKM_Offroad")));
		OffData->AnimClass = AnimClass(FString(VehDir) / TEXT("OffroadCar/Offroad_AnimBP"));
		OffData->VisualParts = { Part(FString(VehDir) / TEXT("OffroadCar/SM_Offroad_Body")) };
		OffData->VisualParts.Append(WheelParts(FString(VehDir) / TEXT("OffroadCar/SM_Offroad_Tire"), { TEXT("VisWheel_FL"), TEXT("VisWheel_FR"), TEXT("VisWheel_BL"), TEXT("VisWheel_BR") }));
		OffData->Mass = 2000.0f;
		OffData->DragCoefficient = 0.45f;
		OffData->ChassisWidth = 200.0f;
		OffData->ChassisHeight = 170.0f;
		OffData->WheelSetups = WheelSetups({ { OffF, TEXT("PhysWheel_FL") }, { OffF, TEXT("PhysWheel_FR") }, { OffR, TEXT("PhysWheel_BL") }, { OffR, TEXT("PhysWheel_BR") } });
		OffData->TorqueCurvePoints = Points({ { 0, 0.7 }, { 1500, 1.0 }, { 3500, 0.95 }, { 5500, 0.7 } });
		OffData->EngineSetup = Engine(800.0f, 5500.0f, 800.0f);
		OffData->TransmissionSetup = Transmission({ 4.5f, 2.9f, 1.9f, 1.3f, 1.0f }, 3.9f, 4800.0f, 2200.0f);
		OffData->DifferentialSetup = Differential(EVehicleDifferential::AllWheelDrive, 0.5f);
		OffData->SteeringCurvePoints = Points({ { 0, 1.0 }, { 50, 0.7 }, { 100, 0.45 }, { 160, 0.3 } });
		OffData->Camera = Camera(750.0f, 200.0f, 85.0f, 96.0f, 140.0f);
		OffData->RadioStations = Radio;
		OCEd::Save(OffData);

		UOCVehicleData* BikeData = VehicleData(TEXT("DA_Vehicle_Bike"), TEXT("Marevia Scrambler"));
		BikeData->Mesh = BikeMesh;
		BikeData->Mass = 230.0f;
		BikeData->DragCoefficient = 0.6f;
		BikeData->ChassisWidth = 40.0f;
		BikeData->ChassisHeight = 110.0f;
		BikeData->bOverrideCenterOfMass = true;
		BikeData->CenterOfMassOffset = FVector(0.0, 0.0, -25.0);
		BikeData->WheelSetups = WheelSetups({ { BikeF, TEXT("PhysWheel_FL") }, { BikeF, TEXT("PhysWheel_FR") }, { BikeR, TEXT("PhysWheel_RL") }, { BikeR, TEXT("PhysWheel_RR") } });
		BikeData->TorqueCurvePoints = Points({ { 0, 0.6 }, { 4000, 0.9 }, { 8000, 1.0 }, { 10000, 0.8 } });
		BikeData->EngineSetup = Engine(140.0f, 10000.0f, 1400.0f);
		BikeData->TransmissionSetup = Transmission({ 3.2f, 2.2f, 1.7f, 1.35f, 1.1f }, 3.0f, 9000.0f, 5000.0f);
		BikeData->DifferentialSetup = Differential(EVehicleDifferential::RearWheelDrive);
		BikeData->SteeringCurvePoints = Points({ { 0, 1.0 }, { 20, 0.6 }, { 40, 0.35 }, { 80, 0.2 }, { 140, 0.12 } });
		BikeData->Camera = Camera(450.0f, 110.0f, 85.0f, 104.0f, 150.0f);
		BikeData->DriverOffset = FVector(0.0, 0.0, 100.0);
		BikeData->ExitOffsets = { FVector(0, -110, 60), FVector(0, 110, 60), FVector(-200, 0, 60) };
		BikeData->MaxExitSpeedKmh = 20.0f;
		BikeData->RadioStations.Reset();
		OCEd::Save(BikeData);

		// ---- Blueprints
		TArray<TPair<FString, UOCVehicleData*>> Datas = { { TEXT("BP_OCVehicle_Coupe"), CoupeData }, { TEXT("BP_OCVehicle_Offroad"), OffData },
			{ TEXT("BP_OCVehicle_Bike"), BikeData } };
		for (const TPair<FString, UOCVehicleData*>& D : Datas)
		{
			const bool bBike = D.Key == TEXT("BP_OCVehicle_Bike");
			UBlueprint* Bp = Blueprint(D.Key, FString(VehDir) / TEXT("Blueprints"), bBike ? AOCMotorbike::StaticClass() : AOCVehicle::StaticClass(), Tag);
			OCEd::SetProp(Cdo(Bp), TEXT("VehicleData"), Ref(D.Value));
			OCEd::SetProp(Cdo(Bp), TEXT("InputConfig"), Ref(Vin));
			if (bBike)
			{
				OCEd::SetProp(Cdo(Bp), TEXT("BodyMesh"), TEXT("/Engine/BasicShapes/Cube.Cube"));
				OCEd::SetProp(Cdo(Bp), TEXT("WheelMesh"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
			}
			OCEd::Save(Bp);
		}
		BuildDrivingMap(Datas);
		return true;
	}

	bool VerifyM1()
	{
		FVerify V;
		UBlueprint* Player = CastChecked<UBlueprint>(OCEd::Require(TEXT("/Game/Core/Player/BP_OCPlayerCharacter")));
		UObject* PlayerCdo = Cdo(Player);
		V.Check(TEXT("player InputConfig"), GetObjectProp(PlayerCdo, TEXT("InputConfig")) != nullptr);
		UObject* Mesh = GetObjectProp(PlayerCdo, TEXT("BodyMesh"));
		V.Check(TEXT("player BodyMesh"), Mesh != nullptr, GetNameSafe(Mesh));
		UObject* Anim = GetObjectProp(PlayerCdo, TEXT("BodyAnimClass"));
		V.Check(TEXT("player BodyAnimClass"), Anim != nullptr, GetNameSafe(Anim));
		UBlueprint* Mode = CastChecked<UBlueprint>(OCEd::Require(TEXT("/Game/Core/BP_OCGameMode")));
		UObject* Pawn = GetObjectProp(Cdo(Mode), TEXT("DefaultPawnClass"));
		V.Check(TEXT("game mode pawn"), Pawn == Player->GeneratedClass, GetNameSafe(Pawn));
		UBlueprint* Abp = CastChecked<UBlueprint>(OCEd::Require(TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed")));
		UClass* Parent = UBlueprintEditorLibrary::GetBlueprintParentClass(Abp);
		V.Check(TEXT("ABP_Unarmed parent"), Parent == UOCCharacterAnimInstance::StaticClass(), GetNameSafe(Parent));
		const TPair<const TCHAR*, int32> Contexts[] = { { TEXT("IMC_OnFoot"), 17 }, { TEXT("IMC_Aiming"), 2 } };
		for (const auto& C : Contexts)
		{
			UInputMappingContext* Imc = CastChecked<UInputMappingContext>(OCEd::Require(FString(InputDir) / C.Key));
			const TArray<FEnhancedActionKeyMapping>& Maps = Imc->GetMappings();
			V.Check(FString::Printf(TEXT("%s mapping count"), C.Key), Maps.Num() == C.Value, FString::FromInt(Maps.Num()));
			for (const FEnhancedActionKeyMapping& M : Maps)
			{
				TArray<FString> Mods, Trigs;
				for (const UInputModifier* Mod : M.Modifiers) { Mods.Add(GetNameSafe(Mod ? Mod->GetClass() : nullptr)); }
				for (const UInputTrigger* T : M.Triggers) { Trigs.Add(FString::Printf(TEXT("%s(%.2f)"), *GetNameSafe(T ? T->GetClass() : nullptr), T ? T->ActuationThreshold : 0.0f)); }
				UE_LOG(LogOpenCity, Display, TEXT("OCVERIFY      %s: %s <- %s [%s] [%s]"), C.Key, *GetNameSafe(M.Action), *M.Key.ToString(),
					*FString::Join(Mods, TEXT(", ")), *FString::Join(Trigs, TEXT(", ")));
				V.Check(FString::Printf(TEXT("%s %s has action"), C.Key, *M.Key.ToString()), M.Action != nullptr);
			}
		}
		const bool bLegacy = GetDefault<UInputSettings>()->bEnableLegacyInputScales;
		V.Check(TEXT("legacy input scales on (look mappings depend on it)"), bLegacy, bLegacy ? TEXT("True") : TEXT("False"));
		UObject* Cfg = OCEd::Require(TEXT("/Game/Core/Input/DA_OCInputConfig"));
		for (const TCHAR* Prop : { TEXT("OnFootContext"), TEXT("AimingContext"), TEXT("Move"), TEXT("Look"), TEXT("Jump"), TEXT("Sprint"),
			TEXT("SprintToggle"), TEXT("Crouch"), TEXT("Aim"), TEXT("SwapShoulder"), TEXT("Interact") })
		{
			V.Check(FString::Printf(TEXT("input config %s"), Prop), GetObjectProp(Cfg, Prop) != nullptr);
		}
		UE_LOG(LogOpenCity, Display, TEXT("OCVERIFY DONE, %d failure(s)"), V.Fails);
		return V.Fails == 0;
	}

	bool VerifyM2()
	{
		FVerify V;
		for (const TCHAR* Name : { TEXT("Coupe"), TEXT("Offroad"), TEXT("Bike") })
		{
			UBlueprint* Bp = CastChecked<UBlueprint>(OCEd::Require(FString::Printf(TEXT("/Game/Vehicles/Blueprints/BP_OCVehicle_%s"), Name)));
			UOCVehicleData* Data = Cast<UOCVehicleData>(GetObjectProp(Cdo(Bp), TEXT("VehicleData")));
			V.Check(FString::Printf(TEXT("BP_OCVehicle_%s VehicleData"), Name), Data != nullptr, GetNameSafe(Data));
			V.Check(FString::Printf(TEXT("BP_OCVehicle_%s InputConfig"), Name), GetObjectProp(Cdo(Bp), TEXT("InputConfig")) != nullptr);
			if (Data)
			{
				V.Check(FString::Printf(TEXT("DA_Vehicle_%s mesh"), Name), Data->Mesh != nullptr);
				V.Check(FString::Printf(TEXT("DA_Vehicle_%s 4 wheels"), Name), Data->WheelSetups.Num() == 4);
				V.Check(FString::Printf(TEXT("DA_Vehicle_%s torque curve"), Name), Data->TorqueCurvePoints.Num() > 0);
			}
		}
		GEditor->GetEditorSubsystem<ULevelEditorSubsystem>()->LoadLevel(TEXT("/Game/Maps/Dev/L_DevDriving"));
		for (AActor* A : GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->GetAllLevelActors())
		{
			if (AOCVehicle* Veh = Cast<AOCVehicle>(A))
			{
				V.Check(FString::Printf(TEXT("placed %s VehicleData"), *Veh->GetActorLabel()), Veh->GetVehicleData() != nullptr, GetNameSafe(Veh->GetVehicleData()));
			}
		}
		UE_LOG(LogOpenCity, Display, TEXT("OCVERIFY DONE, %d failure(s)"), V.Fails);
		return V.Fails == 0;
	}
}

UOCMilestoneCommandlet::UOCMilestoneCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UOCMilestoneCommandlet::Main(const FString& Params)
{
	FString StepList;
	if (!FParse::Value(*Params, TEXT("steps="), StepList, /*bShouldStopOnSeparator*/ false))
	{
		UE_LOG(LogOpenCity, Error, TEXT("usage: -run=OCMilestone -steps=m0,m1,m2,verify-m1,verify-m2"));
		return 1;
	}
	TArray<FString> Steps;
	StepList.ParseIntoArray(Steps, TEXT(","));
	for (const FString& Step : Steps)
	{
		bool bOk = false;
		if (Step == TEXT("m0")) { bOk = OCMilestone::SetupM0(); }
		else if (Step == TEXT("m1")) { bOk = OCMilestone::SetupM1(); }
		else if (Step == TEXT("m2")) { bOk = OCMilestone::SetupM2(); }
		else if (Step == TEXT("verify-m1")) { bOk = OCMilestone::VerifyM1(); }
		else if (Step == TEXT("verify-m2")) { bOk = OCMilestone::VerifyM2(); }
		else { UE_LOG(LogOpenCity, Error, TEXT("OCMilestone: unknown step %s"), *Step); }
		if (!bOk)
		{
			UE_LOG(LogOpenCity, Error, TEXT("OCMilestone: step %s FAILED"), *Step);
			return 1;
		}
	}
	UE_LOG(LogOpenCity, Display, TEXT("OCMilestone: done (%s)"), *StepList);
	return 0;
}
