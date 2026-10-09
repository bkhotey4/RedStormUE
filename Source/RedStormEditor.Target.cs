using UnrealBuildTool;
using System.Collections.Generic;

public class RedStormEditorTarget : TargetRules
{
	public RedStormEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("RedStorm");
		// UE 5.4 + MSVC 14.40 以上：引擎標頭的 __has_feature 會觸發 C4668/C4067
		bOverrideBuildEnvironment = true;
		AdditionalCompilerArguments = "/wd4668 /wd4067 /FI\"R:\\RedStormUE\\Source\\RSMsvcCompat.h\"";
		bUsePCHFiles = false;
	}
}
