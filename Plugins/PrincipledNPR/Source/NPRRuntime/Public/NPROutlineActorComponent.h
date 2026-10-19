#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPROutlinePreset.h"
#include "NPROutlineActorComponent.generated.h"

class UMeshComponent;
class UMaterialInterface;
class UPostProcessComponent;

UENUM(BlueprintType)
enum class ENPROutlineScope : uint8
{
    UnifiedSilhouette UMETA(DisplayName="统一外轮廓（Screen Space）"),
    IndependentRegions UMETA(DisplayName="组件 / 材质 / Section 继承覆盖")
};

/** Only checked fields replace inherited values. Method=None is a second explicit off switch. */
USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPROutlineOverride
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Override") bool bOverrideEnabled = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Override", meta=(EditCondition="bOverrideEnabled")) bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Override") bool bOverrideMethod = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Override") bool bOverrideColor = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Override") bool bOverrideWidth = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Override", meta=(DisplayName="覆写边缘来源")) bool bOverrideEdgeSource = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Override", meta=(DisplayName="覆写边类型倍率表")) bool bOverrideEdgeWidthScales = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Override", meta=(DisplayName="覆写区域宽度倍率")) bool bOverrideLocalWidthScale = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Override") bool bOverrideDepthBias = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Override") bool bOverrideVertexControl = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Override", meta=(DisplayName="覆写 Mask 宽度控制")) bool bOverrideWidthMask = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Override") FNPROutlineSettings Values;
    void Apply(FNPROutlineSettings& Settings, bool& bIsEnabled) const;
};

USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPROutlineMaterialRule
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Region") FName MaterialSlotName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Region") FNPROutlineOverride Override;
};

USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPROutlineSectionRule
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Region", meta=(ClampMin="0")) int32 LODIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Region", meta=(ClampMin="0")) int32 SectionIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Region") FNPROutlineOverride Override;
};

USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPROutlineMeshRule
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source") FName SourceName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Source") TObjectPtr<UMeshComponent> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Override") FNPROutlineOverride Override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Override", meta=(TitleProperty="MaterialSlotName")) TArray<FNPROutlineMaterialRule> Materials;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Override", meta=(ToolTip="仅支持单 LOD 且独占材质槽的 Section；其他组合会报错")) TArray<FNPROutlineSectionRule> Sections;
};

USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPROutlineSectionInfo
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mapping") int32 LODIndex = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mapping") int32 SectionIndex = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mapping") int32 MaterialSlotIndex = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Mapping") FName MaterialSlotName;
};

USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPROutlineResolvedRegion
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Resolved") TObjectPtr<UMeshComponent> Mesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Resolved") int32 MaterialSlotIndex = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Resolved") bool bEnabled = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Resolved") FNPROutlineSettings Settings;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Resolved") FString ParameterSource;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Resolved", meta=(ToolTip="基础宽度 × 边类型倍率 × 区域倍率，尚未乘顶点/Mask 因子；单位由 Method 决定")) float ResolvedWidth = 0.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Resolved") FString WidthParameterSource;
};

/** Actor-owned configuration. Does not replace source materials or mutate mesh assets. */
UCLASS(ClassGroup=(NPR), HideCategories=(Backend,Tags,ComponentReplication,Activation,Cooking,Variable,Replication,AssetUserData,Navigation), meta=(BlueprintSpawnableComponent))
class NPRRUNTIME_API UNPROutlineActorComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UNPROutlineActorComponent();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outline") bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outline") ENPROutlineScope Scope = ENPROutlineScope::IndependentRegions;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outline") FNPROutlineSettings BaseSettings;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Outline", meta=(DisplayName="已应用预设（只读引用）", ToolTip="记录参数来源；不是应用按钮。请使用下方预设选择器或重新应用所选预设。当前调参不会自动回写预设资产。")) TObjectPtr<UNPROutlinePreset> Preset;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Outline", meta=(TitleProperty="SourceName")) TArray<FNPROutlineMeshRule> Meshes;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Backend") TObjectPtr<UMaterialInterface> HullTemplate;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Backend") TObjectPtr<UMaterialInterface> MaskTemplate;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Backend") TObjectPtr<UMaterialInterface> ScreenTemplate;
    UFUNCTION(BlueprintCallable, CallInEditor, Category="NPR|Outline", meta=(DisplayName="重新收集角色组件", ToolTip="重新扫描当前角色的网格组件，保留已有区域规则；组件没有增减时画面可以不变，窗口会报告数量。")) void CollectOwnerMeshes();
    UFUNCTION(BlueprintCallable, Category="NPR|Outline") bool RefreshOutline();
    UFUNCTION(BlueprintCallable, Category="NPR|Outline") bool ApplyPreset(UNPROutlinePreset* InPreset, FString& OutError);
    UFUNCTION(BlueprintCallable, Category="NPR|Outline") bool ResolveRegions(TArray<FNPROutlineResolvedRegion>& OutRegions, FString& OutError) const;
    UFUNCTION(BlueprintCallable, Category="NPR|Outline") static bool GetSectionMapping(UMeshComponent* Mesh, TArray<FNPROutlineSectionInfo>& OutSections, FString& OutError);
    UFUNCTION(BlueprintPure, Category="NPR|Outline") FString GetLastError() const { return LastError; }
    UFUNCTION(BlueprintPure, Category="NPR|Outline") TArray<UMeshComponent*> GetOutlineMeshes() const;
    UFUNCTION(BlueprintPure, Category="NPR|Outline") int32 GetScreenPassCount() const { return PostProcesses.Num(); }
    uint32 GetCollectionRevision() const { return CollectionRevision; }
    const FString& GetCollectionSummary() const { return CollectionSummary; }
    UFUNCTION(BlueprintPure, Category="NPR|Outline|Width") static TArray<FNPROutlineEdgeCapability> GetEdgeCapabilities(ENPROutlineMethod Method, ENPROutlineScope OutlineScope);
    UFUNCTION(BlueprintPure, Category="NPR|Outline|Width") static FString DescribeWidthSupport(ENPROutlineMethod Method, ENPROutlineScope OutlineScope);
protected:
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
    virtual void PostEditUndo() override;
#endif
private:
    void ReleaseHelpers();
    void SyncHelpers();
    UPROPERTY(Transient) TArray<TObjectPtr<UMeshComponent>> Helpers;
    UPROPERTY(Transient) TArray<TObjectPtr<UMeshComponent>> Sources;
    UPROPERTY(Transient) TArray<TObjectPtr<UPostProcessComponent>> PostProcesses;
    UPROPERTY(Transient) TArray<FNPROutlineResolvedRegion> BuiltRegions;
    UPROPERTY(Transient) TArray<TObjectPtr<UObject>> BuiltAssets;
    UPROPERTY(Transient) FString LastError;
    ENPROutlineScope BuiltScope = ENPROutlineScope::IndependentRegions;
    uint32 CollectionRevision = 0;
    FString CollectionSummary;
};
