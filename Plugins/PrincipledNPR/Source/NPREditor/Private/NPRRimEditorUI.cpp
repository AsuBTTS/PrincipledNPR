#include "NPRRimEditorUI.h"
#include "NPRRimEditorOptions.h"
#include "NPRRimMaterialBuilder.h"
#include "Materials/MaterialInstanceConstant.h"
#include "NPRStyleProfile.h"
#include "NPRStyleBindingComponent.h"
#include "NPRSurfaceRecipe.h"
#include "NPRCharacterPreview.h"
#include "NPRModulationEditorLibrary.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/MeshComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "IDetailsView.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/NotifyHook.h"
#include "HAL/FileManager.h"
#include "PropertyEditorModule.h"
#include "PropertyEditorDelegates.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
enum class ERimField : uint8 { Width, Softness, Intensity, Color };
FName FieldId(ERimField Field)
{
    switch (Field)
    {
    case ERimField::Width: return NPRRimContract::WidthId;
    case ERimField::Softness: return NPRRimContract::SoftnessId;
    case ERimField::Intensity: return NPRRimContract::IntensityId;
    default: return NPRRimContract::ColorId;
    }
}
bool SaveProfile(UNPRStyleProfile* Profile)
{
    UPackage* Package = Profile->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    return UPackage::SavePackage(Package, Profile, *Filename, Args);
}
}
struct SNPRRimEditor::FImpl : FNotifyHook
{
    virtual ~FImpl() = default;
    TStrongObjectPtr<UNPRRimEditorOptions> Options;
    TSharedPtr<IDetailsView> Details;
    TArray<TWeakPtr<IDetailsView>> SlotDetails;
    FDelegateHandle UndoHandle;
    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<UNPRStyleBindingComponent> Binding;
    TWeakObjectPtr<UMeshComponent> PreviewMesh;
    int32 PreviewSlot = INDEX_NONE;
    bool bTemporary = false;
    bool bPreview = false;
    bool bLoading = false;
    TSharedPtr<FNPRCharacterPreview> CharacterPreview;
    TSharedPtr<SVerticalBox> MeshList;
    bool bSolo = true;
    double NextInventoryUpdate = 0;
    FString InventorySignature;
    bool bControlsExpanded = true;
    TWeakObjectPtr<UMeshComponent> IsolatedMesh;
    int32 IsolatedSlot = INDEX_NONE;
    TMap<TWeakObjectPtr<UMeshComponent>,bool> CollapsedMeshes;
    TMap<TWeakObjectPtr<UMeshComponent>,TMap<int32,float>> LastNonzeroIntensity;
    bool bInteractiveEdit = false;
    virtual void NotifyPostChange(const FPropertyChangedEvent& Event,FProperty*) override {Changed(Event);}
    void RefreshDetails()
    {Details->ForceRefresh();for(auto View:SlotDetails)if(View.IsValid())View.Pin()->ForceRefresh();}
    static bool IsControlProperty(const FPropertyAndParent& P)
    {
        auto IsControl=[](const FProperty* F){return F && (F->GetFName()==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,WidthDegrees)||
            F->GetFName()==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,SoftnessDegrees)||F->GetFName()==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,Intensity)||
            F->GetFName()==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,Color)||F->GetFName()==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,ShadingModel));};
        return IsControl(&P.Property)||P.ParentProperties.ContainsByPredicate(IsControl);
    }
    TSharedRef<IDetailsView> MakeSlotDetails()
    {
        FDetailsViewArgs A;A.bAllowSearch=false;A.bHideSelectionTip=true;A.NameAreaSettings=FDetailsViewArgs::HideNameArea;A.NotifyHook=this;
        auto View=FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(A);
        View->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateStatic(&FImpl::IsControlProperty));
        View->SetObject(Options.Get());SlotDetails.Add(View);return View;
    }
    UMaterialInstanceDynamic* SlotMID(UMeshComponent* Mesh,int32 Slot)const
    {
        if(!Actor.IsValid())return nullptr;
        for(auto* Component:TInlineComponentArray<UNPRStyleBindingComponent*>(Actor.Get()))
            if(auto* MID=Component->GetOwnedMaterial(Mesh,Slot))return MID;
        return nullptr;
    }
    UMaterialInterface* SourceMaterial(UMeshComponent* Mesh,int32 Slot)const
    {
        if(Actor.IsValid())for(auto* Component:TInlineComponentArray<UNPRStyleBindingComponent*>(Actor.Get()))
            if(Component->GetOwnedMaterial(Mesh,Slot))return Component->GetSourceMaterial(Mesh,Slot);
        return IsValid(Mesh)?Mesh->GetMaterial(Slot):nullptr;
    }
    bool SlotEnabled(UMeshComponent* Mesh,int32 Slot)const
    {
        auto* MID=SlotMID(Mesh,Slot);float Value=0;
        return MID && MID->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(NPRRimContract::IntensityMaterialName)),Value) && Value>0;
    }
    void SetIsolation(UMeshComponent* Mesh,int32 Slot)
    {if(CharacterPreview->Isolate(Mesh,Slot)){IsolatedMesh=Mesh;IsolatedSlot=Mesh?Slot:INDEX_NONE;}}
    void ToggleSlot(UMeshComponent* Mesh,int32 Slot,bool Enabled)
    {
        if(Options->Mesh!=Mesh||Options->SlotIndex!=Slot)SelectTarget(Mesh,Slot);
        const FScopedTransaction T(NSLOCTEXT("NPR","ToggleRimSlot","Toggle Rim material slot"));Options->Modify();
        auto& Last=LastNonzeroIntensity.FindOrAdd(Mesh).FindOrAdd(Slot);
        if(Enabled){if(Options->Intensity<=0)Options->Intensity=Last>0?Last:1.f;}
        else {if(Options->Intensity>0)Last=Options->Intensity;Options->Intensity=0;}
        SetOverride(2,true);Preview(false,Enabled);RefreshDetails();
    }
    void ResetParameters()
    {
        const FScopedTransaction T(NSLOCTEXT("NPR","ResetRimSlot","Reset Rim slot parameters"));Options->Modify();
        FNPRRimParameters Defaults; Defaults.RimEdgeHalfWidthRad=FMath::DegreesToRadians(12.0);FString Error;
        if(IsValid(Options->StyleProfile))
        {auto* OnlyDefaults=NewObject<UNPRStyleProfile>();OnlyDefaults->Defaults=Options->StyleProfile->Defaults;
            if(!OnlyDefaults->ResolveRimParameters(Options->MaterialKind,Defaults,Error)){Message(Error,true);return;}}
        Options->OverrideMask=0;Show(Defaults);
        if(IsValid(Options->Mesh))Preview(false);
        else Message(TEXT("已重置当前槽参数；尚未应用的槽不会因此替换材质。"));
    }
    bool HasOverride(int32 Index) const { return (Options->OverrideMask & (1 << Index)) != 0; }
    void SetOverride(int32 Index,bool Set) { if(Set) Options->OverrideMask |= 1 << Index; else Options->OverrideMask &= ~(1 << Index); }
    FString Status = TEXT("Select an actor in the level, then click Read Selection.");
    bool bError = false;
    void Message(const FString& Text, bool Error = false) { Status = Text; bError = Error; }
    struct FDraft
    {
        TStrongObjectPtr<UNPRStyleBindingComponent> Component;
        TStrongObjectPtr<UNPRStyleProfile> Profile;
        TStrongObjectPtr<UMaterialInterface> Material;
        TWeakObjectPtr<UMeshComponent> Mesh;
        int32 Slot=INDEX_NONE;
        FNPRRimParameters Parameters;
        ENPRRimShadingModel Model=ENPRRimShadingModel::KeepOriginal;
        ENPRMaterialKind Kind=ENPRMaterialKind::Generic;
        uint8 Mask=0;
        bool Temporary=false;
    };
    TArray<FDraft> Drafts;
    void Detach()
    {
        Binding.Reset(); PreviewMesh.Reset();PreviewSlot=INDEX_NONE;
        bTemporary=bPreview=false;
    }
    void Release()
    {
        // Every slot keeps its draft active when another card is selected. Closing/retargeting
        // restores all drafts, and reinstates saved bindings without touching external replacements.
        for (auto& Draft:Drafts)
        {
            auto* Component=Draft.Component.Get();
            if (!IsValid(Component)) continue;
            const bool Owned=Component->OwnsBoundMaterials();
            Component->RestoreMaterials();
            if (Draft.Temporary) Component->DestroyComponent();
            else if (Owned) { FString Error;Component->ApplyConfiguredRim(Error); }
        }
        Drafts.Reset();Detach();
    }
    void RememberDraft()
    {
        auto* Draft=Drafts.FindByPredicate([this](const auto& D){return D.Mesh==Options->Mesh&&D.Slot==Options->SlotIndex;});
        if (!Draft) Draft=&Drafts.AddDefaulted_GetRef();
        Draft->Component.Reset(Binding.Get());Draft->Profile.Reset(Options->StyleProfile);
        Draft->Material.Reset(Options->GeneratedMaterial);Draft->Mesh=Options->Mesh;Draft->Slot=Options->SlotIndex;
        Draft->Parameters=Values();Draft->Model=Options->ShadingModel;Draft->Kind=Options->MaterialKind;
        Draft->Mask=Options->OverrideMask;Draft->Temporary=bTemporary;
    }
    FNPRRimParameters Values() const
    {
        FNPRRimParameters Result;
        Result.RimGrazingAngleRad = FMath::DegreesToRadians(Options->WidthDegrees);
        Result.RimEdgeHalfWidthRad = FMath::DegreesToRadians(Options->SoftnessDegrees);
        Result.RimPeakScale = Options->Intensity;
        Result.RimColor = FLinearColor(Options->Color.R,Options->Color.G,Options->Color.B,1.f);
        return Result;
    }
    void Show(const FNPRRimParameters& Values)
    {
        bLoading = true;
        Options->WidthDegrees = FMath::RadiansToDegrees(Values.RimGrazingAngleRad);
        Options->SoftnessDegrees = FMath::RadiansToDegrees(Values.RimEdgeHalfWidthRad);
        Options->Intensity = Values.RimPeakScale;
        Options->Color = Values.RimColor;
        RefreshDetails();
        bLoading = false;
    }
    void Load()
    {
        UNPRStyleProfile* Profile = Options->StyleProfile;
        if (!IsValid(Profile)) { Message(TEXT("Choose or create a Style Profile first."),true); return; }
        FNPRRimParameters Resolved;
        FString Error;
        if (!Profile->ResolveRimParameters(Options->MaterialKind,Resolved,Error))
        { Message(Error,true); return; }
        const FNPRParameterPatch* Patch = Profile->MaterialOverrides.Find(Options->MaterialKind);
        for (int32 I=0;I<4;++I)
        {
            const ERimField Field = static_cast<ERimField>(I);
            if (Field == ERimField::Color)
            {
                const FNPRColorOverride* Entry = Patch ? Patch->Colors.Find(FieldId(Field)) : nullptr;
                SetOverride(I,Entry && Entry->bIsSet);
            }
            else
            {
                const FNPRFloatOverride* Entry = Patch ? Patch->Values.Find(FieldId(Field)) : nullptr;
                SetOverride(I,Entry && Entry->bIsSet);
            }
        }
        Show(Resolved);
        Message(TEXT("风格参数已读取。可在对应材质槽启用 Rim。"));
        if(bPreview) Preview(false);
    }
    void ReadSelection()
    {
        Release();
        IsolatedMesh.Reset();IsolatedSlot=INDEX_NONE;
        Options->Mesh = nullptr;
        Options->SlotIndex = 0;
        Options->StyleProfile = nullptr;
        Options->SurfaceRecipe = nullptr;
        Options->MaterialKind = ENPRMaterialKind::Generic;
        Options->SourceInputs = FNPRRimSurfaceInputs();
        Options->OverrideMask = 0;
        Options->GeneratedMaterial=nullptr; Options->ShadingModel=ENPRRimShadingModel::KeepOriginal;
        FNPRRimParameters Fresh; Fresh.RimEdgeHalfWidthRad=FMath::DegreesToRadians(12.0); Show(Fresh);
        Actor = GEditor->GetSelectedActorCount()==1 ? Cast<AActor>(GEditor->GetSelectedActors()->GetSelectedObject(0)) : nullptr;
        AActor* Selected = Actor.Get();
        if (!Selected) { CharacterPreview->LoadActor(nullptr);BuildTargets();Message(TEXT("请在关卡大纲中只选中一个角色。"),true); return; }
        TInlineComponentArray<UMeshComponent*> Meshes(Selected);
        for (UMeshComponent* Mesh : Meshes)
            if (IsValid(Mesh) && !Mesh->HasAnyFlags(RF_Transient) && Mesh->GetNumMaterials() > 0) { Options->Mesh = Mesh; break; }
        for (UNPRStyleBindingComponent* Existing : TInlineComponentArray<UNPRStyleBindingComponent*>(Selected))
        {
            if ((!Existing->bUseRimLocalParameters && !IsValid(Existing->RimProfile)) || !IsValid(Existing->RimMesh)) continue;
            Options->Mesh = Existing->RimMesh;
            Options->SlotIndex = Existing->RimSlotIndex;
            Options->StyleProfile = Existing->RimProfile;
            Options->SurfaceRecipe = Existing->RimRecipe;
            Options->MaterialKind = Existing->RimMaterialKind;
            Options->SourceInputs = Existing->RimSurfaceInputs;
            Options->GeneratedMaterial=Existing->RimGeneratedMaterial; Options->ShadingModel=Existing->RimShadingModel;
            Binding = Existing;
            if(Existing->bUseRimLocalParameters)Show(Existing->RimLocalParameters);else Load();
            break;
        }
        RefreshDetails();
        CharacterPreview->LoadActor(Selected);
        CharacterPreview->SetRimTarget(Options->Mesh,Options->SlotIndex);
        BuildTargets();
        Message(FString::Printf(TEXT("Actor: %s. Mesh: %s (%d slots)."),
            *Selected->GetActorLabel(),*GetNameSafe(Options->Mesh),
            IsValid(Options->Mesh) ? Options->Mesh->GetNumMaterials() : 0));
    }
    void NewProfile()
    {
        FString PackageName,AssetName;
        FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(
            TEXT("/Game/NPR/Profiles/SP_Rim"),TEXT(""),PackageName,AssetName);
        UPackage* Package = CreatePackage(*PackageName);
        UNPRStyleProfile* Profile = NewObject<UNPRStyleProfile>(Package,*AssetName,RF_Public|RF_Standalone|RF_Transactional);
        Profile->StyleId = FName(*AssetName);
        auto& Soft=Profile->Defaults.Values.FindOrAdd(NPRRimContract::SoftnessId); Soft.bIsSet=true;Soft.Value=FMath::DegreesToRadians(12.0);
        FAssetRegistryModule::AssetCreated(Profile);
        Package->MarkPackageDirty();
        Options->StyleProfile = Profile;
        Load();
        Message(FString::Printf(TEXT("Created %s. Save to write the asset."),*AssetName));
    }
    UNPRStyleBindingComponent* EnsureBinding()
    {
        if (UNPRStyleBindingComponent* Current = Binding.Get())
        {
            if(Current->IsRimBoundTo(Options->Mesh,Options->SlotIndex) ||
                (PreviewMesh.Get()==Options->Mesh && PreviewSlot==Options->SlotIndex) ||
                (Current->RimMesh==Options->Mesh && Current->RimSlotIndex==Options->SlotIndex))return Current;
            Detach();
        }
        AActor* Selected = Actor.Get();
        if (!IsValid(Selected)) return nullptr;
        if(auto* Draft=Drafts.FindByPredicate([this](const auto& D){return D.Mesh==Options->Mesh&&D.Slot==Options->SlotIndex;}))
        {Binding=Draft->Component.Get();bTemporary=Draft->Temporary;return Binding.Get();}
        for (UNPRStyleBindingComponent* Existing : TInlineComponentArray<UNPRStyleBindingComponent*>(Selected))
            if (Existing->RimMesh == Options->Mesh && Existing->RimSlotIndex == Options->SlotIndex)
            { Binding = Existing; return Existing; }
        UNPRStyleBindingComponent* Created = NewObject<UNPRStyleBindingComponent>(Selected,NAME_None,RF_Transient|RF_Transactional);
        Created->RegisterComponent();
        Binding = Created;
        bTemporary = true;
        return Created;
    }
    void SelectTarget(UMeshComponent* Mesh,int32 Slot)
    {
        if(!IsValid(Mesh)||Mesh->GetOwner()!=Actor.Get()||Slot<0||Slot>=Mesh->GetNumMaterials())
        {Message(TEXT("请选择当前角色的有效 Mesh 与材质槽。"),true);return;}
        Detach();
        Options->Mesh=Mesh;Options->SlotIndex=Slot;Options->SourceInputs=FNPRRimSurfaceInputs();Options->OverrideMask=0;
        Options->StyleProfile=nullptr;Options->GeneratedMaterial=nullptr;Options->ShadingModel=ENPRRimShadingModel::KeepOriginal;
        FNPRRimParameters Fresh;Fresh.RimEdgeHalfWidthRad=FMath::DegreesToRadians(12.0);Show(Fresh);
        for(auto* Existing:TInlineComponentArray<UNPRStyleBindingComponent*>(Actor.Get()))
            if(Existing->RimMesh==Mesh && Existing->RimSlotIndex==Slot)
            {
                Binding=Existing;Options->StyleProfile=Existing->RimProfile;Options->SurfaceRecipe=Existing->RimRecipe;
                Options->MaterialKind=Existing->RimMaterialKind;Options->SourceInputs=Existing->RimSurfaceInputs;
                Options->GeneratedMaterial=Existing->RimGeneratedMaterial;Options->ShadingModel=Existing->RimShadingModel;break;
            }
        if(Binding.IsValid()&&Binding->bUseRimLocalParameters)Show(Binding->RimLocalParameters);
        else if(IsValid(Options->StyleProfile))Load();
        if(auto* Draft=Drafts.FindByPredicate([Mesh,Slot](const auto& D){return D.Mesh==Mesh&&D.Slot==Slot;}))
        {
            Binding=Draft->Component.Get();bTemporary=Draft->Temporary;bPreview=Binding.IsValid()&&Binding->OwnsBoundMaterials();
            Options->StyleProfile=Draft->Profile.Get();Options->GeneratedMaterial=Draft->Material.Get();
            Options->ShadingModel=Draft->Model;Options->MaterialKind=Draft->Kind;Options->OverrideMask=Draft->Mask;
            Show(Draft->Parameters);PreviewMesh=Mesh;PreviewSlot=Slot;
        }
        RefreshDetails();CharacterPreview->SetRimTarget(Mesh,Slot);SetIsolation(nullptr,INDEX_NONE);BuildTargets();
        Message(TEXT("已选择材质槽。启用 Rim 后直接调节边缘光；原材质贴图保持。"));
    }
    bool Preview(bool Recompose, bool ExplicitApply=false)
    {
        auto Fail = [this](const FString& Error)
        {
            Message(Error,true);
            return false;
        };
        if (!IsValid(Actor.Get()) || !IsValid(Options->Mesh) || Options->Mesh->GetOwner()!=Actor.Get() ||
            Options->SlotIndex<0 || Options->SlotIndex>=Options->Mesh->GetNumMaterials())
        { return Fail(TEXT("请读取角色，再选择对应 Mesh 的材质槽。")); }
        FString Error;
        const FNPRRimParameters Parameters = Values();
        if (!Parameters.Validate(Error))
        { return Fail(Error); }
        UNPRStyleBindingComponent* Component = EnsureBinding();
        if (!Component) { return Fail(TEXT("Could not create the actor binding.")); }
        if(!ExplicitApply && Component->HasTrackedMaterials() && !Component->OwnsBoundMaterials())
        { return Fail(TEXT("目标材质已被其他系统替换；已保留外部材质。需要覆盖时请在该槽重新勾选启用 Rim。")); }
        if (ExplicitApply && Component->HasTrackedMaterials() && !Component->OwnsBoundMaterials()) Options->GeneratedMaterial=nullptr;
        const bool Rebind=Recompose || !IsValid(Options->GeneratedMaterial) ||
            !Component->IsRimBoundTo(Options->Mesh,Options->SlotIndex) ||
            Component->GetOwnedMaterial(Options->Mesh,Options->SlotIndex)->Parent!=Options->GeneratedMaterial;
        if (Recompose || !IsValid(Options->GeneratedMaterial))
        {
            auto* Generated=NPRRimMaterialBuilder::Build(SourceMaterial(Options->Mesh,Options->SlotIndex),Options->ShadingModel,Error);
            if (!Generated) { Message(Error,true); return false; }
            Options->GeneratedMaterial=Generated;
        }
        const bool OK = !Rebind && Component->IsRimBoundTo(Options->Mesh,Options->SlotIndex)
            ? Component->SetRimParameters(Parameters,Error)
            : Component->ApplyRimMaterial(Options->Mesh,Options->SlotIndex,Options->GeneratedMaterial,Parameters,Error);
        if (!OK) { return Fail(Error); }
        bPreview = true;
        RememberDraft();
        PreviewMesh=Options->Mesh;PreviewSlot=Options->SlotIndex;
        CharacterPreview->SetRimTarget(Options->Mesh,Options->SlotIndex);
        CharacterPreview->Refresh();
        BuildTargets();
        Message(TEXT("本槽 Rim 正在实时预览，原表面已保留。保存角色 Rim 可持久化。"));
        return true;
    }
    void Changed(const FPropertyChangedEvent& Event)
    {
        if (bLoading) return;
        bInteractiveEdit=(Event.ChangeType&EPropertyChangeType::Interactive)!=0;
        const FName Name = Event.GetPropertyName();
        if(Name==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,Mesh)||Name==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,SlotIndex))
        {SelectTarget(Options->Mesh,Options->SlotIndex);return;}
        const bool W = Name==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,WidthDegrees);
        const bool S = Name==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,SoftnessDegrees);
        const bool I = Name==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,Intensity);
        const bool C = Name==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,Color) ||
            (Event.MemberProperty && Event.MemberProperty->GetFName()==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,Color));
        if (W) SetOverride(0,true);
        if (S) SetOverride(1,true);
        if (I) SetOverride(2,true);
        if (C) SetOverride(3,true);
        const bool ProfileChanged = Name==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,StyleProfile) ||
            Name==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,MaterialKind);
        if (ProfileChanged) {Load();return;}
        // Configured slots must respond on every property edit, including drag/color-picker updates.
        // A loaded saved binding need not have passed through the Apply button in this window.
        if (W||S||I||C)
        {
            if(IsValid(Options->Mesh)) Preview(false);
        }
        else if(Name==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,ShadingModel) || bPreview) Preview(true);
        CharacterPreview->SetRimTarget(Options->Mesh,Options->SlotIndex);
    }
    void Reset(ERimField Field)
    {
        const FScopedTransaction Transaction(NSLOCTEXT("NPR","ResetRim","Reset Rim override"));
        Options->Modify();
        FNPRRimParameters Inherited;Inherited.RimEdgeHalfWidthRad=FMath::DegreesToRadians(12.0);
        if (UNPRStyleProfile* Profile = Options->StyleProfile)
        {
            UNPRStyleProfile* Defaults = NewObject<UNPRStyleProfile>(GetTransientPackage());
            Defaults->Defaults = Profile->Defaults;
            FString Error;
            if (!Defaults->ResolveRimParameters(Options->MaterialKind,Inherited,Error))
            { Message(Error,true); return; }
        }
        SetOverride(static_cast<int32>(Field),false);
        bLoading = true;
        switch (Field)
        {
        case ERimField::Width: Options->WidthDegrees=FMath::RadiansToDegrees(Inherited.RimGrazingAngleRad); break;
        case ERimField::Softness: Options->SoftnessDegrees=FMath::RadiansToDegrees(Inherited.RimEdgeHalfWidthRad); break;
        case ERimField::Intensity: Options->Intensity=Inherited.RimPeakScale; break;
        case ERimField::Color: Options->Color=Inherited.RimColor; break;
        }
        RefreshDetails();
        bLoading = false;
        if (bPreview) Preview(false);
        else Message(TEXT("Override cleared; save to inherit the default."));
    }
    void Save()
    {
        if (Drafts.IsEmpty()) { Message(TEXT("请先在材质槽中启用并调整 Rim。"),true);return; }
        AActor* Selected=Actor.Get();if(!Selected)return;
        FString Error;
        // Write generated assets before any level reference can point at them on disk.
        for(auto& Draft:Drafts)
        {
            if(!Draft.Component->OwnsBoundMaterials())
            {Message(TEXT("有材质槽已被其他系统替换，请重新选择并启用该槽后保存。"),true);return;}
            if(!NPRRimMaterialBuilder::Save(Cast<UMaterialInstanceConstant>(Draft.Material.Get()),Error))
            {Message(Error,true);return;}
        }
        const FScopedTransaction Transaction(NSLOCTEXT("NPR","SaveRim","Save actor Rim slots"));
        Selected->Modify();
        for(auto& Draft:Drafts)
        {
            auto* Component=Draft.Component.Get();Component->Modify();
            Component->RimMesh=Draft.Mesh.Get();Component->RimSlotIndex=Draft.Slot;
            Component->RimProfile=Draft.Profile.Get();Component->RimMaterialKind=Draft.Kind;
            Component->RimGeneratedMaterial=Draft.Material.Get();Component->RimShadingModel=Draft.Model;
            Component->bUseRimLocalParameters=true;Component->RimLocalParameters=Draft.Parameters;
            if(Draft.Temporary)
            {Component->ClearFlags(RF_Transient);Selected->AddInstanceComponent(Component);Draft.Temporary=false;}
            Component->RestoreMaterials();
        }
        bTemporary=false;Selected->MarkPackageDirty();
        const bool Saved=FEditorFileUtils::SaveLevel(Selected->GetLevel());
        for(auto& Draft:Drafts) {FString RestoreError;Draft.Component->ApplyConfiguredRim(RestoreError);}
        bPreview=Binding.IsValid()&&Binding->OwnsBoundMaterials();BuildTargets();
        Message(Saved?TEXT("角色各材质槽的 Rim 已保存。原材质和贴图未改写。"):
            TEXT("关卡未保存；Rim 仍在预览，请重新保存。"),!Saved);
    }
    void BuildTargets()
    {
        if(!MeshList.IsValid()||bInteractiveEdit)return;
        const auto Rows=UNPRModulationEditorLibrary::GetMeshEffects(Actor.Get());
        FString Signature;
        for(const auto& Row:Rows) Signature+=Row.Mesh->GetPathName()+FString::FromInt(Row.SlotIndex)+GetPathNameSafe(SourceMaterial(Row.Mesh,Row.SlotIndex));
        if(Signature==InventorySignature)return;
        InventorySignature=Signature;MeshList->ClearChildren();SlotDetails.Reset();
        TMap<TWeakObjectPtr<UMeshComponent>,TSharedPtr<SVerticalBox>> Groups;
        for(const auto& Row:Rows)
        {
            const TWeakObjectPtr<UMeshComponent> Mesh=Row.Mesh;const int32 Slot=Row.SlotIndex;
            if(!Groups.Contains(Mesh))
            {
                auto Body=SNew(SVerticalBox);Groups.Add(Mesh,Body);
                const int32 Count=Rows.FilterByPredicate([Mesh](const auto& R){return R.Mesh==Mesh;}).Num();
                MeshList->AddSlot().AutoHeight().Padding(0,6)[SNew(SExpandableArea).InitiallyCollapsed(CollapsedMeshes.FindRef(Mesh))
                    .OnAreaExpansionChanged_Lambda([this,Mesh](bool Expanded){CollapsedMeshes.FindOrAdd(Mesh)=!Expanded;})
                    .HeaderContent()[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s · %d 个材质槽"),*Mesh->GetName(),Count))).Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont"))]
                    .BodyContent()[Body]];
            }
            auto* Material=SourceMaterial(Mesh.Get(),Slot);const bool Opaque=Material&&(Material->GetBlendMode()==BLEND_Opaque||Material->GetBlendMode()==BLEND_Masked);
            const FString MaterialName=Material?Material->GetName():TEXT("未指定材质");
            const FString Capability=Opaque?TEXT("保留原材质和贴图 · 仅追加 Rim"):
                Material&&Material->GetBlendMode()==BLEND_Translucent?TEXT("Translucent（半透明） · 当前 Rim 材质派生未适配"):TEXT("此混合模式需专用表面 · 不自动转换");
            auto Controls=MakeSlotDetails();
            Groups.FindChecked(Mesh)->AddSlot().AutoHeight().Padding(0,3)[SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this,Mesh,Slot]{return IsolatedMesh==Mesh&&IsolatedSlot==Slot?FLinearColor(.045f,.095f,.085f):FLinearColor(.035f,.038f,.042f);}).Padding(FMargin(10,8))
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("[%d]  %s"),Slot,*Row.SlotName.ToString()))).Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont")).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[SNew(STextBlock).Text(FText::FromString(MaterialName)).ToolTipText(FText::FromString(GetPathNameSafe(Material))).ColorAndOpacity(FSlateColor::UseSubduedForeground()).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,2,0,6)[SNew(STextBlock).Text(FText::FromString(Capability)).ColorAndOpacity(Opaque?FLinearColor(.55f,.70f,.64f):FLinearColor(.86f,.63f,.34f)).AutoWrapText(true)]
                    + SVerticalBox::Slot().AutoHeight()[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(10,5))
                        + SWrapBox::Slot()[SNew(SCheckBox).IsEnabled(Opaque).IsChecked_Lambda([this,Mesh,Slot]{return SlotEnabled(Mesh.Get(),Slot)?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                            .ToolTipText(FText::FromString(TEXT("启用或关闭本材质槽的 Rim。保留原材质；颜色、宽度、柔和度和强度实时更新。")))
                            .OnCheckStateChanged_Lambda([this,Mesh,Slot](ECheckBoxState S){if(Mesh.IsValid())ToggleSlot(Mesh.Get(),Slot,S==ECheckBoxState::Checked);})[SNew(STextBlock).Text(FText::FromString(TEXT("启用 Rim")))]]
                        + SWrapBox::Slot()[SNew(SCheckBox).IsChecked_Lambda([this,Mesh,Slot]{return IsolatedMesh==Mesh&&IsolatedSlot==Slot?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                            .OnCheckStateChanged_Lambda([this,Mesh,Slot](ECheckBoxState S){SetIsolation(S==ECheckBoxState::Checked?Mesh.Get():nullptr,Slot);})[SNew(STextBlock).Text(FText::FromString(TEXT("隔离")))]]
                        + SWrapBox::Slot()[SNew(SButton).ButtonStyle(FAppStyle::Get(),"PrimaryButton").ContentPadding(FMargin(10,4))
                            .Text_Lambda([this,Mesh,Slot]{return FText::FromString(Options->Mesh==Mesh.Get()&&Options->SlotIndex==Slot&&bControlsExpanded?TEXT("收起参数"):TEXT("编辑参数"));})
                            .OnClicked_Lambda([this,Mesh,Slot]{if(Mesh.IsValid()){if(Options->Mesh==Mesh.Get()&&Options->SlotIndex==Slot)bControlsExpanded=!bControlsExpanded;
                                else{SelectTarget(Mesh.Get(),Slot);bControlsExpanded=true;}RefreshDetails();}return FReply::Handled();})]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(SBox).Visibility_Lambda([this,Mesh,Slot]{return Options->Mesh==Mesh.Get()&&Options->SlotIndex==Slot&&bControlsExpanded?EVisibility::Visible:EVisibility::Collapsed;})[Controls]]]];
        }
    }
};
SNPRRimEditor::SNPRRimEditor() = default;
SNPRRimEditor::~SNPRRimEditor()
{
    if (Impl)
    {
        FEditorDelegates::PostUndoRedo.Remove(Impl->UndoHandle);
        Impl->Release();
    }
}
void SNPRRimEditor::Construct(const FArguments&)
{
    Impl=MakeUnique<FImpl>();
    Impl->Options.Reset(NewObject<UNPRRimEditorOptions>(GetTransientPackage(),NAME_None,RF_Transactional));
    Impl->CharacterPreview=MakeNPRCharacterPreview();
    Impl->CharacterPreview->SetRimSolo(true);
    FPropertyEditorModule& Editor=FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    FDetailsViewArgs Args;
    Args.bAllowSearch=false;
    Args.NameAreaSettings=FDetailsViewArgs::HideNameArea;
    Args.NotifyHook=Impl.Get();
    Impl->Details=Editor.CreateDetailView(Args);
    Impl->Details->SetIsPropertyVisibleDelegate(FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& P){return P.Property.GetFName()==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,StyleProfile)||P.Property.GetFName()==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,MaterialKind);}));
    Impl->Details->SetObject(Impl->Options.Get());
    Impl->UndoHandle=FEditorDelegates::PostUndoRedo.AddLambda([this]
    {
        if (!Impl) return;
        AActor* Selected = Cast<AActor>(GEditor->GetSelectedActors()->GetSelectedObject(0));
        if (Impl->Actor.IsValid() && Impl->Actor.Get() == Selected)
        {
            Impl->RefreshDetails();
            // Values and the sparse mask have already been restored by the transaction.
            // Reloading the Profile here would discard the previous unsaved edit.
            if(Impl->bPreview || (Impl->Binding.IsValid() && Impl->Binding->OwnsBoundMaterials())) Impl->Preview(false);
            Impl->BuildTargets();
            Impl->CharacterPreview->Refresh();
        }
        // Undo in another editor or on another actor must not retarget this page.
    });
    ChildSlot[SNew(SSplitter)
        + SSplitter::Slot().Value(.36f)[SNew(SBorder).Padding(10)[SNew(SScrollBox)+SScrollBox::Slot()
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("Rim Light · 角色与材质槽"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",16))]
            + SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(STextBlock)
                .Text_Lambda([this]{return FText::FromString(Impl->Status);})
                .ColorAndOpacity_Lambda([this]{return Impl->bError?FSlateColor(FLinearColor(.9f,.25f,.2f)):FSlateColor::UseForeground();})
                .AutoWrapText(true)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,12)[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(12,10))
                + SWrapBox::Slot()[SNew(SButton).ContentPadding(FMargin(14,8)).Text(FText::FromString(TEXT("读取选中角色")))
                    .OnClicked_Lambda([this]{Impl->ReadSelection();return FReply::Handled();})]
                + SWrapBox::Slot()[SNew(SButton).ContentPadding(FMargin(14,8)).Text(FText::FromString(TEXT("保存角色 Rim")))
                    .OnClicked_Lambda([this]{Impl->Save();return FReply::Handled();})]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[SNew(SExpandableArea).InitiallyCollapsed(true)
                .HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("风格资产（可选）")))]
                .BodyContent()[SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8)[Impl->Details.ToSharedRef()]
                ]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,12,0,4)[SNew(STextBlock).Text(FText::FromString(TEXT("Mesh / 材质槽 · 已应用效果"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",14))]
            + SVerticalBox::Slot().AutoHeight()[SAssignNew(Impl->MeshList,SVerticalBox)]
        ]]]
        + SSplitter::Slot().Value(.64f)[SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6,6))
                + SWrapBox::Slot()[SNew(SCheckBox)
                .IsChecked_Lambda([this]{return Impl->bSolo?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                .OnCheckStateChanged_Lambda([this](ECheckBoxState S){Impl->bSolo=S==ECheckBoxState::Checked;Impl->CharacterPreview->SetRimSolo(Impl->bSolo);})
                [SNew(STextBlock).Text(FText::FromString(TEXT("只看当前 Rim 效果")))]]
                + SWrapBox::Slot()[SNew(SButton).Text(FText::FromString(TEXT("隔离当前槽"))).OnClicked_Lambda([this]{Impl->SetIsolation(Impl->Options->Mesh,Impl->Options->SlotIndex);return FReply::Handled();})]
                + SWrapBox::Slot()[SNew(SButton).Text(FText::FromString(TEXT("隔离当前 Mesh"))).OnClicked_Lambda([this]{Impl->SetIsolation(Impl->Options->Mesh,INDEX_NONE);return FReply::Handled();})]]
            + SVerticalBox::Slot().FillHeight(1)[Impl->CharacterPreview->Widget()]]];
    Impl->ReadSelection();
}

void SNPRRimEditor::Tick(const FGeometry& Geometry,double Time,float Delta)
{
    SCompoundWidget::Tick(Geometry,Time,Delta);
    if(!Impl)return;
    Impl->CharacterPreview->Refresh();
    if(Time>=Impl->NextInventoryUpdate){Impl->BuildTargets();Impl->NextInventoryUpdate=Time+.5;}
}
void SNPRRimEditor::ReloadSelection(){if(Impl)Impl->ReadSelection();}
void SNPRRimEditor::SyncSelection()
{if(Impl && GEditor && (GEditor->GetSelectedActorCount()!=1 || Impl->Actor.Get()!=GEditor->GetSelectedActors()->GetSelectedObject(0)))Impl->ReadSelection();}
void SNPRRimEditor::EditTarget(UMeshComponent* Mesh,int32 SlotIndex)
{if(Impl && (Impl->Options->Mesh!=Mesh || Impl->Options->SlotIndex!=SlotIndex))Impl->SelectTarget(Mesh,SlotIndex);}
TSharedPtr<FNPRCharacterPreview> SNPRRimEditor::GetPreview()const{return Impl?Impl->CharacterPreview:nullptr;}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PreviewScene.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRRimEditorWorkflowTest,"NPR.Editor.RimWorkflow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPRRimEditorWorkflowTest::RunTest(const FString&)
{
    FPreviewScene Scene(FPreviewScene::ConstructionValues().SetTransactional(true));
    FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient|RF_Transactional;
    AActor* Actor=Scene.GetWorld()->SpawnActor<AActor>(Spawn);
    auto* Mesh=NewObject<UStaticMeshComponent>(Actor,NAME_None,RF_Transactional);
    Actor->SetRootComponent(Mesh);Actor->AddInstanceComponent(Mesh);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));Mesh->RegisterComponent();
    auto* OtherMesh=NewObject<UStaticMeshComponent>(Actor,NAME_None,RF_Transactional);
    Actor->AddInstanceComponent(OtherMesh);OtherMesh->SetStaticMesh(Mesh->GetStaticMesh());OtherMesh->SetWorldLocation(FVector(150,0,0));OtherMesh->RegisterComponent();
    auto* SourceMaterial=Mesh->GetMaterial(0);
    auto* Profile=NewObject<UNPRStyleProfile>();
    auto& Intensity=Profile->Defaults.Values.FindOrAdd(NPRRimContract::IntensityId);Intensity.bIsSet=true;Intensity.Value=.4;
    auto* Binding=NewObject<UNPRStyleBindingComponent>(Actor,NAME_None,RF_Transactional);
    Binding->RimMesh=Mesh;Binding->RimSlotIndex=0;Binding->RimProfile=Profile;
    Binding->RimRecipe=LoadObject<UNPRSurfaceRecipe>(nullptr,TEXT("/PrincipledNPR/Surface/SR_DefaultLit_Rim.SR_DefaultLit_Rim"));
    Binding->RimSurfaceInputs.bConfigured=true;
    Actor->AddInstanceComponent(Binding);Binding->RegisterComponent();
    TArray<TWeakObjectPtr<AActor>> PreviousSelection;
    for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It) if(auto* Selected=Cast<AActor>(*It)) PreviousSelection.Add(Selected);
    GEditor->SelectNone(false,true,false);GEditor->SelectActor(Actor,true,false,true);
    TSharedPtr<SNPRRimEditor> Widget=SNew(SNPRRimEditor);
    ON_SCOPE_EXIT
    {
        Widget.Reset();GEditor->SelectNone(false,true,false);
        for(auto Selected:PreviousSelection)if(Selected.IsValid())GEditor->SelectActor(Selected.Get(),true,false,true);
    };
    auto& UI=*Widget->Impl;
    UI.Options->SourceInputs=FNPRRimSurfaceInputs();
    if(!TestTrue(TEXT("Legacy recipe binding migrates to original surface on first edit without source inputs"),UI.Preview(false)))return false;
    auto* MID=Binding->GetOwnedMaterial(Mesh,0);
    if(!TestNotNull(TEXT("Production MID exists"),MID))return false;
    TestTrue(TEXT("First edit actually rebinds the generated parent instead of only updating the old recipe MID"),MID->Parent==UI.Options->GeneratedMaterial);
    TestEqual(TEXT("Both actor meshes are previewed"),UI.CharacterPreview->MeshCount(),2);
    TestTrue(TEXT("Material slot isolation succeeds"),UI.CharacterPreview->Isolate(Mesh,0));
    TestTrue(TEXT("Both viewports isolate the same mesh"),UI.CharacterPreview->IsMeshVisible(true,Mesh)&&UI.CharacterPreview->IsMeshVisible(false,Mesh));
    TestFalse(TEXT("Unselected preview mesh is hidden"),UI.CharacterPreview->IsMeshVisible(false,OtherMesh));
    TestTrue(TEXT("Source mesh visibility stays unchanged"),Mesh->IsVisible()&&OtherMesh->IsVisible());
    TestTrue(TEXT("Original comparison reads pre-binding material"),UI.CharacterPreview->PreviewMaterial(true,Mesh,0)==SourceMaterial);
    TestFalse(TEXT("Invalid slot cannot change isolation"),UI.CharacterPreview->Isolate(Mesh,999));
    UI.CharacterPreview->Isolate(nullptr,INDEX_NONE);
    TestTrue(TEXT("Show all restores both preview meshes"),UI.CharacterPreview->IsMeshVisible(false,OtherMesh));
    const auto Rows=UNPRModulationEditorLibrary::GetMeshEffects(Actor);
    const auto* Row=Rows.FindByPredicate([Mesh](const auto& Entry){return Entry.Mesh==Mesh&&Entry.SlotIndex==0;});
    TestTrue(TEXT("Inventory records actual owned Rim binding"),Row&&Row->Effects.Contains(TEXT("RimLight"))&&Row->Categories.Contains(TEXT("NonPostProcess")));
    UI.CharacterPreview->SetEffectFilter(TEXT("Surface"));
    auto* SurfaceMID=Cast<UMaterialInstanceDynamic>(UI.CharacterPreview->PreviewMaterial(false,Mesh,0));
    float PreviewIntensity=-1;
    if(SurfaceMID)SurfaceMID->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(NPRRimContract::IntensityMaterialName)),PreviewIntensity);
    TestEqual(TEXT("Surface-only filter removes Rim in preview"),PreviewIntensity,0.f);
    UI.CharacterPreview->SetRimSolo(true);
    UI.bPreview=false;
    UI.Options->Color=FLinearColor::Red;
    FPropertyChangedEvent ColorEvent(FindFProperty<FProperty>(UNPRRimEditorOptions::StaticClass(),GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,Color)),EPropertyChangeType::Interactive);
    UI.NotifyPostChange(ColorEvent,ColorEvent.Property);
    TestTrue(TEXT("Interactive color edit starts a configured loaded preview without Apply"),UI.bPreview);
    FLinearColor PreviewColor;
    auto* LivePreviewMID=Cast<UMaterialInstanceDynamic>(UI.CharacterPreview->PreviewMaterial(false,Mesh,0));
    if(LivePreviewMID)LivePreviewMID->GetVectorParameterValue(FHashedMaterialParameterInfo(FName(NPRRimContract::ColorMaterialName)),PreviewColor);
    TestTrue(TEXT("Interactive color reaches the actual shared viewport MID"),PreviewColor.Equals(FLinearColor::Red));
    TestTrue(TEXT("Auto-start reuses the already owned production MID"),Binding->GetOwnedMaterial(Mesh,0)==MID);
    auto ChangeIntensity=[&](float Value)
    {
        const FScopedTransaction Transaction(NSLOCTEXT("NPRTests","RimIntensity","Edit Rim intensity"));
        UI.Options->Modify();UI.Options->Intensity=Value;
        FPropertyChangedEvent Event(FindFProperty<FProperty>(UNPRRimEditorOptions::StaticClass(),GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,Intensity)));
        UI.NotifyPostChange(Event,Event.Property);
    };
    ChangeIntensity(.8f);ChangeIntensity(1.6f);
    TestTrue(TEXT("Editor transaction undo succeeds"),GEditor->UndoTransaction());
    TestEqual(TEXT("Undo returns previous unsaved value, not saved Profile"),UI.Options->Intensity,.8f);
    float Actual=-1;MID->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(NPRRimContract::IntensityMaterialName)),Actual);
    TestEqual(TEXT("Undo updates actual MID"),Actual,.8f);
    TestTrue(TEXT("Undo preserves production MID identity"),Binding->GetOwnedMaterial(Mesh,0)==MID);
    UI.Reset(ERimField::Intensity);
    TestEqual(TEXT("Reset inherits Profile default"),UI.Options->Intensity,.4f);
    TestFalse(TEXT("Reset removes sparse override intent"),UI.HasOverride(2));
    GEditor->UndoTransaction();
    TestEqual(TEXT("Undo Reset restores previous value"),UI.Options->Intensity,.8f);
    TestTrue(TEXT("Undo Reset restores sparse override intent"),UI.HasOverride(2));
    auto* External=LoadObject<UMaterialInterface>(nullptr,TEXT("/PrincipledNPR/RimLight/M_RimLight.M_RimLight"));
    Mesh->SetMaterial(0,External);UI.Release();
    TestTrue(TEXT("Editor release preserves external replacement"),Mesh->GetMaterial(0)==External);
    TestTrue(TEXT("Read-only source query retains external replacement"),Binding->GetSourceMaterial(Mesh,0)==External);
    FString Error;Binding->ApplyConfiguredRim(Error);UI.Binding=Binding;UI.bPreview=true;
    Mesh->SetMaterial(0,External);
    TestFalse(TEXT("Automatic preview cannot reclaim replaced material"),UI.Preview(false));
    TestTrue(TEXT("Failed preview cleanup also preserves external replacement"),Mesh->GetMaterial(0)==External);
    auto* OtherBinding=NewObject<UNPRStyleBindingComponent>(Actor);
    OtherBinding->RimMesh=OtherMesh;OtherBinding->RimSlotIndex=0;OtherBinding->RimRecipe=Binding->RimRecipe;
    OtherBinding->RimProfile=Profile;OtherBinding->RimSurfaceInputs.bConfigured=true;
    Actor->AddInstanceComponent(OtherBinding);OtherBinding->RegisterComponent();
    UI.SelectTarget(OtherMesh,0);
    TestTrue(TEXT("Another mesh can preview its own saved binding"),UI.Preview(false));
    TestTrue(TEXT("Editing another mesh preserves external material on first mesh"),Mesh->GetMaterial(0)==External);
    UI.SelectTarget(Mesh,0);
    TestTrue(TEXT("Selecting an externally replaced saved target does not reapply it"),Mesh->GetMaterial(0)==External);
    TestTrue(TEXT("Other mesh keeps its independent saved effect"),OtherBinding->GetOwnedMaterial(OtherMesh,0)!=nullptr);
    auto AddMesh=[&](){auto* M=NewObject<UStaticMeshComponent>(Actor,NAME_None,RF_Transactional);Actor->AddInstanceComponent(M);
        M->SetStaticMesh(Mesh->GetStaticMesh());M->SetMaterial(0,SourceMaterial);M->RegisterComponent();return M;};
    auto* FreshA=AddMesh();auto* FreshB=AddMesh();
    UI.SelectTarget(FreshA,0);UI.ToggleSlot(FreshA,0,true);
    auto* FreshBindingA=UI.Binding.Get();auto* FreshMID=FreshBindingA->GetOwnedMaterial(FreshA,0);
    TestEqual(TEXT("Fresh card uses softer authoring default"),UI.Options->SoftnessDegrees,12.0);
    TestTrue(TEXT("Fresh card needs no explicit Profile or Recipe"),!UI.Options->StyleProfile&&FreshMID);
    UI.Options->Color=FLinearColor::Red;UI.NotifyPostChange(ColorEvent,ColorEvent.Property);
    UI.SelectTarget(FreshB,0);UI.ToggleSlot(FreshB,0,true);
    UI.Options->Color=FLinearColor::Green;UI.NotifyPostChange(ColorEvent,ColorEvent.Property);
    TestTrue(TEXT("Previous unsaved slot remains enabled while editing another"),FreshBindingA->GetOwnedMaterial(FreshA,0)==FreshMID);
    FLinearColor KeptColor;FreshMID->GetVectorParameterValue(FHashedMaterialParameterInfo(FName(NPRRimContract::ColorMaterialName)),KeptColor);
    TestTrue(TEXT("Independent draft slot retains red"),KeptColor.Equals(FLinearColor::Red));
    UI.SelectTarget(FreshA,0);
    TestTrue(TEXT("Selecting a draft restores its own controls"),UI.Options->Color.Equals(FLinearColor::Red));
    UI.ToggleSlot(FreshA,0,false);TestFalse(TEXT("Per-slot disable writes valid zero"),UI.SlotEnabled(FreshA,0));
    UI.ToggleSlot(FreshA,0,true);TestTrue(TEXT("Per-slot enable restores last intensity"),UI.SlotEnabled(FreshA,0));
    UI.Binding.Reset();
    return true;
}
#endif
