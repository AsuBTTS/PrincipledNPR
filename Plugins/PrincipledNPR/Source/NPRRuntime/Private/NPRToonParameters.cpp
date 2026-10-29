#include "NPRToonParameters.h"

namespace
{
double Transition(double X,double Center,double HalfWidth)
{
    if(HalfWidth==0.0) return X<Center?0.0:X>Center?1.0:0.5;
    const double Z=FMath::Clamp((X-Center+HalfWidth)/(2.0*HalfWidth),0.0,1.0);
    return Z;
}
bool ValidRGB(const FLinearColor& C)
{
    return FMath::IsFinite(C.R)&&FMath::IsFinite(C.G)&&FMath::IsFinite(C.B)&&C.R>=0&&C.G>=0&&C.B>=0;
}
}

bool FNPRToonParameters::Validate(FString& OutError) const
{
    OutError.Reset();
    if(!FMath::IsFinite(TerminatorDotCenter)||TerminatorDotCenter< -1.0||TerminatorDotCenter>1.0)
        OutError=TEXT("TerminatorDotCenter must be finite and inside [-1,1].");
    else if(!FMath::IsFinite(TerminatorDotHalfWidth)||TerminatorDotHalfWidth<0.0||TerminatorDotHalfWidth>0.5)
        OutError=TEXT("TerminatorDotHalfWidth must be finite and inside [0,0.5].");
    else if(!FMath::IsFinite(SpecHalfAngleRad)||SpecHalfAngleRad<0.017453292519943295||SpecHalfAngleRad>1.0471975511965976)
        OutError=TEXT("SpecHalfAngleRad must be finite and inside [1,60] degrees, stored in radians.");
    else if(!FMath::IsFinite(SpecEdgeSoftness)||SpecEdgeSoftness<0.0||SpecEdgeSoftness>1.0)
        OutError=TEXT("SpecEdgeSoftness must be finite and inside [0,1].");
    else if(!FMath::IsFinite(SpecPeakScale)||SpecPeakScale<0.0)
        OutError=TEXT("SpecPeakScale must be a finite nonnegative multiplier.");
    return OutError.IsEmpty();
}

double FNPRToonParameters::EvaluateDiffuse(double SignedNoL,ENPRToonResponseShape Shape) const
{
    const double Z=Transition(SignedNoL,TerminatorDotCenter,TerminatorDotHalfWidth);
    return Shape==ENPRToonResponseShape::Hermite?Z*Z*(3.0-2.0*Z):Z;
}

double FNPRToonParameters::EvaluateSpecular(double HalfVectorAngleRad) const
{
    constexpr double HalfPi=1.5707963267948966;
    const double Delta=0.95*SpecEdgeSoftness*FMath::Min(SpecHalfAngleRad,HalfPi-SpecHalfAngleRad);
    const double Z=Transition(HalfVectorAngleRad,SpecHalfAngleRad,Delta);
    return SpecPeakScale*(1.0-Z*Z*(3.0-2.0*Z));
}

bool FNPRToonResponseSettings::Validate(FString& OutError) const
{
    OutError.Reset();
    if(Shape!=ENPRToonResponseShape::Linear&&Shape!=ENPRToonResponseShape::Hermite)
        OutError=TEXT("Unknown Toon response shape.");
    else if(ShadowMode!=ENPRToonShadowMode::OutsideRamp&&ShadowMode!=ENPRToonShadowMode::InsideRamp)
        OutError=TEXT("Unknown Toon shadow composition mode.");
    else if(!ValidRGB(DarkResponse)||!ValidRGB(LightResponse))
        OutError=TEXT("Toon response RGB must be finite nonnegative linear coefficients; alpha is ignored.");
    return OutError.IsEmpty();
}
