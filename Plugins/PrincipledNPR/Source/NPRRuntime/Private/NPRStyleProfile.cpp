#include "NPRStyleProfile.h"

bool UNPRStyleProfile::ResolveRimParameters(ENPRMaterialKind Kind, FNPRRimParameters& OutParameters, FString& OutError) const
{
    OutError.Reset();
    FNPRRimParameters Resolved;
    const FNPRParameterPatch* KindPatch = MaterialOverrides.Find(Kind);

    auto ReadScalar = [&](const FName Id, double& OutValue)
    {
        const FNPRFloatOverride* Entry = KindPatch ? KindPatch->Values.Find(Id) : nullptr;
        if (!Entry || !Entry->bIsSet)
        {
            Entry = Defaults.Values.Find(Id);
        }
        if (Entry && Entry->bIsSet)
        {
            OutValue = Entry->Value;
        }
    };

    ReadScalar(NPRRimContract::WidthId, Resolved.RimGrazingAngleRad);
    ReadScalar(NPRRimContract::SoftnessId, Resolved.RimEdgeHalfWidthRad);
    double Intensity = Resolved.RimPeakScale;
    ReadScalar(NPRRimContract::IntensityId, Intensity);
    Resolved.RimPeakScale = static_cast<float>(Intensity);

    const FName ColorId(NPRRimContract::ColorId);
    const FNPRColorOverride* Color = KindPatch ? KindPatch->Colors.Find(ColorId) : nullptr;
    if (!Color || !Color->bIsSet)
    {
        Color = Defaults.Colors.Find(ColorId);
    }
    if (Color && Color->bIsSet)
    {
        Resolved.RimColor = FLinearColor(Color->Value.R, Color->Value.G, Color->Value.B, 1.f);
    }

    if (!Resolved.Validate(OutError))
    {
        OutError = FString::Printf(TEXT("Style Profile '%s' has invalid Rim values: %s"), *GetName(), *OutError);
        return false;
    }
    OutParameters = Resolved;
    return true;
}
