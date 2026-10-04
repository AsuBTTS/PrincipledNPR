#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NPRSemanticTypes.h"
#include "NPRMaterialProfile.generated.h"

UCLASS(BlueprintType)
class NPRRUNTIME_API UNPRMaterialProfile : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FName ModelId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    ENPRMaterialKind Kind = ENPRMaterialKind::Generic;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
    int32 SchemaVersion = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Semantics")
    FNPRParameterPatch Defaults;

    /** Binding requirements only. Actual UV/head-frame/flow facts belong to the character. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Semantics")
    TArray<FName> RequiredAssetInputs;
};
