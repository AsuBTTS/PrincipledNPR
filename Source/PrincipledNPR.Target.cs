using UnrealBuildTool;

public class PrincipledNPRTarget : TargetRules
{
    public PrincipledNPRTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("PrincipledNPR");
    }
}
