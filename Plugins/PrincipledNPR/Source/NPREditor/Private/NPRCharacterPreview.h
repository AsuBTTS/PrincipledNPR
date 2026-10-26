#pragma once

#include "CoreMinimal.h"

class AActor;
class UMeshComponent;
class UMaterialInterface;
class SWidget;

/** Shared Outline-style preview. All geometry, isolation and effect filtering live in preview worlds. */
class FNPRCharacterPreview
{
public:
    virtual ~FNPRCharacterPreview() = default;
    virtual TSharedRef<SWidget> Widget() = 0;
    virtual void LoadActor(AActor* Actor) = 0;
    virtual void Refresh() = 0;
    virtual bool Isolate(UMeshComponent* Mesh, int32 SlotIndex) = 0;
    virtual void SetRimTarget(UMeshComponent* Mesh, int32 SlotIndex) = 0;
    virtual void SetRimSolo(bool bSolo) = 0;
    virtual void SetEffectFilter(FName Effect) = 0;
    virtual bool Focus() = 0;
    virtual int32 MeshCount() const = 0;
    virtual bool Export(const FString& Directory) = 0;
    virtual UMaterialInterface* PreviewMaterial(bool bOriginal, UMeshComponent* Mesh, int32 SlotIndex) const = 0;
    virtual bool IsMeshVisible(bool bOriginal, UMeshComponent* Mesh) const = 0;
};

TSharedRef<FNPRCharacterPreview> MakeNPRCharacterPreview();
