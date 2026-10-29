#pragma once

#include "CoreMinimal.h"
#include "NPRToonParameters.generated.h"

/** Model selection is semantic data; it selects a material family rather than an MID float. */
UENUM(BlueprintType)
enum class ENPRShadingModel : uint8
{
    Inherit UMETA(DisplayName="继承原着色模型"),
    DefaultLit UMETA(DisplayName="Default Lit · 普通受光"),
    SubstrateToon UMETA(DisplayName="Substrate Toon")
};

UENUM(BlueprintType)
enum class ENPRToonResponseShape : uint8
{
    Linear UMETA(DisplayName="线性"),
    Hermite UMETA(DisplayName="平滑 Hermite")
};

UENUM(BlueprintType)
enum class ENPRToonShadowMode : uint8
{
    OutsideRamp UMETA(DisplayName="色阶外相乘"),
    InsideRamp UMETA(DisplayName="进入色阶坐标")
};

namespace NPRToonContract
{
    inline constexpr int32 Version = 1;
    inline constexpr TCHAR CenterId[] = TEXT("TerminatorDotCenter");
    inline constexpr TCHAR HalfWidthId[] = TEXT("TerminatorDotHalfWidth");
    inline constexpr TCHAR SpecAngleId[] = TEXT("SpecHalfAngleRad");
    inline constexpr TCHAR SpecSoftnessId[] = TEXT("SpecEdgeSoftness");
    inline constexpr TCHAR SpecPeakId[] = TEXT("SpecPeakScale");
}

/** Canonical v1 values. No backend object, shared Profile mutation or implicit clamping. */
USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPRToonParameters
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Toon", meta=(DisplayName="明暗中心", ClampMin="-1", ClampMax="1", UIMin="-0.5", UIMax="0.5"))
    double TerminatorDotCenter = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Toon", meta=(DisplayName="明暗过渡半宽", ClampMin="0", ClampMax="0.5", UIMin="0", UIMax="0.25"))
    double TerminatorDotHalfWidth = 0.05;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Toon", meta=(DisplayName="高光半峰角（弧度）", ClampMin="0.017453292519943295", ClampMax="1.0471975511965976"))
    double SpecHalfAngleRad = 0.13962634015954636;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Toon", meta=(DisplayName="高光边缘柔度", ClampMin="0", ClampMax="1"))
    double SpecEdgeSoftness = 0.15;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Toon", meta=(DisplayName="高光峰值倍率", ClampMin="0", UIMin="0", UIMax="2"))
    double SpecPeakScale = 1.0;

    bool Validate(FString& OutError) const;
    /** Reference response; requires validated values and finite signed dot/angle. */
    double EvaluateDiffuse(double SignedNoL, ENPRToonResponseShape Shape) const;
    double EvaluateSpecular(double HalfVectorAngleRad) const;
};

/** Versioned response-resource options, not additional global semantic IDs. */
USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPRToonResponseSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Toon", meta=(DisplayName="用艺术参数生成漫反射曲线"))
    bool bUseArtisticDiffuse = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Toon", meta=(DisplayName="用艺术参数生成高光曲线（近似）"))
    bool bUseArtisticSpecular = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Toon", meta=(DisplayName="过渡形状"))
    ENPRToonResponseShape Shape = ENPRToonResponseShape::Hermite;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Toon", meta=(DisplayName="暗部响应", HideAlphaChannel))
    FLinearColor DarkResponse = FLinearColor(0.1f,0.1f,0.1f,1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Toon", meta=(DisplayName="亮部响应", HideAlphaChannel))
    FLinearColor LightResponse = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Toon", meta=(DisplayName="阴影合成"))
    ENPRToonShadowMode ShadowMode = ENPRToonShadowMode::OutsideRamp;

    bool Validate(FString& OutError) const;
};
