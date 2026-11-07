using UnrealBuildTool;

// All gameplay, organised by feature folder (Dev/, Player/, Vehicles/ ...).
public class OpenCity : ModuleRules
{
	public OpenCity(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"OpenCityCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RHI",        // GPU frame timing for the perf camera
			"RenderCore", // game/render thread timing for the perf camera
		});
	}
}
