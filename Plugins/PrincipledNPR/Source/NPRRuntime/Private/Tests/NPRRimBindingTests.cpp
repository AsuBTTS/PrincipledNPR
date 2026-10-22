#include "NPRStyleBindingComponent.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "NPRBackendStylePackage.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRRimBindingTest, "NPR.Rim.LiveBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPRRimBindingTest::RunTest(const FString& Arguments)
{
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/PrincipledNPR/RimLight/M_RimLight.M_RimLight"));
    if (!TestNotNull(TEXT("Saved Rim material loads"), Material)) return false;
    auto* Other = UMaterial::GetDefaultMaterial(MD_Surface);
    auto* Geometry = NewObject<UStaticMesh>();
    Geometry->GetStaticMaterials().Add(FStaticMaterial(Other));
    Geometry->GetStaticMaterials().Add(FStaticMaterial(Other));
    auto* Mesh = NewObject<UStaticMeshComponent>();
    Mesh->SetStaticMesh(Geometry);
    auto* Binding = NewObject<UNPRStyleBindingComponent>();
    auto* Package = NewObject<UNPRBackendStylePackage>();
    Package->BackendId = TEXT("IndependentRim");
    for (int32 Index = 0; Index < 2; ++Index)
    {
        FNPRBackendMaterialSlot Slot;
        Slot.SlotIndex = Index;
        Slot.Material = Material;
        Package->MaterialSlots.Add(Slot);
    }
    FString Error;
    FNPRRimParameters Values;
    TestFalse(TEXT("Unbound update rejected"), Binding->SetRimParameters(Values, Error));
    if (!TestTrue(TEXT("Two-slot explicit package binds"), Binding->ApplyPackage(Mesh, Package, Error))) return false;
    auto* First = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
    auto* Second = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(1));
    if (!TestTrue(TEXT("Each slot owns a distinct MID"), First && Second && First != Second)) return false;
    auto ReadScalar = [](UMaterialInstanceDynamic* MID, const TCHAR* Name)
    {
        float Value = -1.f;
        MID->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(Name)), Value);
        return Value;
    };
    auto ReadColor = [](UMaterialInstanceDynamic* MID)
    {
        FLinearColor Value;
        MID->GetVectorParameterValue(FHashedMaterialParameterInfo(FName(NPRRimContract::ColorMaterialName)), Value);
        return Value;
    };
    Values.RimGrazingAngleRad = FMath::DegreesToRadians(25.0);
    Values.RimEdgeHalfWidthRad = FMath::DegreesToRadians(4.0);
    Values.RimPeakScale = 2.f;
    Values.RimColor = FLinearColor(4.f, .2f, .1f, 0.f);
    TestTrue(TEXT("Canonical four-control update succeeds"), Binding->SetRimParameters(Values, Error));
    for (auto* MID : {First, Second})
    {
        TestEqual(TEXT("Width converted to material degrees"), ReadScalar(MID, NPRRimContract::WidthMaterialName), 25.f);
        TestEqual(TEXT("Softness converted to material degrees"), ReadScalar(MID, NPRRimContract::SoftnessMaterialName), 4.f);
        TestEqual(TEXT("Intensity readback"), ReadScalar(MID, NPRRimContract::IntensityMaterialName), 2.f);
        TestTrue(TEXT("HDR RGB preserved; alpha normalized to ignored value"), ReadColor(MID).Equals(FLinearColor(4.f,.2f,.1f,1.f)));
    }
    for (int32 Case = 0; Case < 5; ++Case)
    {
        FNPRRimParameters Bad = Values;
        if (Case == 0) Bad.RimGrazingAngleRad = 2.0;
        if (Case == 1) Bad.RimEdgeHalfWidthRad = -1.0;
        if (Case == 2) Bad.RimPeakScale = -1.f;
        if (Case == 3) Bad.RimPeakScale = std::numeric_limits<float>::infinity();
        if (Case == 4) Bad.RimColor.B = std::numeric_limits<float>::quiet_NaN();
        TestFalse(TEXT("Invalid value rejected"), Binding->SetRimParameters(Bad, Error));
        TestFalse(TEXT("Failure supplies a diagnostic"), Error.IsEmpty());
        for (auto* MID : {First, Second})
        {
            TestEqual(TEXT("Rejected update keeps width"), ReadScalar(MID, NPRRimContract::WidthMaterialName), 25.f);
            TestEqual(TEXT("Rejected update keeps softness"), ReadScalar(MID, NPRRimContract::SoftnessMaterialName), 4.f);
            TestEqual(TEXT("Rejected update keeps intensity"), ReadScalar(MID, NPRRimContract::IntensityMaterialName), 2.f);
            TestTrue(TEXT("Rejected update keeps RGB"), ReadColor(MID).Equals(FLinearColor(4.f,.2f,.1f,1.f)));
        }
    }
    Values.RimGrazingAngleRad = 0;
    Values.RimEdgeHalfWidthRad = 0;
    Values.RimPeakScale = 0;
    Values.RimColor = FLinearColor::Black;
    TestTrue(TEXT("Zero and black update succeeds"), Binding->SetRimParameters(Values, Error));
    TestTrue(TEXT("Success clears prior error"), Error.IsEmpty());
    TestEqual(TEXT("Zero readback"), ReadScalar(First, NPRRimContract::IntensityMaterialName), 0.f);
    TestTrue(TEXT("Black readback"), ReadColor(Second).Equals(FLinearColor::Black));
    TestTrue(TEXT("Existing scalar API remains usable"), Binding->SetScalarParameter(FName(NPRRimContract::IntensityMaterialName), .5f, Error));
    Mesh->SetMaterial(1, Other);
    Values.RimPeakScale = 1;
    TestFalse(TEXT("Externally replaced slot rejects entire update"), Binding->SetRimParameters(Values, Error));
    TestEqual(TEXT("Other slot was not partially updated"), ReadScalar(First, NPRRimContract::IntensityMaterialName), .5f);
    Binding->RestoreMaterials();
    TestTrue(TEXT("Original slot restored"), Mesh->GetMaterial(0) == Other);
    TestTrue(TEXT("External material preserved"), Mesh->GetMaterial(1) == Other);

    Package->MaterialSlots[1].Material = Other;
    TestTrue(TEXT("Mixed package can bind without requiring Rim globally"), Binding->ApplyPackage(Mesh, Package, Error));
    First = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
    const float Before = ReadScalar(First, NPRRimContract::WidthMaterialName);
    Values = FNPRRimParameters();
    TestFalse(TEXT("Missing Rim parameters in one slot reject update"), Binding->SetRimParameters(Values, Error));
    TestEqual(TEXT("Compatible slot remains untouched"), ReadScalar(First, NPRRimContract::WidthMaterialName), Before);
    Binding->RestoreMaterials();
    TestTrue(TEXT("Restore leaves both originals"), Mesh->GetMaterial(0) == Other && Mesh->GetMaterial(1) == Other);
    return true;
}
#endif
