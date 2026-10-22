#pragma once

#include "CoreMinimal.h"
#include "NPRRimParameters.generated.h"

/** Frozen v1 names. Material parameters use degrees; semantic/function inputs use radians. */
namespace NPRRimContract
{
    inline constexpr int32 Version = 1;
    inline constexpr TCHAR WidthId[] = TEXT("RimGrazingAngleRad");
    inline constexpr TCHAR SoftnessId[] = TEXT("RimEdgeHalfWidthRad");
    inline constexpr TCHAR IntensityId[] = TEXT("RimPeakScale");
    inline constexpr TCHAR ColorId[] = TEXT("RimColor");
    inline constexpr TCHAR WidthMaterialName[] = TEXT("亮边宽度");
    inline constexpr TCHAR SoftnessMaterialName[] = TEXT("边缘柔和度");
    inline constexpr TCHAR IntensityMaterialName[] = TEXT("边缘光强度");
    inline constexpr TCHAR ColorMaterialName[] = TEXT("边缘光颜色");
}

/** Validated canonical value contract. Storage/patch resolution and MID writes are separate consumers. */
USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPRRimParameters
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Rim", meta=(DisplayName="亮边宽度（弧度）", ClampMin="0", ClampMax="1.5707963267948966"))
    double RimGrazingAngleRad = 0.13962634015954636;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Rim", meta=(DisplayName="边缘柔和度（弧度半宽）", ClampMin="0", ClampMax="1.5707963267948966"))
    double RimEdgeHalfWidthRad = 0.03490658503988659;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Rim", meta=(DisplayName="边缘光强度", ClampMin="0"))
    float RimPeakScale = 0.f;

    /** Linear RGB, finite nonnegative HDR values allowed. Alpha has no shading meaning. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Rim", meta=(DisplayName="边缘光颜色", HideAlphaChannel))
    FLinearColor RimColor = FLinearColor::White;

    /** Reject invalid inputs without changing them. Explicit zero and black are valid. */
    bool Validate(FString& OutError) const;

    /** Call Validate before using these mappings. No parameters or assets are written here. */
    float GetMaterialWidthDegrees() const;
    float GetMaterialSoftnessDegrees() const;
};
