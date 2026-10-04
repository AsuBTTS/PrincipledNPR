#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Materials/MaterialInterface.h"
#include "NPRSemanticTypes.h"
#include "NPRBackendStylePackage.generated.h"

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

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Binding")
    TArray<FNPRBackendMaterialSlot> MaterialSlots;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quality")
    TArray<FNPRMappingQuality> MappingQuality;

    /** NotRun is not a passed rendering gate. This skeleton does not generate calibration. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quality")
    FName ValidationStatus = TEXT("NotRun");
};
