#include "NPRToonParameters.h"
#include "NPRStyleProfile.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRToonContractTest,"NPR.Toon.Contract",
    EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::EngineFilter)

bool FNPRToonContractTest::RunTest(const FString&)
{
    FString Error;
    FNPRToonParameters P;
    TestTrue(TEXT("Canonical defaults valid"),P.Validate(Error));
    TestEqual(TEXT("Frozen center default"),P.TerminatorDotCenter,0.0);
    TestEqual(TEXT("Frozen half width default"),P.TerminatorDotHalfWidth,0.05);
    P.TerminatorDotCenter=.2;P.TerminatorDotHalfWidth=.08;
    for(auto Shape:{ENPRToonResponseShape::Linear,ENPRToonResponseShape::Hermite})
    {
        TestTrue(TEXT("Center is fifty percent"),FMath::IsNearlyEqual(P.EvaluateDiffuse(.2,Shape),.5,1.e-12));
        TestTrue(TEXT("Support lower end"),FMath::IsNearlyZero(P.EvaluateDiffuse(.12,Shape),1.e-12));
        TestTrue(TEXT("Support upper end"),FMath::IsNearlyEqual(P.EvaluateDiffuse(.28,Shape),1.0,1.e-12));
    }
    P.TerminatorDotCenter=.95;P.TerminatorDotHalfWidth=.2;
    TestTrue(TEXT("Out-of-domain plateau must not compress onto endpoint"),
        FMath::IsNearlyEqual(.1+.9*P.EvaluateDiffuse(1,ENPRToonResponseShape::Linear),.6625,1.e-12));
    P.TerminatorDotHalfWidth=0;
    TestEqual(TEXT("Hard step lower"),P.EvaluateDiffuse(.9,ENPRToonResponseShape::Hermite),0.0);
    TestEqual(TEXT("Hard step midpoint explicit"),P.EvaluateDiffuse(.95,ENPRToonResponseShape::Hermite),.5);
    TestEqual(TEXT("Hard step upper"),P.EvaluateDiffuse(1,ENPRToonResponseShape::Hermite),1.0);
    P.SpecEdgeSoftness=0;
    TestEqual(TEXT("Hard specular midpoint"),P.EvaluateSpecular(P.SpecHalfAngleRad),.5);
    TestEqual(TEXT("Hard specular outside"),P.EvaluateSpecular(P.SpecHalfAngleRad+.001),0.0);
    P.SpecPeakScale=0;
    TestEqual(TEXT("Explicit zero peak"),P.EvaluateSpecular(0),0.0);
    P.SpecEdgeSoftness=std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("Nonfinite canonical data rejected"),P.Validate(Error));

    auto* Style=NewObject<UNPRStyleProfile>();
    FNPRToonParameters Resolved;
    TestTrue(TEXT("Old empty style inherits canonical defaults"),Style->ResolveToonParameters(ENPRMaterialKind::Generic,Resolved,Error));
    TestEqual(TEXT("Old style does not select a different shader family"),Style->ShadingModel,ENPRShadingModel::Inherit);
    auto& Center=Style->Defaults.Values.FindOrAdd(NPRToonContract::CenterId);Center.bIsSet=true;Center.Value=.4;
    auto& Override=Style->MaterialOverrides.FindOrAdd(ENPRMaterialKind::Generic).Values.FindOrAdd(NPRToonContract::CenterId);
    Override.bIsSet=true;Override.Value=0;
    TestTrue(TEXT("Sparse override resolves"),Style->ResolveToonParameters(ENPRMaterialKind::Generic,Resolved,Error));
    TestEqual(TEXT("Zero overrides inherited center"),Resolved.TerminatorDotCenter,0.0);
    Style->MaterialOverrides[ENPRMaterialKind::Generic].Values.Remove(NPRToonContract::CenterId);
    TestTrue(TEXT("Reset resolves inherited value"),Style->ResolveToonParameters(ENPRMaterialKind::Generic,Resolved,Error));
    TestEqual(TEXT("Reset means removing override"),Resolved.TerminatorDotCenter,.4);
    Center.Value=2;
    TestFalse(TEXT("Invalid style fails atomically"),Style->ResolveToonParameters(ENPRMaterialKind::Generic,Resolved,Error));
    TestEqual(TEXT("Failure preserves prior output"),Resolved.TerminatorDotCenter,.4);
    Style->ResponseModelVersion=2;
    TestFalse(TEXT("Unknown response versions are not silently interpreted as v1"),Style->ResolveToonParameters(ENPRMaterialKind::Generic,Resolved,Error));
    TestEqual(TEXT("Unsupported version leaves output intact"),Resolved.TerminatorDotCenter,.4);
    FNPRToonResponseSettings Response;Response.DarkResponse=FLinearColor(2,.2,0,0);
    TestTrue(TEXT("HDR response and ignored alpha supported"),Response.Validate(Error));
    Response.LightResponse.R=-1;
    TestFalse(TEXT("Negative RGB rejected"),Response.Validate(Error));
    return true;
}
#endif
