#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPRStyleBindingComponent.generated.h"

class UMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNPRBackendStylePackage;

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

    /** Updates only existing scalar parameters. Does not mutate ToonProfile or static switches. */
    UFUNCTION(BlueprintCallable, Category = "NPR|Binding")
    bool SetScalarParameter(FName ParameterName, float Value, FString& OutError);

    UFUNCTION(BlueprintCallable, Category = "NPR|Binding")
    void RestoreMaterials();

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UMeshComponent> BoundMesh;

    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<UMaterialInterface>> OriginalMaterials;

    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> ActiveMaterials;
};
