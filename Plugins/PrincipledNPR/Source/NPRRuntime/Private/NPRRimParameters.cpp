#include "NPRRimParameters.h"

bool FNPRRimParameters::Validate(FString& OutError) const
{
    constexpr double HalfPi = 1.5707963267948966;
    OutError.Reset();
    if (!FMath::IsFinite(RimGrazingAngleRad) || RimGrazingAngleRad < 0.0 || RimGrazingAngleRad > HalfPi)
        OutError = TEXT("RimGrazingAngleRad must be finite and inside [0, pi/2] radians.");
    else if (!FMath::IsFinite(RimEdgeHalfWidthRad) || RimEdgeHalfWidthRad < 0.0 || RimEdgeHalfWidthRad > HalfPi)
        OutError = TEXT("RimEdgeHalfWidthRad must be finite and inside [0, pi/2] radians.");
    else if (!FMath::IsFinite(RimPeakScale) || RimPeakScale < 0.f)
        OutError = TEXT("RimPeakScale must be a finite nonnegative linear multiplier.");
    else if (!FMath::IsFinite(RimColor.R) || !FMath::IsFinite(RimColor.G) || !FMath::IsFinite(RimColor.B) ||
             RimColor.R < 0.f || RimColor.G < 0.f || RimColor.B < 0.f)
        OutError = TEXT("RimColor RGB must be finite, nonnegative linear values; HDR is allowed, alpha is ignored.");
    return OutError.IsEmpty();
}

float FNPRRimParameters::GetMaterialWidthDegrees() const
{
    return static_cast<float>(FMath::RadiansToDegrees(RimGrazingAngleRad));
}

float FNPRRimParameters::GetMaterialSoftnessDegrees() const
{
    return static_cast<float>(FMath::RadiansToDegrees(RimEdgeHalfWidthRad));
}
