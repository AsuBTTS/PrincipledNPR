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
    return IsValid(Mesh) && BoundMesh == Mesh && Active &&
        IsValid(Active->Get()) && SlotIndex >= 0 && SlotIndex < Mesh->GetNumMaterials() &&
        Mesh->GetMaterial(SlotIndex) == Active->Get() &&
        (ActiveMaterials.Num() == 1 || FindMatCap(Mesh, SlotIndex) || (RimMesh == Mesh && RimSlotIndex == SlotIndex));
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
    // A null texture must not inherit the engine's detailed DefaultNormal map from the recipe.
    MID->SetScalarParameterValue(TEXT("NPR_NormalStrength"), Inputs.NormalTexture ? Inputs.NormalStrength : 0.f);
    MID->SetVectorParameterValue(TEXT("NPR_EmissiveColor"),
        FLinearColor(Inputs.EmissiveColor.R, Inputs.EmissiveColor.G, Inputs.EmissiveColor.B, 1.f));
    if (Inputs.EmissiveTexture)
    {
        MID->SetTextureParameterValue(TEXT("NPR_EmissiveTexture"), Inputs.EmissiveTexture);
    }
    MID->SetScalarParameterValue(TEXT("NPR_EmissiveScale"), Inputs.EmissiveScale);
    if (!SetRimParametersForSlot(SlotIndex, Parameters, OutError))
    {
        RestoreMaterials();
        return false;
    }
    return true;
}

bool UNPRStyleBindingComponent::ApplyConfiguredRim(FString& OutError)
{
    OutError.Reset();
    if (!IsValid(RimMesh) || (!bUseRimLocalParameters && !IsValid(RimProfile)) || (!IsValid(RimGeneratedMaterial) && !IsValid(RimRecipe)) || RimSlotIndex < 0)
    {
        OutError = TEXT("Saved Rim binding is missing a mesh, slot, Style Profile or Surface recipe.");
        return false;
    }
    FNPRRimParameters Parameters=RimLocalParameters;
    if (!bUseRimLocalParameters && !RimProfile->ResolveRimParameters(RimMaterialKind, Parameters, OutError))
    {
        return false;
    }
    // The generated Toon copy already contains the original Rim contribution. Updating Rim
    // must keep this shared material rather than replacing it with the older Rim parent.
    if (ToonPackage && ToonMesh == RimMesh && GetOwnedMaterial(RimMesh, RimSlotIndex))
        return SetRimParametersForSlot(RimSlotIndex, Parameters, OutError);
    return IsValid(RimGeneratedMaterial)
        ? ApplyRimMaterial(RimMesh, RimSlotIndex, RimGeneratedMaterial, Parameters, OutError)
        : ApplyRimPreview(RimMesh, RimSlotIndex, RimRecipe, RimSurfaceInputs, Parameters, OutError);
}

bool UNPRStyleBindingComponent::ApplyConfiguredToon(FString& OutError)
{
    OutError.Reset();
    if(!IsValid(ToonMesh)||!IsValid(ToonPackage)||!ToonPackage->bHasToonParameters||
        (GetOwner()&&ToonMesh->GetOwner()!=GetOwner()))
    {OutError=TEXT("Saved Toon binding requires a mesh on this actor and a generated Toon style package.");return false;}
    if(!ToonPackage->ToonParameters.Validate(OutError)||!ToonPackage->ToonResponse.Validate(OutError))return false;
    return ApplyPackage(ToonMesh,ToonPackage,OutError);
}

bool UNPRStyleBindingComponent::ApplyRimMaterial(UMeshComponent* Mesh, int32 SlotIndex,
    UMaterialInterface* Material, const FNPRRimParameters& Parameters, FString& OutError)
{
    OutError.Reset();
    if (!IsValid(Mesh) || !IsValid(Material) || SlotIndex<0 || SlotIndex>=Mesh->GetNumMaterials() ||
        (GetOwner() && Mesh->GetOwner()!=GetOwner()) || !Parameters.Validate(OutError))
    { if (OutError.IsEmpty()) OutError=TEXT("请选择当前角色的有效材质槽和 Rim 材质。"); return false; }
    // Validate the complete parameter bundle before replacing anything.
    float Scalar=0; FLinearColor Color;
    for (const FName Name : {FName(NPRRimContract::WidthMaterialName),FName(NPRRimContract::SoftnessMaterialName),FName(NPRRimContract::IntensityMaterialName)})
        if (!Material->GetScalarParameterValue(FHashedMaterialParameterInfo(Name),Scalar))
        { OutError=TEXT("生成的材质缺少 Rim 参数；原材质保持不变。"); return false; }
    if (!Material->GetVectorParameterValue(FHashedMaterialParameterInfo(FName(NPRRimContract::ColorMaterialName)),Color))
    { OutError=TEXT("生成的材质缺少 Rim 颜色；原材质保持不变。"); return false; }
    auto* Package=NewObject<UNPRBackendStylePackage>(this);
    FNPRBackendMaterialSlot& Slot=Package->MaterialSlots.AddDefaulted_GetRef();
    Slot.SlotIndex=SlotIndex; Slot.Material=Material;
    if (IsValid(BoundMesh) && BoundMesh!=Mesh) RestoreMaterials();
    return ApplyPackage(Mesh,Package,OutError) && SetRimParametersForSlot(SlotIndex,Parameters,OutError);
}

void UNPRStyleBindingComponent::OnRegister()
{
    Super::OnRegister();
    ApplySavedEffects();
}

void UNPRStyleBindingComponent::ApplySavedEffects()
{
#if WITH_EDITOR
    bHadMatCapConfiguration = HasMatCapSettings();
#endif
    if(IsValid(ToonMesh)&&IsValid(ToonPackage))
    {
        FString Error;
        if(!ApplyConfiguredToon(Error))
            UE_LOG(LogTemp,Warning,TEXT("NPR Toon binding on '%s' was not restored: %s"),*GetNameSafe(GetOwner()),*Error);
    }
    if (IsValid(RimMesh) && (bUseRimLocalParameters || IsValid(RimProfile)) && (IsValid(RimGeneratedMaterial) || IsValid(RimRecipe)) && RimSlotIndex >= 0)
    {
        FString Error;
        if (!ApplyConfiguredRim(Error))
        {
            UE_LOG(LogTemp, Warning, TEXT("NPR Rim binding on '%s' was not restored: %s"), *GetNameSafe(GetOwner()), *Error);
        }
    }
    FString MatCapError;
    if (!ApplyConfiguredMatCap(MatCapError))
        UE_LOG(LogTemp, Warning, TEXT("NPR MatCap binding on '%s' was not restored: %s"), *GetNameSafe(GetOwner()), *MatCapError);
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
        const auto* MatCap = FindMatCap(Mesh, Slot.SlotIndex);
        const bool bMatchingMatCap = MatCap && MatCap->Source == Slot.Material;
        UMaterialInterface* Parent = bMatchingMatCap ? MatCap->Material.Get() : Slot.Material.Get();
        UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, this);
        if (!IsValid(MID))
        {
            OutError = TEXT("Unable to create the MID bundle; mesh was not changed.");
            return false;
        }
        if (MatCap && !MatCap->Parameters.Apply(*MID, OutError)) return false;
        NewMaterials.Add(Slot.SlotIndex, MID);
    }

    // A package replacing one effect must not drop separately configured MatCap slots.
    TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> RetainedMatCaps;
    if (BoundMesh == Mesh)
    {
        for (const auto& Settings : MatCapSlots)
        {
            if (Settings.Mesh == Mesh && !NewMaterials.Contains(Settings.Slot))
            {
                if (auto* Existing = GetOwnedMaterial(Mesh, Settings.Slot)) RetainedMatCaps.Add(Settings.Slot, Existing);
            }
        }
    }

    RestoreMaterials();
    BoundMesh = Mesh;
    for (const auto& Entry : NewMaterials)
    {
        OriginalMaterials.Add(Entry.Key, Mesh->GetMaterial(Entry.Key));
        Mesh->SetMaterial(Entry.Key, Entry.Value);
    }
    for (const auto& Entry : RetainedMatCaps)
    {
        OriginalMaterials.Add(Entry.Key, Mesh->GetMaterial(Entry.Key));
        Mesh->SetMaterial(Entry.Key, Entry.Value);
        NewMaterials.Add(Entry.Key, Entry.Value);
    }
    ActiveMaterials = MoveTemp(NewMaterials);
    return true;
}

const FNPRMatCapSlot* UNPRStyleBindingComponent::FindMatCap(const UMeshComponent* Mesh, int32 Slot) const
{
    return MatCapSlots.FindByPredicate([Mesh, Slot](const FNPRMatCapSlot& Entry) { return Entry.Mesh == Mesh && Entry.Slot == Slot; });
}

bool UNPRStyleBindingComponent::ApplyConfiguredMatCap(FString& OutError)
{
    OutError.Reset();
    for (const auto& Settings : MatCapSlots)
    {
        if (!Settings.Parameters.Validate(OutError)) return false;
        if (!IsValid(Settings.Mesh) || Settings.Slot < 0 || Settings.Slot >= Settings.Mesh->GetNumMaterials() ||
            !IsValid(Settings.Source) || !FNPRMatCapParameters::IsSupported(Settings.Material))
        {
            OutError = TEXT("有 MatCap 槽位引用无效的网格或材质。");
            return false;
        }
    }
    for (const auto& Settings : MatCapSlots)
        if (!ApplyMatCapSlot(Settings, OutError)) return false;
    return true;
}

bool UNPRStyleBindingComponent::ApplyMatCapSlot(const FNPRMatCapSlot& Settings, FString& OutError, bool bConfigure)
{
    OutError.Reset();
    auto* Mesh = Settings.Mesh.Get();
    const int32 Slot = Settings.Slot;
    if (!IsValid(Mesh) || Mesh->GetOwner() != GetOwner() || Slot < 0 || Slot >= Mesh->GetNumMaterials() ||
        (IsValid(BoundMesh) && BoundMesh != Mesh) || !IsValid(Settings.Source) || !FNPRMatCapParameters::IsSupported(Settings.Material))
    {
        OutError = TEXT("MatCap 绑定需要当前角色的有效材质槽和已生成的材质。");
        return false;
    }
    auto* Owned = GetOwnedMaterial(Mesh, Slot);
    // A later saved Toon or Rim graph can already contain the MatCap expressions.
    if (!bConfigure && Owned && FNPRMatCapParameters::IsSupported(Owned)) return Settings.Parameters.Apply(*Owned, OutError);
    const auto* Previous = FindMatCap(Mesh, Slot);
    if ((!Owned && BoundMesh == Mesh && ActiveMaterials.Contains(Slot)) ||
        (bConfigure && !Owned && Mesh->GetMaterial(Slot) != Settings.Source) ||
        (Owned && Owned->Parent != Settings.Source && Owned->Parent != Settings.Material &&
            (!Previous || Owned->Parent != Previous->Material)))
    {
        OutError = TEXT("材质槽已被替换。请重新读取角色和材质槽。");
        return false;
    }
    if (Owned && Owned->Parent == Settings.Material) return Settings.Parameters.Apply(*Owned, OutError);
    auto* Instance = UMaterialInstanceDynamic::Create(Settings.Material, this);
    if (!Instance)
    {
        OutError = TEXT("无法为 MatCap 创建运行时材质实例。");
        return false;
    }
    if (Owned) Instance->CopyParameterOverrides(Owned);
    if (!Settings.Parameters.Apply(*Instance, OutError)) return false;
    if (!Owned) OriginalMaterials.Add(Slot, Mesh->GetMaterial(Slot));
    BoundMesh = Mesh;
    Mesh->SetMaterial(Slot, Instance);
    ActiveMaterials.Add(Slot, Instance);
    return true;
}

bool UNPRStyleBindingComponent::ConfigureMatCap(const FNPRMatCapSlot& Settings, FString& OutError)
{
    if (!ApplyMatCapSlot(Settings, OutError, true)) return false;
    if (auto* Entry = MatCapSlots.FindByPredicate([&](const FNPRMatCapSlot& Value) { return Value.Mesh == Settings.Mesh && Value.Slot == Settings.Slot; }))
    {
        *Entry = Settings;
    }
    else
    {
        MatCapSlots.Add(Settings);
    }
#if WITH_EDITOR
    bHadMatCapConfiguration = true;
#endif
    return true;
}

bool UNPRStyleBindingComponent::SetMatCapParameters(UMeshComponent* Mesh, int32 SlotIndex, const FNPRMatCapParameters& Parameters, FString& OutError)
{
    auto* Instance = GetOwnedMaterial(Mesh, SlotIndex);
    if (!Instance || !FindMatCap(Mesh, SlotIndex))
    {
        OutError = TEXT("请先应用 MatCap。当前槽位没有可更新的 MatCap 绑定。");
        return false;
    }
    return Parameters.Apply(*Instance, OutError);
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
    return WriteRimParameters(Parameters, OutError, {});
}

bool UNPRStyleBindingComponent::SetRimParametersForSlot(int32 SlotIndex, const FNPRRimParameters& Parameters, FString& OutError)
{
    return WriteRimParameters(Parameters, OutError, SlotIndex);
}

bool UNPRStyleBindingComponent::WriteRimParameters(const FNPRRimParameters& Parameters, FString& OutError, TOptional<int32> SlotFilter)
{
    if (!Parameters.Validate(OutError))
    {
        return false;
    }
    if (!IsValid(BoundMesh) || ActiveMaterials.IsEmpty() || (SlotFilter.IsSet() && !ActiveMaterials.Contains(SlotFilter.GetValue())))
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
        if (SlotFilter.IsSet() && Entry.Key != SlotFilter.GetValue()) continue;
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
        if (SlotFilter.IsSet() && Entry.Key != SlotFilter.GetValue()) continue;
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
#if WITH_EDITOR
    SuspendedSaveSlots.Reset();
#endif
}

void UNPRStyleBindingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RestoreMaterials();
    Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void UNPRStyleBindingComponent::SuspendMatCapForSave()
{
    if (!HasMatCapSettings() || !IsValid(BoundMesh)) return;
    for (const auto& Entry : OriginalMaterials)
    {
        if (GetOwnedMaterial(BoundMesh, Entry.Key))
        {
            SuspendedSaveSlots.Add(Entry.Key);
            BoundMesh->SetMaterial(Entry.Key, Entry.Value);
        }
    }
}

void UNPRStyleBindingComponent::ResumeMatCapAfterSave()
{
    if (IsValid(BoundMesh))
    {
        for (int32 Slot : SuspendedSaveSlots)
        {
            if (Slot < BoundMesh->GetNumMaterials() && BoundMesh->GetMaterial(Slot) == OriginalMaterials.FindRef(Slot))
                BoundMesh->SetMaterial(Slot, ActiveMaterials.FindRef(Slot));
        }
    }
    SuspendedSaveSlots.Reset();
}

void UNPRStyleBindingComponent::PostEditUndo()
{
    if (!HasMatCapSettings() && !bHadMatCapConfiguration)
    {
        Super::PostEditUndo();
        return;
    }
    RestoreMaterials();
    Super::PostEditUndo();
    if (IsRegistered()) ApplySavedEffects();
}
#endif
