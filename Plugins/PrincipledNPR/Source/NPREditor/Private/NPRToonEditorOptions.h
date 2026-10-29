#pragma once

#include "CoreMinimal.h"
#include "NPRToonParameters.h"
#include "NPRSemanticTypes.h"
#include "UObject/Object.h"
#include "NPRToonEditorOptions.generated.h"

class UMeshComponent;
class UMaterialInterface;
class UMaterialInstanceConstant;
class UToonProfile;
class UNPRStyleProfile;
class UNPRBackendStylePackage;

/** Transactional, slot-local authoring state. Preview assets never leak into a saved actor. */
UCLASS(Transient)
class UNPRToonEditorOptions : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TObjectPtr<UMeshComponent> Mesh;
    UPROPERTY(Transient) int32 SlotIndex = INDEX_NONE;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> SourceMaterial;
    UPROPERTY(Transient) TObjectPtr<UNPRStyleProfile> StyleProfile;
    UPROPERTY(Transient) TObjectPtr<UNPRBackendStylePackage> BackendPreset;
    UPROPERTY(Transient) FNPRParameterPatch LocalPatch;
    UPROPERTY(Transient) FNPRToonParameters Parameters;
    UPROPERTY(Transient) FNPRToonResponseSettings Response;
    UPROPERTY(Transient) ENPRShadingModel ShadingModel = ENPRShadingModel::SubstrateToon;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceConstant> PreviewMaterial;
    UPROPERTY(Transient) TObjectPtr<UToonProfile> PrivateProfile;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> PreviewSource;
    UPROPERTY(Transient) ENPRShadingModel PreviewModel = ENPRShadingModel::Inherit;
    UPROPERTY(Transient) bool bPreviewArtisticSpecular = false;
};
