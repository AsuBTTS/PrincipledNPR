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

bool UNPRStyleBindingComponent::SetRimParameters(const FNPRRimParameters& Parameters, FString& OutError)
{
    if (!Parameters.Validate(OutError))
    {
        return false;
    }
    if (!IsValid(BoundMesh) || ActiveMaterials.IsEmpty())
    {
        OutError = TEXT("An active mesh/material binding is required for Rim parameters.");
        return false;
    }

    const FName WidthName(NPRRimContract::WidthMaterialName);
    const FName SoftnessName(NPRRimContract::SoftnessMaterialName);
    const FName IntensityName(NPRRimContract::IntensityMaterialName);
    const FName ColorName(NPRRimContract::ColorMaterialName);
    for (const auto& Entry : ActiveMaterials)
    {
        if (!IsValid(Entry.Value) || Entry.Key < 0 || Entry.Key >= BoundMesh->GetNumMaterials() ||
            BoundMesh->GetMaterial(Entry.Key) != Entry.Value)
        {
            OutError = FString::Printf(TEXT("Rim binding in slot %d was replaced or is invalid; no parameters changed."), Entry.Key);
            return false;
        }
        float Scalar = 0.f;
        FLinearColor Color;
        for (const FName Name : {WidthName, SoftnessName, IntensityName})
        {
            if (!Entry.Value->GetScalarParameterValue(FHashedMaterialParameterInfo(Name), Scalar))
            {
                OutError = FString::Printf(TEXT("Rim scalar '%s' is absent in slot %d; no parameters changed."), *Name.ToString(), Entry.Key);
                return false;
            }
        }
        if (!Entry.Value->GetVectorParameterValue(FHashedMaterialParameterInfo(ColorName), Color))
        {
            OutError = FString::Printf(TEXT("Rim vector '%s' is absent in slot %d; no parameters changed."), *ColorName.ToString(), Entry.Key);
            return false;
        }
    }

    // Preflight completed for every slot. Existing scalar-only writes remain unchanged.
    const float WidthDegrees = Parameters.GetMaterialWidthDegrees();
    const float SoftnessDegrees = Parameters.GetMaterialSoftnessDegrees();
    const FLinearColor RGB(Parameters.RimColor.R, Parameters.RimColor.G, Parameters.RimColor.B, 1.f);
    for (const auto& Entry : ActiveMaterials)
    {
        Entry.Value->SetScalarParameterValue(WidthName, WidthDegrees);
        Entry.Value->SetScalarParameterValue(SoftnessName, SoftnessDegrees);
        Entry.Value->SetScalarParameterValue(IntensityName, Parameters.RimPeakScale);
        Entry.Value->SetVectorParameterValue(ColorName, RGB);
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
