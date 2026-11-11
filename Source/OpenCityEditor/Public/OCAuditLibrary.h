#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "OCAuditLibrary.generated.h"

class UStaticMesh;

/** What the asset audit (the OCAudit commandlet) needs to know about a static mesh. */
USTRUCT(BlueprintType)
struct FOCMeshAuditInfo
{
	GENERATED_BODY()

	/** UV channels in the LOD 0 source mesh. */
	UPROPERTY(BlueprintReadOnly, Category = "OpenCity|Audit")
	int32 NumUVChannels = 0;

	/** Source LODs (1 = no LODs). */
	UPROPERTY(BlueprintReadOnly, Category = "OpenCity|Audit")
	int32 NumLODs = 0;

	/** Simple collision primitives (boxes, spheres, capsules, convex hulls incl. UCX_). */
	UPROPERTY(BlueprintReadOnly, Category = "OpenCity|Audit")
	int32 SimpleCollisionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "OpenCity|Audit")
	bool bComplexAsSimple = false;

	UPROPERTY(BlueprintReadOnly, Category = "OpenCity|Audit")
	bool bNanite = false;
};

/**
 * Editor-only queries for the asset audit. The engine's static-mesh scripting libraries refuse to
 * run outside the interactive editor, so the headless audit reads the mesh here instead.
 */
UCLASS()
class OPENCITYEDITOR_API UOCAuditLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "OpenCity|Audit")
	static FOCMeshAuditInfo GetStaticMeshAuditInfo(UStaticMesh* Mesh);
};
