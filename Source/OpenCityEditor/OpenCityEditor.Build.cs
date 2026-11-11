using UnrealBuildTool;

// Editor-only tools and asset validators. Never packaged.
public class OpenCityEditor : ModuleRules
{
	public OpenCityEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Each commandlet port keeps its own file-local helpers; unity builds merged them into name clashes.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "OpenCityCore", "OpenCity", "Landscape", "ImageCore", "RHI", "MeshDescription", "StaticMeshDescription", "AssetRegistry", "Json", "DataLayerEditor", "GameplayTags", "MaterialEditor", "AssetTools", "EditorScriptingUtilities", "LevelEditor", "WorldPartitionEditor", "PhysicsCore", "EnhancedInput", "BlueprintEditorLibrary", "ChaosVehicles", "Projects", "InputCore" });
	}
}
