#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "OCKitPreviewCommandlet.generated.h"

/**
 * Preview renders of the kit (software rasteriser, flat colour per material slot, the meshes' own normals,
 * back faces culled so inside-out faces show as holes). Formerly Tools/blender/render_previews.py.
 *   -src=/Game/Environment/Meshes/Kit   meshes to render (SM_Kit_*)
 *   -ref=<folder>                        also render the same pieces from another folder, side by side
 *   -out=SourceArt/Kit/Previews          pieces/<Piece>.png and contact_sheet.png
 *   -only=A,B                            some pieces only
 */
UCLASS()
class UOCKitPreviewCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UOCKitPreviewCommandlet();
	virtual int32 Main(const FString& Params) override;
};
