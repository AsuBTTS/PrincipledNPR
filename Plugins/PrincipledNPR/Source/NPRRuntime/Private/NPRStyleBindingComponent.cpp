#include "NPRStyleBindingComponent.h"

#include "Components/MeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NPRBackendStylePackage.h"
#include "NPRStyleProfile.h"
#include "NPRSurfaceRecipe.h"
#include "Engine/Texture2D.h"

UNPRStyleBindingComponent::UNPRStyleBindingComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

bool FNPRRimSurfaceInputs::Validate(FString& OutError) const
{
    OutError.Reset();
    if (!bConfigured)
    {
        OutError = TEXT("Specify the source BaseColor, Normal and Emissive inputs before replacing a character material.");
        return false;
    }
    const auto ValidRGB = [](const FLinearColor& Color, float Maximum)
    {
        return FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G) && FMath::IsFinite(Color.B) &&
            Color.R >= 0.f && Color.G >= 0.f && Color.B >= 0.f &&
            Color.R <= Maximum && Color.G <= Maximum && Color.B <= Maximum;
    };
    if (!ValidRGB(BaseColor, 1.f) || !ValidRGB(EmissiveColor, TNumericLimits<float>::Max()) ||
        !FMath::IsFinite(NormalStrength) || NormalStrength < 0.f || NormalStrength > 1.f ||
        !FMath::IsFinite(EmissiveScale) || EmissiveScale < 0.f)
    {
        OutError = TEXT("Surface inputs have invalid RGB, normal strength or emissive scale.");
        return false;
    }
    if ((BaseColorTexture && !IsValid(BaseColorTexture)) ||
        (NormalTexture && !IsValid(NormalTexture)) ||
        (EmissiveTexture && !IsValid(EmissiveTexture)))
    {
        OutError = TEXT("A selected Surface input texture is no longer valid.");
        return false;
    }
    return true;
}

bool UNPRStyleBindingComponent::IsRimBoundTo(const UMeshComponent* Mesh, int32 SlotIndex) const
{
    const auto* Active = ActiveMaterials.Find(SlotIndex);
    return IsValid(Mesh) && BoundMesh == Mesh && ActiveMaterials.Num() == 1 && Active &&
        IsValid(Active->Get()) && SlotIndex >= 0 && SlotIndex < Mesh->GetNumMaterials() &&
        Mesh->GetMaterial(SlotIndex) == Active->Get();
}

bool UNPRStyleBindingComponent::OwnsBoundMaterials() const
{
    if (!IsValid(BoundMesh) || ActiveMaterials.IsEmpty()) return false;
    for (const auto& Entry : ActiveMaterials)
        if (!IsValid(Entry.Value) || Entry.Key < 0 || Entry.Key >= BoundMesh->GetNumMaterials() ||
            BoundMesh->GetMaterial(Entry.Key) != Entry.Value) return false;
    return true;
}

UMaterialInstanceDynamic* UNPRStyleBindingComponent::GetOwnedMaterial(const UMeshComponent* Mesh, int32 SlotIndex) const
{
    UMaterialInstanceDynamic* MID = BoundMesh == Mesh ? ActiveMaterials.FindRef(SlotIndex).Get() : nullptr;
    return IsValid(Mesh) && IsValid(MID) && SlotIndex >= 0 && SlotIndex < Mesh->GetNumMaterials() &&
        Mesh->GetMaterial(SlotIndex) == MID ? MID : nullptr;
}

UMaterialInterface* UNPRStyleBindingComponent::GetSourceMaterial(const UMeshComponent* Mesh, int32 SlotIndex) const
{
    if (!IsValid(Mesh) || SlotIndex < 0 || SlotIndex >= Mesh->GetNumMaterials()) return nullptr;
    return GetOwnedMaterial(Mesh, SlotIndex) ? OriginalMaterials.FindRef(SlotIndex).Get() : Mesh->GetMaterial(SlotIndex);
}

bool UNPRStyleBindingComponent::ApplyRimPreview(UMeshComponent* Mesh, int32 SlotIndex,
    UNPRSurfaceRecipe* Recipe, const FNPRRimSurfaceInputs& Inputs,
    const FNPRRimParameters& Parameters, FString& OutError)
{
    OutError.Reset();
    if (!IsValid(Mesh) || !IsValid(Recipe) || SlotIndex < 0 || SlotIndex >= Mesh->GetNumMaterials() ||
        (GetOwner() && Mesh->GetOwner() != GetOwner()))
    {
        OutError = TEXT("Select a mesh and valid material slot on this actor, plus a loaded Surface recipe.");
        return false;
    }
    if (!Inputs.Validate(OutError) || !Parameters.Validate(OutError))
    {
        return false;
    }

    UNPRSurfaceRecipe* SelectedRecipe = nullptr;
    TArray<UNPRSurfaceRecipe*> Candidates{Recipe};
    TArray<FName> Effects{TEXT("RimLight")};
    TArray<int32> Slots{SlotIndex};
    UNPRBackendStylePackage* Package = UNPRSurfaceComposerLibrary::ComposePackage(
        Mesh, Recipe->FamilyId, Candidates, Effects, Slots, SelectedRecipe, OutError);
    if (!Package || SelectedRecipe != Recipe)
    {
        return false;
    }
    if (IsValid(BoundMesh) && BoundMesh != Mesh)
    {
        RestoreMaterials();
    }
    if (!ApplyPackage(Mesh, Package, OutError))
    {
        return false;
    }

    UMaterialInstanceDynamic* MID = ActiveMaterials.FindRef(SlotIndex);
    if (!IsValid(MID))
    {
        RestoreMaterials();
        OutError = TEXT("The Surface recipe did not create an active MID.");
        return false;
    }
    MID->SetVectorParameterValue(TEXT("NPR_BaseColor"),
        FLinearColor(Inputs.BaseColor.R, Inputs.BaseColor.G, Inputs.BaseColor.B, 1.f));
    if (Inputs.BaseColorTexture)
    {
        MID->SetTextureParameterValue(TEXT("NPR_BaseColorTexture"), Inputs.BaseColorTexture);
    }
    if (Inputs.NormalTexture)
    {
        MID->SetTextureParameterValue(TEXT("NPR_NormalTexture"), Inputs.NormalTexture);
    }
    MID->SetScalarParameterValue(TEXT("NPR_NormalStrength"), Inputs.NormalStrength);
    MID->SetVectorParameterValue(TEXT("NPR_EmissiveColor"),
        FLinearColor(Inputs.EmissiveColor.R, Inputs.EmissiveColor.G, Inputs.EmissiveColor.B, 1.f));
    if (Inputs.EmissiveTexture)
    {
        MID->SetTextureParameterValue(TEXT("NPR_EmissiveTexture"), Inputs.EmissiveTexture);
    }
    MID->SetScalarParameterValue(TEXT("NPR_EmissiveScale"), Inputs.EmissiveScale);
    if (!SetRimParameters(Parameters, OutError))
    {
        RestoreMaterials();
        return false;
    }
    return true;
}

bool UNPRStyleBindingComponent::ApplyConfiguredRim(FString& OutError)
{
    OutError.Reset();
    if (!IsValid(RimMesh) || !IsValid(RimProfile) || !IsValid(RimRecipe) || RimSlotIndex < 0)
    {
        OutError = TEXT("Saved Rim binding is missing a mesh, slot, Style Profile or Surface recipe.");
        return false;
    }
    FNPRRimParameters Parameters;
    if (!RimProfile->ResolveRimParameters(RimMaterialKind, Parameters, OutError))
    {
        return false;
    }
    return ApplyRimPreview(RimMesh, RimSlotIndex, RimRecipe, RimSurfaceInputs, Parameters, OutError);
}

void UNPRStyleBindingComponent::OnRegister()
{
    Super::OnRegister();
    if (IsValid(RimMesh) && IsValid(RimProfile) && IsValid(RimRecipe) && RimSlotIndex >= 0)
    {
        FString Error;
        if (!ApplyConfiguredRim(Error))
        {
            UE_LOG(LogTemp, Warning, TEXT("NPR Rim binding on '%s' was not restored: %s"), *GetNameSafe(GetOwner()), *Error);
        }
    }
}

void UNPRStyleBindingComponent::OnUnregister()
{
    RestoreMaterials();
    Super::OnUnregister();
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
