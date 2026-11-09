#include "OCAuditLibrary.h"

#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"

FOCMeshAuditInfo UOCAuditLibrary::GetStaticMeshAuditInfo(UStaticMesh* Mesh)
{
	FOCMeshAuditInfo Info;
	if (!Mesh)
	{
		return Info;
	}
	Info.NumLODs = Mesh->GetNumSourceModels();
	Info.bNanite = Mesh->GetNaniteSettings().bEnabled;
	if (const FMeshDescription* Desc = Mesh->GetMeshDescription(0))
	{
		Info.NumUVChannels = FStaticMeshConstAttributes(*Desc).GetVertexInstanceUVs().GetNumChannels();
	}
	if (const UBodySetup* Body = Mesh->GetBodySetup())
	{
		Info.SimpleCollisionCount = Body->AggGeom.GetElementCount();
		Info.bComplexAsSimple = Body->CollisionTraceFlag == CTF_UseComplexAsSimple;
	}
	return Info;
}
