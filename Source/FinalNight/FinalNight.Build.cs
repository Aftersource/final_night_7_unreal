using UnrealBuildTool;

public class FinalNight : ModuleRules
{
	public FinalNight(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore",
			"Slate", "SlateCore", "WebSockets", "Json", "JsonUtilities", "HTTP"
		});
	}
}
