#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPROutlinePreset.h"
#include "NPROutlineComponent.generated.h"

class USkeletalMeshComponent;
class UPostProcessComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Persistent per-character binding. Transient rendering helpers are reconstructed on registration. */
UCLASS(ClassGroup = (NPR), meta = (BlueprintSpawnableComponent))
class NPRRUNTIME_API UNPROutlineComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UNPROutlineComponent();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Binding")
    TObjectPtr<USkeletalMeshComponent> TargetMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Binding")
    FName MaterialSlotName;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Binding")
    int32 MaterialSlotIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Binding")
    TObjectPtr<UMaterialInterface> OriginalMaterial;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Binding")
    TObjectPtr<UMaterialInterface> ConvertedMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", meta = (ShowOnlyInnerProperties))
    FNPROutlineSettings Settings;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preset")
    TObjectPtr<UNPROutlinePreset> Preset;

    // Hard references deliberately preserve helper shaders through cooking.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Backend")
    TObjectPtr<UMaterialInterface> HullTemplate;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Backend")
    TObjectPtr<UMaterialInterface> MaskTemplate;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Backend")
    TObjectPtr<UMaterialInterface> ScreenTemplate;

    UFUNCTION(BlueprintCallable, Category = "NPR|Outline")
    bool RefreshOutline();

    UFUNCTION(BlueprintCallable, Category = "NPR|Outline")
    bool ApplyPreset(UNPROutlinePreset* InPreset, FString& OutError);

    UFUNCTION(BlueprintPure, Category = "NPR|Outline")
    FString GetLastError() const { return LastError; }

    UFUNCTION(BlueprintPure, Category = "NPR|Outline")
    USkeletalMeshComponent* GetOutlineMesh() const { return OutlineMesh; }

    UFUNCTION(BlueprintPure, Category = "NPR|Outline")
    int32 GetAllocatedStencil() const { return AllocatedStencil; }

protected:
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
private:
    void ReleaseHelpers();
    void UpdateParameters();
    int32 AllocateStencil() const;
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> OutlineMesh;
    UPROPERTY(Transient) TObjectPtr<UPostProcessComponent> PostProcess;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> OutlineMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> InvisibleMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ScreenMID;
    UPROPERTY(Transient) FString LastError;
    FNPROutlineSettings AppliedSettings;
    ENPROutlineMethod BuiltMethod = ENPROutlineMethod::None;
    TWeakObjectPtr<USkeletalMeshComponent> BuiltTarget;
    int32 BuiltSlot = INDEX_NONE;
    int32 AllocatedStencil = 0;
};
