using UnrealBuildTool;

public class FinalNightTarget : TargetRules
{
	public FinalNightTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("FinalNight");
	}
}
