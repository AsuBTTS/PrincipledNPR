#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NPRModulationEditorLibrary.generated.h"

class AActor;
class UMeshComponent;

USTRUCT(BlueprintType)
struct NPREDITOR_API FNPRMeshEffectInfo
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="NPR") TObjectPtr<UMeshComponent> Mesh;
    UPROPERTY(BlueprintReadOnly, Category="NPR") int32 SlotIndex = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category="NPR") FName SlotName;
    UPROPERTY(BlueprintReadOnly, Category="NPR") TArray<FName> Effects;
    UPROPERTY(BlueprintReadOnly, Category="NPR") TArray<FName> Categories;
    UPROPERTY(BlueprintReadOnly, Category="NPR") FString Description;
};

/** Read-only inventory describes registered plugin effects, never arbitrary third-party material graphs. */
UCLASS()
class NPREDITOR_API UNPRModulationEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="NPR|Editor") static void OpenWorkspace();
    UFUNCTION(BlueprintPure, Category="NPR|Editor") static TArray<FNPRMeshEffectInfo> GetMeshEffects(AActor* Actor);
    /** Overview, Rim, Outline: opens the matching existing editor without changing renderer settings. */
    UFUNCTION(BlueprintCallable, Category="NPR|Editor") static bool SelectFeature(FName Feature);
    UFUNCTION(BlueprintCallable, Category="NPR|Editor") static bool EditEffect(UMeshComponent* Mesh,int32 SlotIndex,FName Effect);
    UFUNCTION(BlueprintCallable, Category="NPR|Editor") static bool IsolatePreview(UMeshComponent* Mesh, int32 SlotIndex = -1);
    UFUNCTION(BlueprintCallable, Category="NPR|Editor") static bool ExportPreview(const FString& Directory);
    UFUNCTION(BlueprintPure, Category="NPR|Editor") static int32 GetPreviewMeshCount();
};
