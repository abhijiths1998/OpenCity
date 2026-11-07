using UnrealBuildTool;

public class OpenCityTarget : TargetRules
{
	public OpenCityTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange(new string[] { "OpenCityCore", "OpenCity" });
	}
}
