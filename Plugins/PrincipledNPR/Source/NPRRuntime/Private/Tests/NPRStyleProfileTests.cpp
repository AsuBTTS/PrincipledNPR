#include "NPRStyleProfile.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRStyleProfileRimTest, "NPR.Rim.StyleProfile",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FNPRStyleProfileRimTest::RunTest(const FString& Parameters)
{
    UNPRStyleProfile* Profile = NewObject<UNPRStyleProfile>();
    FString Error;
    FNPRRimParameters Resolved;
    TestTrue(TEXT("Empty legacy profile resolves"), Profile->ResolveRimParameters(ENPRMaterialKind::Generic, Resolved, Error));
    TestTrue(TEXT("Old missing color inherits white"), Resolved.RimColor.Equals(FLinearColor::White));
    TestTrue(TEXT("Canonical angle defaults stay in radians"),
        FMath::IsNearlyEqual(Resolved.RimGrazingAngleRad, FMath::DegreesToRadians(8.0)));

    auto& DefaultWidth = Profile->Defaults.Values.FindOrAdd(NPRRimContract::WidthId);
    DefaultWidth.bIsSet = true;
    DefaultWidth.Value = FMath::DegreesToRadians(18.0);
    auto& DefaultColor = Profile->Defaults.Colors.FindOrAdd(NPRRimContract::ColorId);
    DefaultColor.bIsSet = true;
    DefaultColor.Value = FLinearColor(2.f, .5f, .25f, 0.f);

    FNPRParameterPatch& Hair = Profile->MaterialOverrides.FindOrAdd(ENPRMaterialKind::Hair);
    auto& HairIntensity = Hair.Values.FindOrAdd(NPRRimContract::IntensityId);
    HairIntensity.bIsSet = true;
    HairIntensity.Value = 0.0;
    auto& HairColor = Hair.Colors.FindOrAdd(NPRRimContract::ColorId);
    HairColor.bIsSet = true;
    HairColor.Value = FLinearColor::Black;
    TestTrue(TEXT("Material override resolves"), Profile->ResolveRimParameters(ENPRMaterialKind::Hair, Resolved, Error));
    TestTrue(TEXT("Default width inherited"), FMath::IsNearlyEqual(Resolved.RimGrazingAngleRad, FMath::DegreesToRadians(18.0)));
    TestEqual(TEXT("Explicit zero intensity survives"), Resolved.RimPeakScale, 0.f);
    TestTrue(TEXT("Explicit black survives"), Resolved.RimColor.Equals(FLinearColor::Black));

    Hair.Colors.Remove(NPRRimContract::ColorId);
    TestTrue(TEXT("Removing override restores inherited RGB"),
        Profile->ResolveRimParameters(ENPRMaterialKind::Hair, Resolved, Error));
    TestTrue(TEXT("Inherited HDR RGB retained"), Resolved.RimColor.Equals(FLinearColor(2.f, .5f, .25f, 1.f)));

    const FNPRRimParameters BeforeInvalid = Resolved;
    HairIntensity.Value = std::numeric_limits<double>::infinity();
    TestFalse(TEXT("Invalid profile is rejected"), Profile->ResolveRimParameters(ENPRMaterialKind::Hair, Resolved, Error));
    TestFalse(TEXT("Invalid profile reports an error"), Error.IsEmpty());
    TestEqual(TEXT("Failed resolution leaves prior value intact"), Resolved.RimPeakScale, BeforeInvalid.RimPeakScale);
    HairIntensity.Value = 0.0;
    TestTrue(TEXT("Valid resolution clears error"), Profile->ResolveRimParameters(ENPRMaterialKind::Hair, Resolved, Error));
    TestTrue(TEXT("Success clears previous error"), Error.IsEmpty());
    return true;
}
#endif
