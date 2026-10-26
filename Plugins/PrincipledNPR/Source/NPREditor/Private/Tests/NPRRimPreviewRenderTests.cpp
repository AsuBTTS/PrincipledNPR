#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "NPRCharacterPreview.h"
#include "NPRRimMaterialBuilder.h"
#include "Materials/MaterialInstanceConstant.h"
#include "NPRStyleBindingComponent.h"
#include "NPRSurfaceRecipe.h"
#include "NPRBackendStylePackage.h"
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
#include "PreviewScene.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"

namespace
{
struct FPreviewRimFixture
{
    FPreviewScene SourceScene{FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false)};
    FPreviewScene CaptureScene{FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false)};
    TSharedPtr<FNPRCharacterPreview> Preview;
    UStaticMeshComponent* Source=nullptr;
    UStaticMeshComponent* RenderMesh=nullptr;
    UNPRStyleBindingComponent* Binding=nullptr;
    UMaterialInterface* FirstPreviewMaterial=nullptr;
    UMaterialInterface* Original=nullptr;
    TArray<FColor> OriginalPixels;
    bool OriginalCaptured=false;
    USceneCaptureComponent2D* Capture=nullptr;
    UTextureRenderTarget2D* Target=nullptr;
    TArray<TArray<FColor>> Pixels;
    FString Directory;
    int32 Variant=0,Delay=45;
    bool bApplied=false;
};

class FPreviewRimFrames : public IAutomationLatentCommand
{
public:
    FPreviewRimFrames(FAutomationTestBase* T,TSharedPtr<FPreviewRimFixture> F):Test(T),Fixture(MoveTemp(F)){}
    virtual bool Update()override
    {
        auto& F=*Fixture;
        if(F.Original && !F.OriginalCaptured)
        {
            F.Original->EnsureIsComplete();
            F.RenderMesh->SetMaterial(0,F.Original);
            F.CaptureScene.GetWorld()->Tick(LEVELTICK_All,1.f/60.f);F.CaptureScene.GetWorld()->SendAllEndOfFrameUpdates();F.Capture->CaptureScene();
            if(--F.Delay>0)return false;
            FlushRenderingCommands();F.Target->GameThread_GetRenderTargetResource()->ReadPixels(F.OriginalPixels);
            FImageUtils::SaveImageByExtension(*(F.Directory/TEXT("Original.png")),FImageView(F.OriginalPixels.GetData(),512,512));
            F.OriginalCaptured=true;F.Delay=45;return false;
        }
        const TCHAR* Names[]={TEXT("Off"),TEXT("Red1"),TEXT("Green1"),TEXT("Green2"),TEXT("OffAgain")};
        if(F.Variant==UE_ARRAY_COUNT(Names))
        {
            double Red[3]={},Green[3]={},Gain=0;int32 OnPixels=0,OffError=0;
            for(int32 I=0;I<F.Pixels[0].Num();++I)
            {
                const auto& Off=F.Pixels[0][I];const auto& R=F.Pixels[1][I];const auto& G=F.Pixels[2][I];const auto& G2=F.Pixels[3][I];const auto& Again=F.Pixels[4][I];
                const int32 A[]={Off.R,Off.G,Off.B},B[]={R.R,R.G,R.B},C[]={G.R,G.G,G.B},D[]={Again.R,Again.G,Again.B};
                bool Visible=false;
                for(int32 Channel=0;Channel<3;++Channel)
                {Red[Channel]+=FMath::Max(0,B[Channel]-A[Channel]);Green[Channel]+=FMath::Max(0,C[Channel]-A[Channel]);Visible|=B[Channel]-A[Channel]>12;OffError=FMath::Max(OffError,FMath::Abs(D[Channel]-A[Channel]));}
                if(Visible)++OnPixels;
                Gain+=int32(G2.G)-int32(G.G);
            }
            Gain/=F.Pixels[0].Num();
            if(F.Original)
            {
                int32 Maximum=0;
                for(int32 I=0;I<F.OriginalPixels.Num();++I)
                {
                    const auto A=F.OriginalPixels[I],B=F.Pixels[0][I];
                    Maximum=FMath::Max(Maximum,FMath::Max3(FMath::Abs(int32(A.R)-B.R),FMath::Abs(int32(A.G)-B.G),FMath::Abs(int32(A.B)-B.B)));
                }
                Test->TestTrue(TEXT("Rim zero preserves original rendered surface, not merely previous override"),Maximum<=2);
                FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"original_vs_rim_zero_max_error\":%d}\n"),Maximum),*(F.Directory/TEXT("SourcePreservation.json")));
            }
            Test->TestTrue(TEXT("Preview material actually produces visible Rim pixels"),OnPixels>500);
            Test->TestTrue(TEXT("Color edit turns preview pixels red"),Red[0]>2*FMath::Max(1.,FMath::Max(Red[1],Red[2])));
            Test->TestTrue(TEXT("Next color edit turns same preview pixels green"),Green[1]>2*FMath::Max(1.,FMath::Max(Green[0],Green[2])));
            Test->TestTrue(TEXT("Intensity edit increases rendered green contribution"),Gain>1.);
            Test->TestTrue(TEXT("Zero intensity restores original preview pixels"),OffError<=2);
            const FString Metrics=FString::Printf(TEXT("{\n  \"on_pixels\":%d,\n  \"red_channel_delta\":[%.9g,%.9g,%.9g],\n  \"green_channel_delta\":[%.9g,%.9g,%.9g],\n  \"green_intensity_gain\":%.9g,\n  \"off_again_max_error\":%d,\n  \"scope\":\"Real GPU capture using the exact MID returned by the shared realtime character preview; no synthetic images\"\n}\n"),
                OnPixels,Red[0],Red[1],Red[2],Green[0],Green[1],Green[2],Gain,OffError);
            Test->TestTrue(TEXT("Preview GPU metrics saved"),FFileHelper::SaveStringToFile(Metrics,*(F.Directory/TEXT("Metrics.json"))));
            F.Binding->RestoreMaterials();Fixture.Reset();return true;
        }
        if(!F.bApplied)
        {
            FNPRRimParameters P;P.RimGrazingAngleRad=FMath::DegreesToRadians(25.);P.RimEdgeHalfWidthRad=FMath::DegreesToRadians(4.);
            P.RimPeakScale=(F.Variant==0||F.Variant==4)?0.f:F.Variant==3?2.f:1.f;
            P.RimColor=F.Variant==1?FLinearColor::Red:FLinearColor::Green;
            FString Error;if(!Test->TestTrue(TEXT("Live production parameters update"),F.Binding->SetRimParameters(P,Error)))return true;
            F.Preview->Refresh();auto* Material=F.Preview->PreviewMaterial(false,F.Source,0);
            if(!F.FirstPreviewMaterial)F.FirstPreviewMaterial=Material;
            if(!Test->TestNotNull(TEXT("Shared viewport supplies real preview material"),Material))return true;
            Test->TestTrue(TEXT("Color/intensity edits preserve the preview MID identity"),Material==F.FirstPreviewMaterial);
            F.RenderMesh->SetMaterial(0,Material);
            FAssetCompilingManager::Get().FinishAllCompilation();Material->EnsureIsComplete();
            if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
            FlushRenderingCommands();F.bApplied=true;
        }
        F.Preview->Refresh(); // Repeated idle refreshes must not wipe the queued render parameters.
        F.CaptureScene.GetWorld()->Tick(LEVELTICK_All,1.f/60.f);F.CaptureScene.GetWorld()->SendAllEndOfFrameUpdates();F.Capture->CaptureScene();
        if(--F.Delay>0)return false;
        FlushRenderingCommands();TArray<FColor> Pixels;
        if(!Test->TestTrue(TEXT("Preview GPU readback"),F.Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)))return true;
        Test->TestEqual(TEXT("Preview capture resolution"),Pixels.Num(),512*512);
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(512,512,Pixels,PNG);
        Test->TestTrue(TEXT("Real preview PNG saved"),FFileHelper::SaveArrayToFile(PNG,*(F.Directory/(FString(Names[F.Variant])+TEXT(".png")))));
        F.Pixels.Add(MoveTemp(Pixels));++F.Variant;F.bApplied=false;F.Delay=12;return false;
    }
private:
    FAutomationTestBase* Test;TSharedPtr<FPreviewRimFixture> Fixture;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRRimPreviewRenderTest,"NPR.Editor.RimPreviewPixels",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)

bool FNPRRimPreviewRenderTest::RunTest(const FString&)
{
    auto F=MakeShared<FPreviewRimFixture>();
    auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto* Recipe=LoadObject<UNPRSurfaceRecipe>(nullptr,TEXT("/PrincipledNPR/Surface/SR_DefaultLit_Rim.SR_DefaultLit_Rim"));
    if(!TestTrue(TEXT("Saved sphere and recipe load"),Sphere&&Recipe))return false;
    FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;
    auto* Actor=F->SourceScene.GetWorld()->SpawnActor<AActor>(Spawn);
    F->Source=NewObject<UStaticMeshComponent>(Actor);Actor->SetRootComponent(F->Source);Actor->AddInstanceComponent(F->Source);
    F->Source->SetStaticMesh(Sphere);F->Source->RegisterComponent();
    F->Binding=NewObject<UNPRStyleBindingComponent>(Actor);Actor->AddInstanceComponent(F->Binding);F->Binding->RegisterComponent();
    FNPRRimSurfaceInputs Inputs;Inputs.bConfigured=true;Inputs.BaseColor=FLinearColor(.1f,.1f,.1f,1);
    FNPRRimParameters Parameters;FString Error;
    if(!TestTrue(TEXT("Explicit production Surface/Rim binding"),F->Binding->ApplyRimPreview(F->Source,0,Recipe,Inputs,Parameters,Error)))return false;
    F->Preview=MakeNPRCharacterPreview();F->Preview->LoadActor(Actor);F->Preview->SetRimTarget(F->Source,0);F->Preview->SetRimSolo(true);
    F->RenderMesh=NewObject<UStaticMeshComponent>();F->RenderMesh->SetStaticMesh(Sphere);F->CaptureScene.AddComponent(F->RenderMesh,FTransform::Identity);
    F->RenderMesh->SetMaterial(0,F->Preview->PreviewMaterial(false,F->Source,0));
    F->Capture=NewObject<USceneCaptureComponent2D>();F->Capture->ProjectionType=ECameraProjectionMode::Orthographic;
    F->Capture->OrthoWidth=150;F->Capture->bCaptureEveryFrame=false;F->Capture->bCaptureOnMovement=false;F->Capture->CaptureSource=SCS_FinalColorLDR;
    F->Capture->ShowFlags.SetTemporalAA(false);F->Capture->ShowFlags.SetBloom(false);F->Capture->ShowFlags.SetMotionBlur(false);
    F->Capture->ShowFlags.SetGlobalIllumination(false);F->Capture->ShowFlags.SetReflectionEnvironment(false);
    auto& PP=F->Capture->PostProcessSettings;PP.bOverride_AutoExposureMethod=true;PP.AutoExposureMethod=AEM_Manual;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true;PP.AutoExposureApplyPhysicalCameraExposure=false;
    PP.bOverride_AutoExposureBias=true;PP.AutoExposureBias=0;
    F->Target=NewObject<UTextureRenderTarget2D>();F->Target->RenderTargetFormat=RTF_RGBA8;F->Target->ClearColor=FLinearColor::Black;
    F->Target->InitAutoFormat(512,512);F->Target->UpdateResourceImmediate(true);F->Capture->TextureTarget=F->Target;
    F->CaptureScene.AddComponent(F->Capture,FTransform(FRotator::ZeroRotator,FVector(-200,0,0)));
    FAssetCompilingManager::Get().FinishAllCompilation();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
    F->Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("NPR/Workspace/RimPreview"));IFileManager::Get().MakeDirectory(*F->Directory,true);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FPreviewRimFrames>(this,F));return true;
}
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FNPRRimOriginalSurfaceRenderTest,"NPR.Editor.RimOriginalSurfacePixels",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
void FNPRRimOriginalSurfaceRenderTest::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{Names.Add(TEXT("Legacy"));Commands.Add(TEXT("Legacy"));Names.Add(TEXT("Toon"));Commands.Add(TEXT("Toon"));}

bool FNPRRimOriginalSurfaceRenderTest::RunTest(const FString& SourceKind)
{
    auto F=MakeShared<FPreviewRimFixture>();
    auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto* Recipe=LoadObject<UNPRSurfaceRecipe>(nullptr,TEXT("/PrincipledNPR/Surface/SR_DefaultLit_Rim.SR_DefaultLit_Rim"));
    if(!TestTrue(TEXT("Saved sphere and recipe load"),Sphere&&Recipe))return false;
    FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;
    auto* Actor=F->SourceScene.GetWorld()->SpawnActor<AActor>(Spawn);
    F->Source=NewObject<UStaticMeshComponent>(Actor);Actor->SetRootComponent(F->Source);Actor->AddInstanceComponent(F->Source);
    F->Source->SetStaticMesh(Sphere);F->Source->RegisterComponent();
    F->Binding=NewObject<UNPRStyleBindingComponent>(Actor);Actor->AddInstanceComponent(F->Binding);F->Binding->RegisterComponent();
    F->Original=SourceKind==TEXT("Toon") ? LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/NPR/Characters/ChenQianyu/Materials/SemiPBRStylized/MI_ChenQianyu_00_Initial.MI_ChenQianyu_00_Initial")) : F->Source->GetMaterial(0);
    if(!TestNotNull(TEXT("Original character/grid surface loads"),F->Original))return false;
    F->Source->SetMaterial(0,F->Original);
    FNPRRimParameters Parameters;FString Error;
    auto* Derived=NPRRimMaterialBuilder::Build(F->Original,ENPRRimShadingModel::KeepOriginal,Error);
    if(!TestNotNull(*Error,Derived))return false;
    if(!TestTrue(TEXT("Original surface Rim binding needs no Recipe or source-input form"),F->Binding->ApplyRimMaterial(F->Source,0,Derived,Parameters,Error)))return false;
    F->Preview=MakeNPRCharacterPreview();F->Preview->LoadActor(Actor);F->Preview->SetRimTarget(F->Source,0);F->Preview->SetRimSolo(true);
    F->RenderMesh=NewObject<UStaticMeshComponent>();F->RenderMesh->SetStaticMesh(Sphere);F->CaptureScene.AddComponent(F->RenderMesh,FTransform::Identity);
    F->RenderMesh->SetMaterial(0,F->Preview->PreviewMaterial(false,F->Source,0));
    F->Capture=NewObject<USceneCaptureComponent2D>();F->Capture->ProjectionType=ECameraProjectionMode::Orthographic;
    F->Capture->OrthoWidth=150;F->Capture->bCaptureEveryFrame=false;F->Capture->bCaptureOnMovement=false;F->Capture->CaptureSource=SCS_FinalColorLDR;
    F->Capture->ShowFlags.SetTemporalAA(false);F->Capture->ShowFlags.SetBloom(false);F->Capture->ShowFlags.SetMotionBlur(false);
    F->Capture->ShowFlags.SetGlobalIllumination(false);F->Capture->ShowFlags.SetReflectionEnvironment(false);
    auto& PP=F->Capture->PostProcessSettings;PP.bOverride_AutoExposureMethod=true;PP.AutoExposureMethod=AEM_Manual;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true;PP.AutoExposureApplyPhysicalCameraExposure=false;
    PP.bOverride_AutoExposureBias=true;PP.AutoExposureBias=0;
    F->Target=NewObject<UTextureRenderTarget2D>();F->Target->RenderTargetFormat=RTF_RGBA8;F->Target->ClearColor=FLinearColor::Black;
    F->Target->InitAutoFormat(512,512);F->Target->UpdateResourceImmediate(true);F->Capture->TextureTarget=F->Target;
    F->CaptureScene.AddComponent(F->Capture,FTransform(FRotator::ZeroRotator,FVector(-200,0,0)));
    FAssetCompilingManager::Get().FinishAllCompilation();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
    F->Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("NPR/RimOriginalSurface")/SourceKind);IFileManager::Get().MakeDirectory(*F->Directory,true);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FPreviewRimFrames>(this,F));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRPreviewSlotParametersTest,"NPR.Editor.PreviewSlotParameters",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FNPRPreviewSlotParametersTest::RunTest(const FString&)
{
    FPreviewScene Scene(FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false));
    auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto* Recipe=LoadObject<UNPRSurfaceRecipe>(nullptr,TEXT("/PrincipledNPR/Surface/SR_DefaultLit_Rim.SR_DefaultLit_Rim"));
    if(!TestTrue(TEXT("Saved assets load"),Sphere&&Recipe))return false;
    auto* Geometry=DuplicateObject<UStaticMesh>(Sphere,GetTransientPackage());
    Geometry->GetStaticMaterials().Add(FStaticMaterial(Recipe->Material));
    FAssetCompilingManager::Get().FinishAllCompilation();
    FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient;
    auto* Actor=Scene.GetWorld()->SpawnActor<AActor>(Spawn);
    auto* Mesh=NewObject<UStaticMeshComponent>(Actor);Actor->SetRootComponent(Mesh);Actor->AddInstanceComponent(Mesh);Mesh->SetStaticMesh(Geometry);Mesh->RegisterComponent();
    auto* Binding=NewObject<UNPRStyleBindingComponent>(Actor);Actor->AddInstanceComponent(Binding);Binding->RegisterComponent();
    auto* Package=NewObject<UNPRBackendStylePackage>();Package->BackendId=TEXT("PreviewSlotTest");
    for(int32 I=0;I<2;++I){FNPRBackendMaterialSlot Slot;Slot.SlotIndex=I;Slot.Material=Recipe->Material;Package->MaterialSlots.Add(Slot);}
    FString Error;if(!TestTrue(TEXT("Two slots bind independently"),Binding->ApplyPackage(Mesh,Package,Error)))return false;
    auto* A=Binding->GetOwnedMaterial(Mesh,0);auto* B=Binding->GetOwnedMaterial(Mesh,1);
    A->SetVectorParameterValue(FName(NPRRimContract::ColorMaterialName),FLinearColor::Red);
    B->SetVectorParameterValue(FName(NPRRimContract::ColorMaterialName),FLinearColor::Green);
    A->SetScalarParameterValue(FName(NPRRimContract::IntensityMaterialName),1);
    B->SetScalarParameterValue(FName(NPRRimContract::IntensityMaterialName),2);
    auto Preview=MakeNPRCharacterPreview();Preview->LoadActor(Actor);Preview->SetRimSolo(true);Preview->SetRimTarget(nullptr,INDEX_NONE);Preview->Refresh();
    auto* PA=Cast<UMaterialInstanceDynamic>(Preview->PreviewMaterial(false,Mesh,0));auto* PB=Cast<UMaterialInstanceDynamic>(Preview->PreviewMaterial(false,Mesh,1));
    if(!TestTrue(TEXT("Different slots never share a preview MID"),PA&&PB&&PA!=PB))return false;
    auto Color=[](UMaterialInstanceDynamic* MID){FLinearColor C;MID->GetVectorParameterValue(FHashedMaterialParameterInfo(FName(NPRRimContract::ColorMaterialName)),C);return C;};
    TestTrue(TEXT("Slot zero stays red"),Color(PA).Equals(FLinearColor::Red));
    TestTrue(TEXT("Slot one stays green"),Color(PB).Equals(FLinearColor::Green));
    A->SetVectorParameterValue(FName(NPRRimContract::ColorMaterialName),FLinearColor::Blue);Preview->Refresh();
    TestTrue(TEXT("Live edit updates only slot zero"),Color(PA).Equals(FLinearColor::Blue)&&Color(PB).Equals(FLinearColor::Green));
    for(int32 I=0;I<20;++I)Preview->Refresh();
    TestTrue(TEXT("Idle frames preserve both preview instances"),Preview->PreviewMaterial(false,Mesh,0)==PA&&Preview->PreviewMaterial(false,Mesh,1)==PB);
    Binding->RestoreMaterials();return true;
}
#endif
