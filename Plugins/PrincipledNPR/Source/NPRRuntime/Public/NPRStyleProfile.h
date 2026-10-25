#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NPRSemanticTypes.h"
#include "NPRRimParameters.h"
#include "NPRStyleProfile.generated.h"

class UCurveFloat;

/** Semantic style data. This asset never stores or mutates UToonProfile settings. */
UCLASS(BlueprintType)
class NPRRUNTIME_API UNPRStyleProfile : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FName StyleId;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
    int32 SchemaVersion = 1;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
    int32 ResponseModelVersion = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Semantics")
    FNPRParameterPatch Defaults;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Semantics")
    TMap<ENPRMaterialKind, FNPRParameterPatch> MaterialOverrides;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Semantics")
    TMap<FName, TSoftObjectPtr<UCurveFloat>> ResponseCurves;

    /** Named semantic strategies; backend-specific pin names do not belong here. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Semantics")
    TMap<FName, FName> RenderingPolicies;

    /** Resolve defaults then the selected material kind. Missing v1 RGB inherits white. */
    UFUNCTION(BlueprintCallable, Category = "NPR|Rim")
    bool ResolveRimParameters(ENPRMaterialKind Kind, FNPRRimParameters& OutParameters, FString& OutError) const;
};
