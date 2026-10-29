using UnrealBuildTool;

public class NPREditor : ModuleRules
{
    public NPREditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "NPRRuntime" });
        PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "Slate", "SlateCore", "ToolMenus", "LevelEditor",
            "PropertyEditor", "ContentBrowser", "AssetTools", "AssetRegistry", "AdvancedPreviewScene", "InputCore", "RenderCore", "RHI", "AppFramework", "Json" });
    }
}
