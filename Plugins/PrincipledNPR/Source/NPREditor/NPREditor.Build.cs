using UnrealBuildTool;

public class NPREditor : ModuleRules
{
    public NPREditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "NPRRuntime", "UnrealEd" });
    }
}
