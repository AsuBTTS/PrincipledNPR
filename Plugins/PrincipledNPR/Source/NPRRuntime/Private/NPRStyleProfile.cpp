#include "NPRStyleProfile.h"

bool UNPRStyleProfile::ResolveToonParameters(ENPRMaterialKind Kind,FNPRToonParameters& OutParameters,FString& OutError) const
{
    OutError.Reset();
    if(SchemaVersion!=1||ResponseModelVersion!=1)
    {OutError=TEXT("Unsupported Toon semantic schema/response version; no values resolved.");return false;}
    FNPRToonParameters Resolved;
    const FNPRParameterPatch* KindPatch=MaterialOverrides.Find(Kind);
    const auto Read=[&](const FName Id,double& Value)
    {
        const FNPRFloatOverride* Entry=KindPatch?KindPatch->Values.Find(Id):nullptr;
        if(!Entry||!Entry->bIsSet) Entry=Defaults.Values.Find(Id);
        if(Entry&&Entry->bIsSet) Value=Entry->Value;
    };
    Read(NPRToonContract::CenterId,Resolved.TerminatorDotCenter);
    Read(NPRToonContract::HalfWidthId,Resolved.TerminatorDotHalfWidth);
    Read(NPRToonContract::SpecAngleId,Resolved.SpecHalfAngleRad);
    Read(NPRToonContract::SpecSoftnessId,Resolved.SpecEdgeSoftness);
    Read(NPRToonContract::SpecPeakId,Resolved.SpecPeakScale);
    if(!Resolved.Validate(OutError)) return false;
    OutParameters=Resolved;
    return true;
}

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
