#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NPRSurfaceRecipe.generated.h"

class UMaterialInterface;
class UMeshComponent;
class UNPRBackendStylePackage;

UENUM(BlueprintType)
enum class ENPRSurfaceParameterType : uint8 { Scalar, Vector, Texture };

UENUM(BlueprintType)
enum class ENPRSurfaceParameterMapping : uint8 { Identity, RadiansToDegrees };

UENUM(BlueprintType)
enum class ENPRSurfaceComposition : uint8
{
    SurfaceInputs,
    AdditiveEmissive,
    NativeLighting
};

/** A global material parameter owned by one effect. Empty SemanticId means a backend input, not a canonical NPR ID. */
USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPRSurfaceParameterBinding
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") FName SemanticId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") FName MaterialParameter;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") ENPRSurfaceParameterType Type = ENPRSurfaceParameterType::Scalar;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Binding") ENPRSurfaceParameterMapping Mapping = ENPRSurfaceParameterMapping::Identity;
};

/** Declares an already compiled effect, its output contract and parameter ownership. Does not create shader code. */
USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPRSurfaceEffectDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect") FName EffectId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect") FName OutputContract;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect") ENPRSurfaceComposition Composition = ENPRSurfaceComposition::AdditiveEmissive;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect") TArray<FName> RequiresEffects;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Effect") TArray<FNPRSurfaceParameterBinding> Parameters;
};

/** Versioned capability manifest for a prebuilt Master/MIC. Dynamic enable/values are separate from capability selection. */
UCLASS(BlueprintType)
class NPRRUNTIME_API UNPRSurfaceRecipe : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Identity") int32 RecipeVersion = 1;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Identity") FName RecipeId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Identity") FName FamilyId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface") TObjectPtr<UMaterialInterface> Material;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface") TArray<FNPRSurfaceEffectDefinition> Effects;

    bool Validate(FString& OutError) const;
};

UCLASS()
class NPRRUNTIME_API UNPRSurfaceComposerLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Deterministic first-compatible choice in caller order, within the explicitly requested family. No mesh/MID writes. */
    UFUNCTION(BlueprintCallable, Category="NPR|Surface")
    static UNPRBackendStylePackage* ComposePackage(UMeshComponent* Mesh, FName FamilyId,
        const TArray<UNPRSurfaceRecipe*>& Candidates, const TArray<FName>& RequestedEffects,
        const TArray<int32>& TargetSlots, UNPRSurfaceRecipe*& SelectedRecipe, FString& OutError);
};
