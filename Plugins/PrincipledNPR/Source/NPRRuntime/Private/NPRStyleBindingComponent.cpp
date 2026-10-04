#include "NPRStyleBindingComponent.h"

#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NPRBackendStylePackage.h"

UNPRStyleBindingComponent::UNPRStyleBindingComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

bool UNPRStyleBindingComponent::ApplyPackage(UMeshComponent* Mesh, const UNPRBackendStylePackage* Package, FString& OutError)
{
    OutError.Reset();
    if (!IsValid(Mesh) || !IsValid(Package) || Package->MaterialSlots.IsEmpty())
    {
        OutError = TEXT("A valid mesh and loaded, nonempty backend package are required.");
        return false;
    }
    if (IsValid(BoundMesh) && BoundMesh != Mesh)
    {
        OutError = TEXT("RestoreMaterials before binding a different mesh.");
        return false;
    }

    TSet<int32> Slots;
    for (const FNPRBackendMaterialSlot& Slot : Package->MaterialSlots)
    {
        if (Slot.SlotIndex < 0 || Slot.SlotIndex >= Mesh->GetNumMaterials() ||
            !IsValid(Slot.Material) || Slots.Contains(Slot.SlotIndex))
        {
            OutError = FString::Printf(TEXT("Invalid, missing or duplicate material slot %d."), Slot.SlotIndex);
            return false;
        }
        Slots.Add(Slot.SlotIndex);
    }

    TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> NewMaterials;
    for (const FNPRBackendMaterialSlot& Slot : Package->MaterialSlots)
    {
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Slot.Material, this);
        if (!IsValid(MID))
        {
            OutError = TEXT("Unable to create the MID bundle; mesh was not changed.");
            return false;
        }
        NewMaterials.Add(Slot.SlotIndex, MID);
    }

    RestoreMaterials();
    BoundMesh = Mesh;
    for (const auto& Entry : NewMaterials)
    {
        OriginalMaterials.Add(Entry.Key, Mesh->GetMaterial(Entry.Key));
        Mesh->SetMaterial(Entry.Key, Entry.Value);
    }
    ActiveMaterials = MoveTemp(NewMaterials);
    return true;
}

bool UNPRStyleBindingComponent::SetScalarParameter(FName ParameterName, float Value, FString& OutError)
{
    OutError.Reset();
    if (ParameterName.IsNone() || !FMath::IsFinite(Value) || ActiveMaterials.IsEmpty())
    {
        OutError = TEXT("A finite value, parameter name and active binding are required.");
        return false;
    }

    const FHashedMaterialParameterInfo ParameterInfo(ParameterName);
    for (const auto& Entry : ActiveMaterials)
    {
        float CurrentValue = 0.0f;
        if (!IsValid(Entry.Value) || !Entry.Value->GetScalarParameterValue(ParameterInfo, CurrentValue))
        {
            OutError = FString::Printf(TEXT("Scalar parameter '%s' is absent in slot %d; no parameters changed."),
                *ParameterName.ToString(), Entry.Key);
            return false;
        }
    }
    for (const auto& Entry : ActiveMaterials)
    {
        Entry.Value->SetScalarParameterValue(ParameterName, Value);
    }
    return true;
}

void UNPRStyleBindingComponent::RestoreMaterials()
{
    if (IsValid(BoundMesh))
    {
        for (const auto& Entry : OriginalMaterials)
        {
            const auto* Owned = ActiveMaterials.Find(Entry.Key);
            // Preserve any material change made by another system after our binding.
            if (Owned && BoundMesh->GetMaterial(Entry.Key) == Owned->Get())
            {
                BoundMesh->SetMaterial(Entry.Key, Entry.Value);
            }
        }
    }
    ActiveMaterials.Reset();
    OriginalMaterials.Reset();
    BoundMesh = nullptr;
}

void UNPRStyleBindingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RestoreMaterials();
    Super::EndPlay(EndPlayReason);
}
