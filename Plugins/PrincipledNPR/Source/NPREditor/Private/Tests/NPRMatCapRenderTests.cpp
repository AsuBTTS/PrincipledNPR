#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionPixelNormalWS.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NPRMatCapGraph.h"
#include "NPRRimMaterialBuilder.h"
#include "PreviewScene.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
constexpr int32 MatCapSize = 256;

UTexture2D* MakeMatCapProbeTexture(bool bGradient, float Mask = 1.f)
{
    constexpr int32 Size = 64;
    TArray<FColor> Pixels;
    Pixels.Reserve(Size * Size);
    for (int32 Y = 0; Y < Size; ++Y)
    {
        for (int32 X = 0; X < Size; ++X)
        {
            const float U = (X + .5f) / Size, V = (Y + .5f) / Size;
            Pixels.Add((bGradient ? FLinearColor(U, V, .25f) : FLinearColor(Mask, Mask, Mask)).ToFColorSRGB());
        }
    }
    auto* Texture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, NAME_None,
        MakeArrayView(reinterpret_cast<const uint8*>(Pixels.GetData()), Pixels.Num() * sizeof(FColor)));
    Texture->NeverStream = true;
    Texture->SRGB = true;
    Texture->UpdateResource();
    return Texture;
}

struct FMatCapProbe
{
    FPreviewScene Scene{FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false).SetTransactional(false)};
    TStrongObjectPtr<UMaterial> Material;
    TStrongObjectPtr<UMaterialInstanceConstant> Surface;
    TStrongObjectPtr<UMaterialInstanceConstant> UpgradedSurface;
    TStrongObjectPtr<UMaterialInstanceDynamic> UnlitMID;
    TStrongObjectPtr<UMaterialInstanceDynamic> ToonMID;
    TStrongObjectPtr<UMaterialInstanceDynamic> UpgradedMID;
    TStrongObjectPtr<UTexture2D> Gradient{MakeMatCapProbeTexture(true)};
    TStrongObjectPtr<UTexture2D> BlackMask{MakeMatCapProbeTexture(false, 0)};
    UStaticMesh* Sphere = nullptr;
    UStaticMesh* Plane = nullptr;
    UStaticMeshComponent* Mesh = nullptr;
    USceneCaptureComponent2D* Capture = nullptr;
    UTextureRenderTarget2D* Target = nullptr;
    TMap<FName, TArray<FLinearColor>> Frames;
    FString Directory;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FNPRCaptureMatCapProbe, FAutomationTestBase*, Test, TSharedPtr<FMatCapProbe>, Probe);

bool FNPRCaptureMatCapProbe::Update()
{
    enum class ESurface { Sphere, Plane, ToonSphere, UpgradedToonSphere };
    struct FCase
    {
        const TCHAR* Name;
        float Weight = 1.f;
        float Perspective = 0.f;
        bool bBlackMask = false;
        bool bOffAxis = false;
        ESurface Surface = ESurface::Sphere;
        float CameraYaw = 0.f;
        float NormalStrength = 0.f;
        float NormalInfluence = 1.f;
        TOptional<float> ExposureEV;
    };
    static const FCase Cases[] = {
        {TEXT("Weight0"), 0},
        {TEXT("WeightHalf"), .5f},
        {TEXT("Weight1")},
        {.Name = TEXT("Masked"), .bBlackMask = true},
        {.Name = TEXT("OffAxisFixed"), .bOffAxis = true},
        {.Name = TEXT("OffAxisCorrected"), .Perspective = 1, .bOffAxis = true},
        {.Name = TEXT("CameraYawMinus30"), .Surface = ESurface::Plane, .CameraYaw = -30},
        {.Name = TEXT("CameraYaw0"), .Surface = ESurface::Plane},
        {.Name = TEXT("CameraYawPlus30"), .Surface = ESurface::Plane, .CameraYaw = 30},
        {.Name = TEXT("ToonRim"), .Weight = 0, .Surface = ESurface::ToonSphere},
        {.Name = TEXT("ToonRimMatCap"), .Surface = ESurface::ToonSphere},
        {.Name = TEXT("NormalMapBaseline"), .Weight = 0, .Surface = ESurface::ToonSphere, .NormalStrength = 1},
        {.Name = TEXT("NormalMapOn"), .Surface = ESurface::ToonSphere, .NormalStrength = 1},
        {.Name = TEXT("NormalMapExcluded"), .Surface = ESurface::ToonSphere, .NormalStrength = 1, .NormalInfluence = 0},
        {.Name = TEXT("NormalMapUpgraded"), .Surface = ESurface::UpgradedToonSphere, .NormalStrength = 1},
        {.Name = TEXT("ExposureMinus1EV"), .ExposureEV = -1.f},
        {.Name = TEXT("ExposurePlus1EV"), .ExposureEV = 1.f},
    };
    const int32 Index = Probe->Frames.Num();
    if (Index < UE_ARRAY_COUNT(Cases))
    {
        const FCase& Case = Cases[Index];
        const bool bPlane = Case.Surface == ESurface::Plane;
        auto* MID = Probe->UnlitMID.Get();
        if (Case.Surface == ESurface::ToonSphere) MID = Probe->ToonMID.Get();
        if (Case.Surface == ESurface::UpgradedToonSphere) MID = Probe->UpgradedMID.Get();
        MID->SetScalarParameterValue(NPRMatCapGraph::Weight, Case.Weight);
        MID->SetScalarParameterValue(NPRMatCapGraph::Perspective, Case.Perspective);
        MID->SetScalarParameterValue(NPRMatCapGraph::NormalInfluence, Case.NormalInfluence);
        MID->SetTextureParameterValue(NPRMatCapGraph::Mask, Case.bBlackMask ? Probe->BlackMask.Get() :
            LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")));
        if (MID != Probe->UnlitMID.Get()) MID->SetScalarParameterValue(TEXT("NPR_NormalStrength"), Case.NormalStrength);
        Probe->Mesh->SetStaticMesh(bPlane ? Probe->Plane : Probe->Sphere);
        Probe->Mesh->SetMaterial(0, MID);
        Probe->Mesh->SetRelativeRotation(bPlane ? FRotationMatrix::MakeFromZ(-FVector::ForwardVector).Rotator() : FRotator::ZeroRotator);
        const FRotator ViewRotation(0, Case.CameraYaw, 0);
        Probe->Capture->SetWorldLocationAndRotation(-200 * ViewRotation.Vector(), ViewRotation);
        Probe->Capture->ProjectionType = Case.bOffAxis ? ECameraProjectionMode::Perspective : ECameraProjectionMode::Orthographic;
        Probe->Capture->CaptureSource = Case.ExposureEV.IsSet() ? SCS_FinalColorHDR : SCS_SceneColorHDR;
        Probe->Capture->PostProcessSettings.AutoExposureBias = Case.ExposureEV.Get(0.f);
        Probe->Mesh->SetRelativeLocation(Case.bOffAxis ? FVector(0, 65, 0) : FVector::ZeroVector);
        Probe->Scene.GetWorld()->SendAllEndOfFrameUpdates();
        Probe->Capture->CaptureScene();
        FlushRenderingCommands();

        auto* Resource = Probe->Target->GameThread_GetRenderTargetResource();
        auto& Frame = Probe->Frames.Add(FName(Case.Name));
        if (!Test->TestTrue(TEXT("Linear GPU readback succeeded"),
            Resource->ReadLinearColorPixels(Frame, FReadSurfaceDataFlags(RCM_MinMax)) && Frame.Num() == MatCapSize * MatCapSize))
        {
            return true;
        }
        TArray<FColor> Preview;
        Preview.Reserve(Frame.Num());
        for (const FLinearColor& Pixel : Frame)
        {
            Preview.Add(Pixel.CopyWithNewOpacity(1).ToFColorSRGB());
        }
        TArray64<uint8> PNG;
        FImageUtils::PNGCompressImageArray(MatCapSize, MatCapSize, Preview, PNG);
        Test->TestTrue(TEXT("GPU capture written"), FFileHelper::SaveArrayToFile(PNG, *(Probe->Directory / (FString(Case.Name) + TEXT(".png")))));
        return false;
    }

    const auto Pixel = [&](FName Frame, int32 X = 128, int32 Y = 128) { return Probe->Frames.FindChecked(Frame)[Y * MatCapSize + X]; };
    const FLinearColor Off = Pixel(TEXT("Weight0")), Half = Pixel(TEXT("WeightHalf")), On = Pixel(TEXT("Weight1"));
    const FLinearColor Masked = Pixel(TEXT("Masked"));
    const FLinearColor Delta = Half - FMath::Lerp(Off, On, .5f);
    const double WeightError = FMath::Max3(FMath::Abs(Delta.R), FMath::Abs(Delta.G), FMath::Abs(Delta.B));
    const double Left = Pixel(TEXT("Weight1"), 86).R, Right = Pixel(TEXT("Weight1"), 170).R;
    const double Top = Pixel(TEXT("Weight1"), 128, 86).G, Bottom = Pixel(TEXT("Weight1"), 128, 170).G;
    const FLinearColor Fixed = Pixel(TEXT("OffAxisFixed"), 170), Corrected = Pixel(TEXT("OffAxisCorrected"), 170);
    const FLinearColor CombinedDelta = Pixel(TEXT("ToonRimMatCap")) - Pixel(TEXT("ToonRim"));
    Test->TestTrue(TEXT("Camera-facing surface reads center of texture"), FMath::IsNearlyEqual(On.R, .5f, .035f) && FMath::IsNearlyEqual(On.G, .5f, .035f) && FMath::IsNearlyEqual(On.B, .25f, .035f));
    Test->TestTrue(TEXT("U increases to camera right"), Right > Left + .2);
    Test->TestTrue(TEXT("V increases down the texture"), Bottom > Top + .2);
    Test->TestTrue(TEXT("Half-weight matches half of actual full contribution"), Half.Equals(FMath::Lerp(Off, On, .5f), .003f));
    Test->TestTrue(TEXT("Black mask restores baseline"), Masked.Equals(Off, .002f));
    Test->TestTrue(TEXT("Off-axis projection moves lookup toward center"), FMath::Abs(Corrected.R - .5f) + .08f < FMath::Abs(Fixed.R - .5f));
    Test->TestTrue(TEXT("Actual Toon plus Rim renders the same MatCap increment"), CombinedDelta.Equals(On - Off, .025f));
    const float YawMinus = Pixel(TEXT("CameraYawMinus30")).R;
    const float YawZero = Pixel(TEXT("CameraYaw0")).R;
    const float YawPlus = Pixel(TEXT("CameraYawPlus30")).R;
    Test->TestEqual(TEXT("Stationary plane at -30 degrees samples expected U"), YawMinus, .25f, .025f);
    Test->TestEqual(TEXT("Stationary plane at 0 degrees samples expected U"), YawZero, .5f, .025f);
    Test->TestEqual(TEXT("Stationary plane at +30 degrees samples expected U"), YawPlus, .75f, .025f);

    // Subtract each material's baseline so the normal map's shading change cannot pass as a MatCap shift.
    const FLinearColor MappedBaseline = Pixel(TEXT("NormalMapBaseline"));
    const FLinearColor Fine = Pixel(TEXT("NormalMapOn")) - MappedBaseline;
    const FLinearColor Smooth = Pixel(TEXT("NormalMapExcluded")) - MappedBaseline;
    Test->TestTrue(TEXT("Surface Toon normal map shifts MatCap lookup"), FMath::Max(FMath::Abs(Fine.R - CombinedDelta.R), FMath::Abs(Fine.G - CombinedDelta.G)) > .1f);
    Test->TestTrue(TEXT("Zero MatCap normal influence restores geometry lookup"), Smooth.Equals(CombinedDelta, .025f));
    Test->TestTrue(TEXT("Upgraded graph renders the same normal-mapped MatCap as a fresh graph"),
        Pixel(TEXT("NormalMapUpgraded")).Equals(Pixel(TEXT("NormalMapOn")), .01f));

    const FLinearColor Dark = Pixel(TEXT("ExposureMinus1EV"));
    const FLinearColor Bright = Pixel(TEXT("ExposurePlus1EV"));
    Test->TestTrue(TEXT("Final linear HDR exposes the MatCap contribution by two stops"), Dark.B > .02f &&
        FMath::IsNearlyEqual(Dark.R * 4, Bright.R, .04f) && FMath::IsNearlyEqual(Dark.G * 4, Bright.G, .04f) &&
        FMath::IsNearlyEqual(Dark.B * 4, Bright.B, .04f));
    const FString Metrics = FString::Printf(TEXT("{\n  \"center\": [%g, %g, %g],\n  \"half\": [%g, %g, %g],\n  \"half_error\": %g,\n  \"horizontal\": [%g, %g],\n  \"vertical\": [%g, %g],\n  \"off_axis_fixed\": %g,\n  \"off_axis_corrected\": %g,\n  \"camera_yaw_u_minus30_0_plus30\": [%g, %g, %g],\n  \"normal_flat_uv\": [%g, %g],\n  \"normal_mapped_uv\": [%g, %g],\n  \"normal_excluded_uv\": [%g, %g],\n  \"exposure_minus1_ev\": [%g, %g, %g],\n  \"exposure_plus1_ev\": [%g, %g, %g]\n}\n"),
        On.R, On.G, On.B, Half.R, Half.G, Half.B, WeightError, Left, Right, Top, Bottom, Fixed.R, Corrected.R,
        YawMinus, YawZero, YawPlus, CombinedDelta.R, CombinedDelta.G, Fine.R, Fine.G, Smooth.R, Smooth.G,
        Dark.R, Dark.G, Dark.B, Bright.R, Bright.G, Bright.B);
    Test->AddInfo(Metrics);
    Test->TestTrue(TEXT("GPU metrics written"), FFileHelper::SaveStringToFile(Metrics, *(Probe->Directory / TEXT("Metrics.json"))));
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRMatCapProjectionTest, "NPR.MatCap.ProjectionGPU",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNPRMatCapProjectionTest::RunTest(const FString& Arguments)
{
    auto Probe = MakeShared<FMatCapProbe>();
    Probe->Directory = FPaths::ProjectSavedDir() / TEXT("NPR/MatCap/Projection");
    IFileManager::Get().MakeDirectory(*Probe->Directory, true);
    Probe->Material.Reset(NewObject<UMaterial>());
    Probe->Material->SetShadingModel(MSM_Unlit);
    auto* Contribution = NPRMatCapGraph::Add(*Probe->Material);
    Probe->Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Probe->Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    auto* Normal = LoadObject<UTexture2D>(nullptr, TEXT("/PrincipledNPR/Surface/T_SurfaceValidationNormal.T_SurfaceValidationNormal"));
    if (!TestTrue(TEXT("Probe graph, meshes and normal map available"), Contribution && Probe->Sphere && Probe->Plane && Normal))
    {
        return false;
    }
    Probe->Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Contribution);
    Probe->Material->PostEditChange();
    Probe->Material->EnsureIsComplete();
    FString Error;
    auto* Toon = LoadObject<UMaterialInterface>(nullptr, TEXT("/PrincipledNPR/Surface/M_NPRSurface_Toon.M_NPRSurface_Toon"));
    TStrongObjectPtr<UMaterialInstanceConstant> Rim(NPRRimMaterialBuilder::Build(Toon, ENPRRimShadingModel::KeepOriginal, Error));
    if (!TestNotNull(*Error, Rim.Get())) return false;
    Probe->Surface.Reset(NPRMatCapMaterialBuilder::Build(Rim.Get(), Error));
    if (!TestNotNull(*Error, Probe->Surface.Get())) return false;
    TStrongObjectPtr<UMaterial> Legacy(DuplicateObject<UMaterial>(Probe->Surface->GetMaterial(), GetTransientPackage()));
    // Recreate the previous generated graph and exercise the editor's rebuild path.
    for (UMaterialExpression* Expression : Legacy->GetExpressions())
    {
        auto* Blend = Cast<UMaterialExpressionLinearInterpolate>(Expression);
        const auto* Influence = Blend ? Cast<UMaterialExpressionScalarParameter>(Blend->Alpha.Expression) : nullptr;
        if (!Influence || Influence->ParameterName != NPRMatCapGraph::NormalInfluence) continue;
        auto* NormalNode = NewObject<UMaterialExpressionPixelNormalWS>(Legacy.Get());
        NormalNode->Material = Legacy.Get();
        Legacy->GetExpressionCollection().AddExpression(NormalNode);
        Blend->B.Connect(0, NormalNode);
        break;
    }
    Legacy->PostEditChange();
    Legacy->EnsureIsComplete();
    if (!TestTrue(TEXT("Previous graph identified for upgrade"), NPRMatCapGraph::NeedsSubstrateNormalUpgrade(*Legacy))) return false;
    Probe->UpgradedSurface.Reset(NPRMatCapMaterialBuilder::Build(Legacy.Get(), Error));
    if (!TestNotNull(*Error, Probe->UpgradedSurface.Get())) return false;
    TestTrue(TEXT("Upgrade leaves source intact and repairs the derived graph"), NPRMatCapGraph::NeedsSubstrateNormalUpgrade(*Legacy) &&
        !NPRMatCapGraph::NeedsSubstrateNormalUpgrade(*Probe->UpgradedSurface->GetMaterial()));

    Probe->UnlitMID.Reset(UMaterialInstanceDynamic::Create(Probe->Material.Get(), GetTransientPackage()));
    Probe->UnlitMID->SetTextureParameterValue(NPRMatCapGraph::Texture, Probe->Gradient.Get());
    Probe->ToonMID.Reset(UMaterialInstanceDynamic::Create(Probe->Surface.Get(), GetTransientPackage()));
    Probe->UpgradedMID.Reset(UMaterialInstanceDynamic::Create(Probe->UpgradedSurface.Get(), GetTransientPackage()));
    for (auto* MID : {Probe->ToonMID.Get(), Probe->UpgradedMID.Get()})
    {
        MID->SetTextureParameterValue(NPRMatCapGraph::Texture, Probe->Gradient.Get());
        MID->SetTextureParameterValue(TEXT("NPR_NormalTexture"), Normal);
        MID->SetScalarParameterValue(NPRRimContract::IntensityMaterialName, .6f);
    }
    Probe->Mesh = NewObject<UStaticMeshComponent>();
    Probe->Mesh->SetStaticMesh(Probe->Sphere);
    Probe->Mesh->SetMaterial(0, Probe->UnlitMID.Get());
    Probe->Scene.AddComponent(Probe->Mesh, FTransform::Identity);
    Probe->Capture = NewObject<USceneCaptureComponent2D>();
    Probe->Capture->CaptureSource = SCS_SceneColorHDR;
    Probe->Capture->OrthoWidth = 150;
    Probe->Capture->FOVAngle = 90;
    Probe->Capture->bCaptureEveryFrame = false;
    Probe->Capture->bCaptureOnMovement = false;
    Probe->Capture->bAlwaysPersistRenderingState = true;
    Probe->Capture->ShowFlags.SetTemporalAA(false);
    Probe->Capture->ShowFlags.SetBloom(false);
    Probe->Capture->ShowFlags.SetGlobalIllumination(false);
    Probe->Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
    Probe->Capture->PostProcessSettings.AutoExposureMethod = AEM_Manual;
    Probe->Capture->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    Probe->Capture->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
    Probe->Capture->PostProcessSettings.bOverride_AutoExposureBias = true;
    Probe->Target = NewObject<UTextureRenderTarget2D>();
    Probe->Target->RenderTargetFormat = RTF_RGBA16f;
    Probe->Target->ClearColor = FLinearColor::Black;
    Probe->Target->InitAutoFormat(MatCapSize, MatCapSize);
    Probe->Target->UpdateResourceImmediate(true);
    Probe->Capture->TextureTarget = Probe->Target;
    Probe->Scene.AddComponent(Probe->Capture, FTransform(FRotator::ZeroRotator, FVector(-200, 0, 0)));
    if (GShaderCompilingManager)
    {
        GShaderCompilingManager->FinishAllCompilation();
    }
    ADD_LATENT_AUTOMATION_COMMAND(FNPRCaptureMatCapProbe(this, MoveTemp(Probe)));
    return true;
}
#endif
