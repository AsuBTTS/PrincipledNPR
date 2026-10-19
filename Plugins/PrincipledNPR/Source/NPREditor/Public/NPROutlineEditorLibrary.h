#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NPROutlinePreset.h"
#include "NPROutlineEditorLibrary.generated.h"

class USkeletalMeshComponent;
class UNPROutlineComponent;
class UNPROutlineActorComponent;
class AActor;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class USkeletalMesh;

USTRUCT(BlueprintType)
struct NPREDITOR_API FNPROutlineConversionResult
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly) FString Error;
    UPROPERTY(BlueprintReadOnly) TObjectPtr<UNPROutlineComponent> Binding;
};

/** The UI and editor Python use the same conversion/preset services. No runtime editor dependency. */
UCLASS()
class NPREDITOR_API UNPROutlineEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="NPR|Outline|Editor")
    static UNPROutlineActorComponent* ConfigureActor(AActor* Actor, FString& OutError);

    UFUNCTION(BlueprintCallable, Category="NPR|Outline|Editor")
    static bool SaveActorOutline(UNPROutlineActorComponent* Component, FString& OutError);
    UFUNCTION(BlueprintCallable, Category="NPR|Outline|Editor")
    static bool SetActorOutlineEnabled(UNPROutlineActorComponent* Component, bool bEnabled, FString& OutError);
    UFUNCTION(BlueprintCallable, Category="NPR|Outline|Editor")
    static bool ApplyActorPreset(UNPROutlineActorComponent* Component, UNPROutlinePreset* Preset, FString& OutError);

    /** Read-only graph and referenced material-function dump for auditing binary assets. */
    UFUNCTION(BlueprintCallable, Category="NPR|Outline|Validation")
    static FString AuditMaterial(UMaterialInterface* Material);

    /** Only permits the explicitly tagged, owned validation scene. */
    UFUNCTION(BlueprintCallable, Category="NPR|Outline|Validation")
    static UStaticMeshComponent* AddValidationStaticMesh(AActor* Actor, UStaticMesh* Mesh, const FTransform& RelativeTransform);

    UFUNCTION(BlueprintCallable, Category="NPR|Outline|Validation")
    static bool SampleValidationAnimation(USkeletalMeshComponent* Mesh, float TimeSeconds);

    UFUNCTION(BlueprintCallable, Category="NPR|Outline|Validation")
    static USkeletalMeshComponent* AddValidationSkeletalMesh(AActor* Actor, USkeletalMesh* Mesh);
    UFUNCTION(BlueprintCallable, Category = "NPR|Outline|Editor")
    static FNPROutlineConversionResult ConvertMesh(USkeletalMeshComponent* Mesh, int32 SlotIndex,
        const FNPROutlineSettings& Settings, const FString& AssetFolder = TEXT("/Game/NPR/Outline/Generated"));

    UFUNCTION(BlueprintCallable, Category = "NPR|Outline|Editor")
    static UNPROutlinePreset* SavePreset(const FString& PackagePath, const FNPROutlineSettings& Settings, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "NPR|Outline|Editor")
    static bool ApplyPreset(UNPROutlineComponent* Binding, UNPROutlinePreset* Preset, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "NPR|Outline|Editor")
    static bool SaveBinding(UNPROutlineComponent* Binding, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "NPR|Outline|Editor")
    static bool RestoreSource(UNPROutlineComponent* Binding, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "NPR|Outline|Editor")
    static void OpenEditor();

    UFUNCTION(BlueprintCallable, Category = "NPR|Outline|Editor")
    static bool ExportComparison(const FString& Directory);

    /** Reproducible automation readback; (0,0) returns to the widget's native resolution. */
    UFUNCTION(BlueprintCallable, Category = "NPR|Outline|Validation")
    static bool SetComparisonCaptureSize(int32 Width, int32 Height);

    UFUNCTION(BlueprintPure, Category="NPR|Outline|Validation")
    static int32 GetComparisonMeshCount(bool bOutlined);

    /** Deterministic motion tests use the same camera in both real preview viewports. */
    UFUNCTION(BlueprintCallable, Category="NPR|Outline|Validation")
    static bool SetComparisonCamera(FVector Location, FRotator Rotation);
    UFUNCTION(BlueprintCallable, Category="NPR|Outline|Validation")
    static bool GetComparisonCamera(FVector& Location, FRotator& Rotation);
    UFUNCTION(BlueprintCallable, Category="NPR|Outline|Validation")
    static bool SetComparisonScreenPercentage(int32 Percentage);
    UFUNCTION(BlueprintPure, Category="NPR|Outline|Validation")
    static FString GetComparisonTemporalState();
    /** Restricted to the owned validation actor; does not save an asset or map. */
    UFUNCTION(BlueprintCallable, Category="NPR|Outline|Validation")
    static bool SetValidationScreenTemplate(UNPROutlineActorComponent* Component, UMaterialInterface* Material);

    /** Developer/automation gate; waits for actual shader compilation and reports backend errors. */
    UFUNCTION(BlueprintCallable, Category = "NPR|Outline|Editor")
    static FString FinishBackendCompilation();
};
