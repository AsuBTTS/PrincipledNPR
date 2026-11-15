#include "NPRMatCapEditorUI.h"
#include "NPRMatCapGraph.h"

#include "Components/MeshComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/Texture2D.h"
#include "IDetailsView.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "NPRCharacterPreview.h"
#include "NPRMatCapEditorOptions.h"
#include "NPRModulationEditorLibrary.h"
#include "NPRRimMaterialBuilder.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Misc/NotifyHook.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

struct SNPRMatCapEditor::FImpl : FNotifyHook
{
    virtual ~FImpl() = default;
    struct FDraft
    {
        TStrongObjectPtr<UNPRMatCapEditorOptions> Options;
        TStrongObjectPtr<UMaterialInstanceConstant> Prepared;
        TStrongObjectPtr<UMaterialInstanceDynamic> PreviewMID;
        TWeakObjectPtr<UMaterialInterface> Source;
        bool bDirty = false;
    };

    TWeakObjectPtr<AActor> Actor;
    TWeakObjectPtr<UMeshComponent> Mesh;
    int32 Slot = INDEX_NONE;
    TMap<TWeakObjectPtr<UMeshComponent>, TMap<int32, FDraft>> Drafts;
    TSharedPtr<FNPRCharacterPreview> Preview;
    TSharedPtr<IDetailsView> Details;
    TSharedPtr<SVerticalBox> Targets;
    FDelegateHandle Undo;
    FString Status;
    bool bPending = false;
    bool bError = false;

    void NotifyPostChange(const FPropertyChangedEvent& Event, FProperty* Property) override
    {
        if (auto* Draft = Current()) Draft->bDirty = true;
        bPending = true;
    }

    static UNPRStyleBindingComponent* Binding(UMeshComponent* Target, int32 Index)
    {
        if (!IsValid(Target)) return nullptr;
        for (auto* Component : TInlineComponentArray<UNPRStyleBindingComponent*>(Target->GetOwner()))
            if (Component->GetOwnedMaterial(Target, Index) || Component->FindMatCap(Target, Index)) return Component;
        return nullptr;
    }

    FDraft* Current()
    {
        auto* Slots = Drafts.Find(Mesh);
        return Slots ? Slots->Find(Slot) : nullptr;
    }

    void ReadActor(AActor* Selected, bool bForce = false)
    {
        if (!bForce && Actor.Get() == Selected && Mesh.IsValid()) return;
        Actor = Selected;
        Mesh.Reset();
        Slot = INDEX_NONE;
        bPending = false;
        Drafts.Reset();
        Preview->ClearMaterialOverrides();
        Preview->LoadActor(Selected);
        Details->SetObject(nullptr);
        Targets->ClearChildren();
        Status = TEXT("请在关卡大纲中只选中一个角色或静态网格对象。");
        bError = false;
        const auto Rows = UNPRModulationEditorLibrary::GetMeshEffects(Selected);
        for (const auto& Row : Rows)
        {
            const TWeakObjectPtr<UMeshComponent> Target = Row.Mesh;
            const int32 Index = Row.SlotIndex;
            const FString Label = FString::Printf(TEXT("%s · [%d] %s"), *Row.Mesh->GetName(), Index, *Row.SlotName.ToString());
            Targets->AddSlot().AutoHeight().Padding(0, 2)
            [SNew(SButton).OnClicked_Lambda([this, Target, Index] { Select(Target.Get(), Index); return FReply::Handled(); })
                .ButtonColorAndOpacity_Lambda([this, Target, Index] { return Mesh == Target && Slot == Index ? FLinearColor(1.f, .55f, .12f) : FLinearColor::White; })
                [SNew(STextBlock).Text(FText::FromString(Label)).AutoWrapText(true)]];
        }
        if (!Rows.IsEmpty()) Select(Rows[0].Mesh, Rows[0].SlotIndex);
    }

    void Select(UMeshComponent* Target, int32 Index)
    {
        if (bPending) Update();
        if (!IsValid(Target) || Target->GetOwner() != Actor.Get() || Index < 0 || Index >= Target->GetNumMaterials()) return;
        Mesh = Target;
        Slot = Index;
        auto& Draft = Drafts.FindOrAdd(Target).FindOrAdd(Index);
        if (!Draft.Options)
        {
            Draft.Options.Reset(NewObject<UNPRMatCapEditorOptions>(GetTransientPackage(), NAME_None, RF_Transactional));
            if (auto* Component = Binding(Target, Index))
            {
                if (const auto* Saved = Component->FindMatCap(Target, Index)) Draft.Options->Parameters = Saved->Parameters;
            }
        }
        Details->SetObject(Draft.Options.Get());
        Preview->SelectRegion(Target, Index);
        Status = TEXT("请选择光泽贴图，勾选启用，再调整混合权重。");
        bError = false;
    }

    bool Update()
    {
        bPending = false;
        auto* Target = Mesh.Get();
        auto* Draft = Current();
        if (!Draft || !IsValid(Target)) return false;
        const auto& Values = Draft->Options->Parameters;
        if (!Values.Validate(Status)) { bError = true; return false; }
        auto* Source = Target->GetMaterial(Slot);
        if (Draft->bDirty && Draft->Source.IsValid() && Draft->Source.Get() != Source)
        {
            auto* Component = Binding(Target, Slot);
            if (!Component || !Component->GetOwnedMaterial(Target, Slot))
            {
                Preview->SetMaterialOverride(Target, Slot, nullptr);
                Status = TEXT("源材质已被替换。请重新选择角色后编辑新材质。");
                bError = true;
                return false;
            }
        }
        if (!Values.bEnabled && !FNPRMatCapParameters::IsSupported(Source) && !Draft->Prepared)
        {
            Preview->SetMaterialOverride(Target, Slot, nullptr);
            Status = TEXT("当前材质尚未应用 MatCap。");
            bError = false;
            return true;
        }
        if (!Draft->PreviewMID || Draft->Source.Get() != Source)
        {
            Draft->PreviewMID.Reset();
            Draft->Prepared.Reset();
            Draft->Source = Source;
            const auto* SourceGraph = Source ? Source->GetMaterial() : nullptr;
            if (!FNPRMatCapParameters::IsSupported(Source) ||
                (SourceGraph && NPRMatCapGraph::NeedsSubstrateNormalUpgrade(*SourceGraph)))
            {
                Draft->Prepared.Reset(NPRMatCapMaterialBuilder::Build(Source, Status));
                if (!Draft->Prepared) { Preview->SetMaterialOverride(Target, Slot, nullptr); bError = true; return false; }
            }
            auto* MID = Cast<UMaterialInstanceDynamic>(Source);
            UMaterialInterface* Parent = Draft->Prepared ? Draft->Prepared.Get() : MID ? MID->Parent.Get() : Source;
            Draft->PreviewMID.Reset(UMaterialInstanceDynamic::Create(Parent, GetTransientPackage()));
        }
        if (!Draft->PreviewMID) { Status = TEXT("无法创建 MatCap 预览材质。"); bError = true; return false; }
        if (auto* MID = Cast<UMaterialInstanceDynamic>(Source)) Draft->PreviewMID->CopyParameterOverrides(MID);
        if (!Values.Apply(*Draft->PreviewMID, Status)) { bError = true; return false; }
        Preview->SetMaterialOverride(Target, Slot, Draft->PreviewMID.Get());
        Status = Values.bEnabled ? TEXT("正在预览当前槽的 MatCap。点击应用后再保存关卡。") : TEXT("当前预览已关闭 MatCap。点击应用可保存关闭状态。");
        bError = false;
        return true;
    }

    bool Apply(const FString& Root = TEXT("/Game/NPR/Generated/MatCap"))
    {
        auto* Target = Mesh.Get();
        auto* Draft = Current();
        if (!Draft || !IsValid(Target) || !Update() || !Draft->PreviewMID) return false;

        auto* Component = Binding(Target, Slot);
        if (Component && Component->HasAnyFlags(RF_Transient))
        {
            Status = TEXT("当前槽有尚未保存的 Rim 预览。请先保存 Rim，再应用 MatCap。");
            bError = true;
            return false;
        }
        auto* Owned = Component ? Component->GetOwnedMaterial(Target, Slot) : nullptr;
        const auto* Existing = Component ? Component->FindMatCap(Target, Slot) : nullptr;
        if (Component && !Owned && (Existing || Component->HasTrackedMaterials()))
        {
            Status = TEXT("材质槽已被其他操作替换，无法应用旧草稿。");
            bError = true;
            return false;
        }

        FNPRMatCapSlot Settings;
        Settings.Mesh = Target;
        Settings.Slot = Slot;
        Settings.Parameters = Draft->Options->Parameters;
        if ((Settings.Parameters.Texture && Settings.Parameters.Texture->GetOutermost() == GetTransientPackage()) ||
            (Settings.Parameters.Mask && Settings.Parameters.Mask->GetOutermost() == GetTransientPackage()))
        {
            Status = TEXT("请先把光泽贴图保存为工程资产。"); bError = true; return false;
        }
        Settings.Source = Existing ? Existing->Source.Get() : Owned ? Owned->Parent.Get() : Target->GetMaterial(Slot);
        Settings.Material = Owned ? Owned->Parent.Get() : Target->GetMaterial(Slot);
        if (!Settings.Source || !Settings.Material || !FPackageName::DoesPackageExist(Settings.Source->GetOutermost()->GetName()) ||
            !FPackageName::DoesPackageExist(Settings.Material->GetOutermost()->GetName()))
        {
            Status = TEXT("请先保存当前槽的源材质、Rim 或 Toon 配置，再应用 MatCap。");
            bError = true;
            return false;
        }
        if (Draft->Prepared)
        {
            // A Toon or Rim edit can install a newer shared parent after this slot's last MatCap apply.
            if (Existing && Owned && Owned->Parent != Existing->Material)
            {
                Settings.Source = Owned->Parent;
            }
            Settings.Material = NPRMatCapMaterialBuilder::Bake(Draft->Prepared.Get(), Root, Status);
        }
        if (!Settings.Material) { bError = true; return false; }

        FScopedTransaction Transaction(NSLOCTEXT("NPR", "ApplyMatCap", "应用 MatCap 到材质槽"));
        Actor->Modify();
        Target->Modify();
        const bool bCreated = !Component;
        if (bCreated)
        {
            Component = NewObject<UNPRStyleBindingComponent>(Actor.Get(), NAME_None, RF_Transactional);
            Actor->AddInstanceComponent(Component);
            Component->RegisterComponent();
        }
        Component->Modify();
        if (!Component->ConfigureMatCap(Settings, Status))
        {
            if (bCreated) Component->DestroyComponent();
            Transaction.Cancel();
            bError = true;
            return false;
        }
        Actor->MarkPackageDirty();
        Draft->bDirty = false;
        Draft->Source.Reset();
        Draft->Prepared.Reset();
        Draft->PreviewMID.Reset();
        Preview->SetMaterialOverride(Target, Slot, nullptr);
        Preview->Refresh();
        Status = TEXT("已应用到所选槽。保存关卡后可在重新打开或运行游戏时恢复。");
        bError = false;
        return true;
    }
};

SNPRMatCapEditor::SNPRMatCapEditor() = default;
SNPRMatCapEditor::~SNPRMatCapEditor()
{
    if (Impl) FEditorDelegates::PostUndoRedo.Remove(Impl->Undo);
}

void SNPRMatCapEditor::Construct(const FArguments& Args)
{
    Impl = MakeUnique<FImpl>();
    Impl->Preview = MakeNPRCharacterPreview();
    FDetailsViewArgs DetailsArgs;
    DetailsArgs.bAllowSearch = false;
    DetailsArgs.NotifyHook = Impl.Get();
    DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
    Impl->Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsArgs);
    Impl->Undo = FEditorDelegates::PostUndoRedo.AddLambda([this]
    {
        if (auto* Draft = Impl->Current())
        {
            if (Draft->bDirty) Impl->bPending = true;
            else
            {
                auto* Binding = Impl->Binding(Impl->Mesh.Get(), Impl->Slot);
                const auto* Saved = Binding ? Binding->FindMatCap(Impl->Mesh.Get(), Impl->Slot) : nullptr;
                Draft->Options->Parameters = Saved ? Saved->Parameters : FNPRMatCapParameters();
                Impl->Details->ForceRefresh();
            }
        }
        Impl->Preview->Refresh();
    });
    Impl->Preview->SetRegionPicked([this](UMeshComponent* Mesh, int32 Slot, int32) { EditTarget(Mesh, Slot); });
    ChildSlot[SNew(SSplitter)
        + SSplitter::Slot().Value(.36f)[SNew(SBorder).Padding(10)[SNew(SScrollBox)
            + SScrollBox::Slot()[SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6, 4))
                    + SWrapBox::Slot()[SNew(SButton).Text(FText::FromString(TEXT("读取选中角色"))).OnClicked_Lambda([this]
                    {
                        auto* Selected = GEditor && GEditor->GetSelectedActorCount() == 1 ? Cast<AActor>(GEditor->GetSelectedActors()->GetSelectedObject(0)) : nullptr;
                        Impl->ReadActor(Selected, true);
                        return FReply::Handled();
                    })]
                    + SWrapBox::Slot()[SNew(SButton).Text(FText::FromString(TEXT("隔离当前槽"))).OnClicked_Lambda([this] { Impl->Preview->Isolate(Impl->Mesh.Get(), Impl->Slot); return FReply::Handled(); })]
                    + SWrapBox::Slot()[SNew(SButton).Text(FText::FromString(TEXT("显示全部材质"))).OnClicked_Lambda([this] { Impl->Preview->Isolate(nullptr, INDEX_NONE); return FReply::Handled(); })]]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 8)[SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Impl->Actor.IsValid() ? Impl->Actor->GetActorLabel() : TEXT("未选中角色")); })]
                + SVerticalBox::Slot().AutoHeight()[Impl->Details.ToSharedRef()]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 6)[SNew(SButton).Text(FText::FromString(TEXT("应用到所选槽"))).OnClicked_Lambda([this] { Impl->Apply(); return FReply::Handled(); })]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 4)[SNew(STextBlock).AutoWrapText(true)
                    .Text_Lambda([this] { return FText::FromString(Impl->Status); })
                    .ColorAndOpacity_Lambda([this] { return FSlateColor(Impl->bError ? FLinearColor(1, .35f, .2f) : FLinearColor(.8f, .85f, .9f)); })]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 8)[SNew(STextBlock).Text(FText::FromString(TEXT("Mesh / 材质槽"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))]
                + SVerticalBox::Slot().AutoHeight()[SAssignNew(Impl->Targets, SVerticalBox)]]]]
        + SSplitter::Slot().Value(.64f)[Impl->Preview->Widget()]];
    SyncSelection();
}

void SNPRMatCapEditor::SyncSelection()
{
    AActor* Selected = GEditor && GEditor->GetSelectedActorCount() == 1 ? Cast<AActor>(GEditor->GetSelectedActors()->GetSelectedObject(0)) : nullptr;
    Impl->ReadActor(Selected);
}

void SNPRMatCapEditor::EditTarget(UMeshComponent* Mesh, int32 Slot)
{
    if (!IsValid(Mesh)) return;
    Impl->ReadActor(Mesh->GetOwner());
    Impl->Select(Mesh, Slot);
}

TSharedPtr<FNPRCharacterPreview> SNPRMatCapEditor::GetPreview() const { return Impl->Preview; }

void SNPRMatCapEditor::Tick(const FGeometry& Geometry, double Time, float Delta)
{
    SCompoundWidget::Tick(Geometry, Time, Delta);
    if (Impl->bPending) Impl->Update();
    Impl->Preview->Refresh();
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "NPRBackendStylePackage.h"
#include "NPRToonBackend.h"
#include "PreviewScene.h"
#include "AssetToolsModule.h"
#include "Engine/DirectionalLight.h"
#include "GameFramework/PlayerStart.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "UObject/SavePackage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SWindow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRMatCapEditorWorkflowTest, "NPR.MatCap.EditorWorkflow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNPRMatCapEditorWorkflowTest::RunTest(const FString&)
{
    FPreviewScene Scene(FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false));
    auto* Actor = Scene.GetWorld()->SpawnActor<AActor>();
    auto* Mesh = NewObject<USkeletalMeshComponent>(Actor);
    auto* Character = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/EndfieldRender/Character/Qianyu/SKM_Chen.SKM_Chen"));
    auto* Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
    auto* AlternateTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
    auto* DemoMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/NPR/MatCapDemo/MI_MatCap.MI_MatCap"));
    auto* Toon = LoadObject<UMaterialInterface>(nullptr, TEXT("/PrincipledNPR/Surface/M_NPRSurface_Toon.M_NPRSurface_Toon"));
    auto* RimTemplate = LoadObject<UMaterialInterface>(nullptr, TEXT("/PrincipledNPR/Surface/M_NPRSurface_DefaultLit.M_NPRSurface_DefaultLit"));
    if (!TestTrue(TEXT("Real character and source assets loaded"), Character && Texture && AlternateTexture && Toon && RimTemplate && DemoMaterial)) return false;
    Mesh->SetSkeletalMeshAsset(Character);
    Actor->SetRootComponent(Mesh);
    Actor->AddInstanceComponent(Mesh);
    Mesh->RegisterComponent();
    if (!TestTrue(TEXT("Skeletal character has two independent material slots"), Mesh->GetNumMaterials() > 1)) return false;
    auto* Original0 = Mesh->GetMaterial(0);
    auto* Original1 = Mesh->GetMaterial(1);

    auto* Existing = NewObject<UNPRStyleBindingComponent>(Actor, NAME_None, RF_Transactional);
    Actor->AddInstanceComponent(Existing);
    Existing->RegisterComponent();
    FString Error;
    auto* Bundle = NewObject<UNPRBackendStylePackage>(Existing);
    Bundle->bHasToonParameters = true;
    Bundle->MaterialSlots.Add({0, Toon});
    Existing->ToonMesh = Mesh;
    Existing->ToonPackage = Bundle;
    Existing->RimMesh = Mesh;
    Existing->RimSlotIndex = 0;
    Existing->RimGeneratedMaterial = Toon;
    Existing->bUseRimLocalParameters = true;
    Existing->RimLocalParameters.RimPeakScale = .65f;
    if (!TestTrue(TEXT("Existing Toon and Rim configured"), Existing->ApplyConfiguredToon(Error) && Existing->ApplyConfiguredRim(Error))) return false;
    auto* Before = Mesh->GetMaterial(0);

    TSharedRef<SNPRMatCapEditor> Widget = SNew(SNPRMatCapEditor);
    auto& UI = *Widget->Impl;
    UI.ReadActor(Actor);
    UI.Select(Mesh, 0);
    auto& Options0 = UI.Current()->Options->Parameters;
    Options0.bEnabled = true;
    Options0.Texture = Texture;
    Options0.Weight = .35f;
    if (!TestTrue(TEXT("Native MatCap widget prepares real Toon material"), UI.Update())) { AddError(UI.Status); return false; }
    TestTrue(TEXT("Preview leaves scene material alone"), Mesh->GetMaterial(0) == Before);
    TestTrue(TEXT("Comparison resolves actual source"), UI.Preview->PreviewMaterial(true, Mesh, 0) == Original0);
    auto* PreviewMID = UI.Current()->PreviewMID.Get();
    Options0.Weight = .75f;
    Options0.Texture = AlternateTexture;
    TestTrue(TEXT("Swapping texture and weight reuses preview MID"), UI.Update() && UI.Current()->PreviewMID.Get() == PreviewMID);
    UTexture* ActualTexture = nullptr;
    PreviewMID->GetTextureParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Texture), ActualTexture);
    TestTrue(TEXT("New texture reaches the actual preview instance"), ActualTexture == AlternateTexture);
    Options0.Texture = Texture;
    const FString Root = TEXT("/Game/Developers/NPRMatCapTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!TestTrue(TEXT("Widget applies combined graph to actual character slot"), UI.Apply(Root))) { AddError(UI.Status); return false; }
    auto* Combined = Existing->GetOwnedMaterial(Mesh, 0);
    TestTrue(TEXT("MatCap and Toon share material with original Toon profile"), Combined && Combined->GetToonProfileOverride_Internal() == Toon->GetToonProfileOverride_Internal());
    float Scalar = 0;
    TestTrue(TEXT("Existing Rim intensity retained on same slot"), Combined && Combined->GetScalarParameterValue(FHashedMaterialParameterInfo(NPRRimContract::IntensityMaterialName), Scalar) && FMath::IsNearlyEqual(Scalar, .65f));
    TestTrue(TEXT("Other character slot stays unchanged"), Mesh->GetMaterial(1) == Original1);

    UI.Select(Mesh, 1);
    auto& Options1 = UI.Current()->Options->Parameters;
    Options1.bEnabled = true;
    Options1.Texture = AlternateTexture;
    Options1.Weight = .2f;
    if (!TestTrue(TEXT("Second slot gets its own saved settings"), UI.Apply(Root))) { AddError(UI.Status); return false; }
    auto* Other = SNPRMatCapEditor::FImpl::Binding(Mesh, 1);
    TestTrue(TEXT("Second application keeps first MID"), Mesh->GetMaterial(0) == Combined && Mesh->GetMaterial(1) != Combined);
    Existing->RestoreMaterials();
    Other->RestoreMaterials();
    TestTrue(TEXT("Original material assignments are restored before saving"), Mesh->GetMaterial(0) == Original0 && Mesh->GetMaterial(1) == Original1);
    Existing->ApplySavedEffects();
    Other->ApplySavedEffects();
    Combined = Existing->GetOwnedMaterial(Mesh, 0);
    TestTrue(TEXT("Registration sequence restores first weight"), Combined && Combined->GetScalarParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Weight), Scalar) && FMath::IsNearlyEqual(Scalar, .75f));
    auto* SecondMID = Other->GetOwnedMaterial(Mesh, 1);
    TestTrue(TEXT("Registration sequence restores second weight"), SecondMID && SecondMID->GetScalarParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Weight), Scalar) && FMath::IsNearlyEqual(Scalar, .2f));
    Existing->SuspendMatCapForSave();
    TestTrue(TEXT("Level serialization sees the original instead of transient MID"), Mesh->GetMaterial(0) == Original0);
    Existing->ResumeMatCapAfterSave();
    TestTrue(TEXT("After save, the live MID retains its identity"), Mesh->GetMaterial(0) == Combined);
    auto Disabled = Options0;
    Disabled.bEnabled = false;
    TestTrue(TEXT("Runtime disable reuses current combined MID"), Existing->SetMatCapParameters(Mesh, 0, Disabled, Error) && Mesh->GetMaterial(0) == Combined);
    TestTrue(TEXT("Runtime disable sets weight to zero"), Combined && Combined->GetScalarParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Weight), Scalar) && Scalar == 0.f);
    auto Invalid = Options0;
    Invalid.Weight = 2;
    TestFalse(TEXT("Invalid runtime weight is rejected"), Existing->SetMatCapParameters(Mesh, 0, Invalid, Error));
    Mesh->SetMaterial(0, RimTemplate);
    TestFalse(TEXT("Runtime does not overwrite an external replacement"), Existing->SetMatCapParameters(Mesh, 0, Options0, Error));
    Existing->RestoreMaterials();
    TestTrue(TEXT("Cleanup preserves external replacement"), Mesh->GetMaterial(0) == RimTemplate);

    // Toon can save a new combined material after MatCap and bake the current mask into that parent.
    Mesh->SetMaterial(0, Original0);
    Existing->ApplySavedEffects();
    UToonProfile* PrivateProfile = nullptr;
    const FNPRToonParameters ToonParameters;
    const FNPRToonResponseSettings ToonResponse;
    UToonProfile* ConvertedProfile = nullptr;
    TStrongObjectPtr<UMaterialInstanceConstant> Converted(NPRToonBackend::BuildPreview(DemoMaterial,
        ENPRShadingModel::SubstrateToon, ToonParameters, ToonResponse, ConvertedProfile, Error));
    if (!TestNotNull(*Error, Converted.Get())) return false;
    TestTrue(TEXT("Converting an existing legacy MatCap to Toon updates its normal input"),
        FNPRMatCapParameters::IsSupported(Converted.Get()) && !NPRMatCapGraph::NeedsSubstrateNormalUpgrade(*Converted->GetMaterial()));
    TStrongObjectPtr<UMaterialInstanceConstant> RevisedPreview(NPRToonBackend::BuildPreview(Mesh->GetMaterial(0),
        ENPRShadingModel::SubstrateToon, ToonParameters, ToonResponse, PrivateProfile, Error));
    if (!TestNotNull(*Error, RevisedPreview.Get())) return false;
    RevisedPreview->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(NPRMatCapContract::Mask), AlternateTexture);
    auto* RevisedBundle = NPRToonBackend::Bake(RevisedPreview.Get(), PrivateProfile, ENPRShadingModel::SubstrateToon,
        ToonParameters, ToonResponse, 0, Root, Error);
    if (!TestNotNull(*Error, RevisedBundle)) return false;
    auto* RevisedToon = RevisedBundle->MaterialSlots[0].Material.Get();
    Existing->ToonPackage = RevisedBundle;
    if (!TestTrue(TEXT("A revised Toon package can replace the shared parent"), Existing->ApplyConfiguredToon(Error))) return false;
    UI.ReadActor(Actor, true);
    UI.Select(Mesh, 0);
    auto& RevisedOptions = UI.Current()->Options->Parameters;
    RevisedOptions.Weight = .45f;
    RevisedOptions.Mask = nullptr;
    if (!TestTrue(TEXT("MatCap remains editable after a new Toon parent is applied"), UI.Apply(Root))) { AddError(UI.Status); return false; }
    Existing->RestoreMaterials();
    Existing->ApplySavedEffects();
    auto* Reopened = Existing->GetOwnedMaterial(Mesh, 0);
    TestTrue(TEXT("Reopening keeps the latest Toon parent and MatCap weight"), Reopened && Reopened->Parent == RevisedToon &&
        Reopened->GetScalarParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Weight), Scalar) && FMath::IsNearlyEqual(Scalar, .45f));
    UTexture* RestoredMask = nullptr;
    auto* White = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
    TestTrue(TEXT("Clearing a previously baked mask restores the unmasked default"), Reopened && White &&
        Reopened->GetTextureParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Mask), RestoredMask) && RestoredMask == White);
    return true;
}

namespace
{
struct FMatCapWidgetCapture
{
    FPreviewScene Scene{FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false)};
    TSharedPtr<SWindow> Window;
    TSharedPtr<SNPRMatCapEditor> Widget;
    int32 Frame = 0;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FNPRCaptureMatCapWidget, FAutomationTestBase*, Test, TSharedPtr<FMatCapWidgetCapture>, State);

bool FNPRCaptureMatCapWidget::Update()
{
    if (++State->Frame == 35 || State->Frame == 55)
    {
        TArray<FColor> Pixels;
        FIntVector Size;
        if (Test->TestTrue(TEXT("Real Slate window rendered"), FSlateApplication::Get().TakeScreenshot(State->Window.ToSharedRef(), Pixels, Size)))
        {
            TArray64<uint8> PNG;
            FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
            const FString Path = FPaths::ProjectSavedDir() / TEXT("NPR/MatCap") / (State->Frame == 35 ? TEXT("Wide.png") : TEXT("Narrow.png"));
            Test->TestTrue(TEXT("MatCap screenshot saved"), FFileHelper::SaveArrayToFile(PNG, *Path));
        }
        if (State->Frame == 35) State->Window->Resize(FVector2D(950, 760));
    }
    if (State->Frame < 55) return false;
    FSlateApplication::Get().RequestDestroyWindow(State->Window.ToSharedRef());
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRMatCapWindowTest, "NPR.MatCap.Window",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNPRMatCapWindowTest::RunTest(const FString&)
{
    if (!FSlateApplication::IsInitialized()) return false;
    auto State = MakeShared<FMatCapWidgetCapture>();
    auto* Actor = State->Scene.GetWorld()->SpawnActor<AStaticMeshActor>();
    Actor->SetActorLabel(TEXT("MatCap 倒角模型"));
    auto* Mesh = Actor->GetStaticMeshComponent();
    Mesh->ClearFlags(RF_Transient);
    auto* Geometry = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/NPR/MatCapDemo/SM_MatCapBevel.SM_MatCapBevel"));
    auto* Original = LoadObject<UMaterialInterface>(nullptr, TEXT("/PrincipledNPR/Surface/MI_NPRSurface_Preview.MI_NPRSurface_Preview"));
    auto* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/NPR/MatCapDemo/MI_MatCap.MI_MatCap"));
    auto* Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/NPR/MatCapDemo/T_MatCapCool.T_MatCapCool"));
    if (!TestTrue(TEXT("Demo artifacts loaded"), Geometry && Original && Parent && Texture)) return false;
    Mesh->SetStaticMesh(Geometry);
    Mesh->SetWorldRotation(FRotator(20, 25, 0));
    Mesh->SetMaterial(0, Original);
    auto* Component = NewObject<UNPRStyleBindingComponent>(Actor);
    Actor->AddInstanceComponent(Component);
    Component->RegisterComponent();
    FNPRMatCapSlot Settings;
    Settings.Mesh = Mesh;
    Settings.Slot = 0;
    Settings.Source = Original;
    Settings.Material = Parent;
    Settings.Parameters.bEnabled = true;
    Settings.Parameters.Texture = Texture;
    Settings.Parameters.Weight = .65f;
    FString Error;
    if (!TestTrue(TEXT("Demo binding configured"), Component->ConfigureMatCap(Settings, Error))) return false;
    State->Widget = SNew(SNPRMatCapEditor);
    State->Widget->EditTarget(Mesh, 0);
    State->Window = SNew(SWindow).Title(FText::FromString(TEXT("MatCap · 材质光泽"))).ClientSize(FVector2D(1280, 780))[State->Widget.ToSharedRef()];
    FSlateApplication::Get().AddWindow(State->Window.ToSharedRef());
    IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("NPR/MatCap")), true);
    ADD_LATENT_AUTOMATION_COMMAND(FNPRCaptureMatCapWidget(this, MoveTemp(State)));
    return true;
}

namespace
{
TSharedPtr<SCheckBox> FindOriginalComparisonControl(const TSharedRef<SWidget>& Widget)
{
    // The shared preview toolbar has one checkbox, 原材质对比.
    if (Widget->GetType() == TEXT("SCheckBox")) return StaticCastSharedRef<SCheckBox>(Widget);
    if (auto* Children = Widget->GetChildren())
    {
        for (int32 Index = 0; Index < Children->Num(); ++Index)
        {
            if (auto Found = FindOriginalComparisonControl(Children->GetChildAt(Index))) return Found;
        }
    }
    return nullptr;
}

struct FMatCapGuideCapture
{
    TSharedPtr<SWindow> Window;
    TFunction<bool(int32)> Prepare;
    FString Directory;
    int32 Frame = 0;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FNPRCaptureMatCapGuide, FAutomationTestBase*, Test, TSharedPtr<FMatCapGuideCapture>, State);

bool FNPRCaptureMatCapGuide::Update()
{
    const TCHAR* Files[] = {TEXT("GuideSphereComparison.png"), TEXT("GuideBeveledCool.png"), TEXT("GuideBeveledWarm.png")};
    constexpr int32 FramesPerCapture = 45;
    if (++State->Frame % FramesPerCapture != 0) return false;
    const int32 Index = State->Frame / FramesPerCapture - 1;
    FlushRenderingCommands();
    TArray<FColor> Pixels;
    FIntVector Size;
    if (Test->TestTrue(TEXT("Captured the rendered MatCap window"),
        FSlateApplication::Get().TakeScreenshot(State->Window.ToSharedRef(), Pixels, Size) && Pixels.Num() == Size.X * Size.Y))
    {
        TArray64<uint8> PNG;
        FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
        Test->TestTrue(Files[Index], FFileHelper::SaveArrayToFile(PNG, *(State->Directory / Files[Index])));
    }
    if (Index + 1 < UE_ARRAY_COUNT(Files) && State->Prepare(Index + 1)) return false;
    FSlateApplication::Get().RequestDestroyWindow(State->Window.ToSharedRef());
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRMatCapGuideCaptureTest, "NPR.MatCap.GuideCapture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNPRMatCapGuideCaptureTest::RunTest(const FString&)
{
    auto* World = GEditor->GetEditorWorldContext().World();
    if (!World || World->GetOutermost()->GetName() != TEXT("/Game/NPR/MatCapDemo/L_MatCapDemo"))
    {
        AddInfo(TEXT("Open L_MatCapDemo before running the documentation capture."));
        return true;
    }
    UMeshComponent* Sphere = nullptr;
    UMeshComponent* Bevel = nullptr;
    for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
    {
        if (It->GetFName() == TEXT("MatCapRuntimeActor")) Sphere = It->GetStaticMeshComponent();
        if (It->GetFName() == TEXT("WarmBeveledModel")) Bevel = It->GetStaticMeshComponent();
    }
    auto* Cool = LoadObject<UTexture2D>(nullptr, TEXT("/Game/NPR/MatCapDemo/T_MatCapCool.T_MatCapCool"));
    auto* Warm = LoadObject<UTexture2D>(nullptr, TEXT("/Game/NPR/MatCapDemo/T_MatCapWarm.T_MatCapWarm"));
    if (!TestTrue(TEXT("Loaded both demo models and authored textures"), Sphere && Bevel && Cool && Warm)) return false;
    TestTrue(TEXT("Demo actor labels match the guide"), Sphere->GetOwner()->GetActorLabel() == TEXT("MatCapRuntimeActor") &&
        Bevel->GetOwner()->GetActorLabel() == TEXT("WarmBeveledModel"));

    auto State = MakeShared<FMatCapGuideCapture>();
    auto Widget = SNew(SNPRMatCapEditor);
    auto Compare = FindOriginalComparisonControl(Widget->GetPreview()->Widget());
    if (!TestNotNull(TEXT("Original material comparison control"), Compare.Get())) return false;
    State->Prepare = [this, Widget, Compare, Sphere, Bevel, Cool, Warm](int32 Stage)
    {
        auto& UI = *Widget->Impl;
        auto* Mesh = Stage == 0 ? Sphere : Bevel;
        if (Stage < 2) Widget->EditTarget(Mesh, 0);
        if (Compare->IsChecked() != (Stage == 0)) Compare->ToggleCheckedState();
        if (!TestEqual(TEXT("Native comparison control reflects the capture state"), Compare->IsChecked(), Stage == 0)) return false;

        auto& Parameters = UI.Current()->Options->Parameters;
        Parameters = FNPRMatCapParameters();
        Parameters.bEnabled = true;
        Parameters.Texture = Stage == 2 ? Warm : Cool;
        Parameters.Weight = .65f;
        UI.Details->ForceRefresh();
        if (!TestTrue(TEXT("MatCap draft updates the actual preview"), UI.Update())) { AddError(UI.Status); return false; }
        UTexture* Sample = nullptr;
        const auto* Current = UI.Preview->PreviewMaterial(false, Mesh, 0);
        return TestTrue(TEXT("The rendered MID uses the requested MatCap texture"), Current &&
            Current->GetTextureParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Texture), Sample) && Sample == Parameters.Texture);
    };
    State->Window = SNew(SWindow).Title(FText::FromString(TEXT("MatCap · 材质光泽"))).ClientSize(FVector2D(1500, 840))[Widget];
    FSlateApplication::Get().AddWindow(State->Window.ToSharedRef());
    if (!State->Prepare(0))
    {
        FSlateApplication::Get().RequestDestroyWindow(State->Window.ToSharedRef());
        return false;
    }
    State->Directory = FPaths::ProjectSavedDir() / TEXT("NPR/MatCap/Guide");
    IFileManager::Get().MakeDirectory(*State->Directory, true);
    ADD_LATENT_AUTOMATION_COMMAND(FNPRCaptureMatCapGuide(this, MoveTemp(State)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRMatCapCreateMapTest, "NPR.MatCap.CreateDemoMap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNPRMatCapCreateMapTest::RunTest(const FString&)
{
    const FString Folder = TEXT("/Game/NPR/MatCapDemo");
    const FString MapPath = Folder / TEXT("L_MatCapDemo");
    if (FPackageName::DoesPackageExist(MapPath))
    {
        AddInfo(TEXT("Existing demo map retained."));
        return true;
    }
    auto* Source = LoadObject<UMaterialInterface>(nullptr, TEXT("/PrincipledNPR/Surface/MI_NPRSurface_Preview.MI_NPRSurface_Preview"));
    auto* Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Game/NPR/MatCapDemo/T_MatCapCool.T_MatCapCool"));
    auto* Warm = LoadObject<UTexture2D>(nullptr, TEXT("/Game/NPR/MatCapDemo/T_MatCapWarm.T_MatCapWarm"));
    auto* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto* Bevel = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/NPR/MatCapDemo/SM_MatCapBevel.SM_MatCapBevel"));
    if (!TestTrue(TEXT("Original material, original textures and both meshes available"), Source && Texture && Warm && Sphere && Bevel)) return false;
    FString Error;
    TStrongObjectPtr<UMaterialInstanceConstant> Preview(NPRMatCapMaterialBuilder::Build(Source, Error));
    if (!TestNotNull(*Error, Preview.Get())) return false;
    auto* Saved = NPRMatCapMaterialBuilder::Bake(Preview.Get(), Folder, Error);
    if (!TestNotNull(*Error, Saved)) return false;

    UPackage* Package = CreatePackage(*MapPath);
    Package->SetPackageFlags(PKG_ContainsMap);
    UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false, TEXT("L_MatCapDemo"), Package);
    World->SetFlags(RF_Public | RF_Standalone);
    const auto Spawn = [&](const TCHAR* Name, UStaticMesh* Geometry, UTexture2D* MatCap, const FVector& Position, const FRotator& Rotation)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.Name = Name;
        auto* Actor = World->SpawnActor<AStaticMeshActor>(Position, Rotation, SpawnParams);
        Actor->SetActorLabel(Name);
        auto* Mesh = Actor->GetStaticMeshComponent();
        Mesh->SetStaticMesh(Geometry);
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetMaterial(0, Source);
        if (!MatCap) return true;
        auto* Binding = NewObject<UNPRStyleBindingComponent>(Actor, TEXT("MatCapBinding"), RF_Transactional);
        Actor->AddInstanceComponent(Binding);
        Binding->RegisterComponent();
        FNPRMatCapSlot Settings;
        Settings.Mesh = Mesh;
        Settings.Slot = 0;
        Settings.Source = Source;
        Settings.Material = Saved;
        Settings.Parameters.bEnabled = true;
        Settings.Parameters.Weight = .65f;
        Settings.Parameters.Texture = MatCap;
        if (!Binding->ConfigureMatCap(Settings, Error)) return false;
        Binding->RestoreMaterials();
        return true;
    };
    const bool Ready = Spawn(TEXT("OriginalSphere"), Sphere, nullptr, FVector(0, -130, 0), FRotator::ZeroRotator) &&
        Spawn(TEXT("MatCapRuntimeActor"), Sphere, Texture, FVector::ZeroVector, FRotator::ZeroRotator) &&
        Spawn(TEXT("WarmBeveledModel"), Bevel, Warm, FVector(0, 130, 0), FRotator(20, 25, 0));
    auto* Light = World->SpawnActor<ADirectionalLight>();
    Light->SetActorRotation(FRotator(-35, 30, 0));
    World->SpawnActor<APlayerStart>(FVector(-420, 0, 35), FRotator(-5, 0, 0));
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    const FString File = FPackageName::LongPackageNameToFilename(MapPath, FPackageName::GetMapPackageExtension());
    const bool bSaved = Ready && UPackage::SavePackage(Package, World, *File, Args);
    World->DestroyWorld(false);
    World->RemoveFromRoot();
    return TestTrue(*FString(TEXT("Separate demo map saved: ") + Error), bSaved);
}
#endif
