#include "NPRStyleBindingComponent.h"
#include "NPRStyleProfile.h"
#include "NPRSurfaceRecipe.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRRimProfileBindingTest, "NPR.Rim.ProfileBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPRRimProfileBindingTest::RunTest(const FString&)
{
    auto* Recipe = LoadObject<UNPRSurfaceRecipe>(nullptr,
        TEXT("/PrincipledNPR/Surface/SR_DefaultLit_Rim.SR_DefaultLit_Rim"));
    if (!TestNotNull(TEXT("Prebuilt Surface recipe loads"), Recipe)) return false;
    UMaterialInterface* Original = UMaterial::GetDefaultMaterial(MD_Surface);
    auto* Geometry = NewObject<UStaticMesh>();
    Geometry->GetStaticMaterials().Add(FStaticMaterial(Original));
    Geometry->GetStaticMaterials().Add(FStaticMaterial(Original));
    auto* Mesh = NewObject<UStaticMeshComponent>();
    Mesh->SetStaticMesh(Geometry);
    auto* Profile = NewObject<UNPRStyleProfile>();
    FNPRFloatOverride& Peak = Profile->MaterialOverrides.FindOrAdd(ENPRMaterialKind::Face)
        .Values.FindOrAdd(NPRRimContract::IntensityId);
    Peak.bIsSet = true;
    Peak.Value = 1.5;
    FNPRColorOverride& Color = Profile->MaterialOverrides.FindOrAdd(ENPRMaterialKind::Face)
        .Colors.FindOrAdd(NPRRimContract::ColorId);
    Color.bIsSet = true;
    Color.Value = FLinearColor(0.f, 0.f, 0.f, 0.f);

    auto* Binding = NewObject<UNPRStyleBindingComponent>();
    Binding->RimMesh = Mesh;
    Binding->RimSlotIndex = 1;
    Binding->RimProfile = Profile;
    Binding->RimRecipe = Recipe;
    Binding->RimMaterialKind = ENPRMaterialKind::Face;
    FString Error;
    TestFalse(TEXT("Unconfigured source rejects without replacing material"),
        Binding->ApplyConfiguredRim(Error));
    TestTrue(TEXT("Failed binding keeps both original slots"),
        Mesh->GetMaterial(0) == Original && Mesh->GetMaterial(1) == Original);

    Binding->RimSurfaceInputs.bConfigured = true;
    Binding->RimSurfaceInputs.BaseColor = FLinearColor(.25f,.5f,.75f,1.f);
    Binding->RimSurfaceInputs.EmissiveColor = FLinearColor(.1f,.2f,.3f,1.f);
    if (!TestTrue(TEXT("Profile resolves and composes one slot"),
        Binding->ApplyConfiguredRim(Error))) return false;
    TestTrue(TEXT("Other slot untouched"), Mesh->GetMaterial(0) == Original);
    TestTrue(TEXT("Selected slot owns a MID"), Binding->IsRimBoundTo(Mesh,1));
    auto* MID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(1));
    if (!TestNotNull(TEXT("Composed MID exists"), MID)) return false;
    float Scalar = -1.f;
    MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("NPR_NormalStrength")),Scalar);
    TestEqual(TEXT("Null normal texture cannot inherit detailed recipe DefaultNormal"),Scalar,0.f);
    FLinearColor Vector;
    TestTrue(TEXT("Rim intensity available"), MID->GetScalarParameterValue(
        FHashedMaterialParameterInfo(FName(NPRRimContract::IntensityMaterialName)),Scalar));
    TestEqual(TEXT("Material-kind override reaches MID"),Scalar,1.5f);
    TestTrue(TEXT("Rim RGB available"),MID->GetVectorParameterValue(
        FHashedMaterialParameterInfo(FName(NPRRimContract::ColorMaterialName)),Vector));
    TestTrue(TEXT("Explicit black is retained"),Vector.Equals(FLinearColor::Black));
    TestTrue(TEXT("Base color available"),MID->GetVectorParameterValue(
        FHashedMaterialParameterInfo(FName(TEXT("NPR_BaseColor"))),Vector));
    TestTrue(TEXT("Explicit base color reaches MID"),Vector.Equals(FLinearColor(.25f,.5f,.75f,1.f)));
    TestTrue(TEXT("Original emissive available"),MID->GetVectorParameterValue(
        FHashedMaterialParameterInfo(FName(TEXT("NPR_EmissiveColor"))),Vector));
    TestTrue(TEXT("Original emissive reaches MID"),Vector.Equals(FLinearColor(.1f,.2f,.3f,1.f)));
    Binding->RestoreMaterials();
    TestTrue(TEXT("Restore returns original slot"),Mesh->GetMaterial(1) == Original);
    return true;
}
#endif
