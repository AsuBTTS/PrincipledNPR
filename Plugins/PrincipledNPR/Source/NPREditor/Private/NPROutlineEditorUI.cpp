#include "NPROutlineEditorUI.h"
#include "NPRRimEditorUI.h"
#include "NPRCharacterPreview.h"
#include "NPRModulationEditorLibrary.h"
#include "NPRStyleBindingComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NPROutlineMeshCustomization.h"

#include "NPROutlineEditorLibrary.h"
#include "NPROutlineComponent.h"
#include "NPROutlineActorComponent.h"
#include "AdvancedPreviewScene.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "SEditorViewport.h"
#include "Engine/Selection.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "PropertyEditorModule.h"
#include "PropertyCustomizationHelpers.h"
#include "IDetailsView.h"
#include "ToolMenus.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/StrongObjectPtr.h"
#include "ScopedTransaction.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Slate/SceneViewport.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"

namespace
{
const FName OutlineTabId(TEXT("PrincipledNPR.OutlineEditor"));
FDelegateHandle MenuStartupHandle;
struct FSharedCamera
{
    FVector Location = FVector(260, -260, 140);
    FRotator Rotation = FRotator(-10, 135, 0);
};

class FOutlinePreviewClient final : public FEditorViewportClient
{
public:
    FOutlinePreviewClient(FAdvancedPreviewScene* Scene, const TSharedRef<SEditorViewport>& Widget, TSharedRef<FSharedCamera> InCamera)
        : FEditorViewportClient(nullptr, Scene, Widget), OwnScene(Scene), Camera(InCamera)
    {
        SetViewMode(VMI_Lit);
        SetRealtime(true);
        SetViewLocation(Camera->Location);
        SetViewRotation(Camera->Rotation);
        LastLocation = GetViewLocation();
        LastRotation = GetViewRotation();
        ViewFOV = 45;
        ExposureSettings.bFixed = true;
        ExposureSettings.FixedEV100 = 1.0f;
        EngineShowFlags.SetSelectionOutline(false);
        EngineShowFlags.SetGrid(false);
    }
    virtual void Tick(float DeltaSeconds) override
    {
        FEditorViewportClient::Tick(DeltaSeconds);
        if (!GetViewLocation().Equals(LastLocation) || !GetViewRotation().Equals(LastRotation))
        {
            Camera->Location = GetViewLocation();
            Camera->Rotation = GetViewRotation();
        }
        else
        {
            SetViewLocation(Camera->Location);
            SetViewRotation(Camera->Rotation);
        }
        LastLocation = GetViewLocation();
        LastRotation = GetViewRotation();
        OwnScene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
    }
    virtual FLinearColor GetBackgroundColor() const override { return FLinearColor(0.12f, 0.14f, 0.18f); }
    void ApplySharedCamera()
    {
        SetViewLocation(Camera->Location); SetViewRotation(Camera->Rotation);
        LastLocation=Camera->Location; LastRotation=Camera->Rotation;
    }
    virtual FSceneView* CalcSceneView(FSceneViewFamily* Family,const int32 StereoViewIndex=INDEX_NONE) override
    {
        FSceneView* View=FEditorViewportClient::CalcSceneView(Family,StereoViewIndex);
        if(View) LastAA=int32(View->AntiAliasingMethod);
        ++ViewCount;
        return View;
    }
    int32 LastAA=-1;
    uint64 ViewCount=0;
private:
    FAdvancedPreviewScene* OwnScene;
    TSharedRef<FSharedCamera> Camera;
    FVector LastLocation;
    FRotator LastRotation;
};

class SOutlineViewport final : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SOutlineViewport) {} SLATE_END_ARGS()
    void Construct(const FArguments&, TSharedRef<FSharedCamera> InCamera)
    {
        Camera = InCamera;
        Scene = MakeUnique<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
        Scene->SetFloorVisibility(false);
        Scene->SetEnvironmentVisibility(false);
        FActorSpawnParameters Spawn;
        Spawn.ObjectFlags = RF_Transient;
        PreviewActor = Scene->GetWorld()->SpawnActor<ASkeletalMeshActor>(Spawn);
        SEditorViewport::Construct(SEditorViewport::FArguments());
        SetLighting(0);
    }
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override
    {
        Client = MakeShared<FOutlinePreviewClient>(Scene.Get(), SharedThis(this), Camera.ToSharedRef());
        return Client.ToSharedRef();
    }
    void Load(USkeletalMeshComponent* Source, UNPROutlineComponent* Binding, int32 Slot, bool bNPR)
    {
        ClearGroup();
        if (IsValid(PreviewBinding)) { PreviewActor->RemoveInstanceComponent(PreviewBinding); PreviewBinding->DestroyComponent(); PreviewBinding = nullptr; }
        USkeletalMeshComponent* Mesh = PreviewActor->GetSkeletalMeshComponent();
        Mesh->SetSkeletalMeshAsset(Source ? Source->GetSkeletalMeshAsset() : nullptr);
        if (!Source) return;
        Mesh->SetWorldRotation(Source->GetComponentRotation());
        for (int32 Index = 0; Index < Source->GetNumMaterials(); ++Index)
            Mesh->SetMaterial(Index, Binding && Index == Slot ? (bNPR ? Binding->ConvertedMaterial.Get() : Binding->OriginalMaterial.Get()) : Source->GetMaterial(Index));
        if (bNPR && Binding)
        {
            PreviewBinding = NewObject<UNPROutlineComponent>(PreviewActor, NAME_None, RF_Transient);
            PreviewBinding->TargetMesh = Mesh;
            PreviewBinding->MaterialSlotIndex = Slot;
            PreviewBinding->MaterialSlotName = Binding->MaterialSlotName;
            PreviewBinding->OriginalMaterial = Binding->OriginalMaterial;
            PreviewBinding->ConvertedMaterial = Binding->ConvertedMaterial;
            PreviewBinding->HullTemplate = Binding->HullTemplate;
            PreviewBinding->MaskTemplate = Binding->MaskTemplate;
            PreviewBinding->ScreenTemplate = Binding->ScreenTemplate;
            PreviewBinding->Settings = Binding->Settings;
            PreviewActor->AddInstanceComponent(PreviewBinding);
            PreviewBinding->RegisterComponent();
        }
        Mesh->UpdateBounds();
        Invalidate();
    }
    void LoadGroup(UNPROutlineActorComponent* Source, bool bOutlined)
    {
        Load(nullptr,nullptr,0,false);
        if(!IsValid(Source)) return;
        FActorSpawnParameters Spawn; Spawn.ObjectFlags=RF_Transient;
        GroupActor=Scene->GetWorld()->SpawnActor<AActor>(Spawn);
        auto* Root=NewObject<USceneComponent>(GroupActor);
        GroupActor->SetRootComponent(Root); Root->RegisterComponent();
        for(const auto& Rule:Source->Meshes)
        {
            UMeshComponent* Original=Rule.Mesh;
            if(!IsValid(Original) || Copies.Contains(Original)) continue;
            UMeshComponent* Copy=nullptr;
            if(auto* Skel=Cast<USkeletalMeshComponent>(Original))
            {
                auto* NewMesh=NewObject<USkeletalMeshComponent>(GroupActor);
                NewMesh->SetupAttachment(Root); NewMesh->SetSkeletalMeshAsset(Skel->GetSkeletalMeshAsset()); Copy=NewMesh;
            }
            else if(auto* Static=Cast<UStaticMeshComponent>(Original))
            {
                auto* NewMesh=NewObject<UStaticMeshComponent>(GroupActor);
                NewMesh->SetupAttachment(Root); NewMesh->SetStaticMesh(Static->GetStaticMesh()); Copy=NewMesh;
            }
            if(!Copy) continue;
            // The preview actor/world are transient. Ordinary source flags let the same
            // runtime resolver distinguish these copies from its transient render helpers.
            Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Copy->SetMobility(EComponentMobility::Movable);
            GroupActor->AddInstanceComponent(Copy); Copy->RegisterComponent(); Copies.Add(Original,Copy);
        }
        if(bOutlined)
        {
            PreviewGroup=NewObject<UNPROutlineActorComponent>(GroupActor,NAME_None,RF_Transient);
            GroupActor->AddInstanceComponent(PreviewGroup);
        }
        SyncGroup(Source);
        if(PreviewGroup) { PreviewGroup->RegisterComponent(); PreviewGroup->RefreshOutline(); }
        Invalidate();
    }
    void SyncGroup(UNPROutlineActorComponent* Source, bool bCopyMaterials=true)
    {
        if(!GroupActor || !IsValid(Source)) return;
        TSet<UMeshComponent*> ValidSources;
        for(const auto& Rule:Source->Meshes) if(IsValid(Rule.Mesh))
        {
            ValidSources.Add(Rule.Mesh);
            if(!Copies.Contains(Rule.Mesh.Get())) { LoadGroup(Source,PreviewGroup!=nullptr); return; }
        }
        if(ValidSources.Num()!=Copies.Num()) { LoadGroup(Source,PreviewGroup!=nullptr); return; }
        GroupBounds=FBox(ForceInit);
        const FVector Origin=Source->GetOwner()->GetActorLocation();
        for(const auto& Pair:Copies)
        {
            auto* Original=Pair.Key.Get(); auto* Copy=Pair.Value;
            if(!IsValid(Original)||!IsValid(Copy)) continue;
            const FTransform Transform(Original->GetComponentRotation(),Original->GetComponentLocation()-Origin,Original->GetComponentScale());
            if(!Copy->GetComponentTransform().Equals(Transform)) Copy->SetWorldTransform(Transform);
            Copy->SetVisibility(Original->IsVisible() && (!IsolatedSource.IsValid() || IsolatedSource.Get()==Original));
            if(auto* S=Cast<USkeletalMeshComponent>(Copy)) S->SetMaterialPreview(IsolatedSource.Get()==Original?IsolatedSlot:INDEX_NONE);
            if(auto* S=Cast<UStaticMeshComponent>(Copy)) S->SetMaterialPreview(IsolatedSource.Get()==Original?IsolatedSlot:INDEX_NONE);
            Copy->SetHiddenInGame(Original->bHiddenInGame);
            Copy->SetRenderCustomDepth(Original->bRenderCustomDepth); Copy->SetCustomDepthStencilValue(Original->CustomDepthStencilValue);
            if(auto* Skel=Cast<USkeletalMeshComponent>(Original))
            {
                auto* Target=CastChecked<USkeletalMeshComponent>(Copy);
                if(Target->GetSkeletalMeshAsset()!=Skel->GetSkeletalMeshAsset()) Target->SetSkeletalMeshAsset(Skel->GetSkeletalMeshAsset());
                Target->SetForcedLOD(Skel->GetForcedLOD());
                if(Skel->GetSkeletalMeshAsset())
                    for(int32 L=0;L<Skel->GetSkeletalMeshAsset()->GetLODNum();++L)
                        for(int32 M=0;M<Skel->GetNumMaterials();++M)
                            if(Target->IsMaterialSectionShown(M,L)!=Skel->IsMaterialSectionShown(M,L))
                                Target->ShowMaterialSection(M,INDEX_NONE,Skel->IsMaterialSectionShown(M,L),L);
            }
            else if(auto* Static=Cast<UStaticMeshComponent>(Original))
            {
                auto* Target=CastChecked<UStaticMeshComponent>(Copy);
                if(Target->GetStaticMesh()!=Static->GetStaticMesh()) Target->SetStaticMesh(Static->GetStaticMesh());
                Target->SetForcedLodModel(Static->ForcedLodModel);
            }
            if(bCopyMaterials) for(int32 M=0;M<Original->GetNumMaterials();++M) Copy->SetMaterial(M,Original->GetMaterial(M));
            Copy->UpdateBounds();
            if(Copy->IsVisible()) GroupBounds+=Copy->Bounds.GetBox();
        }
        if(PreviewGroup)
        {
            PreviewGroup->bEnabled=Source->bEnabled; PreviewGroup->Scope=Source->Scope;
            PreviewGroup->BaseSettings=Source->BaseSettings; PreviewGroup->Meshes=Source->Meshes;
            PreviewGroup->HullTemplate=Source->HullTemplate; PreviewGroup->MaskTemplate=Source->MaskTemplate; PreviewGroup->ScreenTemplate=Source->ScreenTemplate;
            for(auto& Rule:PreviewGroup->Meshes) Rule.Mesh=Copies.FindRef(Rule.Mesh.Get());
            if(MethodFilter.IsSet())
            {
                TArray<FNPROutlineResolvedRegion> Regions;FString Error;
                if(PreviewGroup->ResolveRegions(Regions,Error))
                    for(const auto& Region:Regions) if(Region.Settings.Method!=MethodFilter.GetValue())
                        for(auto& Rule:PreviewGroup->Meshes) if(Rule.Mesh==Region.Mesh)
                        {auto& Override=FNPROutlineMeshCustomization::FindOrAdd(Rule,Rule.Mesh->GetMaterialSlotNames()[Region.MaterialSlotIndex]).Override;Override.bOverrideEnabled=true;Override.bEnabled=false;}
            }
            if(IsolatedSlot!=INDEX_NONE)
            {
                // Filter only transient preview rules. Persistent actor rules and original surfaces are untouched.
                for(auto& Rule:PreviewGroup->Meshes)
                {
                    auto* Original=IsolatedSource.Get();
                    const bool Selected=Original && Rule.Mesh==Copies.FindRef(Original);
                    const auto Names=Rule.Mesh->GetMaterialSlotNames();
                    for(int32 Slot=0;Slot<Names.Num();++Slot) if(!Selected || Slot!=IsolatedSlot)
                    {
                        auto& O=FNPROutlineMeshCustomization::FindOrAdd(Rule,Names[Slot]).Override;
                        O.bOverrideEnabled=true; O.bEnabled=false;
                    }
                    TArray<FNPROutlineSectionInfo> Mapping; FString Error;
                    UNPROutlineActorComponent::GetSectionMapping(Rule.Mesh,Mapping,Error);
                    for(auto& Section:Rule.Sections)
                        for(const auto& Row:Mapping)
                            if(Row.LODIndex==Section.LODIndex && Row.SectionIndex==Section.SectionIndex && (!Selected || Row.MaterialSlotIndex!=IsolatedSlot))
                            { Section.Override.bOverrideEnabled=true; Section.Override.bEnabled=false; }
                }
            }
            if(PreviewGroup->IsRegistered()) PreviewGroup->RefreshOutline();
        }
        Invalidate();
    }
    void SetIsolation(UMeshComponent* Source, int32 Slot)
    {
        IsolatedSource=Source; IsolatedSlot=Slot;
        if(!GroupActor)
        {
            PreviewActor->GetSkeletalMeshComponent()->SetMaterialPreview(Slot);
            if(PreviewBinding && PreviewBinding->GetOutlineMesh()) PreviewBinding->GetOutlineMesh()->SetMaterialPreview(Slot);
        }
        Invalidate();
    }
    int32 MeshCount() const { return GroupActor?Copies.Num():(PreviewActor->GetSkeletalMeshComponent()->GetSkeletalMeshAsset()?1:0); }
    UMeshComponent* CopyOf(UMeshComponent* Source) const { return Copies.FindRef(Source); }
    void SetMethodFilter(TOptional<ENPROutlineMethod> Filter) { MethodFilter=Filter; }
    void ApplySharedCamera() { Client->ApplySharedCamera(); Invalidate(); }
    void SetScreenPercentage(int32 Percentage)
    { Client->SetPreviewScreenPercentage(Percentage); Client->SetPreviewingScreenPercentage(true); Invalidate(); }
    FString TemporalState() const
    {
        FString Names;
        if(PreviewGroup) for(auto* Helper:PreviewGroup->GetOutlineMeshes()) Names+=Helper->GetPathName()+TEXT(";");
        if(PreviewBinding && PreviewBinding->GetOutlineMesh()) Names+=PreviewBinding->GetOutlineMesh()->GetPathName();
        return FString::Printf(TEXT("AA=%d,Views=%llu,Helpers=%s"),Client->LastAA,Client->ViewCount,*Names);
    }
    FString PreviewError() const { return PreviewGroup?PreviewGroup->GetLastError():PreviewBinding?PreviewBinding->GetLastError():FString(); }
    void SettingsChanged(const FNPROutlineSettings& Settings)
    {
        if (IsValid(PreviewBinding)) { PreviewBinding->Settings = Settings; PreviewBinding->RefreshOutline(); }
        Invalidate();
    }
    bool Focus()
    {
        USkeletalMeshComponent* Mesh = PreviewActor->GetSkeletalMeshComponent();
        if (!GroupActor && !Mesh->GetSkeletalMeshAsset()) return false;
        const FBox Bounds = GroupActor?GroupBounds:Mesh->Bounds.GetBox();
        if(!Bounds.IsValid) return false;
        const FVector Center = Bounds.GetCenter();
        const FIntPoint Size = SceneViewport.IsValid() ? SceneViewport->GetSizeXY() : FIntPoint(500,700);
        const float Aspect = Size.X > 0 && Size.Y > 0 ? float(Size.X)/Size.Y : 0.714f;
        const FVector Extent = Bounds.GetExtent();
        const float TanHalfHorizontalFOV = FMath::Tan(FMath::DegreesToRadians(22.5f));
        const float Distance = FMath::Max(70.f, FMath::Max(Extent.X/TanHalfHorizontalFOV,
            Extent.Z/(TanHalfHorizontalFOV/Aspect))/0.78f + Extent.Y);
        Camera->Location = Center + FVector(0, Distance, Distance * 0.07f);
        Camera->Rotation = (Center - Camera->Location).Rotation();
        Client->ApplySharedCamera();
        Invalidate();
        return true;
    }
    void SetLighting(int32 Index)
    {
        Scene->SetSkyBrightness(Index == 2 ? 0.2f : 0.8f);
        Scene->SetLightBrightness(3.0f);
        Scene->SetLightColor(Index == 1 ? FColor(255, 205, 155) : FColor::White);
        Scene->SetLightDirection(Index == 2 ? FRotator(-15, 110, 0) : FRotator(-40, -35, 0));
        Invalidate();
    }
    FIntPoint ViewSize() const { return SceneViewport.IsValid()?SceneViewport->GetSizeXY():FIntPoint::ZeroValue; }
    bool SavePNG(const FString& Filename)
    {
        if (!SceneViewport.IsValid()) return false;
        const FIntPoint Size = SceneViewport->GetSizeXY();
        SceneViewport->Draw();
        TArray<FColor> Pixels;
        if (Size.X < 1 || Size.Y < 1 || !SceneViewport->ReadPixels(Pixels)) return false;
        for (FColor& Pixel : Pixels) Pixel.A = 255;
        TArray64<uint8> PNG;
        FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
        return FFileHelper::SaveArrayToFile(PNG, *Filename);
    }
    bool SetCaptureSize(int32 Width, int32 Height)
    {
        if (!SceneViewport.IsValid()) return false;
        SceneViewport->SetFixedViewportSize(Width, Height);
        Invalidate();
        return true;
    }
private:
    void ClearGroup()
    {
        if(IsValid(PreviewGroup)) PreviewGroup->DestroyComponent();
        PreviewGroup=nullptr;
        Copies.Reset();
        if(IsValid(GroupActor)) GroupActor->Destroy();
        GroupActor=nullptr; GroupBounds=FBox(ForceInit);
    }
    TUniquePtr<FAdvancedPreviewScene> Scene;
    TSharedPtr<FSharedCamera> Camera;
    TSharedPtr<FOutlinePreviewClient> Client;
    ASkeletalMeshActor* PreviewActor = nullptr; // Preview world owns the actor.
    UNPROutlineComponent* PreviewBinding = nullptr;
    AActor* GroupActor=nullptr;
    UNPROutlineActorComponent* PreviewGroup=nullptr;
    TMap<TWeakObjectPtr<UMeshComponent>,UMeshComponent*> Copies;
    TWeakObjectPtr<UMeshComponent> IsolatedSource;
    int32 IsolatedSlot=INDEX_NONE;
    FBox GroupBounds=FBox(ForceInit);
    TOptional<ENPROutlineMethod> MethodFilter;
};

class FCharacterPreview final : public FNPRCharacterPreview, public TSharedFromThis<FCharacterPreview>
{
    struct FSlotMaterial
    {
        TStrongObjectPtr<UMaterialInstanceDynamic> MID;
        uint32 SourceHash=0;
        bool bInitialized=false;
    };
    static uint32 MaterialState(UMaterialInstanceDynamic* MID,UMaterialInterface* Parent)
    {
        uint32 Hash=GetTypeHash(Parent);
        if(!MID)return Hash;
        auto ParameterHash=[](const FMaterialParameterInfo& P)
        {return HashCombineFast(GetTypeHash(P.Name),HashCombineFast(GetTypeHash(P.Index),GetTypeHash(uint8(P.Association))));};
        Hash=HashCombineFast(Hash,GetTypeHash(MID->ScalarParameterValues.Num()));
        for(const auto& V:MID->ScalarParameterValues)Hash=HashCombineFast(Hash,HashCombineFast(ParameterHash(V.ParameterInfo),GetTypeHash(V.ParameterValue)));
        for(const auto& V:MID->VectorParameterValues)Hash=HashCombineFast(Hash,HashCombineFast(ParameterHash(V.ParameterInfo),FCrc::MemCrc32(&V.ParameterValue,sizeof(V.ParameterValue))));
        for(const auto& V:MID->TextureParameterValues)Hash=HashCombineFast(Hash,HashCombineFast(ParameterHash(V.ParameterInfo),GetTypeHash(V.ParameterValue.Get())));
        return Hash;
    }
public:
    void Construct()
    {
        Camera=MakeShared<FSharedCamera>();
        Original=SNew(SOutlineViewport,Camera.ToSharedRef());
        Current=SNew(SOutlineViewport,Camera.ToSharedRef());
        Content=SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(8)
            [SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6,6))
                + SWrapBox::Slot()[Light(TEXT("标准 / Studio"),0)]
                + SWrapBox::Slot()[Light(TEXT("暖光 / Warm"),1)]
                + SWrapBox::Slot()[Light(TEXT("侧光 / Raking"),2)]
                + SWrapBox::Slot()[SNew(SButton).Text(FText::FromString(TEXT("聚焦当前角色"))).OnClicked_Lambda([this]{Focus();return FReply::Handled();})]
                + SWrapBox::Slot()[SNew(SButton).Text(FText::FromString(TEXT("显示全部材质"))).OnClicked_Lambda([this]{Isolate(nullptr,INDEX_NONE);return FReply::Handled();})]
                + SWrapBox::Slot()[SNew(SCheckBox).IsChecked_Lambda([this]{return bCompare?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState S){bCompare=S==ECheckBoxState::Checked;OriginalPane->SetVisibility(bCompare?EVisibility::Visible:EVisibility::Collapsed);bPendingFocus=true;StableSizeFrames=0;})
                    [SNew(STextBlock).Text(FText::FromString(TEXT("原材质对比")))]]]
            + SVerticalBox::Slot().AutoHeight().Padding(8,2)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]
                {return FText::FromString(Isolated.IsValid()?FString::Printf(TEXT("隔离预览：%s · %s（不改变关卡可见性）"),*Isolated->GetName(),Slot>=0?*FString::Printf(TEXT("槽 %d"),Slot):TEXT("整个 Mesh")):
                    TEXT("完整角色 · 参考姿势 · 共享相机与灯光；预览副本与关卡分离"));})]
            + SVerticalBox::Slot().FillHeight(1)[SNew(SSplitter)
                + SSplitter::Slot()[SAssignNew(OriginalPane,SVerticalBox).Visibility(EVisibility::Collapsed)
                    + SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).Text(FText::FromString(TEXT("原材质 / Original")))]
                    + SVerticalBox::Slot().FillHeight(1)[Original.ToSharedRef()]]
                + SSplitter::Slot()[SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(
                        EffectFilter==TEXT("RimLight")?TEXT("只看 Rim Light / 当前参数"):
                        EffectFilter==TEXT("HullOutline")?TEXT("只看反向壳描边 / 非后处理"):
                        EffectFilter==TEXT("ScreenOutline")?TEXT("只看屏幕空间描边 / 后处理"):
                        EffectFilter==TEXT("Surface")?TEXT("只看表面组合 / 不含 Rim 与描边"):TEXT("已应用效果组合 / 当前角色"));})]
                    + SVerticalBox::Slot().FillHeight(1)[Current.ToSharedRef()]]];
    }
    virtual TSharedRef<SWidget> Widget() override { return Content.ToSharedRef(); }
    virtual void LoadActor(AActor* Actor) override
    {
        Source=Actor; Proxy.Reset();EffectProxy.Reset(); SoloMaterials.Reset(); Isolated.Reset(); Slot=INDEX_NONE;
        if(IsValid(Actor))
        {
            Proxy.Reset(NewObject<UNPROutlineActorComponent>(Actor,NAME_None,RF_Transient));
            EffectProxy.Reset(NewObject<UNPROutlineActorComponent>(Actor,NAME_None,RF_Transient));
            Proxy->bEnabled=false;
            TSet<UMeshComponent*> Seen;
            for(const auto& Row:UNPRModulationEditorLibrary::GetMeshEffects(Actor)) if(!Seen.Contains(Row.Mesh))
            { FNPROutlineMeshRule Rule; Rule.Mesh=Row.Mesh;Rule.SourceName=Row.Mesh->GetFName();Proxy->Meshes.Add(Rule);Seen.Add(Row.Mesh); }
        }
        UpdateEffectProxy();Rebuild(); Refresh();Focus();bPendingFocus=true;StableSizeFrames=0;
    }
    virtual void Refresh() override
    {
        if(!Source.IsValid()){if(Proxy.IsValid())LoadActor(nullptr);return;}
        if(!Proxy.IsValid())return;
        if(bPendingFocus)
        {
            auto Size=Current->ViewSize();
            if(Size.X>0&&Size.Y>0&&Size==LastSize)++StableSizeFrames;else StableSizeFrames=0;
            LastSize=Size;if(StableSizeFrames>=2){Focus();bPendingFocus=false;}
        }
        UpdateEffectProxy();
        Original->SyncGroup(Proxy.Get(),false);
        Current->SyncGroup(ShowsOutline()?EffectProxy.Get():Proxy.Get(),false);
        for(const auto& Rule:Proxy->Meshes)
        {
            auto* Mesh=Rule.Mesh.Get(); auto* A=Original->CopyOf(Mesh);auto* B=Current->CopyOf(Mesh);
            if(!IsValid(Mesh)||!A||!B)continue;
            for(int32 I=0;I<Mesh->GetNumMaterials();++I)
            {
                UMaterialInterface* Base=Mesh->GetMaterial(I);
                for(auto* Binding:TInlineComponentArray<UNPRStyleBindingComponent*>(Source.Get()))
                    if(Binding->GetOwnedMaterial(Mesh,I)) {Base=Binding->GetSourceMaterial(Mesh,I);break;}
                for(auto* Binding:TInlineComponentArray<UNPROutlineComponent*>(Source.Get()))
                    if(Binding->TargetMesh==Mesh && Binding->MaterialSlotIndex==I && Base==Binding->ConvertedMaterial) Base=Binding->OriginalMaterial;
                A->SetMaterial(I,Base);
                if(bSolo || EffectFilter==TEXT("Surface") || EffectFilter==TEXT("HullOutline") || EffectFilter==TEXT("ScreenOutline"))
                {
                    const bool SelectedRim=bSolo && ((!RimTarget.IsValid()) || (Mesh==RimTarget.Get() && I==RimSlot));
                    if(SelectedRim || EffectFilter==TEXT("Surface"))
                    {
                        // Clone the live parent. Disable only the independent second contribution in this preview.
                        auto& Stored=SoloMaterials.FindOrAdd(Mesh).FindOrAdd(I);
                        auto* SourceMID=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(I));
                        UMaterialInterface* Parent=SourceMID?SourceMID->Parent.Get():Mesh->GetMaterial(I);
                        if(!Stored.MID.IsValid() || Stored.MID->Parent!=Parent){Stored.MID.Reset(UMaterialInstanceDynamic::Create(Parent,B));Stored.bInitialized=false;}
                        auto* MID=Stored.MID.Get();
                        const uint32 Hash=MaterialState(SourceMID,Parent);
                        // Copy only when source values change. Reinitializing the render resource every Tick
                        // prevents a stable realtime preview and needlessly rebuilds uniform data.
                        if(SourceMID && (!Stored.bInitialized || Stored.SourceHash!=Hash))MID->CopyParameterOverrides(SourceMID);
                        Stored.SourceHash=Hash;Stored.bInitialized=true;
                        if(bSolo) MID->SetScalarParameterValue(TEXT("NPR_AdditionalEmissiveScale"),0.f);
                        else MID->SetScalarParameterValue(FName(NPRRimContract::IntensityMaterialName),0.f);
                        B->SetMaterial(I,MID);
                    }
                    else B->SetMaterial(I,Base);
                }
                else B->SetMaterial(I,Mesh->GetMaterial(I));
            }
        }
    }
    virtual bool Isolate(UMeshComponent* Mesh,int32 InSlot) override
    {
        if(Mesh && (!Source.IsValid() || Mesh->GetOwner()!=Source.Get() || !Original->CopyOf(Mesh) || InSlot<INDEX_NONE || InSlot>=Mesh->GetNumMaterials())) return false;
        Isolated=Mesh;Slot=Mesh?InSlot:INDEX_NONE;
        Original->SetIsolation(Mesh,Slot);Current->SetIsolation(Mesh,Slot);Refresh();return true;
    }
    virtual void SetRimTarget(UMeshComponent* Mesh,int32 InSlot) override
    {if(RimTarget.Get()!=Mesh || RimSlot!=InSlot){RimTarget=Mesh;RimSlot=InSlot;SoloMaterials.Reset();}Refresh();}
    virtual void SetRimSolo(bool Solo) override { SetEffectFilter(Solo?FName(TEXT("RimLight")):FName(TEXT("All"))); }
    virtual void SetEffectFilter(FName Effect) override
    {if(EffectFilter!=Effect){EffectFilter=Effect;bSolo=Effect==TEXT("RimLight");UpdateEffectProxy();Rebuild();Refresh();}}
    virtual bool Focus() override { const bool OK=Current->Focus();Original->ApplySharedCamera();return OK; }
    virtual int32 MeshCount() const override {return Current->MeshCount();}
    virtual bool Export(const FString& Directory) override
    {
        IFileManager::Get().MakeDirectory(*Directory,true);
        if(!bCompare){auto Size=Current->ViewSize();Original->SetCaptureSize(Size.X,Size.Y);}
        Refresh();return Original->SavePNG(Directory/TEXT("Original.png"))&&Current->SavePNG(Directory/TEXT("Effects.png"));
    }
    virtual UMaterialInterface* PreviewMaterial(bool bOriginal,UMeshComponent* Mesh,int32 InSlot) const override
    {auto* Copy=(bOriginal?Original:Current)->CopyOf(Mesh);return Copy?Copy->GetMaterial(InSlot):nullptr;}
    virtual bool IsMeshVisible(bool bOriginal,UMeshComponent* Mesh)const override
    {auto* Copy=(bOriginal?Original:Current)->CopyOf(Mesh);return Copy && Copy->IsVisible();}
private:
    void Rebuild()
    {
        SoloMaterials.Reset();
        Original->LoadGroup(Proxy.Get(),false);
        Current->SetMethodFilter(EffectFilter==TEXT("HullOutline")?TOptional<ENPROutlineMethod>(ENPROutlineMethod::InvertedHull):
            EffectFilter==TEXT("ScreenOutline")?TOptional<ENPROutlineMethod>(ENPROutlineMethod::ScreenSpace):TOptional<ENPROutlineMethod>());
        Current->LoadGroup(ShowsOutline()?EffectProxy.Get():Proxy.Get(),ShowsOutline());
        Original->SetIsolation(Isolated.Get(),Slot);Current->SetIsolation(Isolated.Get(),Slot);
    }
    bool ShowsOutline() const {return EffectProxy.IsValid() && (EffectFilter==TEXT("All") || EffectFilter==TEXT("HullOutline") || EffectFilter==TEXT("ScreenOutline"));}
    void UpdateEffectProxy()
    {
        if(!Source.IsValid() || !EffectProxy.IsValid())return;
        auto* Group=Source->FindComponentByClass<UNPROutlineActorComponent>();
        EffectProxy->bEnabled=Group?Group->bEnabled:true;
        EffectProxy->Scope=Group?Group->Scope:ENPROutlineScope::IndependentRegions;
        EffectProxy->BaseSettings=Group?Group->BaseSettings:FNPROutlineSettings();
        if(!Group)EffectProxy->BaseSettings.Method=ENPROutlineMethod::None;
        EffectProxy->Meshes=Group?Group->Meshes:Proxy->Meshes;
        if(Group)
        {
            EffectProxy->HullTemplate=Group->HullTemplate;EffectProxy->MaskTemplate=Group->MaskTemplate;EffectProxy->ScreenTemplate=Group->ScreenTemplate;
            for(const auto& Rule:Proxy->Meshes) if(!EffectProxy->Meshes.ContainsByPredicate([&](const auto& Existing){return Existing.Mesh==Rule.Mesh;}))
            {auto Copy=Rule;Copy.Override.bOverrideEnabled=true;Copy.Override.bEnabled=false;EffectProxy->Meshes.Add(Copy);}
        }
        else for(auto* Old:TInlineComponentArray<UNPROutlineComponent*>(Source.Get()))
        {
            EffectProxy->HullTemplate=Old->HullTemplate;EffectProxy->MaskTemplate=Old->MaskTemplate;EffectProxy->ScreenTemplate=Old->ScreenTemplate;
            for(auto& Rule:EffectProxy->Meshes) if(Rule.Mesh==Old->TargetMesh)
            {
                auto& O=FNPROutlineMeshCustomization::FindOrAdd(Rule,Old->MaterialSlotName).Override;
                O.bOverrideMethod=O.bOverrideColor=O.bOverrideWidth=O.bOverrideDepthBias=O.bOverrideVertexControl=true;
                O.bOverrideEdgeSource=O.bOverrideEdgeWidthScales=O.bOverrideLocalWidthScale=O.bOverrideWidthMask=true;O.Values=Old->Settings;
            }
        }
    }
    TSharedRef<SWidget> Light(const TCHAR* Label,int32 Index)
    {return SNew(SButton).Text(FText::FromString(Label)).OnClicked_Lambda([this,Index]{Original->SetLighting(Index);Current->SetLighting(Index);return FReply::Handled();});}
    TWeakObjectPtr<AActor> Source;
    TStrongObjectPtr<UNPROutlineActorComponent> Proxy,EffectProxy;
    TWeakObjectPtr<UMeshComponent> Isolated,RimTarget;
    int32 Slot=INDEX_NONE,RimSlot=INDEX_NONE;
    bool bCompare=false,bSolo=false;
    FName EffectFilter=TEXT("All");
    TSharedPtr<FSharedCamera> Camera;
    TSharedPtr<SOutlineViewport> Original,Current;
    TSharedPtr<SVerticalBox> Content,OriginalPane;
    TMap<TWeakObjectPtr<UMeshComponent>,TMap<int32,FSlotMaterial>> SoloMaterials;
    bool bPendingFocus=false;
    int32 StableSizeFrames=0;
    FIntPoint LastSize=FIntPoint::ZeroValue;
};

struct FOutlineSlot { int32 Index; FString Label; };

class SNPROutlineEditor final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SNPROutlineEditor) {} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        Working.Reset(NewObject<UNPROutlinePreset>(GetTransientPackage()));
        FDetailsViewArgs DetailsArgs;
        DetailsArgs.bAllowSearch = false;
        DetailsArgs.bHideSelectionTip = true;
        DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
        Details = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(DetailsArgs);
        Details->RegisterInstancedCustomPropertyTypeLayout(TEXT("NPROutlineMeshRule"), FOnGetPropertyTypeCustomizationInstance::CreateLambda([this]
        { return MakeShared<FNPROutlineMeshCustomization>(SlotUIState,
            [this](UMeshComponent* Source,int32 Slot,bool Checked){if(Checked) IsolatePreview(Source,Slot); else if(IsolatedSource.Get()==Source && IsolatedSlot==Slot) IsolatePreview(nullptr,INDEX_NONE);},
            [this](UMeshComponent* Source,int32 Slot){return IsValid(Source) && IsolatedSource.Get()==Source && IsolatedSlot==Slot;}); }));
        Details->SetObject(Working.Get());
        Details->OnFinishedChangingProperties().AddSP(this, &SNPROutlineEditor::CommitSettings);
        Camera = MakeShared<FSharedCamera>();
        ChildSlot
        [
            SNew(SSplitter)
            + SSplitter::Slot().Value(0.29f).MinSize(350.f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(10,6)
                [SNew(SBorder).Padding(8).BorderBackgroundColor_Lambda([this]{return bStatusError?FLinearColor(0.25f,0.04f,0.03f):FLinearColor(0.03f,0.10f,0.16f);})
                    [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Status);}).AutoWrapText(true)]]
                + SVerticalBox::Slot().FillHeight(1)
                [
                SNew(SScrollBox)
                + SScrollBox::Slot().Padding(10)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Group.IsValid()?TEXT("NPR Outline · 角色与区域"):TEXT("NPR Outline · 单材质槽工作流"));}).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6)[SNew(SButton).Text(FText::FromString(TEXT("使用关卡选中角色 / Use Selection"))).ToolTipText(FText::FromString(TEXT("从关卡大纲读取唯一选中的 Actor；切换当前编辑目标。预览中的点击不等同于关卡选择。"))).OnClicked_Lambda([this]{UseSelection(true); return FReply::Handled();})]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4)[SNew(SButton).Text(FText::FromString(TEXT("创建 / 打开角色级配置"))).ToolTipText(FText::FromString(TEXT("为关卡当前选中的 Actor 创建或打开整角色 Outline 配置；组件、材质槽和 Section 规则在 Meshes 中编辑。"))).OnClicked_Lambda([this]{ConfigureActor();return FReply::Handled();})]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(SelectionLabel);}).AutoWrapText(true)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
                    [SAssignNew(SlotsCombo, SComboBox<TSharedPtr<FOutlineSlot>>).OptionsSource(&Slots)
                        .Visibility_Lambda([this]{return Group.IsValid()?EVisibility::Collapsed:EVisibility::Visible;})
                        .IsEnabled_Lambda([this]{return Mesh.IsValid();})
                        .ToolTipText(FText::FromString(TEXT("旧单槽流程：选择一个材质槽。整角色模式请编辑 Meshes → Materials，而非这里。")))
                        .OnGenerateWidget_Lambda([](TSharedPtr<FOutlineSlot> Slot){return SNew(STextBlock).Text(FText::FromString(Slot->Label));})
                        .OnSelectionChanged_Lambda([this](TSharedPtr<FOutlineSlot> Slot,ESelectInfo::Type){SelectedSlot=Slot;if(!bLoadingSelection){LoadBinding();Report(Binding.IsValid()?TEXT("已读取该材质槽的持久绑定。"):TEXT("已选择材质槽；请转换并绑定后调参。"));}})
                        [SNew(STextBlock).Text_Lambda([this]{return FText::FromString(SelectedSlot.IsValid()?SelectedSlot->Label:TEXT("选择材质槽 / Material Slot"));})]]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6)[SNew(SButton).Visibility_Lambda([this]{return Group.IsValid()?EVisibility::Collapsed:EVisibility::Visible;}).IsEnabled_Lambda([this]{return Mesh.IsValid() && SelectedSlot.IsValid();}).Text(FText::FromString(TEXT("转换 / 更新单槽 Outline 绑定"))).ToolTipText(FText::FromString(TEXT("需要已加载一个骨骼网格和材质槽；创建继承原材质的实例及单槽绑定。整角色模式无需此步骤。"))).OnClicked_Lambda([this]{Convert(); return FReply::Handled();})]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Group.IsValid()?TEXT("角色级调参：Actor → Component → Material → Section；双视口显示整组组件（参考姿势）。"):TEXT("调参：壳宽度为 cm；屏幕宽度为 px。右侧双视口同步相机。"));}).AutoWrapText(true)]
                    + SVerticalBox::Slot().AutoHeight()[Details.ToSharedRef()]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(STextBlock).Visibility_Lambda([this]{return RenderError().IsEmpty()?EVisibility::Collapsed:EVisibility::Visible;}).ColorAndOpacity(FLinearColor(1.f,0.35f,0.25f)).Text_Lambda([this]{return FText::FromString(TEXT("描边状态：")+RenderError());}).AutoWrapText(true)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6)[SNew(SButton).IsEnabled_Lambda([this]{return CanPreview();}).Text(FText::FromString(TEXT("聚焦当前角色 / Focus"))).ToolTipText(FText::FromString(TEXT("将两个预览相机同步到当前角色的正面全身视图；已处于该视角时重复点击不会改变构图。"))).OnClicked_Lambda([this]{FocusCurrent(true); return FReply::Handled();})]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 3)[SNew(STextBlock).Text(FText::FromString(TEXT("选择并应用基础风格预设")))]
                    + SVerticalBox::Slot().AutoHeight()
                    [SNew(SObjectPropertyEntryBox).AllowedClass(UNPROutlinePreset::StaticClass())
                        .ToolTipText(FText::FromString(TEXT("选择新预设后立即应用；要恢复同一个预设的已保存参数，请点击下方重新应用。区域覆写来自当前关卡。")))
                        .ObjectPath_Lambda([this]{return SelectedPreset.IsValid()?SelectedPreset->GetPathName():FString();})
                        .OnObjectChanged_Lambda([this](const FAssetData& Asset){SelectedPreset=Cast<UNPROutlinePreset>(Asset.GetAsset());if(SelectedPreset.IsValid())LoadPreset(SelectedPreset.Get());else Report(TEXT("已清空候选预设。当前参数仍保留。"));})]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(SButton).IsEnabled_Lambda([this]{return HasTarget() && SelectedPreset.IsValid();}).Text(FText::FromString(TEXT("重新应用所选预设"))).ToolTipText(FText::FromString(TEXT("即使预设选择没有变化，也会再次应用资产当前的基础参数；包含资产尚未保存的修改，不从磁盘重新加载。"))).OnClicked_Lambda([this]{LoadPreset(SelectedPreset.Get());return FReply::Handled();})]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3)[SNew(STextBlock).Text(FText::FromString(TEXT("预设保存路径")))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6)[SAssignNew(PresetPath, SEditableTextBox).Text(FText::FromString(TEXT("/Game/NPR/Outline/Presets/OP_Custom")))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4)[SNew(SButton).IsEnabled_Lambda([this]{return Group.IsValid() || Binding.IsValid();}).Text(FText::FromString(TEXT("保存基础预设与当前关卡"))).ToolTipText(FText::FromString(TEXT("将基础风格写入上方路径，并保存当前角色所在关卡（含本地区域覆写）。已有同名预设会更新。"))).OnClicked_Lambda([this]{Save(); return FReply::Handled();})]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4)[SNew(SButton).IsEnabled_Lambda([this]{return SelectedActor.IsValid();}).Text(FText::FromString(TEXT("重新读取当前角色绑定"))).ToolTipText(FText::FromString(TEXT("重新读取窗口当前角色的内存绑定并重建预览；不依赖关卡此刻选中了谁，也不是从磁盘重开关卡。"))).OnClicked_Lambda([this]{ReloadCurrent(); return FReply::Handled();})]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4)[SNew(SButton).IsEnabled_Lambda([this]{return CanPreview();}).Text(FText::FromString(TEXT("导出双视口 PNG"))).ToolTipText(FText::FromString(TEXT("导出当前两个预览画面；每次创建独立时间目录，成功后可打开目录。"))).OnClicked_Lambda([this]{ExportCurrent();return FReply::Handled();})]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(SButton).Visibility_Lambda([this]{return LastExportDirectory.IsEmpty()?EVisibility::Collapsed:EVisibility::Visible;}).Text(FText::FromString(TEXT("打开上次导出目录"))).OnClicked_Lambda([this]{FPlatformProcess::ExploreFolder(*LastExportDirectory);Report(TEXT("已打开导出目录：")+LastExportDirectory);return FReply::Handled();})]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4)[SNew(SButton).IsEnabled_Lambda([this]{return Group.IsValid() || Binding.IsValid();}).Text_Lambda([this]{return FText::FromString(Group.IsValid()?(Group->bEnabled?TEXT("关闭角色描边（保留配置）"):TEXT("启用角色描边")):TEXT("恢复单槽原材质并移除绑定"));}).ToolTipText(FText::FromString(TEXT("角色模式切换描边启用状态并保留区域配置；单槽模式恢复已记录的原材质并移除单槽绑定。"))).OnClicked_Lambda([this]{RestoreOrToggle();return FReply::Handled();})]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 6)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(UNPROutlineActorComponent::DescribeWidthSupport(Group.IsValid()?Group->BaseSettings.Method:Working->Settings.Method,Group.IsValid()?Group->Scope:ENPROutlineScope::IndependentRegions));}).AutoWrapText(true)]
                ]
                ]
            ]
            + SSplitter::Slot().Value(0.71f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(8)
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(4,4))
                    + SWrapBox::Slot().Padding(4)[LightButton(TEXT("标准 / Studio"),0)]
                    + SWrapBox::Slot().Padding(4)[LightButton(TEXT("暖光 / Warm"),1)]
                    + SWrapBox::Slot().Padding(4)[LightButton(TEXT("侧光 / Raking"),2)]
                    + SWrapBox::Slot().Padding(4)[SNew(SCheckBox).IsChecked_Lambda([this]{return bShowOriginal?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([this](ECheckBoxState State){bShowOriginal=State==ECheckBoxState::Checked; if(bShowOriginal && !bFixedCapture) OriginalView->SetCaptureSize(0,0); OriginalPane->SetVisibility(bShowOriginal?EVisibility::Visible:EVisibility::Collapsed);})[SNew(STextBlock).Text(FText::FromString(TEXT("原材质对比")))]]
                    + SWrapBox::Slot().Padding(4)[SNew(SButton).Text(FText::FromString(TEXT("显示全部材质"))).OnClicked_Lambda([this]{IsolatePreview(nullptr,INDEX_NONE);return FReply::Handled();})]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(4,2)[SNew(STextBlock).Text_Lambda([this]{if(IsolatedSource.IsValid()) return FText::FromString(FString::Printf(TEXT("隔离预览：%s · 槽 %d（不改变关卡）"),*IsolatedSource->GetName(),IsolatedSlot)); return FText::FromString(Group.IsValid()?TEXT("整组组件 · 参考姿势 · 共享相机与灯光；右侧应用角色描边"):TEXT("共享相机 · 相同几何、材质与灯光；右侧叠加 Outline"));}).AutoWrapText(true)]]
                + SVerticalBox::Slot().FillHeight(1)
                [SNew(SSplitter)
                    + SSplitter::Slot()[SAssignNew(OriginalPane,SVerticalBox).Visibility(EVisibility::Collapsed)
                        + SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).Text(FText::FromString(TEXT("原材质 / Original")))]
                        + SVerticalBox::Slot().FillHeight(1)[SAssignNew(OriginalView,SOutlineViewport,Camera.ToSharedRef())]]
                    + SSplitter::Slot()[SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).Text(FText::FromString(TEXT("NPR Outline / 当前参数")))]
                        + SVerticalBox::Slot().FillHeight(1)[SAssignNew(NPRView,SOutlineViewport,Camera.ToSharedRef())]]]
            ]
        ];
        UseSelection();
    }
    virtual void Tick(const FGeometry& Geometry,double Time,float Delta) override
    {
        SCompoundWidget::Tick(Geometry,Time,Delta);
        if(Group.IsValid())
        {
            if(IsolatedSlot!=INDEX_NONE)
            {
                auto* Source=IsolatedSource.Get();
                const bool Present=IsValid(Source) && Group->Meshes.ContainsByPredicate([Source](const auto& Rule){return Rule.Mesh==Source;});
                TArray<FNPROutlineSectionInfo> Mapping; FString MappingError;
                if(Present) UNPROutlineActorComponent::GetSectionMapping(Source,Mapping,MappingError);
                if(!Present || !Mapping.ContainsByPredicate([this](const auto& Row){return Row.MaterialSlotIndex==IsolatedSlot;})) IsolatePreview(nullptr,INDEX_NONE);
            }
            OriginalView->SyncGroup(Group.Get()); NPRView->SyncGroup(Group.Get());
            if(CollectionRevision!=Group->GetCollectionRevision())
            {
                CollectionRevision=Group->GetCollectionRevision();
                Details->ForceRefresh();
                Report(Group->GetCollectionSummary()+(Group->GetLastError().IsEmpty()?FString():TEXT(" 描边未能刷新：")+Group->GetLastError()),!Group->GetLastError().IsEmpty());
            }
            return;
        }
        if (Working->Settings != PreviewSettings)
        {
            NPRView->SettingsChanged(Working->Settings);
            PreviewSettings = Working->Settings;
        }
    }
    bool Export(const FString& Directory)
    {
        IFileManager::Get().MakeDirectory(*Directory,true);
        if(!bShowOriginal && !bFixedCapture) { const FIntPoint Size=NPRView->ViewSize(); OriginalView->SetCaptureSize(Size.X,Size.Y); }
        return OriginalView->SavePNG(Directory/TEXT("Original.png")) && NPRView->SavePNG(Directory/TEXT("Outline.png"));
    }
    void ReloadSelection() { UseSelection(false); }
    void SyncSelection() { if(LevelSelection()!=SelectedActor.Get())UseSelection(false); }
    int32 MeshCount(bool bOutlined) const { return bOutlined?NPRView->MeshCount():OriginalView->MeshCount(); }
    bool SetCamera(const FVector& Location,const FRotator& Rotation)
    {
        Camera->Location=Location; Camera->Rotation=Rotation;
        OriginalView->ApplySharedCamera(); NPRView->ApplySharedCamera(); return true;
    }
    bool GetCamera(FVector& Location,FRotator& Rotation) const
    { Location=Camera->Location; Rotation=Camera->Rotation; return true; }
    bool SetScreenPercentage(int32 Percentage)
    { OriginalView->SetScreenPercentage(Percentage); NPRView->SetScreenPercentage(Percentage); return true; }
    FString TemporalState() const { return NPRView->TemporalState(); }
    bool SetCaptureSize(int32 Width, int32 Height)
    {
        bFixedCapture=Width>0 && Height>0;
        const bool bOriginal = OriginalView->SetCaptureSize(Width, Height);
        const bool bOutline = NPRView->SetCaptureSize(Width, Height);
        FocusCurrent(false);
        return bOriginal && bOutline;
    }
private:
    void IsolatePreview(UMeshComponent* Source,int32 Slot)
    {
        if(!IsValid(Source)) {Source=nullptr;Slot=INDEX_NONE;}
        IsolatedSource=Source; IsolatedSlot=Slot;
        OriginalView->SetIsolation(Source,Slot); NPRView->SetIsolation(Source,Slot);
        if(Group.IsValid()) {OriginalView->SyncGroup(Group.Get());NPRView->SyncGroup(Group.Get());}
        Report(Source?FString::Printf(TEXT("仅预览 %s 的材质槽 %d；取消勾选隔离或点“显示全部材质”恢复。关卡规则未改变。"),*Source->GetName(),Slot):TEXT("已恢复全部材质预览。"),false,false);
    }
    void Report(const FString& Message,bool bError=false,bool bNotify=true)
    {
        const FString DisplayMessage=bError?ExplainError(Message):Message;
        const FString NewStatus=(bError?TEXT("需要处理 · "):TEXT("操作结果 · "))+DisplayMessage;
        const bool Changed=Status!=NewStatus;
        bStatusError=bError;
        Status=NewStatus;
        if(bNotify && Changed)
        {
            FNotificationInfo Info(FText::FromString(DisplayMessage)); Info.ExpireDuration=bError?7.f:4.f;
            if(auto Item=FSlateNotificationManager::Get().AddNotification(Info))
                Item->SetCompletionState(bError?SNotificationItem::CS_Fail:SNotificationItem::CS_Success);
        }
    }
    bool HasTarget() const { return Group.IsValid() || Mesh.IsValid(); }
    bool CanPreview() const { return OriginalView.IsValid() && NPRView.IsValid() && OriginalView->MeshCount()>0 && NPRView->MeshCount()>0; }
    FString ExplainError(const FString& Error) const
    {
        if(Error.Contains(TEXT("needs Opaque coverage")))
        {
            FString Region,Unused; Error.Split(TEXT(" needs "),&Region,&Unused);
            return Region+TEXT(" 的材质覆盖尚未适配。当前描边只支持 Opaque（不透明），Masked（遮罩）和 Translucent（半透明）仍可显示原表面与隔离预览。请查看组件的材质槽支持状态。");
        }
        if(Error.Contains(TEXT("Unified Silhouette requires Screen Space")))
            return TEXT("统一外轮廓需要 Screen Space。请切换基础描边方法，或使用独立区域配合反向壳。");
        if(Error.Contains(TEXT("Material rule needs a unique, used slot name")))
            return TEXT("存在空白、未使用或重复的材质槽规则。展开对应组件，使用“移除无效/重复规则”清理，再从真实材质槽列表勾选。");
        return Error;
    }
    FString RenderError() const
    {
        if(Group.IsValid() && !Group->GetLastError().IsEmpty()) return ExplainError(Group->GetLastError());
        if(Binding.IsValid() && !Binding->GetLastError().IsEmpty()) return ExplainError(Binding->GetLastError());
        return NPRView.IsValid()?ExplainError(NPRView->PreviewError()):FString();
    }
    FString VisibilityHint() const
    {
        if(Group.IsValid() && !Group->bEnabled) return TEXT(" 角色描边当前关闭，可点击启用角色描边查看。");
        const auto& Settings=Group.IsValid()?Group->BaseSettings:Working->Settings;
        if(Settings.Method==ENPROutlineMethod::None || Settings.GetResolvedWidth()<=UE_SMALL_NUMBER)
            return TEXT(" 当前 Method=None 或宽度为零，参数已生效但不会显示线条。");
        return FString();
    }
    bool FocusCurrent(bool bNotify)
    {
        if(!CanPreview() || !NPRView->Focus())
        { if(bNotify) Report(TEXT("尚无有效角色边界，无法聚焦。请先加载角色。"),true); return false; }
        // Apply to both clients immediately so their next Tick cannot publish the old camera back.
        OriginalView->ApplySharedCamera(); NPRView->ApplySharedCamera();
        if(bNotify) Report(TEXT("两个相机已聚焦当前角色；重复聚焦同一构图时画面可以不变。"));
        return true;
    }
    AActor* LevelSelection() const
    {
        return GEditor && GEditor->GetSelectedActorCount()==1?Cast<AActor>(GEditor->GetSelectedActors()->GetSelectedObject(0)):nullptr;
    }
    void SyncPresetSelection()
    {
        SelectedPreset=LoadedPreset;
        PresetPath->SetText(FText::FromString(LoadedPreset.IsValid()?LoadedPreset->GetOutermost()->GetName():TEXT("/Game/NPR/Outline/Presets/OP_Custom")));
    }
    TSharedRef<SWidget> LightButton(const TCHAR* Label,int32 Index)
    {
        return SNew(SButton).Text(FText::FromString(Label)).ToolTipText(FText::FromString(TEXT("只改变两个预览场景的灯光；不修改关卡灯光或风格参数。"))).OnClicked_Lambda([this,Index,Label]{OriginalView->SetLighting(Index);NPRView->SetLighting(Index);Report(FString(TEXT("预览灯光已切换为："))+Label);return FReply::Handled();});
    }
    void UseSelection(bool bNotify=false)
    {
        AActor* Actor=LevelSelection();
        if(!IsValid(Actor)) {Report(TEXT("请先在关卡大纲中只选中一个 Actor，再使用关卡选中角色。"),true,bNotify);return;}
        LoadActor(Actor);
        Report(FString(TEXT("已读取关卡选择："))+Actor->GetActorLabel()+(Group.IsValid()?TEXT("，整角色配置。"):Mesh.IsValid()?TEXT("，单材质槽流程。"):TEXT("。请创建角色级配置以编辑其多个网格组件。")),false,bNotify);
    }
    void LoadActor(AActor* Actor)
    {
        Group.Reset(); IsolatePreview(nullptr,INDEX_NONE); SelectedActor.Reset(); Details->SetObject(Working.Get());
        Mesh.Reset(); Binding.Reset(); Slots.Reset(); SelectedSlot.Reset(); LoadedPreset.Reset(); SelectedPreset.Reset();
        SelectedActor=Actor;
        if(IsValid(Actor))
        {
                for(auto* Existing:TInlineComponentArray<UNPROutlineActorComponent*>(Actor))
                { ShowGroup(Existing); return; }
                for(USkeletalMeshComponent* Candidate:TInlineComponentArray<USkeletalMeshComponent*>(Actor))
                {
                    if(Candidate->GetSkeletalMeshAsset() && !Candidate->HasAnyFlags(RF_Transient))
                    {
                        if(Mesh.IsValid()){Mesh.Reset();break;}
                        Mesh=Candidate;
                    }
                }
        }
        if(Mesh.IsValid())
        {
            SelectionLabel=Mesh->GetOwner()->GetActorLabel()+TEXT(" / ")+Mesh->GetSkeletalMeshAsset()->GetName();
            auto Names=Mesh->GetMaterialSlotNames();
            for(int32 Index=0;Index<Names.Num();++Index){auto Entry=MakeShared<FOutlineSlot>();Entry->Index=Index;Entry->Label=FString::Printf(TEXT("%d · %s"),Index,*Names[Index].ToString());Slots.Add(Entry);}
            if(Slots.Num()) SelectedSlot=Slots[0];
            for(UNPROutlineComponent* Candidate:TInlineComponentArray<UNPROutlineComponent*>(Mesh->GetOwner()))
                if(Candidate->TargetMesh==Mesh.Get() && Slots.IsValidIndex(Candidate->MaterialSlotIndex))
                {SelectedSlot=Slots[Candidate->MaterialSlotIndex];break;}
        }
        else SelectionLabel=IsValid(Actor)?Actor->GetActorLabel()+TEXT(" / 请使用角色级配置"):TEXT("未选择角色");
        bLoadingSelection=true;
        SlotsCombo->RefreshOptions();
        if(SelectedSlot.IsValid()) SlotsCombo->SetSelectedItem(SelectedSlot); else SlotsCombo->ClearSelection();
        bLoadingSelection=false;
        LoadBinding();
    }
    void ReloadCurrent()
    {
        if(!SelectedActor.IsValid()) {Report(TEXT("当前角色已失效，请重新使用关卡选择。"),true);return;}
        const FString Name=SelectedActor->GetActorLabel();
        LoadActor(SelectedActor.Get());
        Report(TEXT("已重新读取 ")+Name+TEXT(" 的当前内存绑定并重建预览。磁盘关卡未重新加载。"));
    }
    void LoadBinding()
    {
        Binding.Reset(); LoadedPreset.Reset();
        if(Mesh.IsValid() && SelectedSlot.IsValid())
            for(UNPROutlineComponent* Candidate:TInlineComponentArray<UNPROutlineComponent*>(Mesh->GetOwner()))
                if(Candidate->TargetMesh==Mesh.Get() && Candidate->MaterialSlotIndex==SelectedSlot->Index){Binding=Candidate;break;}
        if(Binding.IsValid())
        {
            Working->Settings=Binding->Settings; LoadedPreset=Binding->Preset;
        }
        SyncPresetSelection();
        Details->ForceRefresh();
        OriginalView->Load(Mesh.Get(),Binding.Get(),SelectedSlot.IsValid()?SelectedSlot->Index:0,false);
        NPRView->Load(Mesh.Get(),Binding.Get(),SelectedSlot.IsValid()?SelectedSlot->Index:0,true);
        PreviewSettings=Working->Settings;
        FocusCurrent(false);
    }
    void Convert()
    {
        auto Result=UNPROutlineEditorLibrary::ConvertMesh(Mesh.Get(),SelectedSlot.IsValid()?SelectedSlot->Index:INDEX_NONE,Working->Settings);
        if(!Result.bSuccess){Report(Result.Error,true);return;}
        LoadBinding();
        Report(TEXT("单槽实例与 Outline 绑定已创建/更新。原表面着色及纹理通过父材质继承。"));
    }
    void CommitSettings(const FPropertyChangedEvent&)
    {
        if(Group.IsValid()){Group->MarkPackageDirty();const bool OK=Group->RefreshOutline();Report(OK?TEXT("角色参数已应用，请保存基础预设与当前关卡。")+VisibilityHint():Group->GetLastError(),!OK,false);return;}
        if(!Binding.IsValid()){Report(TEXT("当前是未绑定的预备参数，请先转换单槽绑定。"));return;}
        FString Error;
        if(!Working->Settings.Validate(Error)){Report(Error,true);return;}
        const FScopedTransaction Transaction(NSLOCTEXT("NPR","EditOutline","Edit NPR Outline settings"));
        Binding->Modify();
        Binding->Settings=Working->Settings;
        Binding->MarkPackageDirty();
        Binding->RefreshOutline();
        Report(Binding->GetLastError().IsEmpty()?TEXT("参数已应用，请保存预设与场景。")+VisibilityHint():Binding->GetLastError(),!Binding->GetLastError().IsEmpty());
    }
    void LoadPreset(UNPROutlinePreset* Preset)
    {
        if(!Preset){Report(TEXT("请选择一个 Outline 预设。"),true);return;}
        if(!HasTarget()){Report(TEXT("请先加载一个可编辑的角色。"),true);return;}
        FString Error;
        if(Preset->SchemaVersion!=1){Report(TEXT("预设版本不受支持，需要 v1 Outline 预设。"),true);return;}
        if(!Preset->Settings.Validate(Error)){Report(TEXT("预设参数无效：")+Error,true);return;}
        if(Group.IsValid())
        {
            if(!UNPROutlineEditorLibrary::ApplyActorPreset(Group.Get(),Preset,Error)){Report(TEXT("预设未应用：")+Error,true);return;}
        }
        else if(Binding.IsValid())
        {
            if(!UNPROutlineEditorLibrary::ApplyPreset(Binding.Get(),Preset,Error)){Report(TEXT("预设未应用：")+Error,true);return;}
            Working->Settings=Binding->Settings;
        }
        else Working->Settings=Preset->Settings;
        LoadedPreset=Preset; SelectedPreset=Preset;
        PresetPath->SetText(FText::FromString(Preset->GetOutermost()->GetName()));
        Details->ForceRefresh();
        Report(TEXT("已应用基础预设：")+Preset->GetName()+VisibilityHint()+(Group.IsValid()?TEXT(" 本地区域覆写仍参与最终参数。"):Binding.IsValid()?FString():TEXT(" 转换并绑定后可查看描边。")));
    }
    void Save()
    {
        FString Error;
        if(Group.IsValid())
        {
            if(!Group->RefreshOutline()){Report(TEXT("无法保存无效角色配置：")+Group->GetLastError(),true);return;}
            auto* Asset=UNPROutlineEditorLibrary::SavePreset(PresetPath->GetText().ToString(),Group->BaseSettings,Error);
            if(!Asset){Report(TEXT("预设保存失败：")+Error,true);return;}
            if(!UNPROutlineEditorLibrary::ApplyActorPreset(Group.Get(),Asset,Error)){Report(TEXT("预设已写入，但绑定失败：")+Error,true);return;}
            LoadedPreset=Asset; SelectedPreset=Asset; Details->ForceRefresh();
            if(!UNPROutlineEditorLibrary::SaveActorOutline(Group.Get(),Error)){Report(TEXT("预设已保存，但关卡保存未完成：")+Error,true);return;}
            Report(TEXT("已保存基础预设 ")+Asset->GetName()+TEXT(" 与角色所在关卡（包括本地区域覆写）。"));
            return;
        }
        if(!Binding.IsValid()){Report(TEXT("请先转换选中的材质槽。"),true);return;}
        if(!Working->Settings.Validate(Error)){Report(Error,true);return;}
        UNPROutlinePreset* Preset=UNPROutlineEditorLibrary::SavePreset(PresetPath->GetText().ToString(),Working->Settings,Error);
        if(!Preset){Report(Error,true);return;}
        if(!UNPROutlineEditorLibrary::ApplyPreset(Binding.Get(),Preset,Error)){Report(TEXT("预设已写入，但应用失败：")+Error,true);return;}
        LoadedPreset=Preset; SelectedPreset=Preset;
        if(!UNPROutlineEditorLibrary::SaveBinding(Binding.Get(),Error)){Report(TEXT("预设已保存，但绑定/关卡保存未完成：")+Error,true);return;}
        Report(TEXT("已保存预设 ")+Preset->GetName()+TEXT(" 与单槽绑定所在关卡。"));
    }
    void ExportCurrent()
    {
        if(!CanPreview()){Report(TEXT("没有角色画面可导出，请先加载角色。"),true);return;}
        const FDateTime Now=FDateTime::Now();
        const FString Stamp=Now.ToString(TEXT("%Y%m%d-%H%M%S-"))+FString::Printf(TEXT("%03d"),Now.GetMillisecond());
        const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("NPR/Outline/Previews")/Stamp);
        if(!Export(Directory)){Report(TEXT("PNG 导出失败：视口尚未完成绘制，或目录不可写。"),true);return;}
        LastExportDirectory=Directory;
        Report(TEXT("已导出 Original.png 与 Outline.png：")+Directory);
    }
    void RestoreOrToggle()
    {
        FString Error;
        if(Group.IsValid())
        {
            const bool Enable=!Group->bEnabled;
            if(!UNPROutlineEditorLibrary::SetActorOutlineEnabled(Group.Get(),Enable,Error)){Report(Error,true);return;}
            Details->ForceRefresh(); OriginalView->SyncGroup(Group.Get()); NPRView->SyncGroup(Group.Get());
            Report(Enable?TEXT("角色描边已启用。")+VisibilityHint():TEXT("角色描边已关闭，区域配置保留；请保存当前关卡。"));
            return;
        }
        if(!Binding.IsValid()){Report(TEXT("没有可恢复的单槽绑定。"),true);return;}
        if(!UNPROutlineEditorLibrary::RestoreSource(Binding.Get(),Error)){Report(TEXT("原材质恢复失败：")+Error,true);return;}
        LoadBinding();
        Report(TEXT("已恢复单槽原材质并移除该槽的 Outline 绑定，请保存当前关卡。"));
    }
    TStrongObjectPtr<UNPROutlinePreset> Working;
    void ShowGroup(UNPROutlineActorComponent* Value)
    {
        if(Group.Get()!=Value)
        {
            IsolatedSource.Reset(); IsolatedSlot=INDEX_NONE;
            OriginalView->SetIsolation(nullptr,INDEX_NONE); NPRView->SetIsolation(nullptr,INDEX_NONE);
        }
        Group=Value; Binding.Reset(); Mesh.Reset(); Slots.Reset(); SelectedSlot.Reset();
        SelectionLabel=Value->GetOwner()->GetActorLabel()+TEXT(" / Actor Outline");
        Details->SetObject(Value);
        bLoadingSelection=true;
        SlotsCombo->RefreshOptions(); SlotsCombo->ClearSelection();
        bLoadingSelection=false;
        OriginalView->LoadGroup(Value,false); NPRView->LoadGroup(Value,true); FocusCurrent(false);
        LoadedPreset=Value->Preset;
        SyncPresetSelection();
        CollectionRevision=Value->GetCollectionRevision();
    }
    void ConfigureActor()
    {
        AActor* Actor=LevelSelection();
        if(!IsValid(Actor)){Report(TEXT("请先在关卡大纲中只选中一个 Actor，再创建/打开角色配置。"),true);return;}
        FString Error;
        auto* Value=UNPROutlineEditorLibrary::ConfigureActor(Actor,Error);
        if(!Value){Report(Error,true);return;}
        SelectedActor=Actor;
        const bool OK=Value->RefreshOutline();
        ShowGroup(Value);
        Report(OK?FString::Printf(TEXT("已打开 %s 的角色配置，共 %d 个源组件。"),*Actor->GetActorLabel(),Value->Meshes.Num())+VisibilityHint():TEXT("角色配置已打开，但描边需要处理：")+Value->GetLastError(),!OK);
    }
    TWeakObjectPtr<AActor> SelectedActor;
    TWeakObjectPtr<UNPROutlineActorComponent> Group;
    TWeakObjectPtr<USkeletalMeshComponent> Mesh;
    TWeakObjectPtr<UNPROutlineComponent> Binding;
    TWeakObjectPtr<UNPROutlinePreset> LoadedPreset;
    TWeakObjectPtr<UNPROutlinePreset> SelectedPreset;
    TWeakObjectPtr<UMeshComponent> IsolatedSource;
    int32 IsolatedSlot=INDEX_NONE;
    bool bShowOriginal=false;
    bool bFixedCapture=false;
    TSharedPtr<SVerticalBox> OriginalPane;
    TSharedRef<FNPROutlineSlotUIState> SlotUIState=MakeShared<FNPROutlineSlotUIState>();
    TSharedPtr<IDetailsView> Details;
    TSharedPtr<SComboBox<TSharedPtr<FOutlineSlot>>> SlotsCombo;
    TArray<TSharedPtr<FOutlineSlot>> Slots;
    TSharedPtr<FOutlineSlot> SelectedSlot;
    TSharedPtr<SEditableTextBox> PresetPath;
    TSharedPtr<FSharedCamera> Camera;
    TSharedPtr<SOutlineViewport> OriginalView,NPRView;
    FNPROutlineSettings PreviewSettings;
    FString SelectionLabel,Status;
    FString LastExportDirectory;
    bool bStatusError=false;
    bool bLoadingSelection=false;
    uint32 CollectionRevision=0;
};
// Keep the old Outline tab identity and scripting facade for saved layouts and existing tools.
class SNPRActorEffects final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SNPRActorEffects) {} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        Preview=MakeNPRCharacterPreview();
        ChildSlot[SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(10,8)[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(12,8))
                + SWrapBox::Slot()[SNew(SBorder).Padding(10)[SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[SNew(STextBlock).Text(FText::FromString(TEXT("非后处理"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",12))]
                    + SVerticalBox::Slot().AutoHeight()[SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(6,6))
                        + SWrapBox::Slot()[CategoryEntry(TEXT("Rim Light · 边缘光"),TEXT("RimLight"))]
                        + SWrapBox::Slot()[CategoryEntry(TEXT("反向壳 · 几何描边"),TEXT("HullOutline"))]
                        + SWrapBox::Slot()[CategoryEntry(TEXT("Surface · 表面组合"),TEXT("Surface"))]]]]
                + SWrapBox::Slot()[SNew(SBorder).Padding(10)[SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[SNew(STextBlock).Text(FText::FromString(TEXT("后处理"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",12))]
                    + SVerticalBox::Slot().AutoHeight()[CategoryEntry(TEXT("Screen · 屏幕描边"),TEXT("ScreenOutline"))]]]]
            + SVerticalBox::Slot().FillHeight(1)[SNew(SSplitter)
            + SSplitter::Slot().Value(.32f)[SNew(SBorder).Padding(10)[SNew(SScrollBox)+SScrollBox::Slot()[SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("角色 · Mesh · 已应用效果"))).Font(FCoreStyle::GetDefaultFontStyle("Bold",16))]
                + SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(SButton).Text(FText::FromString(TEXT("使用关卡选中角色"))).OnClicked_Lambda([this]{Reload();return FReply::Handled();})]
                + SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(STextBlock).Text_Lambda([this]{return FText::FromString(Actor.IsValid()?Actor->GetActorLabel():TEXT("请在关卡大纲中只选中一个角色。"));}).AutoWrapText(true)]
                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(TEXT("按实际登记的绑定查看效果。选择单项只影响预览，原材质图中未登记的功能不作猜测。"))).AutoWrapText(true)]
                + SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(SButton).Text(FText::FromString(TEXT("查看全部效果组合"))).OnClicked_Lambda([this]{Filter=TEXT("All");Preview->SetEffectFilter(Filter);Preview->SetRimTarget(nullptr,INDEX_NONE);return FReply::Handled();})]
                + SVerticalBox::Slot().AutoHeight()[SAssignNew(RowsBox,SVerticalBox)]]]]
            + SSplitter::Slot().Value(.68f)[Preview->Widget()]]];
        Reload();
    }
    void Reload()
    {
        Actor=GEditor && GEditor->GetSelectedActorCount()==1?Cast<AActor>(GEditor->GetSelectedActors()->GetSelectedObject(0)):nullptr;
        Preview->LoadActor(Actor.Get());Signature=TEXT("!reload");BuildRows();
    }
    void FilterEffect(FName Effect){Filter=Effect;Preview->SetRimTarget(nullptr,INDEX_NONE);Preview->SetEffectFilter(Effect);}
    TSharedPtr<FNPRCharacterPreview> GetPreview()const{return Preview;}
    virtual void Tick(const FGeometry& G,double T,float D)override
    {SCompoundWidget::Tick(G,T,D);Preview->Refresh();if(T>=NextUpdate){BuildRows();NextUpdate=T+.5;}}
private:
    TSharedRef<SWidget> CategoryEntry(const TCHAR* Label,FName Effect)
    {return SNew(SButton).Text(FText::FromString(Label)).OnClicked_Lambda([this,Effect]
        {if(Effect==TEXT("RimLight"))FNPROutlineEditorUI::SelectFeature(TEXT("Rim"));else FilterEffect(Effect);return FReply::Handled();});}
    void BuildRows()
    {
        const auto Rows=UNPRModulationEditorLibrary::GetMeshEffects(Actor.Get());
        FString NewSignature;
        for(const auto& Row:Rows)NewSignature+=Row.Mesh->GetPathName()+FString::FromInt(Row.SlotIndex)+Row.Description;
        if(NewSignature==Signature)return;Signature=NewSignature;RowsBox->ClearChildren();
        for(const auto& Row:Rows)
        {
            const TWeakObjectPtr<UMeshComponent> Mesh=Row.Mesh;const int32 Slot=Row.SlotIndex;
            TSharedRef<SWrapBox> Buttons=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(5,5));
            Buttons->AddSlot()[SNew(SButton).Text(FText::FromString(TEXT("隔离 Mesh"))).OnClicked_Lambda([this,Mesh]{Preview->Isolate(Mesh.Get(),INDEX_NONE);return FReply::Handled();})];
            Buttons->AddSlot()[SNew(SButton).Text(FText::FromString(TEXT("隔离材质槽"))).OnClicked_Lambda([this,Mesh,Slot]{Preview->Isolate(Mesh.Get(),Slot);return FReply::Handled();})];
            for(const FName Effect:Row.Effects)
            {
                Buttons->AddSlot()[SNew(SButton).Text(FText::FromString(TEXT("只看 ")+Effect.ToString()))
                    .OnClicked_Lambda([this,Mesh,Slot,Effect]{Filter=Effect;Preview->SetRimTarget(Mesh.Get(),Slot);Preview->SetEffectFilter(Effect);Preview->Isolate(Mesh.Get(),Slot);return FReply::Handled();})];
                if(Effect==TEXT("RimLight")||Effect==TEXT("HullOutline")||Effect==TEXT("ScreenOutline"))
                    Buttons->AddSlot()[SNew(SButton).Text(FText::FromString(Effect==TEXT("RimLight")?TEXT("编辑 Rim Light"):TEXT("编辑描边")))
                        .OnClicked_Lambda([Mesh,Slot,Effect]{FNPROutlineEditorUI::EditEffect(Mesh.Get(),Slot,Effect);return FReply::Handled();})];
            }
            RowsBox->AddSlot().AutoHeight().Padding(0,4)[SNew(SBorder).Padding(8)[SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s · 槽 %d / %s"),*Row.Mesh->GetName(),Slot,*Row.SlotName.ToString()))).AutoWrapText(true)]
                + SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(STextBlock).Text(FText::FromString(Row.Description)).AutoWrapText(true)]
                + SVerticalBox::Slot().AutoHeight()[Buttons]]];
        }
    }
    TWeakObjectPtr<AActor> Actor;
    TSharedPtr<FNPRCharacterPreview> Preview;
    TSharedPtr<SVerticalBox> RowsBox;
    FString Signature;FName Filter=TEXT("All");double NextUpdate=0;
};

class SNPRModulationEditor final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SNPRModulationEditor) {} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        ChildSlot
        [SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(14,10))
                [SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8,6))
                    + SWrapBox::Slot()[FeatureEntry(TEXT("NPR 调制"),2)]
                    + SWrapBox::Slot()[FeatureEntry(TEXT("Outline · 描边"),0)]
                    + SWrapBox::Slot()[FeatureEntry(TEXT("Rim Light · 边缘光"),1)]]]
            + SVerticalBox::Slot().FillHeight(1)
            [SAssignNew(FeaturePages,SWidgetSwitcher)
                + SWidgetSwitcher::Slot()[SAssignNew(Outline,SNPROutlineEditor)]
                + SWidgetSwitcher::Slot()[SAssignNew(Rim,SNPRRimEditor)]
                + SWidgetSwitcher::Slot()[SAssignNew(Overview,SNPRActorEffects)]
            ]
        ];
    }
    void ShowOutline() {Show(0);}
    void Show(int32 Feature)
    {ActiveFeature=Feature;FeaturePages->SetActiveWidgetIndex(Feature);if(Feature==2){Overview->Reload();Overview->FilterEffect(TEXT("All"));}else if(Feature==1)Rim->SyncSelection();else Outline->SyncSelection();}
    bool EditEffect(UMeshComponent* Mesh,int32 Slot,FName Effect)
    {
        if(!IsValid(Mesh)||Slot<0||Slot>=Mesh->GetNumMaterials())return false;
        if(Effect==TEXT("RimLight")){Show(1);Rim->EditTarget(Mesh,Slot);return true;}
        if(Effect==TEXT("HullOutline")||Effect==TEXT("ScreenOutline")){Show(0);return true;}
        return false;
    }
    bool SelectFeature(FName Feature)
    {
        if(Feature==TEXT("Overview")){Show(2);return true;}
        if(Feature==TEXT("Rim")){Show(1);return true;}
        if(Feature==TEXT("Outline")){Show(0);return true;}
        if(Feature==TEXT("Surface")||Feature==TEXT("HullOutline")||Feature==TEXT("ScreenOutline")){Show(2);Overview->FilterEffect(Feature);return true;}
        return false;
    }
    TSharedPtr<FNPRCharacterPreview> CurrentPreview()const{return ActiveFeature==1?Rim->GetPreview():ActiveFeature==2?Overview->GetPreview():nullptr;}
    TSharedPtr<SNPROutlineEditor> GetOutline() const { return Outline; }
private:
    TSharedRef<SWidget> FeatureEntry(const TCHAR* Label,int32 Feature)
    {
        return SNew(SCheckBox).Style(FAppStyle::Get(),"ToggleButtonCheckbox").Padding(FMargin(12,6))
            .IsChecked_Lambda([this,Feature]{return ActiveFeature==Feature?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
            .OnCheckStateChanged_Lambda([this,Feature](ECheckBoxState Value){if(Value==ECheckBoxState::Checked){Show(Feature);FSlateApplication::Get().DismissAllMenus();}})
            [SNew(SBox).HeightOverride(28).VAlign(VAlign_Center)
                [SNew(STextBlock).Text(FText::FromString(Label)).Font(FCoreStyle::GetDefaultFontStyle("Bold",Feature==2?16:12))]];
    }
    int32 ActiveFeature=0;
    TSharedPtr<SWidgetSwitcher> FeaturePages;
    TSharedPtr<SNPROutlineEditor> Outline;
    TSharedPtr<SNPRRimEditor> Rim;
    TSharedPtr<SNPRActorEffects> Overview;
};
TWeakPtr<SNPRModulationEditor> ActiveWorkspace;
TWeakPtr<SNPROutlineEditor> ActiveEditor;
}

TSharedRef<FNPRCharacterPreview> MakeNPRCharacterPreview()
{auto Preview=MakeShared<FCharacterPreview>();Preview->Construct();return Preview;}

void FNPROutlineEditorUI::Register()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(OutlineTabId,FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
    {
        TSharedRef<SNPRModulationEditor> Widget=SNew(SNPRModulationEditor);
        ActiveWorkspace=Widget;
        ActiveEditor=Widget->GetOutline();
        return SNew(SDockTab).TabRole(ETabRole::NomadTab)[Widget];
    })).SetDisplayName(FText::FromString(TEXT("NPR 调制")));
    MenuStartupHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]
    {
        FToolMenuOwnerScoped Owner(TEXT("PrincipledNPROutline"));
        UToolMenu* Menu=UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Window"));
        Menu->FindOrAddSection(TEXT("NPR")).AddMenuEntry(TEXT("OpenNPROutline"),FText::FromString(TEXT("NPR 调制")),
            FText::FromString(TEXT("打开 NPR 效果工作区：分类、角色预览、Mesh 隔离与调参")),FSlateIcon(),FUIAction(FExecuteAction::CreateStatic(&FNPROutlineEditorUI::OpenWorkspace)));
        UToolMenu* Toolbar=UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.User"));
        Toolbar->FindOrAddSection(TEXT("NPR")).AddEntry(FToolMenuEntry::InitToolBarButton(TEXT("NPRModulationWorkspace"),
            FUIAction(FExecuteAction::CreateStatic(&FNPROutlineEditorUI::OpenWorkspace)),FText::FromString(TEXT("NPR 调制")),
            FText::FromString(TEXT("NPR 效果工作区：角色预览、Mesh/材质槽隔离与效果分类")),FSlateIcon(FAppStyle::GetAppStyleSetName(),TEXT("LevelEditor.Tabs.Viewports"))));
    }));
}
void FNPROutlineEditorUI::Unregister()
{
    UToolMenus::UnRegisterStartupCallback(MenuStartupHandle);
    UToolMenus::UnregisterOwner(TEXT("PrincipledNPROutline"));
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(OutlineTabId);
}
void FNPROutlineEditorUI::Open(){FGlobalTabmanager::Get()->TryInvokeTab(OutlineTabId);if(ActiveWorkspace.IsValid())ActiveWorkspace.Pin()->ShowOutline();if(ActiveEditor.IsValid())ActiveEditor.Pin()->ReloadSelection();}
void FNPROutlineEditorUI::OpenWorkspace(){FGlobalTabmanager::Get()->TryInvokeTab(OutlineTabId);if(ActiveWorkspace.IsValid())ActiveWorkspace.Pin()->Show(2);}
bool FNPROutlineEditorUI::SelectFeature(FName Feature){FGlobalTabmanager::Get()->TryInvokeTab(OutlineTabId);return ActiveWorkspace.IsValid()&&ActiveWorkspace.Pin()->SelectFeature(Feature);}
bool FNPROutlineEditorUI::EditEffect(UMeshComponent* Mesh,int32 Slot,FName Effect)
{
    if(!IsValid(Mesh)||!IsValid(Mesh->GetOwner()))return false;
    if(GEditor->GetSelectedActorCount()!=1||GEditor->GetSelectedActors()->GetSelectedObject(0)!=Mesh->GetOwner())
    {GEditor->SelectNone(false,true,false);GEditor->SelectActor(Mesh->GetOwner(),true,true,true);}
    FGlobalTabmanager::Get()->TryInvokeTab(OutlineTabId);
    return ActiveWorkspace.IsValid()&&ActiveWorkspace.Pin()->EditEffect(Mesh,Slot,Effect);
}
TSharedPtr<FNPRCharacterPreview> FNPROutlineEditorUI::CurrentPreview(){return ActiveWorkspace.IsValid()?ActiveWorkspace.Pin()->CurrentPreview():nullptr;}
bool FNPROutlineEditorUI::ExportComparison(const FString& Directory){return ActiveEditor.IsValid()&&ActiveEditor.Pin()->Export(Directory);}
bool FNPROutlineEditorUI::SetComparisonCaptureSize(int32 Width,int32 Height){return ActiveEditor.IsValid()&&ActiveEditor.Pin()->SetCaptureSize(Width,Height);}
int32 FNPROutlineEditorUI::GetComparisonMeshCount(bool bOutlined){return ActiveEditor.IsValid()?ActiveEditor.Pin()->MeshCount(bOutlined):0;}
bool FNPROutlineEditorUI::SetComparisonCamera(const FVector& Location,const FRotator& Rotation)
{ return ActiveEditor.IsValid() && ActiveEditor.Pin()->SetCamera(Location,Rotation); }
bool FNPROutlineEditorUI::GetComparisonCamera(FVector& Location,FRotator& Rotation)
{ return ActiveEditor.IsValid() && ActiveEditor.Pin()->GetCamera(Location,Rotation); }
bool FNPROutlineEditorUI::SetComparisonScreenPercentage(int32 Percentage)
{ return Percentage>=50 && Percentage<=100 && ActiveEditor.IsValid() && ActiveEditor.Pin()->SetScreenPercentage(Percentage); }
FString FNPROutlineEditorUI::GetComparisonTemporalState()
{ return ActiveEditor.IsValid()?ActiveEditor.Pin()->TemporalState():FString(); }
