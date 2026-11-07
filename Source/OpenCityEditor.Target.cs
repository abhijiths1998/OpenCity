using UnrealBuildTool;

public class OpenCityEditorTarget : TargetRules
{
	public OpenCityEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange(new string[] { "OpenCityCore", "OpenCity", "OpenCityEditor" });
	}
}
