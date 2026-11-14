using UnrealBuildTool;

public class FinalNightEditorTarget : TargetRules
{
	public FinalNightEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("FinalNight");
	}
}
