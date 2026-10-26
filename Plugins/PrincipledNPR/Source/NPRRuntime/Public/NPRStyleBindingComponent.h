#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPRRimParameters.h"
#include "NPRSemanticTypes.h"
#include "NPRStyleBindingComponent.generated.h"

class UMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNPRBackendStylePackage;
class UNPRStyleProfile;
class UNPRSurfaceRecipe;
class UTexture2D;

/** Explicit source values for the prebuilt Surface material. No arbitrary graph conversion occurs. */
USTRUCT(BlueprintType)
struct NPRRUNTIME_API FNPRRimSurfaceInputs
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Surface")
    bool bConfigured = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Surface", meta=(HideAlphaChannel))
    FLinearColor BaseColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Surface")
    TObjectPtr<UTexture2D> BaseColorTexture = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Surface")
    TObjectPtr<UTexture2D> NormalTexture = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Surface", meta=(ClampMin="0", ClampMax="1"))
    float NormalStrength = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Surface", meta=(HideAlphaChannel))
    FLinearColor EmissiveColor = FLinearColor::Black;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Surface")
    TObjectPtr<UTexture2D> EmissiveTexture = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPR|Surface", meta=(ClampMin="0"))
    float EmissiveScale = 1.f;

    bool Validate(FString& OutError) const;
};

/** Minimal explicit binding. Asset streaming, semantic resolving and calibration remain separate. */
UCLASS(ClassGroup = (NPR), meta = (BlueprintSpawnableComponent))
class NPRRUNTIME_API UNPRStyleBindingComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UNPRStyleBindingComponent();

    /** The package must already be loaded; validates every slot before changing the mesh. */
    UFUNCTION(BlueprintCallable, Category = "NPR|Binding")
    bool ApplyPackage(UMeshComponent* Mesh, const UNPRBackendStylePackage* Package, FString& OutError);

    /** Compose one compatible Surface slot, bind its MID, then apply explicit base inputs and Rim. */
    UFUNCTION(BlueprintCallable, Category = "NPR|Rim")
    bool ApplyRimPreview(UMeshComponent* Mesh, int32 SlotIndex, UNPRSurfaceRecipe* Recipe,
        const FNPRRimSurfaceInputs& Inputs, const FNPRRimParameters& Parameters, FString& OutError);

    /** Reapply the persisted actor/slot/profile configuration after a map is reopened. */
    UFUNCTION(BlueprintCallable, Category = "NPR|Rim")
    bool ApplyConfiguredRim(FString& OutError);

    bool IsRimBoundTo(const UMeshComponent* Mesh, int32 SlotIndex) const;

    /** Read-only ownership queries for the editor's effect inventory and original comparison. */
    bool OwnsBoundMaterials() const;
    bool HasTrackedMaterials() const { return IsValid(BoundMesh) && !ActiveMaterials.IsEmpty(); }
    UMaterialInstanceDynamic* GetOwnedMaterial(const UMeshComponent* Mesh, int32 SlotIndex) const;
    UMaterialInterface* GetSourceMaterial(const UMeshComponent* Mesh, int32 SlotIndex) const;

    /** Saved actor configuration; preview changes stay transient until the editor saves the level. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "NPR|Rim")
    TObjectPtr<UMeshComponent> RimMesh = nullptr;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "NPR|Rim", meta=(ClampMin="0"))
    int32 RimSlotIndex = INDEX_NONE;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "NPR|Rim")
    TObjectPtr<UNPRStyleProfile> RimProfile = nullptr;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "NPR|Rim")
    TObjectPtr<UNPRSurfaceRecipe> RimRecipe = nullptr;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "NPR|Rim")
    ENPRMaterialKind RimMaterialKind = ENPRMaterialKind::Generic;

    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "NPR|Rim")
    FNPRRimSurfaceInputs RimSurfaceInputs;

    /** Updates only existing scalar parameters. Does not mutate ToonProfile or static switches. */
    UFUNCTION(BlueprintCallable, Category = "NPR|Binding")
    bool SetScalarParameter(FName ParameterName, float Value, FString& OutError);

    /** Validates the four canonical Rim values and every active slot before any MID write. */
    UFUNCTION(BlueprintCallable, Category = "NPR|Binding")
    bool SetRimParameters(const FNPRRimParameters& Parameters, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "NPR|Binding")
    void RestoreMaterials();

protected:
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UMeshComponent> BoundMesh;

    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<UMaterialInterface>> OriginalMaterials;

    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> ActiveMaterials;
};
