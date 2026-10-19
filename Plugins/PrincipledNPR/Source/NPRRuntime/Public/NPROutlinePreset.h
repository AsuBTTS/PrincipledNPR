#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NPROutlinePreset.generated.h"

UENUM(BlueprintType)
enum class ENPROutlineMethod : uint8
{
    None UMETA(DisplayName = "关闭 / None"),
    InvertedHull UMETA(DisplayName = "反向壳 / Inverted Hull"),
    ScreenSpace UMETA(DisplayName = "屏幕空间 / Screen Space")
};

class UTexture2D;

/** Edge provenance is separate from the rendering algorithm. No automatic crease classifier exists. */
UENUM(BlueprintType)
enum class ENPROutlineEdgeSource : uint8
{
    Silhouette UMETA(DisplayName="轮廓 / Silhouette"),
    Crease UMETA(Hidden),
    Occlusion UMETA(Hidden),
    FeatureBoundary UMETA(DisplayName="材质/特征区域边界（手工指定）")
};

USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPROutlineEdgeWidthScales
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Width", meta=(DisplayName="轮廓倍率 / Silhouette", ClampMin="0", ClampMax="4"))
    float Silhouette = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Width", meta=(DisplayName="手工区域边界倍率 / Feature Boundary", ClampMin="0", ClampMax="4", ToolTip="仅控制手工划定区域的轮廓，不自动识别纹理线、折痕或材质 ID"))
    float FeatureBoundary = 1.f;
    bool operator==(const FNPROutlineEdgeWidthScales& Other) const
    { return Silhouette==Other.Silhouette && FeatureBoundary==Other.FeatureBoundary; }
};

USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPROutlineEdgeCapability
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Capability") ENPROutlineEdgeSource Source = ENPROutlineEdgeSource::Silhouette;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Capability") bool bAutomatic = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Capability") bool bAuthoredRegion = false;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Capability") FString Limitation;
};

/** Style values only: mesh, material slots and source textures belong to the binding. */
USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPROutlineSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", meta = (DisplayName = "描边方式 / Method"))
    ENPROutlineMethod Method = ENPROutlineMethod::InvertedHull;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", meta = (DisplayName = "描边颜色 / Color", HideAlphaChannel))
    FLinearColor Color = FLinearColor(0.015f, 0.008f, 0.025f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", meta = (DisplayName = "基础屏幕宽度 / Base Width (px)", ClampMin = "0", ClampMax = "8", EditCondition = "Method == ENPROutlineMethod::ScreenSpace"))
    float WidthPixels = 1.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", meta = (DisplayName = "基础壳宽度 / Base Width (cm)", ClampMin = "0", ClampMax = "5", EditCondition = "Method == ENPROutlineMethod::InvertedHull", Units = "cm"))
    float HullWidthCm = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Width", meta=(DisplayName="边缘来源 / Edge Source", ToolTip="Silhouette 是当前算法的轮廓；Feature Boundary 需手工用区域划分，不能充当自动边缘分类"))
    ENPROutlineEdgeSource WidthEdgeSource = ENPROutlineEdgeSource::Silhouette;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Width", meta=(DisplayName="边类型宽度倍率"))
    FNPROutlineEdgeWidthScales EdgeWidthScales;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Width", meta=(DisplayName="区域宽度倍率 / Local Scale", ClampMin="0", ClampMax="4", ToolTip="在继承链中由最深层勾选的覆写替换，不逐层连乘"))
    float LocalWidthScale = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Outline", meta = (DisplayName = "深度容差 / Depth Bias (cm)", ClampMin = "0", ClampMax = "5", EditCondition = "Method == ENPROutlineMethod::ScreenSpace", Units = "cm"))
    float DepthBiasCm = 0.1f;

    /** Optional local factor: saturate(VertexColor.R). Old assets default to uniform width. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", meta = (DisplayName = "顶点红色通道控制壳宽度", EditCondition = "Method == ENPROutlineMethod::InvertedHull"))
    bool bUseVertexColorWidth = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Width", meta=(DisplayName="UV0 Mask 控制壳宽度", EditCondition="Method == ENPROutlineMethod::InvertedHull"))
    bool bUseWidthMask = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Width", meta=(DisplayName="宽度 Mask（R 通道）", EditCondition="bUseWidthMask && Method == ENPROutlineMethod::InvertedHull", ToolTip="UV0，R=0 隐去、R=1 保持；线性 Masks 纹理，非 Virtual Texture；在顶点采样，细节受网格密度限制"))
    TObjectPtr<UTexture2D> WidthMask;

    bool Validate(FString& OutError) const;
    float GetEdgeWidthScale() const;
    float GetResolvedWidth() const;
    void ApplyMaterialParameters(class UMaterialInstanceDynamic* MID) const;
    bool operator==(const FNPROutlineSettings& Other) const;
    bool operator!=(const FNPROutlineSettings& Other) const { return !(*this == Other); }
};

UCLASS(BlueprintType)
class NPRRUNTIME_API UNPROutlinePreset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Version")
    int32 SchemaVersion = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", meta = (ShowOnlyInnerProperties))
    FNPROutlineSettings Settings;
};
