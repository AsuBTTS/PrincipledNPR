#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Materials/MaterialInterface.h"
#include "NPRSemanticTypes.h"
#include "NPRToonParameters.h"
#include "NPRBackendStylePackage.generated.h"

class UNPRStyleProfile;

USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPRBackendMaterialSlot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Binding", meta = (ClampMin = "0"))
    int32 SlotIndex = 0;

    /** Prebuilt material/MIC already owns the backend's immutable ToonProfile reference. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Binding")
    TObjectPtr<UMaterialInterface> Material = nullptr;
};

/** Prebuilt backend artifacts, distinct from canonical semantic style assets. */
UCLASS(BlueprintType)
class NPRRUNTIME_API UNPRBackendStylePackage : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FName PackageId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FName BackendId = TEXT("S58");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FName SemanticStyleId;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
    TSoftObjectPtr<UNPRStyleProfile> SemanticSource;

    /** Optional provenance. Existing packages keep Inherit and do not acquire Toon semantics. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Toon")
    ENPRShadingModel ShadingModel = ENPRShadingModel::Inherit;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Toon")
    bool bHasToonParameters = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Toon")
    FNPRToonParameters ToonParameters;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Toon")
    FNPRToonResponseSettings ToonResponse;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Binding")
    TArray<FNPRBackendMaterialSlot> MaterialSlots;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quality")
    TArray<FNPRMappingQuality> MappingQuality;

    /** NotRun is not a passed rendering gate. This skeleton does not generate calibration. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quality")
    FName ValidationStatus = TEXT("NotRun");
};
