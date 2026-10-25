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
    UPROPERTY(EditAnywhere, Category="Rim|Target")
    TObjectPtr<UMeshComponent> Mesh = nullptr;

    UPROPERTY(EditAnywhere, Category="Rim|Target", meta=(ClampMin="0"))
    int32 SlotIndex = 0;

    UPROPERTY(EditAnywhere, Category="Rim|Assets")
    TObjectPtr<UNPRStyleProfile> StyleProfile = nullptr;

    UPROPERTY(EditAnywhere, Category="Rim|Assets")
    TObjectPtr<UNPRSurfaceRecipe> SurfaceRecipe = nullptr;

    UPROPERTY(EditAnywhere, Category="Rim|Assets")
    ENPRMaterialKind MaterialKind = ENPRMaterialKind::Generic;

    UPROPERTY(EditAnywhere, Category="Rim|Controls", meta=(DisplayName="亮边宽度（度）", ClampMin="0", ClampMax="90", UIMin="0", UIMax="35"))
    double WidthDegrees = 8.0;

    UPROPERTY(EditAnywhere, Category="Rim|Controls", meta=(DisplayName="边缘柔和度（度）", ClampMin="0", ClampMax="90", UIMin="0", UIMax="10"))
    double SoftnessDegrees = 2.0;

    UPROPERTY(EditAnywhere, Category="Rim|Controls", meta=(DisplayName="边缘光强度", ClampMin="0", UIMin="0", UIMax="2"))
    float Intensity = 0.f;

    UPROPERTY(EditAnywhere, Category="Rim|Controls", meta=(DisplayName="边缘光颜色", HideAlphaChannel))
    FLinearColor Color = FLinearColor::White;

    UPROPERTY(EditAnywhere, Category="Rim|Source Surface")
    FNPRRimSurfaceInputs SourceInputs;
};
