using UnrealBuildTool;

// Interfaces, shared types and pure logic. Depends only on engine modules,
// never on other OpenCity modules.
public class OpenCityCore : ModuleRules
{
	public OpenCityCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Json" }); // road graph layout loading
	}
}
