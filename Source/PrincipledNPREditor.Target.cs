using UnrealBuildTool;

public class PrincipledNPREditorTarget : TargetRules
{
    public PrincipledNPREditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("PrincipledNPR");
    }
}
