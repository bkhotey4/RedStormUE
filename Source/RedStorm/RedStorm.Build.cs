using UnrealBuildTool;

public class RedStorm : ModuleRules
{
	public RedStorm(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// 原始碼含中文字串：所有 .cpp/.h 以 UTF-8 (BOM) 存檔
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "RHI",
			"Json", "JsonUtilities",
			"Slate", "SlateCore",
			"ImageWrapper",
			"AudioMixer", "RenderCore",
			"MediaAssets", "Media",
			"DeveloperSettings",
			"ProceduralMeshComponent"
		});
	}
}
