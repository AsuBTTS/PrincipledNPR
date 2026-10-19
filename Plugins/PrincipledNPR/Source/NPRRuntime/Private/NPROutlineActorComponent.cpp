#include "NPROutlineActorComponent.h"
#include "NPROutlineComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "StaticMeshResources.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"

namespace
{
UObject* MeshAsset(UMeshComponent* Mesh)
{
    if (auto* Skel = Cast<USkeletalMeshComponent>(Mesh)) return Skel->GetSkeletalMeshAsset();
    if (auto* Static = Cast<UStaticMeshComponent>(Mesh)) return Static->GetStaticMesh();
    return nullptr;
}
bool SameRegions(const TArray<FNPROutlineResolvedRegion>& A, const TArray<FNPROutlineResolvedRegion>& B)
{
    if (A.Num() != B.Num()) return false;
    for (int32 I=0; I<A.Num(); ++I)
        if (A[I].Mesh != B[I].Mesh || A[I].MaterialSlotIndex != B[I].MaterialSlotIndex ||
            A[I].bEnabled != B[I].bEnabled || A[I].Settings != B[I].Settings) return false;
    return true;
}
void SetParameters(UMaterialInstanceDynamic* MID, const FNPROutlineSettings& S)
{
    S.ApplyMaterialParameters(MID);
}
}

void FNPROutlineOverride::Apply(FNPROutlineSettings& S, bool& Enabled) const
{
    if (bOverrideEnabled) Enabled = bEnabled;
    if (bOverrideMethod) S.Method = Values.Method;
    if (bOverrideColor) S.Color = Values.Color;
    if (bOverrideWidth) { S.WidthPixels = Values.WidthPixels; S.HullWidthCm = Values.HullWidthCm; }
    if (bOverrideEdgeSource) S.WidthEdgeSource = Values.WidthEdgeSource;
    if (bOverrideEdgeWidthScales) S.EdgeWidthScales = Values.EdgeWidthScales;
    if (bOverrideLocalWidthScale) S.LocalWidthScale = Values.LocalWidthScale;
    if (bOverrideDepthBias) S.DepthBiasCm = Values.DepthBiasCm;
    if (bOverrideVertexControl) S.bUseVertexColorWidth = Values.bUseVertexColorWidth;
    if (bOverrideWidthMask) { S.bUseWidthMask = Values.bUseWidthMask; S.WidthMask = Values.WidthMask; }
}

TArray<FNPROutlineEdgeCapability> UNPROutlineActorComponent::GetEdgeCapabilities(ENPROutlineMethod Method,ENPROutlineScope OutlineScope)
{
    const bool Active=Method==ENPROutlineMethod::ScreenSpace || (Method==ENPROutlineMethod::InvertedHull && OutlineScope==ENPROutlineScope::IndependentRegions);
    const bool Regions=Active && OutlineScope==ENPROutlineScope::IndependentRegions;
    TArray<FNPROutlineEdgeCapability> Result;
    auto Add=[&](ENPROutlineEdgeSource Source,bool Automatic,bool Authored,const TCHAR* Note)
    { FNPROutlineEdgeCapability C; C.Source=Source; C.bAutomatic=Automatic; C.bAuthoredRegion=Authored; C.Limitation=Note; Result.Add(C); };
    Add(ENPROutlineEdgeSource::Silhouette,Active,Regions,TEXT("Hull/Stencil 生成区域轮廓；独立区域模式不保证它是整个角色的最外轮廓。Hull 不输出边类型 ID。Unified 只支持整组统一宽度。"));
    Add(ENPROutlineEdgeSource::Crease,false,false,TEXT("没有邻接/二面角或法线折痕检测；硬法线壳接缝不是 crease 分类。可用局部宽度控制已有轮廓，不能因此生成内部折线。"));
    Add(ENPROutlineEdgeSource::Occlusion,false,false,TEXT("Screen 深度比较仅抑制被遮挡的线；没有独立的遮挡边检测/倍率，Hull 仅接受普通深度遮挡。"));
    Add(ENPROutlineEdgeSource::FeatureBoundary,false,Regions && Method==ENPROutlineMethod::ScreenSpace,TEXT("仅 Screen + Independent Regions：手工用组件/Material Slot/受限 Section 划区；不识别纹理或语义特征。Hull 不能凭切分材质槽生成内部边界线。"));
    return Result;
}

FString UNPROutlineActorComponent::DescribeWidthSupport(ENPROutlineMethod Method,ENPROutlineScope OutlineScope)
{
    if(Method==ENPROutlineMethod::None) return TEXT("描边已关闭。宽度配置会保留。");
    FString Text=TEXT("宽度 = 基础 × 边类型倍率 × 区域倍率。crease / occlusion 暂无独立检测。\n");
    Text+=OutlineScope==ENPROutlineScope::UnifiedSilhouette?TEXT("统一外轮廓：同组必须共用宽度；局部差异请切换独立区域。\n"):TEXT("区域覆写：Component → Material Slot → 独占槽 Section，最深层勾选项生效。\n");
    Text+=Method==ENPROutlineMethod::InvertedHull?TEXT("Hull 可再乘资产 Vertex Color.R 与 UV0 Mask.R；这是轮廓宽度控制，不生成内部特征线。"):TEXT("Screen 支持区域常量宽度；不读取连续 Vertex Color / UV Mask 宽度。");
    return Text;
}

UNPROutlineActorComponent::UNPROutlineActorComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    bTickInEditor = true;
}

void UNPROutlineActorComponent::CollectOwnerMeshes()
{
    if (!GetOwner()) return;
#if WITH_EDITOR
    if(GetWorld() && !GetWorld()->IsGameWorld()) Modify();
#endif
    TArray<FNPROutlineMeshRule> Collected;
    for (UMeshComponent* Mesh : TInlineComponentArray<UMeshComponent*>(GetOwner()))
    {
        if (!Mesh->HasAnyFlags(RF_Transient) && MeshAsset(Mesh) && !Mesh->IsA<UInstancedStaticMeshComponent>())
        {
            const auto* Old = Meshes.FindByPredicate([Mesh](const FNPROutlineMeshRule& R){return R.Mesh == Mesh;});
            FNPROutlineMeshRule Rule = Old ? *Old : FNPROutlineMeshRule();
            Rule.Mesh = Mesh;
            Rule.SourceName = Mesh->GetFName();
            Collected.Add(Rule);
        }
    }
    int32 Added=0,Removed=0;
    for(const auto& R:Collected) if(!Meshes.ContainsByPredicate([&](const FNPROutlineMeshRule& Old){return Old.Mesh==R.Mesh;})) ++Added;
    for(const auto& Old:Meshes) if(!Collected.ContainsByPredicate([&](const FNPROutlineMeshRule& R){return Old.Mesh==R.Mesh;})) ++Removed;
    Meshes = MoveTemp(Collected);
    ++CollectionRevision;
    CollectionSummary=FString::Printf(TEXT("已重新收集 %d 个源网格组件：新增 %d，移除 %d，保留已有规则。"),Meshes.Num(),Added,Removed);
#if WITH_EDITOR
    if(GetWorld() && !GetWorld()->IsGameWorld()) MarkPackageDirty();
#endif
    if(IsRegistered()) RefreshOutline();
}

bool UNPROutlineActorComponent::GetSectionMapping(UMeshComponent* Mesh, TArray<FNPROutlineSectionInfo>& Out, FString& Error)
{
    Out.Reset(); Error.Reset();
    if (!IsValid(Mesh) || !MeshAsset(Mesh)) { Error = TEXT("Missing mesh asset."); return false; }
    const TArray<FName> Names = Mesh->GetMaterialSlotNames();
    auto Add = [&](int32 LOD, int32 Section, int32 Slot)
    {
        FNPROutlineSectionInfo Row;
        Row.LODIndex=LOD; Row.SectionIndex=Section; Row.MaterialSlotIndex=Slot;
        Row.MaterialSlotName=Names.IsValidIndex(Slot)?Names[Slot]:NAME_None;
        Out.Add(Row);
    };
    if (auto* Skel = Cast<USkeletalMeshComponent>(Mesh))
    {
        USkeletalMesh* Asset = Skel->GetSkeletalMeshAsset();
        const FSkeletalMeshRenderData* Data = Asset->GetResourceForRendering();
        if (!Data) { Error=TEXT("Skeletal render data is not ready."); return false; }
        for (int32 L=0; L<Data->LODRenderData.Num(); ++L)
        {
            const auto* Info=Asset->GetLODInfo(L);
            const auto& Sections=Data->LODRenderData[L].RenderSections;
            for (int32 S=0; S<Sections.Num(); ++S)
            {
                int32 Slot=Sections[S].MaterialIndex;
                if (Info && Info->LODMaterialMap.IsValidIndex(S) && Info->LODMaterialMap[S]!=INDEX_NONE) Slot=Info->LODMaterialMap[S];
                Add(L,S,Slot);
            }
        }
    }
    else if (auto* Static = Cast<UStaticMeshComponent>(Mesh))
    {
        const FStaticMeshRenderData* Data=Static->GetStaticMesh()->GetRenderData();
        if (!Data) { Error=TEXT("Static mesh render data is not ready."); return false; }
        for(int32 L=0; L<Data->LODResources.Num(); ++L)
            for(int32 S=0; S<Data->LODResources[L].Sections.Num(); ++S)
                Add(L,S,Data->LODResources[L].Sections[S].MaterialIndex);
    }
    else { Error=TEXT("Only SkeletalMesh and ordinary StaticMesh components are supported."); return false; }
    if (Out.IsEmpty()) { Error=TEXT("No render sections available."); return false; }
    return true;
}

bool UNPROutlineActorComponent::ResolveRegions(TArray<FNPROutlineResolvedRegion>& Out, FString& Error) const
{
    Out.Reset(); Error.Reset();
    if (!BaseSettings.Validate(Error)) return false;
    if (Scope!=ENPROutlineScope::UnifiedSilhouette && Scope!=ENPROutlineScope::IndependentRegions)
    { Error=TEXT("Unknown Outline Scope."); return false; }
    if (Scope==ENPROutlineScope::UnifiedSilhouette && BaseSettings.Method!=ENPROutlineMethod::ScreenSpace && BaseSettings.Method!=ENPROutlineMethod::None)
    { Error=TEXT("Unified Silhouette requires Screen Space. Hull cannot guarantee removal of internal seams."); return false; }
    if(Scope==ENPROutlineScope::UnifiedSilhouette && BaseSettings.WidthEdgeSource!=ENPROutlineEdgeSource::Silhouette)
    { Error=TEXT("Unified Silhouette cannot generate authored feature boundaries. Select Independent Regions."); return false; }
    if (Meshes.IsEmpty()) { Error=TEXT("Collect or assign the actor's mesh components first."); return false; }
    TSet<UMeshComponent*> Seen;
    for (const FNPROutlineMeshRule& Rule : Meshes)
    {
        UMeshComponent* Mesh=Rule.Mesh;
        if (!IsValid(Mesh) || Mesh->GetOwner()!=GetOwner() || Mesh->HasAnyFlags(RF_Transient) || Seen.Contains(Mesh) || Mesh->IsA<UInstancedStaticMeshComponent>())
        { Error=TEXT("Mesh sources must be unique, persistent, ordinary components owned by this actor."); return false; }
        Seen.Add(Mesh);
        TArray<FNPROutlineSectionInfo> Mapping;
        if (!GetSectionMapping(Mesh,Mapping,Error)) return false;
        TSet<int32> UsedSlots;
        for(const auto& Row:Mapping) UsedSlots.Add(Row.MaterialSlotIndex);
        const auto Names=Mesh->GetMaterialSlotNames();
        TMap<int32,const FNPROutlineOverride*> Materials,Sections;
        for(const auto& Material:Rule.Materials)
        {
            const int32 Slot=Mesh->GetMaterialIndex(Material.MaterialSlotName);
            if(Slot==INDEX_NONE || !UsedSlots.Contains(Slot) || Materials.Contains(Slot) || Names.FilterByPredicate([&](FName N){return N==Material.MaterialSlotName;}).Num()!=1)
            { Error=TEXT("Material rule needs a unique, used slot name; duplicate overrides are not allowed."); return false; }
            Materials.Add(Slot,&Material.Override);
        }
        for(const auto& Section:Rule.Sections)
        {
            const auto* Row=Mapping.FindByPredicate([&](const FNPROutlineSectionInfo& R){return R.LODIndex==Section.LODIndex && R.SectionIndex==Section.SectionIndex;});
            if(!Row) { Error=TEXT("Section override references a missing (LOD, Section)."); return false; }
            if(Mapping.ContainsByPredicate([](const FNPROutlineSectionInfo& R){return R.LODIndex!=0;}))
            { Error=TEXT("Independent Section overrides on multi-LOD meshes need explicit cross-LOD identity mapping. Use Material overrides instead."); return false; }
            if(Mapping.FilterByPredicate([&](const FNPROutlineSectionInfo& R){return R.LODIndex==Row->LODIndex && R.MaterialSlotIndex==Row->MaterialSlotIndex;}).Num()!=1)
            { Error=TEXT("This Section shares its material slot. Independent Section control requires splitting the asset slot mapping or a custom render proxy."); return false; }
            if(Sections.Contains(Row->MaterialSlotIndex)) { Error=TEXT("Duplicate Section override."); return false; }
            Sections.Add(Row->MaterialSlotIndex,&Section.Override);
        }
        TArray<int32> SortedSlots=UsedSlots.Array(); SortedSlots.Sort();
        for(int32 Slot:SortedSlots)
        {
            FNPROutlineResolvedRegion R;
            R.Mesh=Mesh; R.MaterialSlotIndex=Slot; R.Settings=BaseSettings; R.bEnabled=true;
            FString BaseSource=TEXT("Actor"),EdgeSource=TEXT("Actor"),ScaleSource=TEXT("Actor"),KindSource=TEXT("Actor"),MaskSource=TEXT("Actor"),VertexSource=TEXT("Actor");
            auto RecordWidthSource=[&](const FNPROutlineOverride& O,const FString& Name)
            {
                if(O.bOverrideWidth) BaseSource=Name;
                if(O.bOverrideEdgeWidthScales) EdgeSource=Name;
                if(O.bOverrideLocalWidthScale) ScaleSource=Name;
                if(O.bOverrideEdgeSource) KindSource=Name;
                if(O.bOverrideWidthMask) MaskSource=Name;
                if(O.bOverrideVertexControl) VertexSource=Name;
            };
            R.ParameterSource=TEXT("Actor > Component");
            Rule.Override.Apply(R.Settings,R.bEnabled);
            RecordWidthSource(Rule.Override,TEXT("Component"));
            if(auto* Override=Materials.Find(Slot)) { (*Override)->Apply(R.Settings,R.bEnabled); R.ParameterSource+=TEXT(" > Material"); }
            if(auto* Override=Materials.Find(Slot)) RecordWidthSource(**Override,TEXT("Material Slot"));
            if(auto* Override=Sections.Find(Slot)) { (*Override)->Apply(R.Settings,R.bEnabled); R.ParameterSource+=TEXT(" > Section"); }
            if(auto* Override=Sections.Find(Slot)) RecordWidthSource(**Override,TEXT("Section"));
            R.ResolvedWidth=R.Settings.GetResolvedWidth();
            R.WidthParameterSource=FString::Printf(TEXT("Base=%s; EdgeScales=%s; LocalScale=%s; EdgeSource=%s; Vertex=%s; Mask=%s"),*BaseSource,*EdgeSource,*ScaleSource,*KindSource,*VertexSource,*MaskSource);
            R.bEnabled=R.bEnabled && bEnabled && R.Settings.Method!=ENPROutlineMethod::None;
            if(!R.Settings.Validate(Error)) return false;
            R.bEnabled=R.bEnabled && R.ResolvedWidth>UE_SMALL_NUMBER;
            if(R.bEnabled)
            {
                auto* Mat=Mesh->GetMaterial(Slot);
                if(!Mat || Mat->GetBlendMode()!=BLEND_Opaque)
                { Error=FString::Printf(TEXT("%s slot %d needs Opaque coverage; masked/translucent coverage adapters are not implemented."),*Mesh->GetName(),Slot); return false; }
                if(Scope==ENPROutlineScope::UnifiedSilhouette && R.Settings!=BaseSettings)
                { Error=TEXT("Unified Silhouette uses one style and stencil. Local style overrides require Independent Regions; local disable is allowed."); return false; }
                if(R.Settings.Method==ENPROutlineMethod::ScreenSpace && Mesh->bRenderCustomDepth)
                { Error=TEXT("Source already writes CustomDepth; resolve stencil ownership or select Hull."); return false; }
                if(R.Settings.Method==ENPROutlineMethod::InvertedHull && Mesh->GetComponentScale().GetMin()<=0)
                { Error=TEXT("Hull with zero or negative scale is not validated. Apply the transform in the asset first."); return false; }
                if(auto* Static=Cast<UStaticMeshComponent>(Mesh))
                    if(Static->GetStaticMesh()->HasValidNaniteData())
                    { Error=TEXT("Nanite sources require a dedicated coverage/LOD adapter; use a non-Nanite asset."); return false; }
            }
            Out.Add(R);
        }
    }
    return true;
}

void UNPROutlineActorComponent::ReleaseHelpers()
{
    for(UPostProcessComponent* PP:PostProcesses) if(IsValid(PP)) PP->DestroyComponent();
    for(UMeshComponent* Mesh:Helpers) if(IsValid(Mesh)) Mesh->DestroyComponent();
    PostProcesses.Reset(); Helpers.Reset(); Sources.Reset(); BuiltRegions.Reset(); BuiltAssets.Reset();
}

bool UNPROutlineActorComponent::RefreshOutline()
{
    if(IsTemplate() || !GetWorld() || !GetOwner()) return false;
    auto Fail=[this](){ReleaseHelpers(); return false;};
    if(!bEnabled) { ReleaseHelpers(); LastError.Reset(); return true; }
    for(auto* Legacy:TInlineComponentArray<UNPROutlineComponent*>(GetOwner()))
        if(Legacy->Settings.Method!=ENPROutlineMethod::None)
        { LastError=TEXT("An active legacy slot binding already owns this actor. Disable it before enabling actor outlines."); return Fail(); }
    for(auto* Other:TInlineComponentArray<UNPROutlineActorComponent*>(GetOwner()))
        if(Other!=this && Other->bEnabled)
        { LastError=TEXT("Only one enabled Outline actor coordinator is allowed per actor."); return Fail(); }
    TArray<FNPROutlineResolvedRegion> Regions;
    if(!ResolveRegions(Regions,LastError)) return Fail();
    const bool Screen=Regions.ContainsByPredicate([](const FNPROutlineResolvedRegion& R){return R.bEnabled && R.Settings.Method==ENPROutlineMethod::ScreenSpace;});
    const bool Hull=Regions.ContainsByPredicate([](const FNPROutlineResolvedRegion& R){return R.bEnabled && R.Settings.Method==ENPROutlineMethod::InvertedHull;});
    if((Screen || Hull) && (!MaskTemplate || (Hull && !HullTemplate) || (Screen && !ScreenTemplate)))
    { LastError=TEXT("Outline backend templates are missing."); return Fail(); }
    if(Screen)
    {
        static const auto* CVar=IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"));
        if(!CVar || CVar->GetInt()!=3) { LastError=TEXT("Screen Space requires r.CustomDepth=3."); return Fail(); }
        if(ScreenTemplate->GetMaterial()->bIsBlendable)
        { LastError=TEXT("Rebuild Outline backend assets: Screen material must have Is Blendable disabled for independent groups."); return Fail(); }
    }
    TArray<TObjectPtr<UObject>> Assets;
    for(const auto& R:Regions) Assets.Add(MeshAsset(R.Mesh));
    Assets.Add(HullTemplate); Assets.Add(MaskTemplate); Assets.Add(ScreenTemplate);
    if(BuiltScope==Scope && BuiltAssets==Assets && SameRegions(Regions,BuiltRegions) &&
        !Helpers.ContainsByPredicate([](UMeshComponent* M){return !IsValid(M);}))
    { SyncHelpers(); LastError.Reset(); return true; }

    struct FGroup { FNPROutlineSettings Settings; UMeshComponent* Mesh=nullptr; int32 Slot=INDEX_NONE; TArray<int32> Rows; int32 Stencil=0; };
    TArray<FGroup> Groups;
    for(int32 I=0; I<Regions.Num(); ++I)
    {
        const auto& R=Regions[I]; if(!R.bEnabled) continue;
        auto* Group=Groups.FindByPredicate([&](const FGroup& G){return G.Settings==R.Settings && (Scope==ENPROutlineScope::UnifiedSilhouette || G.Mesh==R.Mesh) &&
            (R.Settings.WidthEdgeSource!=ENPROutlineEdgeSource::FeatureBoundary || G.Slot==R.MaterialSlotIndex);});
        if(!Group) { FGroup New; New.Settings=R.Settings; New.Mesh=R.Mesh; New.Slot=R.MaterialSlotIndex; Groups.Add(New); Group=&Groups.Last(); }
        Group->Rows.Add(I);
    }
    // Reserve before releasing/building so failure never produces a partially enabled actor.
    TSet<int32> Used;
    for(TObjectIterator<UPrimitiveComponent> It; It; ++It)
        if(It->GetWorld()==GetWorld() && It->bRenderCustomDepth && !Helpers.ContainsByPredicate([&](UMeshComponent* H){return H==*It;})) Used.Add(It->CustomDepthStencilValue);
    for(auto& G:Groups) if(G.Settings.Method==ENPROutlineMethod::ScreenSpace)
    {
        for(int32 ID=1; ID<256; ++ID) if(!Used.Contains(ID)) { G.Stencil=ID; Used.Add(ID); break; }
        if(!G.Stencil) { LastError=TEXT("CustomStencil IDs exhausted; actor outline was disabled."); return Fail(); }
    }
    ReleaseHelpers();
    if(Groups.IsEmpty()) { BuiltRegions=Regions; BuiltAssets=Assets; BuiltScope=Scope; LastError.Reset(); return true; }
    auto* Invisible=UMaterialInstanceDynamic::Create(MaskTemplate,this);
    if(Invisible) Invisible->SetScalarParameterValue(TEXT("Visible"),0);
    for(const auto& G:Groups)
    {
        auto* MID=UMaterialInstanceDynamic::Create(G.Settings.Method==ENPROutlineMethod::InvertedHull?HullTemplate:MaskTemplate,this);
        SetParameters(MID,G.Settings); MID->SetScalarParameterValue(TEXT("Visible"),1);
        TArray<UMeshComponent*> GroupSources;
        for(int32 Row:G.Rows) GroupSources.AddUnique(Regions[Row].Mesh);
        for(auto* Source:GroupSources)
        {
            UMeshComponent* Helper=nullptr;
            const EObjectFlags Flags=RF_Transient|RF_DuplicateTransient|RF_TextExportTransient;
            if(auto* Skel=Cast<USkeletalMeshComponent>(Source))
            {
                auto* Copy=NewObject<USkeletalMeshComponent>(GetOwner(),NAME_None,Flags);
                Copy->SetupAttachment(Source); Copy->SetSkeletalMeshAsset(Skel->GetSkeletalMeshAsset());
                Copy->SetLeaderPoseComponent(Skel); Copy->bSyncAttachParentLOD=true;
                Copy->AddTickPrerequisiteComponent(Skel); Helper=Copy;
            }
            else if(auto* Static=Cast<UStaticMeshComponent>(Source))
            {
                auto* Copy=NewObject<UStaticMeshComponent>(GetOwner(),NAME_None,Flags);
                Copy->SetupAttachment(Source); Copy->SetStaticMesh(Static->GetStaticMesh()); Helper=Copy;
            }
            check(Helper);
            Helper->SetCollisionEnabled(ECollisionEnabled::NoCollision); Helper->SetCastShadow(false);
            Helper->SetReceivesDecals(false); Helper->SetBoundsScale(1.2f);
            for(int32 Slot=0; Slot<Source->GetNumMaterials(); ++Slot)
            {
                const bool Selected=G.Rows.ContainsByPredicate([&](int32 R){return Regions[R].Mesh==Source && Regions[R].MaterialSlotIndex==Slot;});
                Helper->SetMaterial(Slot,Selected?MID:Invisible);
            }
            if(G.Stencil)
            {
                Helper->SetRenderInMainPass(false); Helper->SetRenderInDepthPass(false);
                Helper->SetRenderCustomDepth(true); Helper->SetCustomDepthStencilValue(G.Stencil);
            }
            Helper->RegisterComponent(); Helpers.Add(Helper); Sources.Add(Source);
        }
        if(G.Stencil)
        {
            auto* ScreenMID=UMaterialInstanceDynamic::Create(ScreenTemplate,this);
            SetParameters(ScreenMID,G.Settings); ScreenMID->SetScalarParameterValue(TEXT("StencilId"),G.Stencil);
            auto* PP=NewObject<UPostProcessComponent>(GetOwner(),NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);
            PP->SetupAttachment(GetOwner()->GetRootComponent()); PP->bUnbound=true; PP->Priority=100;
            PP->Settings.AddBlendable(ScreenMID,1); PP->RegisterComponent(); PostProcesses.Add(PP);
        }
    }
    BuiltRegions=Regions; BuiltAssets=Assets; BuiltScope=Scope; SyncHelpers(); LastError.Reset(); return true;
}

void UNPROutlineActorComponent::SyncHelpers()
{
    for(int32 I=0; I<Helpers.Num(); ++I)
    {
        auto* H=Helpers[I].Get(); auto* S=Sources[I].Get(); if(!IsValid(H)||!IsValid(S)) continue;
        H->SetVisibility(S->IsVisible()); H->SetHiddenInGame(S->bHiddenInGame);
        if(auto* Skel=Cast<USkeletalMeshComponent>(S))
        {
            auto* Copy=CastChecked<USkeletalMeshComponent>(H);
            Copy->SetForcedLOD(Skel->GetForcedLOD());
            for(int32 L=0; L<Skel->GetSkeletalMeshAsset()->GetLODNum(); ++L)
                for(int32 Slot=0; Slot<Skel->GetNumMaterials(); ++Slot)
                    if(Copy->IsMaterialSectionShown(Slot,L)!=Skel->IsMaterialSectionShown(Slot,L))
                        Copy->ShowMaterialSection(Slot,INDEX_NONE,Skel->IsMaterialSectionShown(Slot,L),L);
        }
        if(auto* Static=Cast<UStaticMeshComponent>(S)) CastChecked<UStaticMeshComponent>(H)->SetForcedLodModel(Static->ForcedLodModel);
    }
}

TArray<UMeshComponent*> UNPROutlineActorComponent::GetOutlineMeshes() const
{ TArray<UMeshComponent*> Result; for(UMeshComponent* H:Helpers) Result.Add(H); return Result; }
void UNPROutlineActorComponent::OnRegister() { Super::OnRegister(); RefreshOutline(); }
void UNPROutlineActorComponent::OnUnregister() { ReleaseHelpers(); Super::OnUnregister(); }
void UNPROutlineActorComponent::TickComponent(float DeltaTime,ELevelTick Type,FActorComponentTickFunction* Function)
{ Super::TickComponent(DeltaTime,Type,Function); RefreshOutline(); }
bool UNPROutlineActorComponent::ApplyPreset(UNPROutlinePreset* Value,FString& Error)
{
    if(!IsValid(Value)||Value->SchemaVersion!=1) { Error=TEXT("A v1 Outline preset is required."); return false; }
    if(!Value->Settings.Validate(Error)) return false;
    const auto Old=BaseSettings; auto* OldPreset=Preset.Get(); BaseSettings=Value->Settings; Preset=Value;
    if(RefreshOutline()) { Error.Reset(); return true; }
    Error=LastError; BaseSettings=Old; Preset=OldPreset; RefreshOutline(); return false;
}
#if WITH_EDITOR
void UNPROutlineActorComponent::PostEditChangeProperty(FPropertyChangedEvent& Event)
{ Super::PostEditChangeProperty(Event); if(IsRegistered()) RefreshOutline(); }
void UNPROutlineActorComponent::PostEditUndo() { Super::PostEditUndo(); if(IsRegistered()) RefreshOutline(); }
#endif
