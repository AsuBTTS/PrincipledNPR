#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../NPRRimMaterialBuilder.h"
#include "AssetCompilingManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "Misc/AutomationTest.h"
#include "ShaderCompiler.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRRimOriginalMaterialTest,"NPR.Editor.RimOriginalMaterial",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FNPRRimOriginalMaterialTest::RunTest(const FString&)
{
    const TCHAR* Paths[]={TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"),
        TEXT("/Game/NPR/Characters/ChenQianyu/Materials/SemiPBRStylized/MI_ChenQianyu_00_Initial.MI_ChenQianyu_00_Initial")};
    for(const TCHAR* Path:Paths)
    {
        auto* Source=LoadObject<UMaterialInterface>(nullptr,Path);
        if(!TestNotNull(TEXT("Source loads"),Source))return false;
        auto* Root=Source->GetMaterial();const auto Count=Root->GetExpressions().Num();
        const auto* OriginalToon=Cast<UMaterialExpressionSubstrateToonBSDF>(Root->GetEditorOnlyData()->FrontMaterial.Expression);
        FString Error;
        auto* Derived=NPRRimMaterialBuilder::Build(Source,ENPRRimShadingModel::KeepOriginal,Error);
        if(!TestNotNull(*Error,Derived))return false;
        TestTrue(TEXT("Source graph is never edited"),Root->GetExpressions().Num()==Count && Derived->GetMaterial()!=Root);
        TestEqual(TEXT("Blend mode retained"),Derived->GetBlendMode(),Source->GetBlendMode());
        TestEqual(TEXT("Two-sided state retained"),Derived->IsTwoSided(),Source->IsTwoSided());
        TArray<FMaterialParameterInfo> Infos;TArray<FGuid> Ids;
        Source->GetAllTextureParameterInfo(Infos,Ids);
        for(const auto& Info:Infos)
        {
            UTexture* A=nullptr;UTexture* B=nullptr;
            Source->GetTextureParameterValue(Info,A);Derived->GetTextureParameterValue(Info,B);
            TestTrue(*FString::Printf(TEXT("Original texture retained: %s"),*Info.Name.ToString()),A==B);
        }
        if(OriginalToon)
        {
            auto* Clone=Cast<UMaterialExpressionSubstrateToonBSDF>(Derived->GetMaterial()->GetEditorOnlyData()->FrontMaterial.Expression);
            if(!TestNotNull(TEXT("Native Toon shading stays native Toon"),Clone))return false;
            TestTrue(TEXT("Original ToonProfile retained"),OriginalToon->ToonProfile==Clone->ToonProfile);
            TestTrue(TEXT("An absent source normal stays absent, no DefaultNormal fallback injected"),!OriginalToon->Normal.Expression&&!Clone->Normal.Expression);
        }
        auto* Subsurface=NPRRimMaterialBuilder::Build(Source,ENPRRimShadingModel::Subsurface,Error);
        if(!TestNotNull(*Error,Subsurface))return false;
        if(OriginalToon)
        {
            auto* SSS=Cast<UMaterialExpressionSubstrateShadingModels>(Subsurface->GetMaterial()->GetEditorOnlyData()->FrontMaterial.Expression);
            TestTrue(TEXT("Subsurface choice changes the actual BSDF shading model"),SSS&&SSS->ShadingModelOverride==MSM_Subsurface);
        }
        else TestTrue(TEXT("Legacy Subsurface model actually selected"),Subsurface->GetMaterial()->GetShadingModels().HasShadingModel(MSM_Subsurface));
        TestEqual(TEXT("Subsurface generation does not mutate source"),Root->GetExpressions().Num(),Count);
    }
    FAssetCompilingManager::Get().FinishAllCompilation();
    if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
    return true;
}
#endif
