#include "NPRRimEditorUI.h"
#include "NPRRimEditorOptions.h"
#include "NPRStyleProfile.h"
#include "NPRStyleBindingComponent.h"
#include "NPRSurfaceRecipe.h"
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
#include "HAL/FileManager.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
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
struct SNPRRimEditor::FImpl
{
    TStrongObjectPtr<UNPRRimEditorOptions> Options;
    TSharedPtr<IDetailsView> Details;
    FDelegateHandle UndoHandle;
    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<UNPRStyleBindingComponent> Binding;
    bool bTemporary = false;
    bool bPreview = false;
    bool bLoading = false;
    bool Overrides[4] = {false,false,false,false};
    FString Status = TEXT("Select an actor in the level, then click Read Selection.");
    bool bError = false;
    void Message(const FString& Text, bool Error = false) { Status = Text; bError = Error; }
    void Release()
    {
        if (UNPRStyleBindingComponent* Component = Binding.Get())
        {
            Component->RestoreMaterials();
            if (bTemporary) Component->DestroyComponent();
            else if (IsValid(Component->RimProfile))
            {
                FString Error;
                Component->ApplyConfiguredRim(Error);
            }
        }
        Binding.Reset();
        bTemporary = bPreview = false;
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
        Details->ForceRefresh();
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
                Overrides[I] = Entry && Entry->bIsSet;
            }
            else
            {
                const FNPRFloatOverride* Entry = Patch ? Patch->Values.Find(FieldId(Field)) : nullptr;
                Overrides[I] = Entry && Entry->bIsSet;
            }
        }
        Show(Resolved);
        Message(TEXT("Profile loaded. Apply Preview to bind the selected mesh slot."));
    }
    void ReadSelection()
    {
        Release();
        Options->Mesh = nullptr;
        Options->SlotIndex = 0;
        Options->StyleProfile = nullptr;
        Options->SurfaceRecipe = nullptr;
        Options->MaterialKind = ENPRMaterialKind::Generic;
        Options->SourceInputs = FNPRRimSurfaceInputs();
        for (bool& Override : Overrides) Override = false;
        Show(FNPRRimParameters());
        Actor = Cast<AActor>(GEditor->GetSelectedActors()->GetSelectedObject(0));
        AActor* Selected = Actor.Get();
        if (!Selected) { Message(TEXT("Select one actor in the level."),true); return; }
        TInlineComponentArray<UMeshComponent*> Meshes(Selected);
        for (UMeshComponent* Mesh : Meshes)
            if (IsValid(Mesh) && Mesh->GetNumMaterials() > 0) { Options->Mesh = Mesh; break; }
        for (UNPRStyleBindingComponent* Existing : TInlineComponentArray<UNPRStyleBindingComponent*>(Selected))
        {
            if (!IsValid(Existing->RimProfile) || !IsValid(Existing->RimMesh)) continue;
            Options->Mesh = Existing->RimMesh;
            Options->SlotIndex = Existing->RimSlotIndex;
            Options->StyleProfile = Existing->RimProfile;
            Options->SurfaceRecipe = Existing->RimRecipe;
            Options->MaterialKind = Existing->RimMaterialKind;
            Options->SourceInputs = Existing->RimSurfaceInputs;
            Binding = Existing;
            Load();
            break;
        }
        Details->ForceRefresh();
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
        FAssetRegistryModule::AssetCreated(Profile);
        Package->MarkPackageDirty();
        Options->StyleProfile = Profile;
        Load();
        Message(FString::Printf(TEXT("Created %s. Save to write the asset."),*AssetName));
    }
    UNPRStyleBindingComponent* EnsureBinding()
    {
        if (UNPRStyleBindingComponent* Current = Binding.Get()) return Current;
        AActor* Selected = Actor.Get();
        if (!IsValid(Selected)) return nullptr;
        for (UNPRStyleBindingComponent* Existing : TInlineComponentArray<UNPRStyleBindingComponent*>(Selected))
            if (Existing->RimMesh == Options->Mesh && Existing->RimSlotIndex == Options->SlotIndex)
            { Binding = Existing; return Existing; }
        UNPRStyleBindingComponent* Created = NewObject<UNPRStyleBindingComponent>(Selected,NAME_None,RF_Transient|RF_Transactional);
        Created->RegisterComponent();
        Binding = Created;
        bTemporary = true;
        return Created;
    }
    bool Preview(bool Recompose)
    {
        auto Fail = [this](const FString& Error)
        {
            Release();
            Message(Error,true);
            return false;
        };
        if (!IsValid(Actor.Get()) || !IsValid(Options->Mesh) || Options->Mesh->GetOwner()!=Actor.Get() ||
            Options->SlotIndex<0 || Options->SlotIndex>=Options->Mesh->GetNumMaterials() ||
            !IsValid(Options->StyleProfile) || !IsValid(Options->SurfaceRecipe))
        { return Fail(TEXT("Select actor, its mesh and slot, a Profile and a compatible Recipe.")); }
        FString Error;
        const FNPRRimParameters Parameters = Values();
        if (!Parameters.Validate(Error) || !Options->SourceInputs.Validate(Error))
        { return Fail(Error); }
        UNPRStyleBindingComponent* Component = EnsureBinding();
        if (!Component) { return Fail(TEXT("Could not create the actor binding.")); }
        const bool OK = !Recompose && bPreview && Component->IsRimBoundTo(Options->Mesh,Options->SlotIndex)
            ? Component->SetRimParameters(Parameters,Error)
            : Component->ApplyRimPreview(Options->Mesh,Options->SlotIndex,Options->SurfaceRecipe,
                Options->SourceInputs,Parameters,Error);
        if (!OK) { return Fail(Error); }
        bPreview = true;
        Message(TEXT("Live preview active on the selected slot. Save to persist."));
        return true;
    }
    void Changed(const FPropertyChangedEvent& Event)
    {
        if (bLoading) return;
        const FName Name = Event.GetPropertyName();
        const bool W = Name==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,WidthDegrees);
        const bool S = Name==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,SoftnessDegrees);
        const bool I = Name==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,Intensity);
        const bool C = Name==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,Color);
        if (W) Overrides[0]=true;
        if (S) Overrides[1]=true;
        if (I) Overrides[2]=true;
        if (C) Overrides[3]=true;
        const bool ProfileChanged = Name==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,StyleProfile) ||
            Name==GET_MEMBER_NAME_CHECKED(UNPRRimEditorOptions,MaterialKind);
        if (ProfileChanged) Load();
        if (bPreview) Preview(!(W||S||I||C));
    }
    void Reset(ERimField Field)
    {
        FNPRRimParameters Inherited;
        if (UNPRStyleProfile* Profile = Options->StyleProfile)
        {
            UNPRStyleProfile* Defaults = NewObject<UNPRStyleProfile>(GetTransientPackage());
            Defaults->Defaults = Profile->Defaults;
            FString Error;
            if (!Defaults->ResolveRimParameters(Options->MaterialKind,Inherited,Error))
            { Message(Error,true); return; }
        }
        Overrides[static_cast<int32>(Field)] = false;
        bLoading = true;
        switch (Field)
        {
        case ERimField::Width: Options->WidthDegrees=FMath::RadiansToDegrees(Inherited.RimGrazingAngleRad); break;
        case ERimField::Softness: Options->SoftnessDegrees=FMath::RadiansToDegrees(Inherited.RimEdgeHalfWidthRad); break;
        case ERimField::Intensity: Options->Intensity=Inherited.RimPeakScale; break;
        case ERimField::Color: Options->Color=Inherited.RimColor; break;
        }
        Details->ForceRefresh();
        bLoading = false;
        if (bPreview) Preview(false);
        else Message(TEXT("Override cleared; save to inherit the default."));
    }
    void Save()
    {
        if (!Preview(false)) return;
        AActor* Selected = Actor.Get();
        UNPRStyleProfile* Profile = Options->StyleProfile;
        UNPRStyleBindingComponent* Component = Binding.Get();
        if (!Selected || !Profile || !Component) return;
        const FScopedTransaction Transaction(NSLOCTEXT("NPR","SaveRim","Save Rim profile and actor binding"));
        Profile->Modify(); Selected->Modify(); Component->Modify();
        FNPRParameterPatch& Patch = Profile->MaterialOverrides.FindOrAdd(Options->MaterialKind);
        const FNPRRimParameters Parameters = Values();
        const double Scalars[3] = {Parameters.RimGrazingAngleRad,Parameters.RimEdgeHalfWidthRad,Parameters.RimPeakScale};
        for (int32 I=0;I<3;++I)
        {
            const FName Id=FieldId(static_cast<ERimField>(I));
            if (Overrides[I])
            {
                FNPRFloatOverride& Entry=Patch.Values.FindOrAdd(Id);
                Entry.bIsSet=true; Entry.Value=Scalars[I];
            }
            else Patch.Values.Remove(Id);
        }
        if (Overrides[3])
        {
            FNPRColorOverride& Entry=Patch.Colors.FindOrAdd(FieldId(ERimField::Color));
            Entry.bIsSet=true; Entry.Value=Parameters.RimColor;
        }
        else Patch.Colors.Remove(FieldId(ERimField::Color));
        Component->RimMesh=Options->Mesh;
        Component->RimSlotIndex=Options->SlotIndex;
        Component->RimProfile=Profile;
        Component->RimRecipe=Options->SurfaceRecipe;
        Component->RimMaterialKind=Options->MaterialKind;
        Component->RimSurfaceInputs=Options->SourceInputs;
        if (bTemporary)
        {
            Component->ClearFlags(RF_Transient);
            Selected->AddInstanceComponent(Component);
            bTemporary=false;
        }
        Profile->MarkPackageDirty(); Selected->MarkPackageDirty();
        if (!SaveProfile(Profile)) { Message(TEXT("Style Profile asset save failed."),true); return; }
        Component->RestoreMaterials();
        bPreview=false;
        if (!FEditorFileUtils::SaveLevel(Selected->GetLevel()))
        {
            FString RestoreError;
            Component->ApplyConfiguredRim(RestoreError);
            Message(TEXT("Profile saved, but level save failed or was canceled."),true);
            return;
        }
        FString RestoreError;
        if (!Component->ApplyConfiguredRim(RestoreError))
        { Message(FString::Printf(TEXT("Saved, but preview could not be restored: %s"),*RestoreError),true); return; }
        bPreview=true;
        Message(TEXT("Style Profile and actor binding saved."));
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
    FPropertyEditorModule& Editor=FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    FDetailsViewArgs Args;
    Args.bAllowSearch=false;
    Args.NameAreaSettings=FDetailsViewArgs::HideNameArea;
    Impl->Details=Editor.CreateDetailView(Args);
    Impl->Details->SetObject(Impl->Options.Get());
    Impl->Details->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent& Event){Impl->Changed(Event);});
    Impl->UndoHandle=FEditorDelegates::PostUndoRedo.AddLambda([this]
    {
        if (!Impl) return;
        AActor* Selected = Cast<AActor>(GEditor->GetSelectedActors()->GetSelectedObject(0));
        if (Impl->bTemporary && Impl->bPreview && Impl->Actor.Get() == Selected)
        {
            Impl->Details->ForceRefresh();
            Impl->Preview(true);
        }
        else Impl->ReadSelection();
    });
    ChildSlot[SNew(SBorder).Padding(12)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("Rim Light")))]
            + SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(STextBlock)
                .Text_Lambda([this]{return FText::FromString(Impl->Status);})
                .ColorAndOpacity_Lambda([this]{return Impl->bError?FSlateColor(FLinearColor(.9f,.25f,.2f)):FSlateColor::UseForeground();})
                .AutoWrapText(true)]
            + SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(FText::FromString(TEXT("Read Selection")))
                .OnClicked_Lambda([this]{Impl->ReadSelection();return FReply::Handled();})]
            + SVerticalBox::Slot().FillHeight(1).Padding(0,8)[SNew(SScrollBox)+SScrollBox::Slot()[Impl->Details.ToSharedRef()]]
            + SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("New Profile")))
                    .OnClicked_Lambda([this]{Impl->NewProfile();return FReply::Handled();})]
                + SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(FText::FromString(TEXT("Load Profile")))
                    .OnClicked_Lambda([this]{Impl->Load();return FReply::Handled();})]
                + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Apply Preview")))
                    .OnClicked_Lambda([this]{Impl->Preview(true);return FReply::Handled();})]]
            + SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Reset Width")))
                    .OnClicked_Lambda([this]{Impl->Reset(ERimField::Width);return FReply::Handled();})]
                + SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(FText::FromString(TEXT("Reset Softness")))
                    .OnClicked_Lambda([this]{Impl->Reset(ERimField::Softness);return FReply::Handled();})]
                + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("Reset Intensity")))
                    .OnClicked_Lambda([this]{Impl->Reset(ERimField::Intensity);return FReply::Handled();})]
                + SHorizontalBox::Slot().AutoWidth().Padding(8,0)[SNew(SButton).Text(FText::FromString(TEXT("Reset Color")))
                    .OnClicked_Lambda([this]{Impl->Reset(ERimField::Color);return FReply::Handled();})]]
            + SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(FText::FromString(TEXT("Save Profile and Actor Binding")))
                .OnClicked_Lambda([this]{Impl->Save();return FReply::Handled();})]
        ]];
}
