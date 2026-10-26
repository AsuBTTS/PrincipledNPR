#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "NPRSemanticTypes.h"
#include "NPRStyleBindingComponent.h"
#include "NPRRimEditorOptions.generated.h"

class UMeshComponent;
class UNPRStyleProfile;
class UNPRSurfaceRecipe;

/** Unsaved values displayed by the Rim page. Only Save writes the Profile and actor. */
UCLASS(Transient)
class UNPRRimEditorOptions : public UObject
{
    GENERATED_BODY()

public:
    // Transactional sparse-override state follows the displayed values through Undo/Redo.
    UPROPERTY(Transient)
    uint8 OverrideMask = 0;

    UPROPERTY(Transient)
    TObjectPtr<UMeshComponent> Mesh = nullptr;

    UPROPERTY(Transient)
    int32 SlotIndex = 0;

    UPROPERTY(EditAnywhere, Category="Rim|Assets")
    TObjectPtr<UNPRStyleProfile> StyleProfile = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UNPRSurfaceRecipe> SurfaceRecipe = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> GeneratedMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category="Rim|Controls", meta=(DisplayName="着色模型"))
    ENPRRimShadingModel ShadingModel = ENPRRimShadingModel::KeepOriginal;

    UPROPERTY(EditAnywhere, Category="Rim|Assets")
    ENPRMaterialKind MaterialKind = ENPRMaterialKind::Generic;

    UPROPERTY(EditAnywhere, Category="Rim|Controls", meta=(DisplayName="亮边宽度（度）", ClampMin="0", ClampMax="90", UIMin="0", UIMax="35"))
    double WidthDegrees = 8.0;

    UPROPERTY(EditAnywhere, Category="Rim|Controls", meta=(DisplayName="边缘柔和度（度）", ClampMin="0", ClampMax="90", UIMin="0", UIMax="90"))
    double SoftnessDegrees = 12.0;

    UPROPERTY(EditAnywhere, Category="Rim|Controls", meta=(DisplayName="边缘光强度", ClampMin="0", UIMin="0", UIMax="2"))
    float Intensity = 0.f;

    UPROPERTY(EditAnywhere, Category="Rim|Controls", meta=(DisplayName="边缘光颜色", HideAlphaChannel))
    FLinearColor Color = FLinearColor::White;

    UPROPERTY(Transient)
    FNPRRimSurfaceInputs SourceInputs;
};
