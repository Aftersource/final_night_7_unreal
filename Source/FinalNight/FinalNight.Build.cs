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
		// FN7 (Mac session, backend OK 2026-11-15): runtime god statues are built from their GLB with MeshDescription.
		PrivateDependencyModuleNames.AddRange(new string[] { "MeshDescription", "StaticMeshDescription" });
	}
}
