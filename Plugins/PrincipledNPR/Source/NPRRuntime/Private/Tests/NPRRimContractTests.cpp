#include "NPRRimParameters.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRRimContractTest, "NPR.Rim.MaterialContract",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FNPRRimContractTest::RunTest(const FString& Parameters)
{
    FNPRRimParameters Values;
    FString Error;
    TestTrue(TEXT("Defaults validate"), Values.Validate(Error));
    TestTrue(TEXT("Width maps to 8 degrees"), FMath::IsNearlyEqual(Values.GetMaterialWidthDegrees(), 8.f));
    TestTrue(TEXT("Softness maps to 2 degrees"), FMath::IsNearlyEqual(Values.GetMaterialSoftnessDegrees(), 2.f));
    Values.RimGrazingAngleRad = 0;
    Values.RimEdgeHalfWidthRad = 0;
    Values.RimPeakScale = 0;
    Values.RimColor = FLinearColor::Black;
    TestTrue(TEXT("Explicit zeros and black are valid"), Values.Validate(Error));
    Values.RimColor = FLinearColor(4, 2, 0, 0);
    TestTrue(TEXT("HDR RGB is accepted, alpha ignored"), Values.Validate(Error));
    Values.RimGrazingAngleRad = 1.5707963267948966;
    Values.RimEdgeHalfWidthRad = 1.5707963267948966;
    TestTrue(TEXT("90 degree boundary is valid"), Values.Validate(Error));
    Values.RimGrazingAngleRad = 2;
    TestFalse(TEXT("Out of range angle rejected"), Values.Validate(Error));
    TestEqual(TEXT("Invalid input was not clamped"), Values.RimGrazingAngleRad, 2.0);
    Values = FNPRRimParameters();
    Values.RimEdgeHalfWidthRad = -1;
    TestFalse(TEXT("Negative softness rejected"), Values.Validate(Error));
    Values = FNPRRimParameters();
    Values.RimPeakScale = std::numeric_limits<float>::infinity();
    TestFalse(TEXT("Infinite intensity rejected"), Values.Validate(Error));
    Values.RimPeakScale = -1;
    TestFalse(TEXT("Negative intensity rejected"), Values.Validate(Error));
    Values = FNPRRimParameters();
    Values.RimColor.R = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("NaN color rejected"), Values.Validate(Error));
    Values.RimColor = FLinearColor(-1, 0, 0, 1);
    TestFalse(TEXT("Negative RGB rejected"), Values.Validate(Error));
    Values = FNPRRimParameters();
    TestTrue(TEXT("Valid input clears previous diagnostic"), Values.Validate(Error) && Error.IsEmpty());
    return true;
}
#endif
