#pragma once

#include "CoreMinimal.h"
#include "NPRSemanticTypes.generated.h"

UENUM(BlueprintType)
enum class ENPRMaterialKind : uint8
{
    Generic,
    Face,
    Hair
};

UENUM(BlueprintType)
enum class ENPRMappingLevel : uint8
{
    NotEvaluated,
    Exact,
    Approximate,
    Unsupported
};

/** Sparse patch entry. An explicit zero is valid; reset removes the entry. */
USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPRFloatOverride
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPR")
    bool bIsSet = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPR")
    double Value = 0.0;
};

USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPRParameterPatch
{
    GENERATED_BODY()

    /** Keys are canonical IDs from NPRParameterContract.v1.json, not shader pin names. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPR")
    TMap<FName, FNPRFloatOverride> Values;
};

USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPRMappingQuality
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPR")
    FName SemanticId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPR")
    ENPRMappingLevel Level = ENPRMappingLevel::NotEvaluated;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPR")
    FString Scope;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NPR")
    FString Conditions;
};
