#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AssetCompilingManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NPRBackendStylePackage.h"
#include "NPROutlineActorComponent.h"
#include "NPRStyleBindingComponent.h"
#include "NPRSurfaceRecipe.h"
#include "PreviewScene.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
struct FSurfaceFixture
{
    FPreviewScene Scene{FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false).SetTransactional(false)};
    TStrongObjectPtr<UNPRStyleBindingComponent> Binding{NewObject<UNPRStyleBindingComponent>()};
    UStaticMeshComponent* Mesh=nullptr;
    UMaterialInstanceDynamic* MID=nullptr;
    UNPROutlineActorComponent* Outline=nullptr;
    USceneCaptureComponent2D* Capture=nullptr;
    UTextureRenderTarget2D* Target=nullptr;
    TMap<FString,TArray<FLinearColor>> Captures;
    FString Directory;
    int32 Variant=0, Delay=60;
    bool bApplied=false;
};

class FSurfaceFrames : public IAutomationLatentCommand
{
public:
    FSurfaceFrames(FAutomationTestBase* InTest,TSharedPtr<FSurfaceFixture> InFixture):Test(InTest),Fixture(MoveTemp(InFixture)){}
    virtual bool Update() override
    {
        struct FVariant {const TCHAR* Name; ESceneCaptureSource Source; bool Rim; bool BaseEmission; bool Additional; int32 Outline; bool Toon; bool FlatNormal;};
        const FVariant Variants[]={
            {TEXT("BaseColorOff"),SCS_BaseColor,false,false,false,0,false,false},
            {TEXT("BaseColorRim"),SCS_BaseColor,true,false,false,0,false,false},
            {TEXT("NormalOff"),SCS_Normal,false,false,false,0,false,false},
            {TEXT("NormalRim"),SCS_Normal,true,false,false,0,false,false},
            {TEXT("NormalFlat"),SCS_Normal,false,false,false,0,false,true},
            {TEXT("BaselineHDR"),SCS_SceneColorHDR,false,false,false,0,false,false},
            {TEXT("BaseEmissionHDR"),SCS_SceneColorHDR,false,true,false,0,false,false},
            {TEXT("RimOnlyHDR"),SCS_SceneColorHDR,true,false,false,0,false,false},
            {TEXT("CombinedHDR"),SCS_SceneColorHDR,true,true,false,0,false,false},
            {TEXT("AdditionalHDR"),SCS_SceneColorHDR,false,false,true,0,false,false},
            {TEXT("AllHDR"),SCS_SceneColorHDR,true,true,true,0,false,false},
            {TEXT("SurfaceBase"),SCS_FinalColorLDR,false,true,false,0,false,false},
            {TEXT("SurfaceRim"),SCS_FinalColorLDR,true,true,true,0,false,false},
            {TEXT("SurfaceScreenOutline"),SCS_FinalColorLDR,true,true,true,1,false,false},
            {TEXT("SurfaceHullOutline"),SCS_FinalColorLDR,true,true,true,2,false,false},
            {TEXT("ToonBase"),SCS_FinalColorLDR,false,true,false,0,true,false},
            {TEXT("ToonRim"),SCS_FinalColorLDR,true,true,false,0,true,false}
        };
        auto& F=*Fixture;
        if(F.Variant==UE_ARRAY_COUNT(Variants))
        {
            auto Difference=[&](const TCHAR* A,const TCHAR* B)
            {
                double Sum=0; const auto& X=F.Captures.FindChecked(A);const auto& Y=F.Captures.FindChecked(B);
                for(int32 I=0;I<X.Num();++I)Sum+=FMath::Abs(X[I].R-Y[I].R)+FMath::Abs(X[I].G-Y[I].G)+FMath::Abs(X[I].B-Y[I].B);
                return Sum/X.Num();
            };
            Test->TestTrue(TEXT("Rim does not change actual BaseColor buffer"),Difference(TEXT("BaseColorOff"),TEXT("BaseColorRim"))<1e-5);
            Test->TestTrue(TEXT("Rim does not change actual Normal buffer"),Difference(TEXT("NormalOff"),TEXT("NormalRim"))<1e-5);
            Test->TestTrue(TEXT("Normal texture actually affects shading normal"),Difference(TEXT("NormalOff"),TEXT("NormalFlat"))>.01);
            Test->TestTrue(TEXT("Base emissive is visible without Rim"),Difference(TEXT("BaseEmissionHDR"),TEXT("BaselineHDR"))>.01);
            Test->TestTrue(TEXT("Rim changes actual radiance"),Difference(TEXT("RimOnlyHDR"),TEXT("BaselineHDR"))>.01);
            Test->TestTrue(TEXT("Second additive module changes actual radiance"),Difference(TEXT("AdditionalHDR"),TEXT("BaselineHDR"))>.01);
            double Error=0,Scale=0;
            const auto& Base=F.Captures.FindChecked(TEXT("BaselineHDR"));
            const auto& Em=F.Captures.FindChecked(TEXT("BaseEmissionHDR"));
            const auto& Rim=F.Captures.FindChecked(TEXT("RimOnlyHDR"));
            const auto& Extra=F.Captures.FindChecked(TEXT("AdditionalHDR"));
            const auto& Combined=F.Captures.FindChecked(TEXT("CombinedHDR"));
            const auto& All=F.Captures.FindChecked(TEXT("AllHDR"));
            for(int32 I=0;I<Base.Num();++I)
                for(int32 C=0;C<3;++C)
                {
                    Error+=FMath::Abs(Combined[I].Component(C)-(Em[I].Component(C)+Rim[I].Component(C)-Base[I].Component(C)));
                    Error+=FMath::Abs(All[I].Component(C)-(Em[I].Component(C)+Rim[I].Component(C)+Extra[I].Component(C)-2*Base[I].Component(C)));
                    Scale+=FMath::Abs(All[I].Component(C))+FMath::Abs(Combined[I].Component(C));
                }
            Test->TestTrue(TEXT("Original emissive plus Rim plus second effect are additive in HDR"),Scale>.01 && Error/Scale<.01);
            Test->TestTrue(TEXT("Screen Outline visibly coexists with Surface effects"),Difference(TEXT("SurfaceRim"),TEXT("SurfaceScreenOutline"))>.001);
            Test->TestTrue(TEXT("Hull Outline visibly coexists with Surface effects"),Difference(TEXT("SurfaceRim"),TEXT("SurfaceHullOutline"))>.001);
            Test->TestTrue(TEXT("Native Toon and Rim coexist on one material slot"),Difference(TEXT("ToonBase"),TEXT("ToonRim"))>.01);
            const FString Metrics=FString::Printf(TEXT("{\n  \"captured_at_utc\":\"%s\",\n  \"base_color_rim_difference\":%.9g,\n  \"normal_rim_difference\":%.9g,\n  \"normal_texture_difference\":%.9g,\n  \"base_emissive_difference\":%.9g,\n  \"rim_difference\":%.9g,\n  \"second_effect_difference\":%.9g,\n  \"hdr_relative_addition_error\":%.9g,\n  \"screen_outline_difference\":%.9g,\n  \"hull_outline_difference\":%.9g,\n  \"toon_rim_difference\":%.9g\n}\n"),
                *FDateTime::UtcNow().ToIso8601(),Difference(TEXT("BaseColorOff"),TEXT("BaseColorRim")),Difference(TEXT("NormalOff"),TEXT("NormalRim")),
                Difference(TEXT("NormalOff"),TEXT("NormalFlat")),Difference(TEXT("BaseEmissionHDR"),TEXT("BaselineHDR")),Difference(TEXT("RimOnlyHDR"),TEXT("BaselineHDR")),
                Difference(TEXT("AdditionalHDR"),TEXT("BaselineHDR")),Error/FMath::Max(Scale,.0001),Difference(TEXT("SurfaceRim"),TEXT("SurfaceScreenOutline")),
                Difference(TEXT("SurfaceRim"),TEXT("SurfaceHullOutline")),Difference(TEXT("ToonBase"),TEXT("ToonRim")));
            Test->TestTrue(TEXT("Raw GPU measurements saved"),FFileHelper::SaveStringToFile(Metrics,*(F.Directory/TEXT("GpuMetrics.json"))));
            Test->AddInfo(FString::Printf(TEXT("17 real GPU captures: %s; relative HDR composition error=%g"),*F.Directory,Error/FMath::Max(Scale,.0001)));
            F.Outline->bEnabled=false;F.Outline->RefreshOutline();F.Binding->RestoreMaterials();
            Fixture.Reset();return true;
        }
        const auto& V=Variants[F.Variant];
        if(!F.bApplied)
        {
            FString Error;
            if(V.Toon && F.Variant==15)
            {
                auto* Recipe=LoadObject<UNPRSurfaceRecipe>(nullptr,TEXT("/PrincipledNPR/Surface/SR_Toon_Rim.SR_Toon_Rim"));
                UNPRSurfaceRecipe* Selected=nullptr;
                auto* Package=UNPRSurfaceComposerLibrary::ComposePackage(F.Mesh,TEXT("S58Toon"),{Recipe},{TEXT("Toon"),TEXT("RimLight")},{0},Selected,Error);
                if(!Test->TestTrue(TEXT("Prebuilt Toon family selected and bound"),Package && F.Binding->ApplyPackage(F.Mesh,Package,Error)))return true;
                F.MID=Cast<UMaterialInstanceDynamic>(F.Mesh->GetMaterial(0));
            }
            F.MID->SetVectorParameterValue(TEXT("NPR_BaseColor"),FLinearColor(.35f,.22f,.12f,1));
            F.MID->SetTextureParameterValue(TEXT("NPR_BaseColorTexture"),LoadObject<UTexture>(nullptr,TEXT("/PrincipledNPR/Surface/T_SurfaceValidationChecker.T_SurfaceValidationChecker")));
            F.MID->SetTextureParameterValue(TEXT("NPR_NormalTexture"),LoadObject<UTexture>(nullptr,TEXT("/PrincipledNPR/Surface/T_SurfaceValidationNormal.T_SurfaceValidationNormal")));
            F.MID->SetScalarParameterValue(TEXT("NPR_NormalStrength"),V.FlatNormal?0.f:1.f);
            F.MID->SetVectorParameterValue(TEXT("NPR_EmissiveColor"),V.BaseEmission?FLinearColor(.03f,.06f,.12f,1):FLinearColor::Black);
            F.MID->SetVectorParameterValue(TEXT("NPR_AdditionalEmissiveColor"),FLinearColor(0,.12f,0,1));
            F.MID->SetScalarParameterValue(TEXT("NPR_AdditionalEmissiveScale"),V.Additional?1.f:0.f);
            FNPRRimParameters Values;Values.RimGrazingAngleRad=FMath::DegreesToRadians(25.);Values.RimPeakScale=V.Rim?1.f:0.f;Values.RimColor=FLinearColor::Red;
            if(!Test->TestTrue(TEXT("Existing Rim setter on composed MID"),F.Binding->SetRimParameters(Values,Error)))return true;
            Test->TestTrue(TEXT("All effects share one stable slot MID"),F.Mesh->GetMaterial(0)==F.MID);
            F.Outline->bEnabled=V.Outline!=0;
            F.Outline->BaseSettings.Method=V.Outline==2?ENPROutlineMethod::InvertedHull:ENPROutlineMethod::ScreenSpace;
            if(!Test->TestTrue(TEXT("Existing Outline controller refreshes without owning surface"),F.Outline->RefreshOutline()))return true;
            Test->TestTrue(TEXT("Outline does not replace Surface MID"),F.Mesh->GetMaterial(0)==F.MID);
            Test->TestEqual(TEXT("Expected Screen pass count"),F.Outline->GetScreenPassCount(),V.Outline==1?1:0);
            F.Capture->CaptureSource=V.Source;
            // Parameter texture assignment and a newly selected family can trigger work after initial fixture setup.
            // Frame counts alone are insufficient: a fast headless preview can capture the fallback material repeatedly.
            FAssetCompilingManager::Get().FinishAllCompilation();
            F.MID->EnsureIsComplete();
            if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
            FlushRenderingCommands();
            F.bApplied=true;
        }
        F.Scene.GetWorld()->Tick(LEVELTICK_All,1.f/60.f);F.Scene.GetWorld()->SendAllEndOfFrameUpdates();F.Capture->CaptureScene();
        if(--F.Delay>0)return false;
        FlushRenderingCommands();
        TArray<FLinearColor> Linear;
        if(!Test->TestTrue(TEXT("GPU linear readback"),F.Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Linear,FReadSurfaceDataFlags(RCM_MinMax))))return true;
        if(!Test->TestEqual(TEXT("Readback resolution"),Linear.Num(),512*512))return true;
        double Energy=0;for(const auto& C:Linear)Energy+=FMath::Abs(C.R)+FMath::Abs(C.G)+FMath::Abs(C.B);
        Test->TestTrue(TEXT("Capture contains nonblack rendered surface"),Energy>1);
        F.Captures.Add(V.Name,MoveTemp(Linear));
        TArray<FColor> Pixels;F.Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(512,512,Pixels,PNG);
        Test->TestTrue(TEXT("Real GPU capture saved"),FFileHelper::SaveArrayToFile(PNG,*(F.Directory/(FString(V.Name)+TEXT(".png")))));
        ++F.Variant;F.Delay=15;F.bApplied=false;return false;
    }
private:
    FAutomationTestBase* Test;TSharedPtr<FSurfaceFixture> Fixture;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRSurfaceRenderTest,"NPR.Surface.RenderComposition",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)

bool FNPRSurfaceRenderTest::RunTest(const FString& Arguments)
{
    auto* Recipe=LoadObject<UNPRSurfaceRecipe>(nullptr,TEXT("/PrincipledNPR/Surface/SR_DefaultLit_Rim.SR_DefaultLit_Rim"));
    auto* ToonRecipe=LoadObject<UNPRSurfaceRecipe>(nullptr,TEXT("/PrincipledNPR/Surface/SR_Toon_Rim.SR_Toon_Rim"));
    auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if(!TestTrue(TEXT("Saved Surface recipes and sphere load"),Recipe&&ToonRecipe&&Sphere))return false;
    LoadObject<UTexture>(nullptr,TEXT("/PrincipledNPR/Surface/T_SurfaceValidationChecker.T_SurfaceValidationChecker"));
    LoadObject<UTexture>(nullptr,TEXT("/PrincipledNPR/Surface/T_SurfaceValidationNormal.T_SurfaceValidationNormal"));
    Recipe->Material->EnsureIsComplete();ToonRecipe->Material->EnsureIsComplete();
    FAssetCompilingManager::Get().FinishAllCompilation();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
    auto Fixture=MakeShared<FSurfaceFixture>();auto& F=*Fixture;
    auto* Actor=F.Scene.GetWorld()->SpawnActor<AActor>();
    F.Mesh=NewObject<UStaticMeshComponent>(Actor,NAME_None,RF_Transactional);F.Mesh->SetStaticMesh(Sphere);
    Actor->SetRootComponent(F.Mesh);Actor->AddInstanceComponent(F.Mesh);F.Mesh->RegisterComponent();
    UNPRSurfaceRecipe* Selected=nullptr;FString Error;
    auto* Package=UNPRSurfaceComposerLibrary::ComposePackage(F.Mesh,TEXT("DefaultLit"),{Recipe},{TEXT("RimLight"),TEXT("AdditionalEmission")},{0},Selected,Error);
    if(!TestTrue(TEXT("Production composer and existing binder apply"),Package&&F.Binding->ApplyPackage(F.Mesh,Package,Error)))return false;
    F.MID=Cast<UMaterialInstanceDynamic>(F.Mesh->GetMaterial(0));
    F.Outline=NewObject<UNPROutlineActorComponent>(Actor);
    F.Outline->bEnabled=false;F.Outline->BaseSettings.Method=ENPROutlineMethod::ScreenSpace;
    F.Outline->BaseSettings.Color=FLinearColor(0,.8f,0,1);F.Outline->BaseSettings.WidthPixels=3;F.Outline->BaseSettings.HullWidthCm=1.5f;
    F.Outline->HullTemplate=LoadObject<UMaterialInterface>(nullptr,TEXT("/PrincipledNPR/Outline/M_OutlineHull.M_OutlineHull"));
    F.Outline->MaskTemplate=LoadObject<UMaterialInterface>(nullptr,TEXT("/PrincipledNPR/Outline/M_OutlineMask.M_OutlineMask"));
    F.Outline->ScreenTemplate=LoadObject<UMaterialInterface>(nullptr,TEXT("/PrincipledNPR/Outline/M_OutlineScreen.M_OutlineScreen"));
    F.Outline->CollectOwnerMeshes();Actor->AddInstanceComponent(F.Outline);F.Outline->RegisterComponent();
    F.Capture=NewObject<USceneCaptureComponent2D>();F.Capture->ProjectionType=ECameraProjectionMode::Orthographic;
    F.Capture->OrthoWidth=150;F.Capture->bCaptureEveryFrame=false;F.Capture->bCaptureOnMovement=false;
    F.Capture->bAlwaysPersistRenderingState=true;
    F.Capture->ShowFlags.SetPostProcessing(true);F.Capture->ShowFlags.SetPostProcessMaterial(true);
    F.Capture->ShowFlags.SetTemporalAA(false);F.Capture->ShowFlags.SetBloom(false);F.Capture->ShowFlags.SetMotionBlur(false);
    F.Capture->ShowFlags.SetGlobalIllumination(false);F.Capture->ShowFlags.SetReflectionEnvironment(false);
    auto& PP=F.Capture->PostProcessSettings;PP.bOverride_AutoExposureMethod=true;PP.AutoExposureMethod=AEM_Manual;
    PP.bOverride_AutoExposureBias=true;PP.AutoExposureBias=0;PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true;PP.AutoExposureApplyPhysicalCameraExposure=false;
    F.Target=NewObject<UTextureRenderTarget2D>();F.Target->RenderTargetFormat=RTF_RGBA16f;F.Target->ClearColor=FLinearColor::Black;
    F.Target->InitAutoFormat(512,512);F.Target->UpdateResourceImmediate(true);F.Capture->TextureTarget=F.Target;
    F.Scene.AddComponent(F.Capture,FTransform(FRotator::ZeroRotator,FVector(-200,0,0)));
    F.Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Documentation/Surface/Validation"));
    IFileManager::Get().MakeDirectory(*F.Directory,true);
    FAssetCompilingManager::Get().FinishAllCompilation();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSurfaceFrames>(this,Fixture));return true;
}
#endif
