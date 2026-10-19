#include "NPROutlineComponent.h"
#include "NPROutlineActorComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

UNPROutlineComponent::UNPROutlineComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    bTickInEditor = true;
}

void UNPROutlineComponent::OnRegister()
{
    Super::OnRegister();
    RefreshOutline();
}

void UNPROutlineComponent::OnUnregister()
{
    ReleaseHelpers();
    Super::OnUnregister();
}

void UNPROutlineComponent::ReleaseHelpers()
{
    if (IsValid(PostProcess)) PostProcess->DestroyComponent();
    if (IsValid(OutlineMesh)) OutlineMesh->DestroyComponent();
    PostProcess = nullptr;
    OutlineMesh = nullptr;
    OutlineMID = nullptr;
    InvisibleMID = nullptr;
    ScreenMID = nullptr;
    BuiltMethod = ENPROutlineMethod::None;
    BuiltTarget.Reset();
    BuiltSlot = INDEX_NONE;
    AllocatedStencil = 0;
}

int32 UNPROutlineComponent::AllocateStencil() const
{
    TSet<int32> Used;
    for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
    {
        if (It->GetWorld() == GetWorld() && It->bRenderCustomDepth && *It != OutlineMesh)
            Used.Add(It->CustomDepthStencilValue);
    }
    for (int32 Id = 1; Id < 256; ++Id) if (!Used.Contains(Id)) return Id;
    return 0;
}

bool UNPROutlineComponent::RefreshOutline()
{
    if (IsTemplate() || !GetWorld() || !GetOwner()) return false;
    if (!Settings.Validate(LastError)) { ReleaseHelpers(); return false; }
    if (!IsValid(TargetMesh) || !TargetMesh->GetSkeletalMeshAsset() ||
        MaterialSlotIndex < 0 || MaterialSlotIndex >= TargetMesh->GetNumMaterials() ||
        TargetMesh->GetMaterialIndex(MaterialSlotName) != MaterialSlotIndex)
    {
        LastError = TEXT("A matching skeletal mesh and stable material-slot binding are required.");
        ReleaseHelpers();
        return false;
    }
    if (Settings.Method == ENPROutlineMethod::None || Settings.GetResolvedWidth() <= UE_SMALL_NUMBER)
    {
        ReleaseHelpers();
        AppliedSettings = Settings;
        BuiltTarget = TargetMesh;
        BuiltSlot = MaterialSlotIndex;
        LastError.Reset();
        return true;
    }
    for(auto* Coordinator:TInlineComponentArray<UNPROutlineActorComponent*>(GetOwner()))
        if(Coordinator->bEnabled)
        { LastError=TEXT("Disable the actor Outline coordinator before enabling a legacy slot binding."); ReleaseHelpers(); return false; }
    if (!IsValid(MaskTemplate) || (Settings.Method == ENPROutlineMethod::InvertedHull && !IsValid(HullTemplate)) ||
        (Settings.Method == ENPROutlineMethod::ScreenSpace && !IsValid(ScreenTemplate)))
    {
        LastError = TEXT("Outline backend materials are missing. Create the plugin outline assets first.");
        ReleaseHelpers();
        return false;
    }
    const bool bRebuild = !IsValid(OutlineMesh) || BuiltMethod != Settings.Method ||
        BuiltTarget != TargetMesh || BuiltSlot != MaterialSlotIndex ||
        OutlineMesh->GetSkeletalMeshAsset() != TargetMesh->GetSkeletalMeshAsset();
    if (bRebuild)
    {
        ReleaseHelpers();
        if (Settings.Method == ENPROutlineMethod::ScreenSpace)
        {
            if (TargetMesh->bRenderCustomDepth)
            {
                LastError = TEXT("The source mesh already writes Custom Depth. Use Inverted Hull or explicitly resolve the source stencil ownership first.");
                return false;
            }
            const IConsoleVariable* CustomDepth = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"));
            if (!CustomDepth || CustomDepth->GetInt() != 3)
            {
                LastError = TEXT("Screen Space requires Custom Depth-Stencil Pass = Enabled with Stencil (r.CustomDepth=3), then restart the editor.");
                return false;
            }
            AllocatedStencil = AllocateStencil();
            if (!AllocatedStencil) { LastError = TEXT("No free custom stencil ID remains in this world."); return false; }
        }
        InvisibleMID = UMaterialInstanceDynamic::Create(MaskTemplate, this);
        InvisibleMID->SetScalarParameterValue(TEXT("Visible"), 0);
        OutlineMID = UMaterialInstanceDynamic::Create(
            Settings.Method == ENPROutlineMethod::InvertedHull ? HullTemplate : MaskTemplate, this);
        OutlineMID->SetScalarParameterValue(TEXT("Visible"), 1);
        OutlineMesh = NewObject<USkeletalMeshComponent>(GetOwner(), NAME_None, RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);
        OutlineMesh->SetupAttachment(TargetMesh);
        OutlineMesh->SetSkeletalMeshAsset(TargetMesh->GetSkeletalMeshAsset());
        OutlineMesh->SetLeaderPoseComponent(TargetMesh);
        OutlineMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        OutlineMesh->SetCastShadow(false);
        OutlineMesh->SetReceivesDecals(false);
        OutlineMesh->SetBoundsScale(1.2f);
        for (int32 Slot = 0; Slot < TargetMesh->GetNumMaterials(); ++Slot)
            OutlineMesh->SetMaterial(Slot, Slot == MaterialSlotIndex ? OutlineMID.Get() : InvisibleMID.Get());
        if (Settings.Method == ENPROutlineMethod::ScreenSpace)
        {
            OutlineMesh->SetRenderInMainPass(false);
            OutlineMesh->SetRenderInDepthPass(false);
            OutlineMesh->SetRenderCustomDepth(true);
            OutlineMesh->SetCustomDepthStencilValue(AllocatedStencil);
            ScreenMID = UMaterialInstanceDynamic::Create(ScreenTemplate, this);
            ScreenMID->SetScalarParameterValue(TEXT("StencilId"), AllocatedStencil);
            PostProcess = NewObject<UPostProcessComponent>(GetOwner(), NAME_None, RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);
            PostProcess->SetupAttachment(TargetMesh);
            PostProcess->bUnbound = true;
            PostProcess->Priority = 100;
            PostProcess->Settings.AddBlendable(ScreenMID, 1);
            PostProcess->RegisterComponent();
        }
        OutlineMesh->RegisterComponent();
        BuiltMethod = Settings.Method;
        BuiltTarget = TargetMesh;
        BuiltSlot = MaterialSlotIndex;
    }
    UpdateParameters();
    AppliedSettings = Settings;
    LastError.Reset();
    return true;
}

void UNPROutlineComponent::UpdateParameters()
{
    if (IsValid(OutlineMID))
    {
        Settings.ApplyMaterialParameters(OutlineMID);
    }
    if (IsValid(ScreenMID))
    {
        Settings.ApplyMaterialParameters(ScreenMID);
    }
}

void UNPROutlineComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (Settings != AppliedSettings || (Settings.Method != ENPROutlineMethod::None && Settings.GetResolvedWidth()>UE_SMALL_NUMBER && !IsValid(OutlineMesh)) ||
        BuiltTarget != TargetMesh || BuiltSlot != MaterialSlotIndex ||
        (IsValid(OutlineMesh) && IsValid(TargetMesh) && OutlineMesh->GetSkeletalMeshAsset() != TargetMesh->GetSkeletalMeshAsset()))
        RefreshOutline();
    if (IsValid(OutlineMesh) && IsValid(TargetMesh))
        OutlineMesh->SetVisibility(TargetMesh->IsVisible());
}

bool UNPROutlineComponent::ApplyPreset(UNPROutlinePreset* InPreset, FString& OutError)
{
    OutError.Reset();
    if (!IsValid(InPreset) || InPreset->SchemaVersion != 1 || !InPreset->Settings.Validate(OutError))
    {
        if (OutError.IsEmpty()) OutError = TEXT("A supported outline preset is required.");
        return false;
    }
    const FNPROutlineSettings PreviousSettings = Settings;
    UNPROutlinePreset* PreviousPreset = Preset;
    Settings = InPreset->Settings;
    Preset = InPreset;
    const bool bSuccess = RefreshOutline();
    OutError = LastError;
    if (!bSuccess)
    {
        Settings = PreviousSettings;
        Preset = PreviousPreset;
        RefreshOutline();
    }
    return bSuccess;
}

#if WITH_EDITOR
void UNPROutlineComponent::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    Super::PostEditChangeProperty(Event);
    if (IsRegistered()) RefreshOutline();
}
#endif
