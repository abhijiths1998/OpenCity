using UnrealBuildTool;

// Editor-only tools and asset validators. Never packaged.
public class OpenCityEditor : ModuleRules
{
	public OpenCityEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "OpenCityCore", "OpenCity", "Landscape", "ImageCore", "RHI" });
	}
}
