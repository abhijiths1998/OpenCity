#pragma once

// The final-art pipeline (UOCBuildArtCommandlet, scripts/build_art.sh). Formerly
// OCArtMaterials.cpp (decals), OCArtMaterials.cpp, OCArtData.cpp (weather), OCArtData.cpp (physical materials),
// OCArtImport.cpp (props), OCArtImport.cpp (kit), OCBuildArt -steps=stats and the OCImportTextures commandlet.

#include "CoreMinimal.h"

namespace OCArt
{
	/** Decal textures (SourceArt/Downloads/Decals), M_Master_Decal and MI_Decal_*. Run before BuildMaterials:
	 *  the surface master uses a leak scan as wall grime. Returns false if no decals are downloaded. */
	bool BuildDecals();

	/** CC0 texture sets, the LUT, MPC_World, every master in /Game/Materials and their instances. */
	bool BuildMaterials();

	/** DA_Weather_{Clear,Cloudy,Rain}. */
	void BuildWeather();

	/** PM_<Surface> physical materials, assigned to the MI_Paving_* instances. */
	void BuildPhysicalMaterials();

	/** Props from the merged glTF files in SourceArt/Downloads/Props into /Game/Environment/Props, with MI_Prop_* materials. */
	bool ImportProps();

	/** Kit pieces from SourceArt/Kit/FBX into /Game/Environment/Meshes/Kit, with the MI_Kit_* materials. */
	bool ImportKit();

	/** MATSTAT lines: pixel shader instructions of each master against ART_PIPELINE.md budgets. */
	void MaterialStats();

	/** A T_<Asset>_<BC|N|ORM|E|M> texture folder into Dest, with one MI_<Asset> per set on Parent. */
	bool ImportTextureFolder(const FString& Src, const FString& Dest, const FString& Parent);
}
