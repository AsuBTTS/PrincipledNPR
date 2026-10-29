#include "NPRToonBackend.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AssetCompilingManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/ToonProfile.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShared.h"
#include "NPRBackendStylePackage.h"
#include "NPRStyleBindingComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "PreviewScene.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "UObject/StrongObjectPtr.h"

namespace NPRToonRender
{
constexpr int32 Size=256;
void SaveJson(const TSharedRef<FJsonObject>& O,const FString& Path)
{FString Text;auto W=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(O,W);FFileHelper::SaveStringToFile(Text,*Path);}
TSharedPtr<FJsonObject> Stats(){FShaderCompilerStats S;GShaderCompilingManager->GetLocalStats(S);return S.ToJson();}
double NativeDiffuseReference(const FNPRToonParameters& P,ENPRToonResponseShape Shape,double Q,int32 Resolution)
{
    // Source-confirmed native resource contract: samples at (i+.5)/R, FP16 storage,
    // then bilinear clamped lookup. Keep the ideal function error as a separate metric.
    const double Pixel=FMath::Clamp((Q+1)*.5*Resolution-.5,0.0,double(Resolution-1));
    const int32 A=FMath::FloorToInt(Pixel),B=FMath::Min(A+1,Resolution-1);
    auto Sample=[&](int32 I){return double(FFloat16(float(.1+.9*P.EvaluateDiffuse(2.0*(I+.5)/Resolution-1,Shape))).GetFloat());};
    return FMath::Lerp(Sample(A),Sample(B),Pixel-A);
}
struct FCase{FString Name;double Q=1,Center=.2,Width=.08,Beta=8,Softness=.15,Peak=1;bool Spec=false;ENPRToonResponseShape Shape=ENPRToonResponseShape::Hermite;};
struct FFixture
{
    FPreviewScene Scene{FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false).SetTransactional(false).SetCreateDefaultLighting(false)};
    TArray<TStrongObjectPtr<UObject>> Owned;
    UStaticMeshComponent* Mesh=nullptr;UDirectionalLightComponent* Light=nullptr;USceneCaptureComponent2D* Capture=nullptr;
    UTextureRenderTarget2D* Target=nullptr;UToonProfile* Profile=nullptr;UMaterialInstanceConstant* Material=nullptr;
    UNPRStyleBindingComponent* Binding=nullptr;TArray<FCase> Cases;int32 Index=0,Delay=30;bool Applied=false;
    FString Dir;double DiffuseReference=0,SpecularReference=0;TSharedPtr<FJsonObject> Before;
    TArray<TSharedPtr<FJsonValue>> Rows;
    const FMaterialShaderMap* ShaderMap=nullptr;
    template<class T>T* Keep(T* O){Owned.Emplace(O);return O;}
};
class FFrames:public IAutomationLatentCommand
{
    FAutomationTestBase* Test;TSharedPtr<FFixture> F;
public:
    FFrames(FAutomationTestBase* T,TSharedPtr<FFixture> Fixture):Test(T),F(Fixture){}
    bool Update()override
    {
        if(F->Index>=F->Cases.Num())
        {
            const auto After=Stats();auto O=MakeShared<FJsonObject>();O->SetObjectField(TEXT("before"),F->Before);O->SetObjectField(TEXT("after"),After);
            O->SetArrayField(TEXT("captures"),F->Rows);O->SetStringField(TEXT("adapter"),GRHIAdapterName);O->SetStringField(TEXT("utc"),FDateTime::UtcNow().ToIso8601());
            SaveJson(O,F->Dir/TEXT("RenderMetrics.json"));
            Test->TestEqual(TEXT("Hot Profile updates assign zero new shader jobs"),After->GetObjectField(TEXT("Counters"))->GetNumberField(TEXT("JobsAssigned")),F->Before->GetObjectField(TEXT("Counters"))->GetNumberField(TEXT("JobsAssigned")));
            Test->TestEqual(TEXT("Hot Profile updates cause zero material translations"),After->GetObjectField(TEXT("MaterialCounters"))->GetNumberField(TEXT("MaterialTranslateCalls")),F->Before->GetObjectField(TEXT("MaterialCounters"))->GetNumberField(TEXT("MaterialTranslateCalls")));
            Test->AddInfo(FString::Printf(TEXT("%d real GPU plane captures, signed-dot and angular sweeps: %s"),F->Cases.Num(),*F->Dir));F.Reset();return true;
        }
        const auto& C=F->Cases[F->Index];
        if(!F->Applied)
        {
            FNPRToonParameters P;P.TerminatorDotCenter=C.Center;P.TerminatorDotHalfWidth=C.Width;P.SpecHalfAngleRad=FMath::DegreesToRadians(C.Beta);P.SpecEdgeSoftness=C.Softness;P.SpecPeakScale=C.Peak;
            FNPRToonResponseSettings S;S.Shape=C.Shape;if(C.Spec)S.DarkResponse=S.LightResponse=FLinearColor::Black;
            FString Error;if(!Test->TestTrue(*FString(TEXT("Adapter update ")+C.Name),NPRToonBackend::UpdateProfile(F->Profile,P,S,Error)))return true;
            F->Binding->SetScalarParameter(TEXT("NPR_Specular"),C.Spec?1.f:0.f,Error);
            const double Q=C.Spec?FMath::Cos(FMath::DegreesToRadians(C.Q*2)):C.Q;
            F->Light->SetWorldRotation(FRotator(-FMath::RadiansToDegrees(FMath::Asin(Q)),0,0));F->Applied=true;
        }
        F->Scene.GetWorld()->Tick(LEVELTICK_All,1.f/60);F->Scene.GetWorld()->SendAllEndOfFrameUpdates();F->Capture->CaptureScene();
        if(--F->Delay>0)return false;
        FlushRenderingCommands();TArray<FLinearColor> Pixels;
        if(!Test->TestTrue(TEXT("Real HDR GPU readback"),F->Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels,FReadSurfaceDataFlags(RCM_MinMax))))return true;
        double Mean=0;int32 Count=0;bool Finite=true;
        for(int32 Y=110;Y<146;++Y)for(int32 X=110;X<146;++X){auto V=Pixels[Y*Size+X];Finite&=FMath::IsFinite(V.R)&&FMath::IsFinite(V.G)&&FMath::IsFinite(V.B);Mean+=V.R;++Count;}
        Mean/=Count;Test->TestTrue(TEXT("HDR readback remains finite"),Finite);
        if(C.Name==TEXT("DiffuseReference")){F->DiffuseReference=Mean;Test->TestTrue(TEXT("Lit plane has nonzero reference"),Mean>.01);}
        if(C.Name==TEXT("SpecularReference")){F->SpecularReference=Mean;Test->TestTrue(TEXT("Specular reference is visible"),Mean>.001);}
        FNPRToonParameters P;P.TerminatorDotCenter=C.Center;P.TerminatorDotHalfWidth=C.Width;P.SpecHalfAngleRad=FMath::DegreesToRadians(C.Beta);P.SpecEdgeSoftness=C.Softness;P.SpecPeakScale=C.Peak;
        const double Expected=C.Spec?P.EvaluateSpecular(FMath::DegreesToRadians(C.Q))*FMath::Max(0.0,FMath::Cos(FMath::DegreesToRadians(2*C.Q))):.1+.9*P.EvaluateDiffuse(C.Q,C.Shape);
        const double Reference=C.Spec?F->SpecularReference:F->DiffuseReference,Actual=Reference>0?Mean/Reference:0;
        const auto* ResolutionCVar=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Substrate.ToonProfile.Resolution"));
        const int32 Resolution=FMath::Max(64,ResolutionCVar?ResolutionCVar->GetInt():64);
        const double NativeExpected=C.Spec?Expected:NativeDiffuseReference(P,C.Shape,C.Q,Resolution);
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),C.Name);Row->SetNumberField(TEXT("input"),C.Q);Row->SetNumberField(TEXT("mean_red"),Mean);
        Row->SetNumberField(TEXT("normalized"),Actual);Row->SetNumberField(TEXT("expected_reference"),Expected);Row->SetNumberField(TEXT("absolute_error"),FMath::Abs(Actual-Expected));
        Row->SetNumberField(TEXT("native_filtered_reference"),NativeExpected);Row->SetNumberField(TEXT("native_filtered_error"),FMath::Abs(Actual-NativeExpected));Row->SetNumberField(TEXT("profile_resolution"),Resolution);
        Row->SetBoolField(TEXT("specular"),C.Spec);F->Rows.Add(MakeShared<FJsonValueObject>(Row));
        if(C.Name!=TEXT("DiffuseReference")&&C.Name!=TEXT("SpecularReference"))
            Test->TestTrue(*FString(TEXT("Bounded GPU/native sampled response ")+C.Name),FMath::Abs(Actual-NativeExpected)<(C.Spec?.12:.025));
        Test->TestTrue(TEXT("Hot edit preserves compiled ShaderMap"),F->Material->GetMaterial()->GetMaterialResource(GMaxRHIShaderPlatform)->GetGameThreadShaderMap()==F->ShaderMap);
        FFileHelper::SaveArrayToFile(TArrayView<const uint8>((const uint8*)Pixels.GetData(),Pixels.Num()*sizeof(FLinearColor)),*(F->Dir/(C.Name+TEXT(".rgba32f"))));
        TArray<FColor> Display;for(auto V:Pixels)Display.Add(FLinearColor(V.R,V.G,V.B,1).ToFColorSRGB());TArray64<uint8> PNG;
        FImageUtils::PNGCompressImageArray(Size,Size,Display,PNG);FFileHelper::SaveArrayToFile(PNG,*(F->Dir/(C.Name+TEXT(".png"))));
        ++F->Index;F->Delay=2;F->Applied=false;return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRToonRenderTest,"NPR.Toon.Render",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FNPRToonRenderTest::RunTest(const FString&)
{
    using namespace NPRToonRender;auto F=MakeShared<FFixture>();
    auto* Source=LoadObject<UMaterialInterface>(nullptr,TEXT("/PrincipledNPR/Surface/M_NPRSurface_Toon.M_NPRSurface_Toon"));
    auto* Inputs=F->Keep(UMaterialInstanceDynamic::Create(Source,GetTransientPackage()));Inputs->SetVectorParameterValue(TEXT("NPR_BaseColor"),FLinearColor::White);
    Inputs->SetScalarParameterValue(TEXT("NPR_NormalStrength"),0);Inputs->SetScalarParameterValue(TEXT("NPR_EmissiveScale"),0);
    Inputs->SetScalarParameterValue(TEXT("NPR_AdditionalEmissiveScale"),0);Inputs->SetScalarParameterValue(TEXT("边缘光强度"),0);
    FNPRToonParameters P;FNPRToonResponseSettings S;FString Error;
    F->Material=F->Keep(NPRToonBackend::BuildPreview(Inputs,ENPRShadingModel::SubstrateToon,P,S,F->Profile,Error));
    if(!TestNotNull(*FString(TEXT("Production Toon adapter creates render material: ")+Error),F->Material))return false;
    F->Mesh=NewObject<UStaticMeshComponent>();F->Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane")));F->Mesh->SetCastShadow(false);F->Scene.AddComponent(F->Mesh,FTransform::Identity);
    F->Binding=F->Keep(NewObject<UNPRStyleBindingComponent>());auto* Bundle=F->Keep(NewObject<UNPRBackendStylePackage>());
    auto& Slot=Bundle->MaterialSlots.AddDefaulted_GetRef();Slot.Material=F->Material;
    if(!TestTrue(TEXT("Runtime package binder drives the rendered mesh"),F->Binding->ApplyPackage(F->Mesh,Bundle,Error)))return false;
    F->Light=NewObject<UDirectionalLightComponent>();F->Light->SetMobility(EComponentMobility::Movable);F->Light->SetIntensity(PI);F->Scene.AddComponent(F->Light,FTransform(FRotator(-90,0,0)));
    F->Capture=NewObject<USceneCaptureComponent2D>();F->Capture->ProjectionType=ECameraProjectionMode::Orthographic;F->Capture->OrthoWidth=120;
    F->Capture->CaptureSource=SCS_SceneColorHDR;F->Capture->bCaptureEveryFrame=false;F->Capture->bCaptureOnMovement=false;F->Capture->bAlwaysPersistRenderingState=true;
    F->Capture->ShowFlags.SetTemporalAA(false);F->Capture->ShowFlags.SetBloom(false);F->Capture->ShowFlags.SetMotionBlur(false);F->Capture->ShowFlags.SetGlobalIllumination(false);
    F->Capture->ShowFlags.SetSkyLighting(false);F->Capture->ShowFlags.SetReflectionEnvironment(false);
    auto& PP=F->Capture->PostProcessSettings;PP.bOverride_AutoExposureMethod=true;PP.AutoExposureMethod=AEM_Manual;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true;PP.AutoExposureApplyPhysicalCameraExposure=false;
    PP.bOverride_DynamicGlobalIlluminationMethod=true;PP.DynamicGlobalIlluminationMethod=EDynamicGlobalIlluminationMethod::None;
    F->Target=F->Keep(NewObject<UTextureRenderTarget2D>());F->Target->RenderTargetFormat=RTF_RGBA16f;F->Target->InitAutoFormat(Size,Size);F->Target->UpdateResourceImmediate(true);F->Capture->TextureTarget=F->Target;
    F->Scene.AddComponent(F->Capture,FTransform(FRotator(-90,0,0),FVector(0,0,200)));
    F->Dir=FPaths::ProjectSavedDir()/TEXT("ToonValidation/Render");IFileManager::Get().MakeDirectory(*F->Dir,true);
    F->Cases.Add({TEXT("DiffuseReference"),1});
    for(auto Shape:{ENPRToonResponseShape::Linear,ENPRToonResponseShape::Hermite})for(int32 I=0;I<5;++I)
    {FCase C;C.Name=FString::Printf(TEXT("Diffuse_%d_%d"),int32(Shape),I);C.Q=.12+.04*I;C.Shape=Shape;F->Cases.Add(C);}
    FCase Hard;Hard.Name=TEXT("HardLower");Hard.Q=.1;Hard.Width=0;F->Cases.Add(Hard);Hard.Name=TEXT("HardUpper");Hard.Q=.3;F->Cases.Add(Hard);
    FCase Edge;Edge.Name=TEXT("ClippedBoundary");Edge.Q=1;Edge.Center=.95;Edge.Width=.2;Edge.Shape=ENPRToonResponseShape::Linear;F->Cases.Add(Edge);
    FCase Spec;Spec.Name=TEXT("SpecularReference");Spec.Q=0;Spec.Spec=true;F->Cases.Add(Spec);
    for(double Beta:{8.0,18.0})for(int32 I=0;I<3;++I)
    {FCase C;C.Name=FString::Printf(TEXT("Specular_%d_%d"),int32(Beta),I);C.Spec=true;C.Beta=Beta;C.Q=Beta+(I-1)*Beta*.15*.95;F->Cases.Add(C);}
    Spec.Name=TEXT("SpecularDouble");Spec.Peak=2;F->Cases.Add(Spec);Spec.Name=TEXT("SpecularZero");Spec.Peak=0;F->Cases.Add(Spec);
    FAssetCompilingManager::Get().FinishAllCompilation();GShaderCompilingManager->FinishAllCompilation();F->Material->EnsureIsComplete();
    F->Before=Stats();F->ShaderMap=F->Material->GetMaterial()->GetMaterialResource(GMaxRHIShaderPlatform)->GetGameThreadShaderMap();
    ADD_LATENT_AUTOMATION_COMMAND(FFrames(this,F));return true;
}
#endif
