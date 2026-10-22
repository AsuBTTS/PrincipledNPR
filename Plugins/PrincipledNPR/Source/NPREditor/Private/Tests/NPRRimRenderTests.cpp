#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AssetCompilingManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NPRBackendStylePackage.h"
#include "NPRStyleBindingComponent.h"
#include "PreviewScene.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
struct FRimRenderFixture
{
    FPreviewScene Scene{FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false).SetTransactional(false)};
    TStrongObjectPtr<UNPRStyleBindingComponent> Binding{NewObject<UNPRStyleBindingComponent>()};
    UStaticMeshComponent* Mesh = nullptr;
    UMaterialInstanceDynamic* MID = nullptr;
    UMaterialInterface* Original = nullptr;
    USceneCaptureComponent2D* Capture = nullptr;
    UTextureRenderTarget2D* Target = nullptr;
    FString Directory;
    TArray<FColor> OffPixels;
    int32 Variant = 0;
    int32 Delay = 45;
    bool bApplied = false;
};

class FRimCaptureFrames : public IAutomationLatentCommand
{
public:
    FRimCaptureFrames(FAutomationTestBase* InTest, TSharedPtr<FRimRenderFixture> InFixture)
        : Test(InTest), Fixture(MoveTemp(InFixture)) {}

    virtual bool Update() override
    {
        struct FVariant { const TCHAR* Name; double Width; double Softness; float Peak; FLinearColor Color; };
        const FVariant Variants[] = {
            {TEXT("Off"),8,2,0,FLinearColor::White}, {TEXT("Default8"),8,2,1,FLinearColor::White},
            {TEXT("Wide25"),25,2,1,FLinearColor::White}, {TEXT("Hard25"),25,0,1,FLinearColor::White},
            {TEXT("Soft25"),25,10,1,FLinearColor::White}, {TEXT("Red25"),25,2,1,FLinearColor::Red},
            {TEXT("Red25Double"),25,2,2,FLinearColor::Red}, {TEXT("OffAgain"),8,2,0,FLinearColor::White}
        };
        auto& F = *Fixture;
        if (F.Variant == UE_ARRAY_COUNT(Variants))
        {
            F.Binding->RestoreMaterials();
            Test->TestTrue(TEXT("Preview original material restored"), F.Mesh->GetMaterial(0) == F.Original);
            Test->AddInfo(FString::Printf(TEXT("Eight real GPU captures across frames: %s. Image measurements are a separate gate."), *F.Directory));
            Fixture.Reset();
            return true;
        }
        const auto& Variant = Variants[F.Variant];
        if (!F.bApplied)
        {
            FNPRRimParameters Values;
            Values.RimGrazingAngleRad = FMath::DegreesToRadians(Variant.Width);
            Values.RimEdgeHalfWidthRad = FMath::DegreesToRadians(Variant.Softness);
            Values.RimPeakScale = Variant.Peak;
            Values.RimColor = Variant.Color;
            FString Error;
            if (!Test->TestTrue(*FString::Printf(TEXT("%s: production SetRimParameters"), Variant.Name), F.Binding->SetRimParameters(Values, Error))) return true;
            Test->TestTrue(TEXT("Same MID retained across frames and values"), F.Mesh->GetMaterial(0) == F.MID);
            F.bApplied = true;
        }
        F.Scene.GetWorld()->Tick(LEVELTICK_All, 1.f / 60.f);
        F.Scene.GetWorld()->SendAllEndOfFrameUpdates();
        F.Capture->CaptureScene();
        if (--F.Delay > 0) return false;
        FlushRenderingCommands();
        TArray<FColor> Pixels;
        if (!Test->TestTrue(TEXT("GPU readback"), F.Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels))) return true;
        Test->TestEqual(TEXT("Capture resolution"), Pixels.Num(), 512*512);
        if (F.Variant == 0) F.OffPixels = Pixels;
        else if (F.Variant < 7) Test->TestTrue(TEXT("Enabled Rim changes actual pixels from Off"), Pixels != F.OffPixels);
        else Test->TestTrue(TEXT("Returning to Off restores actual pixels"), Pixels == F.OffPixels);
        TArray64<uint8> PNG;
        FImageUtils::PNGCompressImageArray(512,512,Pixels,PNG);
        Test->TestTrue(TEXT("Real GPU PNG saved"), FFileHelper::SaveArrayToFile(PNG, *(F.Directory/(FString(Variant.Name)+TEXT(".png")))));
        ++F.Variant;
        F.bApplied = false;
        F.Delay = 12;
        return false;
    }
private:
    FAutomationTestBase* Test;
    TSharedPtr<FRimRenderFixture> Fixture;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRRimRenderBindingTest, "NPR.Rim.RenderBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNPRRimRenderBindingTest::RunTest(const FString& Arguments)
{
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/PrincipledNPR/RimLight/M_RimLight.M_RimLight"));
    auto* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (!TestTrue(TEXT("Saved material and engine sphere load"), Material && Sphere)) return false;
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();

    // This world is owned by the test. No actors, materials or dirty flags in the user's map change.
    auto Fixture = MakeShared<FRimRenderFixture>();
    auto& Scene = Fixture->Scene;
    auto* Mesh = NewObject<UStaticMeshComponent>();
    Mesh->SetStaticMesh(Sphere);
    Scene.AddComponent(Mesh, FTransform::Identity);
    auto* Binding = Fixture->Binding.Get();
    auto* Package = NewObject<UNPRBackendStylePackage>();
    Package->BackendId = TEXT("IndependentRim");
    FNPRBackendMaterialSlot Slot;
    Slot.Material = Material;
    Package->MaterialSlots.Add(Slot);
    FString Error;
    if (!TestTrue(TEXT("Production ApplyPackage binds preview sphere"), Binding->ApplyPackage(Mesh, Package, Error))) return false;
    auto* MID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
    if (!TestNotNull(TEXT("Production binding creates MID"), MID)) return false;

    auto* Capture = NewObject<USceneCaptureComponent2D>();
    Capture->ProjectionType = ECameraProjectionMode::Orthographic;
    Capture->OrthoWidth = 150.f;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->CaptureSource = SCS_FinalColorLDR;
    Capture->ShowFlags.SetTemporalAA(false);
    Capture->ShowFlags.SetBloom(false);
    Capture->ShowFlags.SetMotionBlur(false);
    Capture->ShowFlags.SetGlobalIllumination(false);
    Capture->ShowFlags.SetReflectionEnvironment(false);
    auto& PP = Capture->PostProcessSettings;
    PP.bOverride_AutoExposureMethod = true;
    PP.AutoExposureMethod = AEM_Manual;
    PP.bOverride_AutoExposureBias = true;
    PP.AutoExposureBias = 0;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    PP.AutoExposureApplyPhysicalCameraExposure = false;
    auto* Target = NewObject<UTextureRenderTarget2D>();
    Target->RenderTargetFormat = RTF_RGBA8;
    Target->ClearColor = FLinearColor::Black;
    Target->InitAutoFormat(512, 512);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target;
    Scene.AddComponent(Capture, FTransform(FRotator::ZeroRotator, FVector(-200,0,0)));

    FAssetCompilingManager::Get().FinishAllCompilation();
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    Fixture->Mesh = Mesh;
    Fixture->MID = MID;
    Fixture->Original = Sphere->GetStaticMaterials()[0].MaterialInterface;
    Fixture->Capture = Capture;
    Fixture->Target = Target;
    Fixture->Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Documentation/RimLight/BindingValidation"));
    IFileManager::Get().MakeDirectory(*Fixture->Directory, true);
    FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FRimCaptureFrames>(this, Fixture));
    return true;
}
#endif
