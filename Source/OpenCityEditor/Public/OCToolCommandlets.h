#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "OCToolCommandlets.generated.h"

/** Camera bookmarks for the visual-QA loop (/Game/Dev/DA_QABookmarks), three per district from the road graph. */
UCLASS()
class UOCQABookmarksCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UOCQABookmarksCommandlet();
	virtual int32 Main(const FString& Params) override;
};

/** Whether plugins are enabled (scripts/plugin_smoke.sh):  -plugins=Water,ZoneGraph,...  One OCPROBE line each. */
UCLASS()
class UOCPluginProbeCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UOCPluginProbeCommandlet();
	virtual int32 Main(const FString& Params) override;
};

/** Checks L_Marevia's blockout against WORLD_METRICS.md (BLOCKOUT_ names, engine cube, 100 cm grid, 320 cm storeys). */
UCLASS()
class UOCVerifyBlockoutCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UOCVerifyBlockoutCommandlet();
	virtual int32 Main(const FString& Params) override;
};

/** World-building audit: scans /Game (meshes, textures, materials, tiling, maps) and writes AUDIT.md (+ AUDIT_NOTES.md). */
UCLASS()
class UOCAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UOCAuditCommandlet();
	virtual int32 Main(const FString& Params) override;
};
