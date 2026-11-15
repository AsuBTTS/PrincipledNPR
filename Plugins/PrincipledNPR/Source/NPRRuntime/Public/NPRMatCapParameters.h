#pragma once

#include "CoreMinimal.h"
#include "NPRMatCapParameters.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMeshComponent;
class UTexture2D;

namespace NPRMatCapContract
{
    inline constexpr TCHAR WeightId[] = TEXT("MatCapWeight");
    inline constexpr TCHAR Texture[] = TEXT("NPR_MatCapTexture");
    inline constexpr TCHAR Weight[] = TEXT("NPR_MatCapWeight");
    inline constexpr TCHAR Tint[] = TEXT("NPR_MatCapTint");
    inline constexpr TCHAR Mask[] = TEXT("NPR_MatCapMask");
    inline constexpr TCHAR NormalInfluence[] = TEXT("NPR_MatCapNormalInfluence");
    inline constexpr TCHAR Perspective[] = TEXT("NPR_MatCapPerspective");
}

/** One camera-aligned additive layer. Weight is a unitless semantic value from 0 to 1. */
USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPRMatCapParameters
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MatCap", meta=(DisplayName="启用 MatCap"))
    bool bEnabled = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MatCap", meta=(DisplayName="光泽贴图"))
    TObjectPtr<UTexture2D> Texture = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MatCap", meta=(DisplayName="混合权重", ClampMin="0", ClampMax="1"))
    float Weight = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MatCap", meta=(DisplayName="染色", HideAlphaChannel))
    FLinearColor Tint = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="高级", meta=(DisplayName="遮罩", ToolTip="按网格 UV0 取红通道，并以线性值乘到光泽贡献。"))
    TObjectPtr<UTexture2D> Mask = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="高级", meta=(DisplayName="表面法线影响", ClampMin="0", ClampMax="1"))
    float NormalInfluence = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="高级", meta=(DisplayName="透视修正", ClampMin="0", ClampMax="1"))
    float Perspective = 0.f;

    bool Validate(FString& Error) const;
    bool Apply(UMaterialInstanceDynamic& Instance, FString& Error) const;
    static bool IsSupported(const UMaterialInterface* Material);
};

USTRUCT()
struct NPRRUNTIME_API FNPRMatCapSlot
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UMeshComponent> Mesh = nullptr;

    UPROPERTY()
    int32 Slot = INDEX_NONE;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Source = nullptr;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Material = nullptr;

    UPROPERTY()
    FNPRMatCapParameters Parameters;
};
