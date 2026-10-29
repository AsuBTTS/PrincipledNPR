// Project-owned isolated research fixture. Never saves or edits production assets.
#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "AssetCompilingManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/ToonProfile.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/EngineVersion.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PreviewScene.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "UObject/StrongObjectPtr.h"
#include "NPROutlineActorComponent.h"
#include "NPRRimParameters.h"
#include "StaticMeshDescription.h"
#include "StaticMeshResources.h"

namespace ToonGPU
{
constexpr int32 Size=256;
template<class T> T* Node(UMaterial* M)
{
    auto* N=NewObject<T>(M);N->Material=M;M->GetExpressionCollection().AddExpression(N);return N;
}
void Curve(FRuntimeCurveLinearColor& C,const TArray<TPair<float,FLinearColor>>& Keys)
{
    C.ExternalCurve=nullptr;
    for(int32 I=0;I<4;++I)
    {
        C.ColorCurves[I].Reset();
        for(const auto& K:Keys){auto H=C.ColorCurves[I].AddKey(K.Key,K.Value.Component(I));C.ColorCurves[I].SetKeyInterpMode(H,RCIM_Linear);}
    }
}
struct FCase
{
    FString Name,Profile=TEXT("Step0");
    float Yaw=55,Pitch=0,Dir=1,Point=0,Spot=0,Sky=0,Rough=.4f,Spec=0,NormalTilt=0,Rim=0;
    FLinearColor Base=FLinearColor(.8f,.8f,.8f,1);
    bool Block=false,GI=false,Room=false,RedWall=false,Lit=false,Pair=false,SelfShadow=true,MultiSlot=false;
    float BlockerY=-65;
    int32 Frames=20,Outline=0;
    ESceneCaptureSource Source=SCS_SceneColorHDR;
};
struct FFixture
{
    FPreviewScene Scene{FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false).SetTransactional(false)
        .SetCreateDefaultLighting(false).AllowLumenPrimitiveTrackingInPreviewWorld(true)};
    TArray<TStrongObjectPtr<UObject>> Owned;
    TMap<FString,UMaterial*> Mats;TMap<FString,UToonProfile*> Profiles;
    TMap<FString,UMaterialInstanceDynamic*> MIDs;
    TArray<FCase> Cases;
    UStaticMeshComponent *Mesh=nullptr,*Second=nullptr,*Blocker=nullptr,*Floor=nullptr,*Wall=nullptr;
    UDirectionalLightComponent* Directional=nullptr;UPointLightComponent* Point=nullptr;USpotLightComponent* Spot=nullptr;
    USkyLightComponent* Sky=nullptr;USceneCaptureComponent2D* Capture=nullptr;UTextureRenderTarget2D* Target=nullptr;
    UNPROutlineActorComponent* Outline=nullptr;
    UStaticMesh *SphereAsset=nullptr,*MultiAsset=nullptr;
    int32 Index=0,Remaining=0;bool Applied=false;FString Directory,CSV;
    template<class T> T* Keep(T* O){Owned.Emplace(O);return O;}
    UMaterial* Material(const FString& Name,UToonProfile* P,bool Lit=false)
    {
        auto* M=Keep(NewObject<UMaterial>(GetTransientPackage(),FName(*FString::Printf(TEXT("Probe_%s"),*Name))));
        auto* B=Node<UMaterialExpressionVectorParameter>(M);B->ParameterName=TEXT("NPR_BaseColor");B->DefaultValue=FLinearColor(.8f,.8f,.8f,1);
        auto* N=Node<UMaterialExpressionVectorParameter>(M);N->ParameterName=TEXT("ProbeNormal");N->DefaultValue=FLinearColor(0,0,1,0);
        auto* E=Node<UMaterialExpressionVectorParameter>(M);E->ParameterName=TEXT("ProbeEmission");E->DefaultValue=FLinearColor::Black;
        auto* R=Node<UMaterialExpressionScalarParameter>(M);R->ParameterName=TEXT("NPR_Roughness");R->DefaultValue=.4;
        auto* S=Node<UMaterialExpressionScalarParameter>(M);S->ParameterName=TEXT("NPR_Specular");S->DefaultValue=0;
        if(Lit)
        {
            auto* T=Node<UMaterialExpressionSubstrateShadingModels>(M);T->ShadingModelOverride=MSM_DefaultLit;
            T->BaseColor.Connect(0,B);T->Normal.Connect(0,N);T->Roughness.Connect(0,R);T->Specular.Connect(0,S);T->EmissiveColor.Connect(0,E);
            M->GetEditorOnlyData()->FrontMaterial.Connect(0,T);
        }
        else
        {
            auto* T=Node<UMaterialExpressionSubstrateToonBSDF>(M);T->ToonProfile=P;
            T->BaseColor.Connect(0,B);T->Normal.Connect(0,N);T->Roughness.Connect(0,R);T->Specular.Connect(0,S);T->EmissiveColor.Connect(0,E);
            M->GetEditorOnlyData()->FrontMaterial.Connect(0,T);
        }
        M->PostEditChange();Mats.Add(Name,M);return M;
    }
    void Profile(const FString& Name,float Tau,float Width,int32 Kind=0,bool Shadow=false,float Indirect=0)
    {
        auto* P=Keep(NewObject<UToonProfile>());P->Guid=FGuid::NewGuid();auto& S=P->Settings;
        S.bDiffuseRampIncludeShadow=Shadow;S.DiffuseIndirectScale=Indirect;S.SpecularIndirectScale=0;
        S.ShadowExtinctionCoefficient=100;S.ShadowHatchingPatternStrength=0;
        S.DiffuseRampOffsetStrength=0;S.SpecularRampOffsetStrength=0;
        const float U=(Tau+1)*.5f,H=Width*.5f;
        TArray<TPair<float,FLinearColor>> K={{0,FLinearColor(.1f,.1f,.1f,1)},{U-H,FLinearColor(.1f,.1f,.1f,1)},
            {U+H,FLinearColor::White},{1,FLinearColor::White}};
        if(Kind==1)K={{0,FLinearColor(0,0,1,1)},{.32f,FLinearColor(0,0,1,1)},{.34f,FLinearColor(0,1,0,1)},
            {.65f,FLinearColor(0,1,0,1)},{.67f,FLinearColor(1,0,0,1)},{1,FLinearColor(1,0,0,1)}};
        if(Kind==2)K={{0,FLinearColor::Black},{1,FLinearColor::White}};
        if(Kind==3)K={{0,FLinearColor::White},{1,FLinearColor::White}};
        if(Kind==4)K={{0,FLinearColor::Black},{1,FLinearColor::Black}};
        Curve(S.DiffuseRamp,K);
        Curve(S.SpecularRamp,{{0,FLinearColor::Black},{.5f,FLinearColor::Black},{.52f,FLinearColor::White},{1,FLinearColor::White}});
        Profiles.Add(Name,P);Material(Name,P);
    }
    UStaticMeshComponent* Geometry(UStaticMesh* SM,const FTransform& T)
    {
        auto* C=NewObject<UStaticMeshComponent>();C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(SM);
        Scene.AddComponent(C,T);return C;
    }
};

class FFrames:public IAutomationLatentCommand
{
    FAutomationTestBase* Test;TSharedPtr<FFixture> F;
public:
    FFrames(FAutomationTestBase* T,TSharedPtr<FFixture> In):Test(T),F(In){}
    bool Update() override
    {
        if(F->Index>=F->Cases.Num())
        {
            Test->TestTrue(TEXT("Save full configuration CSV"),FFileHelper::SaveStringToFile(F->CSV,*(F->Directory/TEXT("cases.csv"))));
            Test->AddInfo(FString::Printf(TEXT("Completed %d real GPU captures in %s"),F->Cases.Num(),*F->Directory));
            F.Reset();return true;
        }
        const auto& V=F->Cases[F->Index];
        if(!F->Applied)
        {
            UMaterialInstanceDynamic* MID=F->MIDs.FindRef(V.Lit?TEXT("Lit"):V.Profile);
            if(!Test->TestNotNull(TEXT("Case material exists"),MID))return true;
            F->Mesh->SetMaterial(0,MID);F->Mesh->SetWorldLocation(V.Pair?FVector(0,-35,0):FVector::ZeroVector);
            F->Mesh->SetWorldScale3D(V.Pair?FVector(.6):FVector(1));
            F->Mesh->SetStaticMesh(V.MultiSlot?F->MultiAsset:F->SphereAsset);
            F->Mesh->SetMaterial(0,MID);
            if(V.MultiSlot)F->Mesh->SetMaterial(1,F->MIDs[TEXT("SlotRight")]);
            F->Mesh->SetCastShadow(V.SelfShadow);
            F->Second->SetVisibility(V.Pair);F->Second->SetMaterial(0,F->MIDs[TEXT("PairRight")]);
            MID->SetVectorParameterValue(TEXT("NPR_BaseColor"),V.Base);
            MID->SetScalarParameterValue(TEXT("NPR_Roughness"),V.Rough);MID->SetScalarParameterValue(TEXT("NPR_Specular"),V.Spec);
            const float A=FMath::DegreesToRadians(V.NormalTilt);
            MID->SetVectorParameterValue(TEXT("ProbeNormal"),FLinearColor(FMath::Sin(A),0,FMath::Cos(A),0));
            // Existing Surface master parameters. The duplicate has its own immutable research profile.
            MID->SetScalarParameterValue(TEXT("NPR_NormalStrength"),0);
            MID->SetVectorParameterValue(TEXT("NPR_EmissiveColor"),FLinearColor::Black);
            MID->SetScalarParameterValue(TEXT("NPR_AdditionalEmissiveScale"),0);
            MID->SetScalarParameterValue(NPRRimContract::WidthMaterialName,25);
            MID->SetScalarParameterValue(NPRRimContract::SoftnessMaterialName,4);
            MID->SetScalarParameterValue(NPRRimContract::IntensityMaterialName,V.Rim);
            MID->SetVectorParameterValue(NPRRimContract::ColorMaterialName,FLinearColor::Red);
            F->Directional->SetIntensity(V.Dir*PI);F->Directional->SetWorldRotation(FRotator(V.Pitch,V.Yaw,0));
            F->Point->SetIntensity(V.Point*4000);F->Spot->SetIntensity(V.Spot*4000);
            F->Sky->SetIntensity(V.Sky);F->Blocker->SetVisibility(V.Block);
            F->Blocker->SetWorldLocation(FVector(-100,V.BlockerY,0));
            F->Floor->SetVisibility(V.Room);F->Wall->SetVisibility(V.Room);
            auto* WallMID=F->MIDs[TEXT("Wall")];WallMID->SetVectorParameterValue(TEXT("NPR_BaseColor"),V.RedWall?FLinearColor(.9,.01,.01,1):FLinearColor(.7,.7,.7,1));
            F->Capture->ShowFlags.SetGlobalIllumination(V.GI);F->Capture->ShowFlags.SetLumenGlobalIllumination(V.GI);
            F->Capture->ShowFlags.SetSkyLighting(V.Sky>0);
            auto& PP=F->Capture->PostProcessSettings;PP.bOverride_DynamicGlobalIlluminationMethod=true;
            PP.DynamicGlobalIlluminationMethod=V.GI?EDynamicGlobalIlluminationMethod::Lumen:EDynamicGlobalIlluminationMethod::None;
            PP.bOverride_LumenSceneViewDistance=true;PP.LumenSceneViewDistance=1000;
            F->Capture->CaptureSource=V.Source;
            F->Outline->bEnabled=V.Outline>0;F->Outline->BaseSettings.Method=V.Outline==2?ENPROutlineMethod::InvertedHull:ENPROutlineMethod::ScreenSpace;
            Test->TestTrue(TEXT("Outline refresh in isolated fixture"),F->Outline->RefreshOutline());
            FAssetCompilingManager::Get().FinishAllCompilation();MID->EnsureIsComplete();
            if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();FlushRenderingCommands();
            F->Remaining=V.Frames;F->Applied=true;
        }
        F->Scene.GetWorld()->Tick(LEVELTICK_All,1.f/60);F->Scene.GetWorld()->SendAllEndOfFrameUpdates();
        F->Scene.UpdateCaptureContents();F->Capture->CaptureScene();
        if(--F->Remaining>0)return false;
        FlushRenderingCommands();TArray<FLinearColor> Pixels;
        if(!Test->TestTrue(TEXT("Real HDR GPU readback"),F->Target->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(Pixels,FReadSurfaceDataFlags(RCM_MinMax))))return true;
        Test->TestEqual(TEXT("Capture resolution"),Pixels.Num(),Size*Size);
        Test->TestTrue(TEXT("Save linear RGBA float32 readback"),FFileHelper::SaveArrayToFile(
            TArrayView<const uint8>(reinterpret_cast<const uint8*>(Pixels.GetData()),Pixels.Num()*sizeof(FLinearColor)),
            *(F->Directory/(V.Name+TEXT(".rgba32f")))));
        TArray<FColor> Display;Display.Reserve(Pixels.Num());double Sum=0;
        for(const auto& C:Pixels){Display.Add(FLinearColor(C.R,C.G,C.B,1).ToFColorSRGB());Sum+=C.R+C.G+C.B;}
        TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size,Size,Display,PNG);
        Test->TestTrue(TEXT("Save PNG from GPU pixels"),FFileHelper::SaveArrayToFile(PNG,*(F->Directory/(V.Name+TEXT(".png")))));
        F->CSV+=FString::Printf(TEXT("%s,%s,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%d,%d,%d,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%d,%d,%d,%.9g,%d,%.6g\n"),
            *V.Name,*V.Profile,int(V.Source),V.Yaw,V.Pitch,V.Dir,V.Point,V.Spot,V.Sky,int(V.Block),int(V.GI),int(V.Room),int(V.RedWall),
            V.Rough,V.Spec,V.NormalTilt,V.Base.R,V.Base.G,V.Base.B,V.Rim,V.Outline,int(V.Pair),V.Frames,Sum/Pixels.Num(),int(V.SelfShadow),V.BlockerY);
        Test->AddInfo(FString::Printf(TEXT("Capture %d/%d %s mean_rgb_sum=%g"),F->Index+1,F->Cases.Num(),*V.Name,Sum/Pixels.Num()));
        ++F->Index;F->Applied=false;return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FToonGPUResearch,"NPR.Research.ToonGPU",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FToonGPUResearch::RunTest(const FString& Args)
{
    using namespace ToonGPU;auto F=MakeShared<FFixture>();
    F->Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Evidence"));IFileManager::Get().MakeDirectory(*F->Directory,true);
    FString OverrideDirectory;if(FParse::Value(FCommandLine::Get(),TEXT("ToonEvidence="),OverrideDirectory))
    {F->Directory=FPaths::ConvertRelativePathToFull(OverrideDirectory);IFileManager::Get().MakeDirectory(*F->Directory,true);}
    F->CSV=TEXT("name,profile,source,yaw_deg,pitch_deg,dir_pi,point_4000,spot_4000,sky,block,gi,room,redwall,roughness,specular,normal_tilt_deg,base_r,base_g,base_b,rim,outline,pair,warmup_frames,mean_rgb_sum,self_shadow,blocker_y_cm\n");
    F->Profile(TEXT("StepLow"),-.5,.01);F->Profile(TEXT("Step0"),0,.01);F->Profile(TEXT("StepHigh"),.5,.01);
    F->Profile(TEXT("Soft"),0,.3);F->Profile(TEXT("RGB"),0,.01,1);F->Profile(TEXT("Continuous"),0,.01,2);
    F->Profile(TEXT("SoftPositive"),.5f,.15f);
    F->Profile(TEXT("White"),0,.01,3);F->Profile(TEXT("ShadowIn"),0,.01,0,true);
    F->Profile(TEXT("Spec"),0,.01,4);F->Profile(TEXT("Indirect"),0,.01,0,false,1);
    F->Profile(TEXT("Ext10Out"),0,.01);F->Profiles[TEXT("Ext10Out")]->Settings.ShadowExtinctionCoefficient=10;
    F->Profile(TEXT("Ext10In"),0,.01,0,true);F->Profiles[TEXT("Ext10In")]->Settings.ShadowExtinctionCoefficient=10;
    F->Profile(TEXT("Ext0Out"),0,.01);F->Profiles[TEXT("Ext0Out")]->Settings.ShadowExtinctionCoefficient=0;
    F->Profile(TEXT("Ext0In"),0,.01,0,true);F->Profiles[TEXT("Ext0In")]->Settings.ShadowExtinctionCoefficient=0;
    F->Profile(TEXT("SpecSoft"),0,.01,4);
    Curve(F->Profiles[TEXT("SpecSoft")]->Settings.SpecularRamp,{{0,FLinearColor::Black},{.3f,FLinearColor::Black},{.7f,FLinearColor::White},{1,FLinearColor::White}});
    F->Profile(TEXT("SpecColor"),0,.01,4);
    Curve(F->Profiles[TEXT("SpecColor")]->Settings.SpecularRamp,{{0,FLinearColor::Black},{.5f,FLinearColor::Black},{.52f,FLinearColor(0,0,1,1)},{1,FLinearColor(0,0,1,1)}});
    F->Material(TEXT("Lit"),nullptr,true);
    auto* Existing=LoadObject<UMaterial>(nullptr,TEXT("/PrincipledNPR/Surface/M_NPRSurface_Toon.M_NPRSurface_Toon"));
    if(!TestNotNull(TEXT("Existing Surface Toon master available for isolated duplication"),Existing))return false;
    auto* Surface=F->Keep(DuplicateObject<UMaterial>(Existing,GetTransientPackage(),TEXT("ProbeSurface")));
    auto* Root=Cast<UMaterialExpressionSubstrateToonBSDF>(Surface->GetEditorOnlyData()->FrontMaterial.Expression);
    if(!TestNotNull(TEXT("Existing master really uses native Toon"),Root))return false;
    Root->ToonProfile=F->Profiles[TEXT("Step0")];Surface->PostEditChange();F->Mats.Add(TEXT("Surface"),Surface);
    FAssetCompilingManager::Get().FinishAllCompilation();if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
    for(auto& E:F->Mats){E.Value->EnsureIsComplete();F->MIDs.Add(E.Key,F->Keep(UMaterialInstanceDynamic::Create(E.Value,GetTransientPackage())));}
    F->MIDs.Add(TEXT("PairRight"),F->Keep(UMaterialInstanceDynamic::Create(F->Mats[TEXT("Step0")],GetTransientPackage())));
    F->MIDs[TEXT("PairRight")]->SetVectorParameterValue(TEXT("NPR_BaseColor"),FLinearColor(.2,.7,.3,1));
    F->MIDs.Add(TEXT("Wall"),F->Keep(UMaterialInstanceDynamic::Create(F->Mats[TEXT("Lit")],GetTransientPackage())));
    auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(!TestTrue(TEXT("Basic geometry loads"),Sphere&&Cube))return false;
    F->SphereAsset=Sphere;
    auto* Description=F->Keep(UStaticMesh::CreateStaticMeshDescription());
    for(int32 GroupIndex=0;GroupIndex<2;++GroupIndex)
    {
        auto Group=Description->CreatePolygonGroup();Description->SetPolygonGroupMaterialSlotName(Group,FName(*FString::Printf(TEXT("Slot%d"),GroupIndex)));
        // Create centered cubes, then translate just their vertices. At this CL the helper does not apply nonzero Center as a translation.
        const int32 FirstVertex=Description->GetMeshDescription().Vertices().Num();
        FPolygonID Faces[6];Description->CreateCube(FVector::ZeroVector,FVector(25,25,35),Group,Faces[0],Faces[1],Faces[2],Faces[3],Faces[4],Faces[5]);
        auto Positions=Description->GetVertexPositions();
        for(auto ID:Description->GetMeshDescription().Vertices().GetElementIDs())if(ID.GetValue()>=FirstVertex)Positions[ID]+=FVector3f(0,GroupIndex?35.f:-35.f,0);
    }
    F->MultiAsset=F->Keep(NewObject<UStaticMesh>());
    F->MultiAsset->GetStaticMaterials().Add(FStaticMaterial(F->Mats[TEXT("Step0")],TEXT("Slot0"),TEXT("Slot0")));
    F->MultiAsset->GetStaticMaterials().Add(FStaticMaterial(F->Mats[TEXT("StepHigh")],TEXT("Slot1"),TEXT("Slot1")));
    F->MultiAsset->BuildFromStaticMeshDescriptions({Description},false,true);
    TestEqual(TEXT("Two real mesh sections"),F->MultiAsset->GetRenderData()->LODResources[0].Sections.Num(),2);
    TestEqual(TEXT("First section uses slot zero"),F->MultiAsset->GetRenderData()->LODResources[0].Sections[0].MaterialIndex,0);
    TestEqual(TEXT("Second section uses slot one"),F->MultiAsset->GetRenderData()->LODResources[0].Sections[1].MaterialIndex,1);
    F->MIDs.Add(TEXT("SlotRight"),F->Keep(UMaterialInstanceDynamic::Create(F->Mats[TEXT("StepHigh")],GetTransientPackage())));
    F->MIDs[TEXT("SlotRight")]->SetVectorParameterValue(TEXT("NPR_BaseColor"),FLinearColor(.2f,.7f,.3f,1));
    auto* A=F->Scene.GetWorld()->SpawnActor<AActor>();F->Mesh=NewObject<UStaticMeshComponent>(A);F->Mesh->SetStaticMesh(Sphere);
    A->SetRootComponent(F->Mesh);A->AddInstanceComponent(F->Mesh);F->Mesh->SetMobility(EComponentMobility::Movable);F->Mesh->RegisterComponent();
    F->Second=F->Geometry(Sphere,FTransform(FRotator::ZeroRotator,FVector(0,35,0),FVector(.6)));
    F->Blocker=F->Geometry(Cube,FTransform(FRotator::ZeroRotator,FVector(-100,-65,0),FVector(.25,.25,.8)));
    F->Blocker->SetMaterial(0,F->MIDs[TEXT("Lit")]);
    F->Floor=F->Geometry(Cube,FTransform(FRotator::ZeroRotator,FVector(0,0,-65),FVector(6,6,.2)));
    F->Wall=F->Geometry(Cube,FTransform(FRotator::ZeroRotator,FVector(0,100,30),FVector(6,.2,2)));
    F->Floor->SetMaterial(0,F->MIDs[TEXT("Lit")]);F->Wall->SetMaterial(0,F->MIDs[TEXT("Wall")]);
    F->Directional=NewObject<UDirectionalLightComponent>();F->Directional->SetMobility(EComponentMobility::Movable);F->Directional->SetCastShadows(true);
    F->Directional->bTransmission=true;F->Scene.AddComponent(F->Directional,FTransform::Identity);
    F->Point=NewObject<UPointLightComponent>();F->Point->SetMobility(EComponentMobility::Movable);F->Point->SetAttenuationRadius(600);
    F->Point->SetLightColor(FLinearColor(1,.25,.1,1));F->Scene.AddComponent(F->Point,FTransform(FVector(-150,80,50)));
    F->Spot=NewObject<USpotLightComponent>();F->Spot->SetMobility(EComponentMobility::Movable);F->Spot->SetAttenuationRadius(600);
    F->Spot->SetOuterConeAngle(40);F->Spot->SetInnerConeAngle(25);F->Spot->SetLightColor(FLinearColor(.1,.3,1,1));
    FVector SP(-160,-60,40);F->Scene.AddComponent(F->Spot,FTransform((-SP).Rotation(),SP));
    F->Sky=NewObject<USkyLightComponent>();F->Sky->SetMobility(EComponentMobility::Movable);F->Sky->SourceType=SLS_SpecifiedCubemap;
    F->Sky->Cubemap=LoadObject<UTextureCube>(nullptr,TEXT("/Engine/EngineResources/GrayLightTextureCube.GrayLightTextureCube"));
    if(!TestNotNull(TEXT("Known uniform sky cubemap"),F->Sky->Cubemap.Get()))return false;
    F->Sky->bLowerHemisphereIsBlack=false;F->Scene.AddComponent(F->Sky,FTransform::Identity);F->Sky->RecaptureSky();
    F->Capture=NewObject<USceneCaptureComponent2D>();F->Capture->ProjectionType=ECameraProjectionMode::Perspective;F->Capture->FOVAngle=40;
    F->Capture->bCaptureEveryFrame=false;F->Capture->bCaptureOnMovement=false;F->Capture->bAlwaysPersistRenderingState=true;
    F->Capture->ShowFlags.SetTemporalAA(false);F->Capture->ShowFlags.SetBloom(false);F->Capture->ShowFlags.SetMotionBlur(false);
    F->Capture->ShowFlags.SetReflectionEnvironment(false);F->Capture->ShowFlags.SetScreenSpaceReflections(false);
    auto& PP=F->Capture->PostProcessSettings;PP.bOverride_AutoExposureMethod=true;PP.AutoExposureMethod=AEM_Manual;
    PP.bOverride_AutoExposureBias=true;PP.AutoExposureBias=0;PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true;PP.AutoExposureApplyPhysicalCameraExposure=false;
    PP.bOverride_ReflectionMethod=true;PP.ReflectionMethod=EReflectionMethod::None;
    F->Target=F->Keep(NewObject<UTextureRenderTarget2D>());F->Target->RenderTargetFormat=RTF_RGBA16f;
    F->Target->ClearColor=FLinearColor::Black;F->Target->InitAutoFormat(Size,Size);F->Target->UpdateResourceImmediate(true);
    F->Capture->TextureTarget=F->Target;F->Scene.AddComponent(F->Capture,FTransform(FRotator::ZeroRotator,FVector(-230,0,0)));
    F->Outline=NewObject<UNPROutlineActorComponent>(A);F->Outline->bEnabled=false;F->Outline->BaseSettings.WidthPixels=3;
    F->Outline->BaseSettings.HullWidthCm=1.5;F->Outline->BaseSettings.Color=FLinearColor(0,1,0,1);
    F->Outline->HullTemplate=LoadObject<UMaterialInterface>(nullptr,TEXT("/PrincipledNPR/Outline/M_OutlineHull.M_OutlineHull"));
    F->Outline->MaskTemplate=LoadObject<UMaterialInterface>(nullptr,TEXT("/PrincipledNPR/Outline/M_OutlineMask.M_OutlineMask"));
    F->Outline->ScreenTemplate=LoadObject<UMaterialInterface>(nullptr,TEXT("/PrincipledNPR/Outline/M_OutlineScreen.M_OutlineScreen"));
    F->Outline->CollectOwnerMeshes();A->AddInstanceComponent(F->Outline);F->Outline->RegisterComponent();
    auto Add=[&](const TCHAR* Name,const TCHAR* Profile=TEXT("Step0"))->FCase&{FCase V;V.Name=Name;V.Profile=Profile;return F->Cases.Add_GetRef(V);};
    Add(TEXT("Mask"),TEXT("White")).Source=SCS_BaseColor;Add(TEXT("Normals"),TEXT("White")).Source=SCS_Normal;
    Add(TEXT("Dark")).Dir=0;Add(TEXT("StepLow"),TEXT("StepLow"));Add(TEXT("Step0"));Add(TEXT("StepHigh"),TEXT("StepHigh"));
    Add(TEXT("Soft"),TEXT("Soft"));Add(TEXT("RGB"),TEXT("RGB"));Add(TEXT("Continuous"),TEXT("Continuous"));
    Add(TEXT("SoftPositive"),TEXT("SoftPositive"));
    for(int I=0;I<7;++I){auto& V=Add(*FString::Printf(TEXT("Angle%d"),I));V.Yaw=I*30;}
    Add(TEXT("ShadowOutClear")).Yaw=33;auto& B=Add(TEXT("ShadowOutBlocked"));B.Yaw=33;B.Block=true;
    Add(TEXT("ShadowInClear"),TEXT("ShadowIn")).Yaw=33;auto& C=Add(TEXT("ShadowInBlocked"),TEXT("ShadowIn"));C.Yaw=33;C.Block=true;
    auto& LC=Add(TEXT("LitShadowClear"));LC.Lit=true;LC.Yaw=33;auto& LB=Add(TEXT("LitShadowBlocked"));LB.Lit=true;LB.Yaw=33;LB.Block=true;
    auto& P=Add(TEXT("Point"));P.Dir=0;P.Point=1;auto& S=Add(TEXT("Spot"));S.Dir=0;S.Spot=1;
    auto& PS=Add(TEXT("PointSpot"));PS.Dir=0;PS.Point=1;PS.Spot=1;
    auto& All=Add(TEXT("AllLights"));All.Point=1;All.Spot=1;
    auto& Sky0=Add(TEXT("SkyScale0"));Sky0.Dir=0;Sky0.Sky=1;
    auto& Sky1=Add(TEXT("SkyScale1"),TEXT("Indirect"));Sky1.Dir=0;Sky1.Sky=1;
    auto& SkyL=Add(TEXT("SkyLit"));SkyL.Dir=0;SkyL.Sky=1;SkyL.Lit=true;
    for(int I=0;I<4;++I){auto& V=Add(*FString::Printf(TEXT("SpecR%d"),I),TEXT("Spec"));V.Spec=.5;V.Rough=.15f+I*.2f;V.Yaw=25;}
    auto& SB=Add(TEXT("SpecBlue"),TEXT("Spec"));SB.Spec=.5;SB.Rough=.35;SB.Base=FLinearColor(0,0,1,1);SB.Yaw=25;
    auto& NC=Add(TEXT("NormalTilt"));NC.NormalTilt=30;
    auto& BC=Add(TEXT("BaseTint"));BC.Base=FLinearColor(.2,.7,.3,1);
    Add(TEXT("SurfaceBase"),TEXT("Surface"));Add(TEXT("SurfaceRim"),TEXT("Surface")).Rim=1;
    auto& SF=Add(TEXT("SurfaceRimFinal"),TEXT("Surface"));SF.Rim=1;SF.Source=SCS_FinalColorLDR;
    auto& OS=Add(TEXT("SurfaceScreen"),TEXT("Surface"));OS.Rim=1;OS.Outline=1;OS.Source=SCS_FinalColorLDR;
    auto& OH=Add(TEXT("SurfaceHull"),TEXT("Surface"));OH.Rim=1;OH.Outline=2;
    auto& Pair0=Add(TEXT("PairBefore"));Pair0.Pair=true;
    auto& Pair1=Add(TEXT("PairAfter"));Pair1.Pair=true;Pair1.Base=FLinearColor(.8,.1,.1,1);
    auto& Switch=Add(TEXT("SwitchLit"));Switch.Lit=true;
    Add(TEXT("SwitchToon"));Add(TEXT("RepeatStep0"));
    Add(TEXT("SlotBefore")).MultiSlot=true;auto& SlotAfter=Add(TEXT("SlotAfter"));SlotAfter.MultiSlot=true;SlotAfter.Base=FLinearColor(.8f,.1f,.1f,1);
    auto& SlotProfile=Add(TEXT("SlotProfileSwitch"),TEXT("StepHigh"));SlotProfile.MultiSlot=true;
    for(const TCHAR* PName:{TEXT("StepLow"),TEXT("Step0"),TEXT("White")})
    {auto& V=Add(*(FString(PName)+TEXT("NoSelfShadow")),PName);V.SelfShadow=false;}
    auto& RGBFull=Add(TEXT("RGBNoSelfShadow"),TEXT("RGB"));RGBFull.SelfShadow=false;RGBFull.Yaw=90;
    for(const TCHAR* PName:{TEXT("Ext10Out"),TEXT("Ext10In"),TEXT("Ext0Out"),TEXT("Ext0In")})
    {auto& V=Add(PName,PName);V.Yaw=33;V.Block=true;}
    auto& SS=Add(TEXT("SpecSoft"),TEXT("SpecSoft"));SS.Spec=.5;SS.Rough=.35;SS.Yaw=25;
    auto& SC=Add(TEXT("SpecColor"),TEXT("SpecColor"));SC.Spec=.5;SC.Rough=.35;SC.Yaw=25;
    auto& SL=Add(TEXT("SpecIntensityLow"),TEXT("Spec"));SL.Spec=.25;SL.Rough=.35;SL.Yaw=25;
    auto& SH=Add(TEXT("SpecIntensityHigh"),TEXT("Spec"));SH.Spec=1;SH.Rough=.35;SH.Yaw=25;
    auto& L0=Add(TEXT("LocalPointBlocked"));L0.Dir=0;L0.Point=1;L0.Block=true;L0.BlockerY=-35;
    auto& L1=Add(TEXT("LocalSpotBlocked"));L1.Dir=0;L1.Spot=1;L1.Block=true;L1.BlockerY=-35;
    auto& L2=Add(TEXT("LocalBothBlocked"));L2.Dir=0;L2.Point=1;L2.Spot=1;L2.Block=true;L2.BlockerY=-35;
    for(int I=0;I<6;++I)
    {
        auto& V=Add(*FString::Printf(TEXT("GI%d"),I),I==0?TEXT("Step0"):TEXT("Indirect"));
        V.GI=I!=2;V.Room=true;V.RedWall=I==3;V.Lit=I>=4;V.RedWall|=I==5;
        V.Yaw=90;V.Pitch=-35;V.Frames=96;
    }
    auto& GRepeat=Add(TEXT("GIRepeat"),TEXT("Indirect"));GRepeat.GI=true;GRepeat.Room=true;GRepeat.Yaw=90;GRepeat.Pitch=-35;GRepeat.Frames=192;
    auto& GRed=Add(TEXT("GIRedRepeat"),TEXT("Indirect"));GRed.GI=true;GRed.Room=true;GRed.RedWall=true;GRed.Yaw=90;GRed.Pitch=-35;GRed.Frames=192;
    FString Env=FString::Printf(TEXT("engine=%s\ngpu=%s\nrhi=%s\nsize=%d\ncamera=(-230,0,0),FOV=40\nworld=EditorPreview,LumenPrimitiveTracking=true\ncapture=SceneColorHDR,FP16 target,float32 readback; PNG sRGB clipped 0..1\n"),
        *FEngineVersion::Current().ToString(),*GRHIAdapterName,GDynamicRHI?GDynamicRHI->GetName():TEXT("none"),Size);
    const TCHAR* CVs[]={TEXT("r.Substrate"),TEXT("r.Substrate.ProjectGBufferFormat"),TEXT("r.Shadow.Virtual.Enable"),TEXT("r.DynamicGlobalIlluminationMethod"),TEXT("r.RayTracing"),TEXT("r.Substrate.ToonProfile.Resolution"),TEXT("r.Substrate.Experimental.ToonUnifiedDiffuse"),TEXT("r.Substrate.Experimental.ToonReflectionQuantizationEnabled")};
    for(auto CV:CVs)if(auto* Value=IConsoleManager::Get().FindConsoleVariable(CV))Env+=FString::Printf(TEXT("%s=%s\n"),CV,*Value->GetString());
    Env+=FString::Printf(TEXT("utc=%s\n"),*FDateTime::UtcNow().ToIso8601());
    FString ProfileText;
    for(auto& Entry:F->Profiles){ProfileText+=TEXT("PROFILE ")+Entry.Key+TEXT("\n");FToonProfileStruct::StaticStruct()->ExportText(ProfileText,&Entry.Value->Settings,nullptr,nullptr,PPF_None,nullptr);ProfileText+=TEXT("\n");}
    FFileHelper::SaveStringToFile(ProfileText,*(F->Directory/TEXT("profiles.txt")));
    FFileHelper::SaveStringToFile(Env,*(F->Directory/TEXT("environment.txt")));
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFrames>(this,F));return true;
}
#endif
