#include "NPRToonEditorUI.h"
#include "NPRToonEditorOptions.h"
#include "NPRToonBackend.h"
#include "NPRCharacterPreview.h"
#include "NPRStyleProfile.h"
#include "NPRStyleBindingComponent.h"
#include "NPRBackendStylePackage.h"
#include "NPROutlineActorComponent.h"
#include "NPRModulationEditorLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/MeshComponent.h"
#include "Engine/ToonProfile.h"
#include "Engine/Selection.h"
#include "Editor.h"
#include "IDetailsView.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "PropertyEditorModule.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Rendering/DrawElements.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Text/STextBlock.h"

namespace NPRToonUI
{
const FLinearColor Accent(.05f,.45f,.95f),Amber(1.f,.55f,.12f);
const TCHAR* Ids[]={NPRToonContract::CenterId,NPRToonContract::HalfWidthId,NPRToonContract::SpecAngleId,NPRToonContract::SpecSoftnessId,NPRToonContract::SpecPeakId};
double& Value(FNPRToonParameters& P,int32 Index)
{
    switch(Index){case 0:return P.TerminatorDotCenter;case 1:return P.TerminatorDotHalfWidth;case 2:return P.SpecHalfAngleRad;case 3:return P.SpecEdgeSoftness;default:return P.SpecPeakScale;}
}
DECLARE_DELEGATE_TwoParams(FOnCurveEdit,int32,double);
class SCurve final:public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCurve){} SLATE_ATTRIBUTE(FNPRToonParameters,Parameters) SLATE_ATTRIBUTE(ENPRToonResponseShape,Shape)
        SLATE_EVENT(FOnCurveEdit,OnEdit) SLATE_EVENT(FSimpleDelegate,OnBeginEdit) SLATE_EVENT(FSimpleDelegate,OnEndEdit) SLATE_END_ARGS()
    void Construct(const FArguments& A)
    {Parameters=A._Parameters;Shape=A._Shape;Edit=A._OnEdit;Begin=A._OnBeginEdit;End=A._OnEndEdit;
        SetToolTipText(FText::FromString(TEXT("拖动调整明暗中心；按住 Shift 拖动调整半宽。")));}
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(280,155);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle&,bool)const override
    {
        const FVector2f Size(G.GetLocalSize()),Origin(30,8),Span(FMath::Max(1.f,Size.X-42),FMath::Max(1.f,Size.Y-38));
        FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(),FAppStyle::GetBrush("WhiteBrush"),ESlateDrawEffect::None,FLinearColor(.025f,.033f,.044f));
        for(int32 I=0;I<=4;++I)
        {
            const float T=I/4.f;
            FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),{FVector2f(Origin.X+T*Span.X,Origin.Y),FVector2f(Origin.X+T*Span.X,Origin.Y+Span.Y)},ESlateDrawEffect::None,FLinearColor(.12f,.15f,.18f));
            FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),{FVector2f(Origin.X,Origin.Y+T*Span.Y),FVector2f(Origin.X+Span.X,Origin.Y+T*Span.Y)},ESlateDrawEffect::None,FLinearColor(.12f,.15f,.18f));
        }
        const auto P=Parameters.Get();TArray<FVector2f> Points;
        for(int32 I=0;I<=256;++I){const float X=I/256.f;Points.Add(Origin+FVector2f(X*Span.X,float(1-P.EvaluateDiffuse(2*X-1,Shape.Get()))*Span.Y));}
        FSlateDrawElement::MakeLines(Out,Layer+2,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,Accent,true,2.f);
        const float Center=float((P.TerminatorDotCenter+1)*.5)*Span.X+Origin.X;
        for(double Q:{P.TerminatorDotCenter-P.TerminatorDotHalfWidth,P.TerminatorDotCenter+P.TerminatorDotHalfWidth})
            if(Q>=-1&&Q<=1){const float X=Origin.X+float((Q+1)*.5)*Span.X;FSlateDrawElement::MakeLines(Out,Layer+2,G.ToPaintGeometry(),{FVector2f(X,Origin.Y),FVector2f(X,Origin.Y+Span.Y)},ESlateDrawEffect::None,FLinearColor(.18f,.3f,.43f));}
        FSlateDrawElement::MakeLines(Out,Layer+3,G.ToPaintGeometry(),{FVector2f(Center,Origin.Y),FVector2f(Center,Origin.Y+Span.Y)},ESlateDrawEffect::None,Amber,true,1.f);
        const auto Font=FCoreStyle::GetDefaultFontStyle("Regular",9);
        auto Text=[&](FVector2f Pos,const FString& S){FSlateDrawElement::MakeText(Out,Layer+4,G.ToPaintGeometry(FVector2f(100,16),FSlateLayoutTransform(Pos)),S,Font,ESlateDrawEffect::None,FLinearColor(.75f,.8f,.85f));};
        Text(FVector2f(Origin.X-6,Origin.Y+Span.Y+3),TEXT("-1"));Text(FVector2f(Origin.X+Span.X*.5f-3,Origin.Y+Span.Y+3),TEXT("0"));
        Text(FVector2f(Origin.X+Span.X-3,Origin.Y+Span.Y+3),TEXT("1"));Text(FVector2f(2,Origin.Y),TEXT("1.0"));Text(FVector2f(2,Origin.Y+Span.Y-8),TEXT("0.0"));
        return Layer+4;
    }
    virtual FReply OnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)override
    {if(E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();WidthDrag=E.IsShiftDown();Begin.ExecuteIfBound();Change(G,E);return FReply::Handled().CaptureMouse(SharedThis(this));}
    virtual FReply OnMouseMove(const FGeometry& G,const FPointerEvent& E)override
    {if(!HasMouseCapture())return FReply::Unhandled();Change(G,E);return FReply::Handled();}
    virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent& E)override
    {if(!HasMouseCapture()||E.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();End.ExecuteIfBound();return FReply::Handled().ReleaseMouseCapture();}
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&)override{End.ExecuteIfBound();}
private:
    void Change(const FGeometry& G,const FPointerEvent& E)
    {const double Q=FMath::Clamp((G.AbsoluteToLocal(E.GetScreenSpacePosition()).X-30)/FMath::Max(1.0,G.GetLocalSize().X-42)*2-1,-1.0,1.0);
        Edit.ExecuteIfBound(WidthDrag?1:0,WidthDrag?FMath::Min(.5,FMath::Abs(Q-Parameters.Get().TerminatorDotCenter)):Q);}
    TAttribute<FNPRToonParameters> Parameters;TAttribute<ENPRToonResponseShape> Shape;FOnCurveEdit Edit;FSimpleDelegate Begin,End;bool WidthDrag=false;
};
}

struct SNPRToonEditor::FImpl
{
    TWeakPtr<SNPRToonEditor> Owner;
    struct FCurveData{FNPRToonParameters Values;ENPRToonResponseShape Shape=ENPRToonResponseShape::Hermite;};
    TSharedRef<FCurveData> CurveData=MakeShared<FCurveData>();
    struct FGenerated{FNPRToonParameters Values;FNPRToonResponseSettings Response;};
    TMap<TWeakObjectPtr<UToonProfile>,FGenerated> Generated;
    TWeakObjectPtr<AActor> Actor;
    TStrongObjectPtr<UNPRToonEditorOptions> Options;
    TMap<TWeakObjectPtr<UMeshComponent>,TMap<int32,TStrongObjectPtr<UNPRToonEditorOptions>>> Drafts;
    TSharedPtr<FNPRCharacterPreview> Preview;
    TSharedPtr<SVerticalBox> Targets;
    TSharedPtr<NPRToonUI::SCurve> MainCurve;
    TSharedPtr<IDetailsView> NativeDetails;
    TUniquePtr<FScopedTransaction> Transaction;
    FDelegateHandle Undo;
    FString Status=TEXT("选择关卡角色，再在模型或区域列表中指定材质槽。");
    bool Pending=false,Rebuild=false,Error=false;
    FString AssetRoot=TEXT("/Game/NPR/Generated/Toon");
    FString SemanticRoot=TEXT("/Game/NPR/Styles");
    double NextUpdate=0;

    UNPRStyleBindingComponent* Binding(UMeshComponent* Mesh,int32 Slot)const
    {
        if(!IsValid(Mesh)||!Mesh->GetOwner())return nullptr;
        for(auto* B:TInlineComponentArray<UNPRStyleBindingComponent*>(Mesh->GetOwner()))if(B->GetOwnedMaterial(Mesh,Slot))return B;
        for(auto* B:TInlineComponentArray<UNPRStyleBindingComponent*>(Mesh->GetOwner()))
            if((B->RimMesh==Mesh&&B->RimSlotIndex==Slot)||(B->ToonMesh==Mesh&&B->ToonPackage&&
                B->ToonPackage->MaterialSlots.ContainsByPredicate([Slot](const auto& S){return S.SlotIndex==Slot;})))return B;
        return nullptr;
    }
    void BeginEdit()
    {if(!Transaction){Transaction=MakeUnique<FScopedTransaction>(FText::FromString(TEXT("调整 Toon 参数")));if(Options.IsValid())Options->Modify();}}
    void EndEdit(){Transaction.Reset();}
    void Schedule(bool Build=false){Pending=true;Rebuild|=Build;}
    bool Resolve()
    {
        if(!Options.IsValid())return false;
        FNPRToonParameters P;
        if(Options->StyleProfile&&!Options->StyleProfile->ResolveToonParameters(ENPRMaterialKind::Generic,P,Status)){Error=true;return false;}
        for(int32 I=0;I<5;++I)if(auto* Entry=Options->LocalPatch.Values.Find(NPRToonUI::Ids[I]);Entry&&Entry->bIsSet)NPRToonUI::Value(P,I)=Entry->Value;
        if(!P.Validate(Status)||!Options->Response.Validate(Status)){Error=true;return false;}
        Options->Parameters=P;CurveData->Values=P;CurveData->Shape=Options->Response.Shape;return true;
    }
    void SetScalar(int32 Index,double DisplayValue)
    {
        if(!Options.IsValid())return;BeginEdit();
        auto& Entry=Options->LocalPatch.Values.FindOrAdd(NPRToonUI::Ids[Index]);Entry.bIsSet=true;
        Entry.Value=Index==2?FMath::DegreesToRadians(DisplayValue):DisplayValue;
        if(Resolve())Schedule();
    }
    void Reset(int32 Index)
    {
        if(!Options.IsValid())return;BeginEdit();Options->LocalPatch.Values.Remove(NPRToonUI::Ids[Index]);Resolve();EndEdit();Schedule();
    }
    void SetProfile(UNPRStyleProfile* Profile)
    {
        if(!Options.IsValid())return;BeginEdit();Options->StyleProfile=Profile;
        if(Profile){Options->Response=Profile->ToonResponse;Options->ShadingModel=Profile->ShadingModel;}
        Resolve();EndEdit();Schedule(true);
    }
    void SetPreset(UNPRBackendStylePackage* Preset)
    {
        if(!Options.IsValid())return;
        if(!IsValid(Preset)){BeginEdit();Options->BackendPreset=nullptr;EndEdit();return;}
        if(!Preset->bHasToonParameters||Preset->MaterialSlots.IsEmpty()){Status=TEXT("所选资源不是有效的 Toon 后端预设。");Error=true;return;}
        if(!IsValid(Preset->MaterialSlots[0].Material)){Status=TEXT("后端预设缺少已保存的材质。");Error=true;return;}
        BeginEdit();Options->BackendPreset=Preset;Options->StyleProfile=Preset->SemanticSource.LoadSynchronous();
        Options->ShadingModel=Preset->ShadingModel;Options->Response=Preset->ToonResponse;
        Options->LocalPatch.Values.Reset();
        for(int32 I=0;I<5;++I){auto& E=Options->LocalPatch.Values.FindOrAdd(NPRToonUI::Ids[I]);E.bIsSet=true;E.Value=NPRToonUI::Value(Preset->ToonParameters,I);}
        Options->PreviewMaterial=nullptr;Options->PrivateProfile=nullptr;Resolve();EndEdit();Schedule(true);
    }
    void SyncSource()
    {
        if(!Options.IsValid()||!IsValid(Options->Mesh))return;
        auto* Current=Options->Mesh->GetMaterial(Options->SlotIndex);if(!IsValid(Current))return;
        if(!Options->PreviewMaterial){Options->SourceMaterial=Current;return;}
        if(!Options->SourceMaterial||Options->SourceMaterial->GetMaterial()!=Current->GetMaterial())
        {Options->SourceMaterial=Current;Options->PreviewMaterial=nullptr;Schedule();return;}
        // Other pages may update the same MID. Refresh uniforms without rebuilding its graph.
        Options->PreviewMaterial->CopyMaterialUniformParametersEditorOnly(Current,false);
        if(Options->Response.bUseArtisticSpecular&&Cast<UMaterialExpressionSubstrateToonBSDF>(Options->PreviewMaterial->GetMaterial()->GetEditorOnlyData()->FrontMaterial.Expression))
            Options->PreviewMaterial->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("NPR_ToonReferenceRoughness")),NPRToonBackend::SpecularReferenceRoughness);
        Options->SourceMaterial=Current;Options->PreviewSource=Current;
    }
    void ReadSelection(bool Force=false)
    {
        AActor* Selected=GEditor?Cast<AActor>(GEditor->GetSelectedActors()->GetSelectedObject(0)):nullptr;
        if(Selected==Actor.Get()&&!Force)return;
        TWeakObjectPtr<UMeshComponent> PreviousMesh=Options.IsValid()?Options->Mesh.Get():nullptr;
        const int32 PreviousSlot=Options.IsValid()?Options->SlotIndex:INDEX_NONE;
        EndEdit();Actor=Selected;Options.Reset();Preview->LoadActor(Selected);
        if(IsValid(Selected))
        {
            for(const auto& Entry:Drafts)if(Entry.Key.IsValid()&&Entry.Key->GetOwner()==Selected)
                for(const auto& Slot:Entry.Value)if(Slot.Value.IsValid()&&Slot.Value->PreviewMaterial)Preview->SetMaterialOverride(Entry.Key.Get(),Slot.Key,Slot.Value->PreviewMaterial);
            const auto Rows=UNPRModulationEditorLibrary::GetMeshEffects(Selected);
            if(PreviousMesh.IsValid()&&PreviousMesh->GetOwner()==Selected&&PreviousSlot>=0&&PreviousSlot<PreviousMesh->GetNumMaterials())Select(PreviousMesh.Get(),PreviousSlot);
            else if(!Rows.IsEmpty())Select(Rows[0].Mesh,Rows[0].SlotIndex);
        }
        BuildTargets();
    }
    void Select(UMeshComponent* Mesh,int32 Slot)
    {
        if(!IsValid(Mesh)||Mesh->GetOwner()!=Actor.Get()||Slot<0||Slot>=Mesh->GetNumMaterials())return;
        EndEdit();auto& Draft=Drafts.FindOrAdd(Mesh).FindOrAdd(Slot);
        if(!Draft.IsValid())
        {
            Draft.Reset(NewObject<UNPRToonEditorOptions>(GetTransientPackage(),NAME_None,RF_Transactional));
            Draft->Mesh=Mesh;Draft->SlotIndex=Slot;Draft->SourceMaterial=Mesh->GetMaterial(Slot);
            if(auto* B=Binding(Mesh,Slot);B&&B->ToonPackage)
            {
                const auto* P=B->ToonPackage.Get();Draft->Parameters=P->ToonParameters;Draft->Response=P->ToonResponse;Draft->ShadingModel=P->ShadingModel;
                for(int32 I=0;I<5;++I){auto& E=Draft->LocalPatch.Values.FindOrAdd(NPRToonUI::Ids[I]);E.bIsSet=true;E.Value=NPRToonUI::Value(Draft->Parameters,I);}
            }
        }
        Options.Reset(Draft.Get());Resolve();Preview->SelectRegion(Mesh,Slot);
        NativeDetails->SetObject(Options->PrivateProfile);BuildTargets();
        Status=FString::Printf(TEXT("当前：%s · 材质槽 %d。预览只影响独立副本；应用后写入角色绑定。"),*Mesh->GetName(),Slot);Error=false;
    }
    bool Update()
    {
        Pending=false;if(!Options.IsValid()||!Resolve())return false;
        if(!Options->PreviewMaterial||Options->PreviewSource!=Options->SourceMaterial||Options->PreviewModel!=Options->ShadingModel||
            Options->bPreviewArtisticSpecular!=Options->Response.bUseArtisticSpecular)
        {
            UToonProfile* P=nullptr;
            UToonProfile* Template=Options->PrivateProfile;
            if(!Template&&Options->BackendPreset&&!Options->BackendPreset->MaterialSlots.IsEmpty())
                Template=Options->BackendPreset->MaterialSlots[0].Material->GetToonProfileOverride_Internal();
            auto* Material=NPRToonBackend::BuildPreview(Options->SourceMaterial,Options->ShadingModel,Options->Parameters,Options->Response,P,Status,Template);
            Rebuild=false;if(!Material){Error=true;return false;}
            Options->PrivateProfile=P;Options->PreviewMaterial=Material;NativeDetails->SetObject(P);
            Options->PreviewSource=Options->SourceMaterial;Options->PreviewModel=Options->ShadingModel;
            Options->bPreviewArtisticSpecular=Options->Response.bUseArtisticSpecular;
        }
        else
        {
            auto Changed=Options->Response;
            if(const auto* Last=Generated.Find(Options->PrivateProfile))
            {
                const auto& A=Last->Values;const auto& B=Options->Parameters;
                Changed.bUseArtisticDiffuse&=A.TerminatorDotCenter!=B.TerminatorDotCenter||A.TerminatorDotHalfWidth!=B.TerminatorDotHalfWidth||
                    Last->Response.Shape!=Options->Response.Shape||!Last->Response.DarkResponse.Equals(Options->Response.DarkResponse)||!Last->Response.LightResponse.Equals(Options->Response.LightResponse)||!Last->Response.bUseArtisticDiffuse;
                Changed.bUseArtisticSpecular&=A.SpecHalfAngleRad!=B.SpecHalfAngleRad||A.SpecEdgeSoftness!=B.SpecEdgeSoftness||A.SpecPeakScale!=B.SpecPeakScale||!Last->Response.bUseArtisticSpecular;
            }
            const bool ShadowChanged=Options->PrivateProfile->Settings.bDiffuseRampIncludeShadow!=(Options->Response.ShadowMode==ENPRToonShadowMode::InsideRamp);
            if((Changed.bUseArtisticDiffuse||Changed.bUseArtisticSpecular||ShadowChanged)&&
                !NPRToonBackend::UpdateProfile(Options->PrivateProfile,Options->Parameters,Changed,Status)){Error=true;return false;}
        }
        Generated.Add(Options->PrivateProfile,FGenerated{Options->Parameters,Options->Response});
        Preview->SetMaterialOverride(Options->Mesh,Options->SlotIndex,Options->PreviewMaterial);
        Status=TEXT("私有预览已更新。拖动使用 Profile 刷新；首次选择材质家族需准备 Shader。高光为参考近似。");Error=false;return true;
    }
    bool Apply()
    {
        if(!Options.IsValid()||!IsValid(Options->Mesh)||Options->Mesh->GetOwner()!=Actor.Get()){Status=TEXT("请先选择有效角色与材质槽。");Error=true;return false;}
        if((Pending||!Options->PreviewMaterial)&&!Update())return false;
        UNPRStyleBindingComponent* B=Binding(Options->Mesh,Options->SlotIndex);
        if(B&&B->GetBoundSlotCount()>1)
        {Status=TEXT("当前组件同时管理多个材质槽；请通过完整后端包更新，避免覆盖其他槽。");Error=true;return false;}
        auto* Bundle=NPRToonBackend::Bake(Options->PreviewMaterial,Options->PrivateProfile,Options->ShadingModel,
            Options->Parameters,Options->Response,Options->SlotIndex,AssetRoot,Status);
        if(!Bundle){Error=true;return false;}
        const FScopedTransaction Tx(FText::FromString(TEXT("应用角色 Toon 风格")));Actor->Modify();Options->Mesh->Modify();
        if(!B){B=NewObject<UNPRStyleBindingComponent>(Actor.Get(),NAME_None,RF_Transactional);Actor->AddInstanceComponent(B);B->RegisterComponent();}
        B->Modify();B->ToonMesh=Options->Mesh;B->ToonPackage=Bundle;
        if(!B->ApplyConfiguredToon(Status)){Error=true;return false;}
        Actor->MarkPackageDirty();
        Preview->SetMaterialOverride(Options->Mesh,Options->SlotIndex,nullptr);Preview->Refresh();BuildTargets();
        Status=TEXT("已应用并保存不可变 Toon 材质包。请保存关卡以持久化角色绑定；其他系统的外部材质修改会被保留。");Error=false;return true;
    }
    bool SaveStyle()
    {
        if(!Options.IsValid()||!Resolve())return false;
        if((Pending||!Options->PreviewMaterial)&&!Update())return false;
        FString Package,Name;FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(SemanticRoot/TEXT("Style_Toon"),TEXT(""),Package,Name);
        const bool ExistingStyle=Options->StyleProfile!=nullptr;
        auto* P=ExistingStyle?DuplicateObject<UNPRStyleProfile>(Options->StyleProfile,CreatePackage(*Package),*Name):
            NewObject<UNPRStyleProfile>(CreatePackage(*Package),*Name,RF_Public|RF_Standalone);
        P->ClearFlags(RF_Transient);P->SetFlags(RF_Public|RF_Standalone);
        P->StyleId=FName(*Name);P->ShadingModel=Options->ShadingModel;P->ToonResponse=Options->Response;
        FNPRParameterPatch& SavedPatch=ExistingStyle?P->MaterialOverrides.FindOrAdd(ENPRMaterialKind::Generic):P->Defaults;
        for(int32 I=0;I<5;++I){auto& E=SavedPatch.Values.FindOrAdd(NPRToonUI::Ids[I]);E.bIsSet=true;E.Value=NPRToonUI::Value(Options->Parameters,I);}
        const FString File=FPackageName::LongPackageNameToFilename(Package,FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
        if(!UPackage::SavePackage(P->GetOutermost(),P,*File,Args)){Status=TEXT("保存风格失败。");Error=true;return false;}
        FAssetRegistryModule::AssetCreated(P);
        auto* Preset=NPRToonBackend::Bake(Options->PreviewMaterial,Options->PrivateProfile,Options->ShadingModel,
            Options->Parameters,Options->Response,0,AssetRoot/TEXT("Presets"),Status);
        if(!Preset){Error=true;return false;}
        Preset->SemanticStyleId=P->StyleId;Preset->SemanticSource=P;
        const FString PresetFile=FPackageName::LongPackageNameToFilename(Preset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
        if(!UPackage::SavePackage(Preset->GetOutermost(),Preset,*PresetFile,Args)){Status=TEXT("保存后端预设失败。");Error=true;return false;}
        BeginEdit();Options->StyleProfile=P;Options->BackendPreset=Preset;Options->LocalPatch.Values.Reset();EndEdit();
        Status=TEXT("已保存统一艺术风格及包含全部高级 Profile 设置的后端预设：")+Preset->GetPathName();Error=false;return true;
    }
    void BuildTargets()
    {
        if(!Targets)return;Targets->ClearChildren();if(!Actor.IsValid())return;
        TSet<UMeshComponent*> Seen;
        for(const auto& Row:UNPRModulationEditorLibrary::GetMeshEffects(Actor.Get()))
        {
            auto* Mesh=Row.Mesh.Get();if(Seen.Contains(Mesh))continue;Seen.Add(Mesh);
            Targets->AddSlot().AutoHeight().Padding(0,8)[SNew(STextBlock).Text(FText::FromString(Mesh->GetName())).AutoWrapText(true)
                .WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping).ToolTipText(FText::FromString(Mesh->GetPathName())).Font(FCoreStyle::GetDefaultFontStyle("Bold",12))];
            TArray<FNPROutlineSectionInfo> Mapping;FString MappingError;UNPROutlineActorComponent::GetSectionMapping(Mesh,Mapping,MappingError);
            if(Mapping.IsEmpty())
            {
                for(int32 I=0;I<Mesh->GetNumMaterials();++I)AddTarget(Mesh,I,FString::Printf(TEXT("材质槽 %d"),I));
            }
            else for(const auto& M:Mapping)AddTarget(Mesh,M.MaterialSlotIndex,FString::Printf(TEXT("LOD %d / Section %d → 槽 %d\n%s"),M.LODIndex,M.SectionIndex,M.MaterialSlotIndex,*M.MaterialSlotName.ToString()));
        }
    }
    void AddTarget(UMeshComponent* Mesh,int32 Slot,const FString& Text)
    {
        TWeakObjectPtr<UMeshComponent> Weak=Mesh;
        Targets->AddSlot().AutoHeight().Padding(0,2)[SNew(SButton).ContentPadding(FMargin(8,6)).ToolTipText(FText::FromString(Text))
            .ButtonColorAndOpacity_Lambda([this,Weak,Slot]{return Options.IsValid()&&Options->Mesh==Weak.Get()&&Options->SlotIndex==Slot?NPRToonUI::Amber:FLinearColor(.1f,.13f,.16f);})
            .OnClicked_Lambda([this,Weak,Slot]{Select(Weak.Get(),Slot);return FReply::Handled();})
            [SNew(STextBlock).Text(FText::FromString(Text)).AutoWrapText(true).WrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping)]];
    }
    TSharedRef<SWidget> Scalar(const TCHAR* Label,int32 Index,double Min,double Max,double SliderMax)
    {
        return SNew(SHorizontalBox)
            +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Label))]
            +SHorizontalBox::Slot().FillWidth(1)[SNew(SSpinBox<double>).MinValue(Min).MaxValue(Max).MinSliderValue(Min).MaxSliderValue(SliderMax)
                .Value_Lambda([this,Index]{if(!Options.IsValid())return 0.0;const double V=NPRToonUI::Value(Options->Parameters,Index);return Index==2?FMath::RadiansToDegrees(V):V;})
                .OnBeginSliderMovement_Lambda([this]{BeginEdit();})
                .OnValueChanged_Lambda([this,Index](double V){SetScalar(Index,V);})
                .OnValueCommitted_Lambda([this](double,ETextCommit::Type){EndEdit();})
                .OnEndSliderMovement_Lambda([this](double){EndEdit();})]
            +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SButton).Text(FText::FromString(TEXT("↺"))).ToolTipText(FText::FromString(TEXT("删除本层覆写，恢复继承值")))
                .OnClicked_Lambda([this,Index]{Reset(Index);return FReply::Handled();})];
    }
    TSharedRef<SWidget> Choice(TFunction<FString()> Label,const TArray<FString>& Items,TFunction<void(int32)> Set)
    {
        return SNew(SComboButton).ButtonContent()[SNew(STextBlock).Text_Lambda([Label]{return FText::FromString(Label());})]
            .OnGetMenuContent_Lambda([this,Items,Set]
            {
                auto Box=SNew(SVerticalBox);
                for(int32 I=0;I<Items.Num();++I)Box->AddSlot().AutoHeight()[SNew(SButton).Text(FText::FromString(Items[I])).OnClicked_Lambda([this,I,Set]
                    {if(Options.IsValid()){BeginEdit();Set(I);EndEdit();}FSlateApplication::Get().DismissAllMenus();return FReply::Handled();})];
                return Box;
            });
    }
    TSharedRef<SWidget> Color(const TCHAR* Label,bool Dark)
    {
        return SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(FText::FromString(Label))]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SColorBlock).Size(FVector2D(90,20)).Color_Lambda([this,Dark]{return Options.IsValid()?(Dark?Options->Response.DarkResponse:Options->Response.LightResponse):FLinearColor::Black;})
                .OnMouseButtonDown_Lambda([this,Dark](const FGeometry&,const FPointerEvent&)
                {
                    if(!Options.IsValid())return FReply::Handled();FColorPickerArgs Args;
                    Args.InitialColor=Dark?Options->Response.DarkResponse:Options->Response.LightResponse;
                    Args.bUseAlpha=false;Args.bOnlyRefreshOnMouseUp=false;
                    TWeakObjectPtr<UNPRToonEditorOptions> Editing=Options.Get();
                    Args.OnColorCommitted=FOnLinearColorValueChanged::CreateLambda([Weak=Owner,Dark,Editing](FLinearColor C)
                    {
                        auto W=Weak.Pin();if(!W||!Editing.IsValid()||W->Impl->Options.Get()!=Editing.Get())return;
                        auto& UI=*W->Impl;UI.BeginEdit();(Dark?UI.Options->Response.DarkResponse:UI.Options->Response.LightResponse)=C;UI.Schedule();
                    });
                    Args.OnColorPickerWindowClosed=FOnWindowClosed::CreateLambda([Weak=Owner](const TSharedRef<SWindow>&)
                    {if(auto W=Weak.Pin())W->Impl->EndEdit();});
                    OpenColorPicker(Args);return FReply::Handled();
                })];
    }
    TSharedRef<SWidget> Curve(bool Main=false)
    {
        auto Widget=SNew(NPRToonUI::SCurve).Parameters_Lambda([State=CurveData]{return State->Values;})
        .Shape_Lambda([State=CurveData]{return State->Shape;})
        .OnBeginEdit_Lambda([Weak=Owner]{if(auto W=Weak.Pin())W->Impl->BeginEdit();})
        .OnEdit_Lambda([Weak=Owner](int32 I,double V){if(auto W=Weak.Pin())W->Impl->SetScalar(I,V);})
        .OnEndEdit_Lambda([Weak=Owner]{if(auto W=Weak.Pin())W->Impl->EndEdit();});
        if(Main)MainCurve=Widget;return Widget;
    }
};

SNPRToonEditor::SNPRToonEditor()=default;
SNPRToonEditor::~SNPRToonEditor()
{if(Impl){Impl->EndEdit();FEditorDelegates::PostUndoRedo.Remove(Impl->Undo);Impl->Preview->SetRegionPicked({});}}

void SNPRToonEditor::Construct(const FArguments&)
{
    Impl=MakeUnique<FImpl>();Impl->Owner=SharedThis(this);Impl->Preview=MakeNPRCharacterPreview();
    FDetailsViewArgs Args;Args.bAllowSearch=true;Args.NameAreaSettings=FDetailsViewArgs::HideNameArea;
    Impl->NativeDetails=FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(Args);
    Impl->NativeDetails->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&)
    {
        if(Impl->Options.IsValid()&&Impl->Options->PrivateProfile)
            Impl->Options->Response.ShadowMode=Impl->Options->PrivateProfile->Settings.bDiffuseRampIncludeShadow?
                ENPRToonShadowMode::InsideRamp:ENPRToonShadowMode::OutsideRamp;
    });
    Impl->Undo=FEditorDelegates::PostUndoRedo.AddLambda([this]{if(Impl->Options.IsValid()){Impl->Resolve();Impl->Schedule(true);Impl->BuildTargets();}});
    Impl->Preview->SetRegionPicked([this](UMeshComponent* Mesh,int32 Material,int32 Section)
    {
        if(Material<0&&Mesh&&Mesh->GetNumMaterials()==1)Material=0;
        if(Material<0){Impl->Status=TEXT("该命中未包含材质槽信息，请在右侧 Section 列表选择。");return;}
        Impl->Select(Mesh,Material);
    });
    auto Title=[](const TCHAR* Label){return SNew(STextBlock).Text(FText::FromString(Label)).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Bold",13));};
    ChildSlot[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(10,8)[SNew(SHorizontalBox)
            +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Title(TEXT("Toon Light · 角色与区域"))]
            +SHorizontalBox::Slot().FillWidth(1).Padding(20,0)[Impl->Choice([this]{return Impl->Options.IsValid()?StaticEnum<ENPRShadingModel>()->GetDisplayNameTextByValue(int64(Impl->Options->ShadingModel)).ToString():FString(TEXT("着色模型"));},
                {TEXT("继承原模型"),TEXT("Default Lit"),TEXT("Substrate Toon")},[this](int32 I){Impl->Options->ShadingModel=ENPRShadingModel(I);Impl->Schedule(true);})]
            +SHorizontalBox::Slot().AutoWidth().Padding(5,0)[SNew(SButton).Text(FText::FromString(TEXT("应用到所选槽"))).OnClicked_Lambda([this]{Impl->Apply();return FReply::Handled();})]
            +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("保存风格"))).OnClicked_Lambda([this]{Impl->SaveStyle();return FReply::Handled();})]]
        +SVerticalBox::Slot().AutoHeight().Padding(10,2)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]{return FText::FromString(Impl->Status);})
            .ColorAndOpacity_Lambda([this]{return Impl->Error?FSlateColor(FLinearColor(.95f,.3f,.25f)):FSlateColor::UseForeground();})]
        +SVerticalBox::Slot().FillHeight(1)[SNew(SSplitter)
            +SSplitter::Slot().Value(.26f)[SNew(SBorder).Padding(10)[SNew(SScrollBox)+SScrollBox::Slot()[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().Padding(0,5)[SNew(SButton).Text(FText::FromString(TEXT("使用关卡选中角色"))).OnClicked_Lambda([this]{Impl->ReadSelection(true);Impl->SyncSource();return FReply::Handled();})]
                +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(SObjectPropertyEntryBox).AllowedClass(UNPRStyleProfile::StaticClass())
                    .ObjectPath_Lambda([this]{return Impl->Options.IsValid()&&Impl->Options->StyleProfile?Impl->Options->StyleProfile->GetPathName():FString();})
                    .OnObjectChanged_Lambda([this](const FAssetData& Data){Impl->SetProfile(Cast<UNPRStyleProfile>(Data.GetAsset()));})]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(STextBlock).Text(FText::FromString(TEXT("后端预设（含原始高级设置）")))]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(SObjectPropertyEntryBox).AllowedClass(UNPRBackendStylePackage::StaticClass())
                    .ObjectPath_Lambda([this]{return Impl->Options.IsValid()&&Impl->Options->BackendPreset?Impl->Options->BackendPreset->GetPathName():FString();})
                    .OnObjectChanged_Lambda([this](const FAssetData& Data){Impl->SetPreset(Cast<UNPRBackendStylePackage>(Data.GetAsset()));})]
                +SVerticalBox::Slot().AutoHeight().Padding(0,8)[Title(TEXT("明暗分界"))]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Impl->Scalar(TEXT("明暗中心"),0,-1,1,.5)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Impl->Scalar(TEXT("过渡半宽"),1,0,.5,.25)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Impl->Choice([this]{return Impl->Options.IsValid()&&Impl->Options->Response.Shape==ENPRToonResponseShape::Linear?FString(TEXT("过渡形状：线性")):FString(TEXT("过渡形状：Hermite"));},
                    {TEXT("线性"),TEXT("平滑 Hermite")},[this](int32 I){Impl->Options->Response.Shape=ENPRToonResponseShape(I);Impl->Schedule();})]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SNew(SHorizontalBox)+SHorizontalBox::Slot().FillWidth(1)[Title(TEXT("明暗响应 · N·L"))]
                    +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("放大编辑 ↗"))).OnClicked_Lambda([this]
                        {auto W=SNew(SWindow).Title(FText::FromString(TEXT("Toon 明暗响应曲线"))).ClientSize(FVector2D(650,360))[Impl->Curve()];FSlateApplication::Get().AddWindow(W);return FReply::Handled();})]]
                +SVerticalBox::Slot().AutoHeight()[SNew(SBox).HeightOverride(155)[Impl->Curve(true)]]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Impl->Color(TEXT("暗部响应"),true)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Impl->Color(TEXT("亮部响应"),false)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Impl->Choice([this]{return Impl->Options.IsValid()&&Impl->Options->Response.ShadowMode==ENPRToonShadowMode::InsideRamp?FString(TEXT("阴影：进入色阶坐标")):FString(TEXT("阴影：色阶外相乘"));},
                    {TEXT("色阶外相乘"),TEXT("进入色阶坐标")},[this](int32 I){Impl->Options->Response.ShadowMode=ENPRToonShadowMode(I);Impl->Schedule();})]
                +SVerticalBox::Slot().AutoHeight().Padding(0,10)[Title(TEXT("高光 · 原生后端近似"))]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Impl->Scalar(TEXT("半峰角（度）"),2,1,60,25)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Impl->Scalar(TEXT("边缘柔度"),3,0,1,1)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Impl->Scalar(TEXT("峰值倍率"),4,0,TNumericLimits<float>::Max(),2)]
                +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(SButton).Text(FText::FromString(TEXT("创建 / 刷新私有预览"))).OnClicked_Lambda([this]{Impl->Schedule();Impl->Update();return FReply::Handled();})]
                +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(SExpandableArea).InitiallyCollapsed(true)
                    .HeaderContent()[Title(TEXT("后端高级 · 保留原始 Profile 控制"))]
                    .BodyContent()[SNew(SVerticalBox)
                        +SVerticalBox::Slot().AutoHeight()[SNew(SCheckBox).IsChecked_Lambda([this]{return Impl->Options.IsValid()&&Impl->Options->Response.bUseArtisticDiffuse?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                            .OnCheckStateChanged_Lambda([this](ECheckBoxState S){if(Impl->Options.IsValid()){Impl->BeginEdit();Impl->Options->Response.bUseArtisticDiffuse=S==ECheckBoxState::Checked;Impl->EndEdit();Impl->Schedule();}})
                            [SNew(STextBlock).Text(FText::FromString(TEXT("艺术参数生成漫反射（取消后直接编辑曲线）")))]]
                        +SVerticalBox::Slot().AutoHeight()[SNew(SCheckBox).IsChecked_Lambda([this]{return Impl->Options.IsValid()&&Impl->Options->Response.bUseArtisticSpecular?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                            .OnCheckStateChanged_Lambda([this](ECheckBoxState S){if(Impl->Options.IsValid()){Impl->BeginEdit();Impl->Options->Response.bUseArtisticSpecular=S==ECheckBoxState::Checked;Impl->EndEdit();Impl->Schedule(true);}})
                            [SNew(STextBlock).Text(FText::FromString(TEXT("艺术参数生成高光（近似）")))]]
                        +SVerticalBox::Slot().AutoHeight()[Impl->NativeDetails.ToSharedRef()]]]
            ]]]
            +SSplitter::Slot().Value(.57f)[Impl->Preview->Widget()]
            +SSplitter::Slot().Value(.17f)[SNew(SBorder).Padding(10)[SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight()[Title(TEXT("区域选择 / Submesh"))]
                +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("在预览模型上点选，或选择下方 Section。应用以材质槽为单位；共用槽的区域同步变化。")))]
                +SVerticalBox::Slot().AutoHeight()[SNew(SButton).Text(FText::FromString(TEXT("隔离当前槽"))).OnClicked_Lambda([this]{if(Impl->Options.IsValid())Impl->Preview->Isolate(Impl->Options->Mesh,Impl->Options->SlotIndex);return FReply::Handled();})]
                +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[SAssignNew(Impl->Targets,SVerticalBox)]]
            ]]
        ]
    ];
    Impl->ReadSelection();
}
void SNPRToonEditor::Tick(const FGeometry& G,double Time,float Delta)
{
    SCompoundWidget::Tick(G,Time,Delta);if(!Impl)return;
    if(Impl->Pending&&Time>=Impl->NextUpdate){Impl->Update();Impl->NextUpdate=Time+1.0/30.0;}
    Impl->Preview->Refresh();
}
void SNPRToonEditor::SyncSelection(){if(Impl){Impl->ReadSelection();Impl->SyncSource();}}
void SNPRToonEditor::EditTarget(UMeshComponent* Mesh,int32 Slot)
{if(Impl&&IsValid(Mesh)){if(Impl->Actor.Get()!=Mesh->GetOwner()){Impl->Actor=Mesh->GetOwner();Impl->Preview->LoadActor(Mesh->GetOwner());}Impl->Select(Mesh,Slot);}}
TSharedPtr<FNPRCharacterPreview> SNPRToonEditor::GetPreview()const{return Impl?Impl->Preview:nullptr;}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PreviewScene.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Level.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Layout/WidgetPath.h"
#include "RenderingThread.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRToonEditorWorkflowTest,"NPR.Toon.EditorWorkflow",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FNPRToonEditorWorkflowTest::RunTest(const FString&)
{
    FPreviewScene Scene(FPreviewScene::ConstructionValues().SetTransactional(true));
    FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient|RF_Transactional;
    AActor* Actor=Scene.GetWorld()->SpawnActor<AActor>(Spawn);
    auto* Mesh=NewObject<UStaticMeshComponent>(Actor,NAME_None,RF_Transactional);Actor->SetRootComponent(Mesh);Actor->AddInstanceComponent(Mesh);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));Mesh->RegisterComponent();
    auto* Source=LoadObject<UMaterialInterface>(nullptr,TEXT("/PrincipledNPR/Surface/M_NPRSurface_Toon.M_NPRSurface_Toon"));Mesh->SetMaterial(0,Source);
    auto* Other=NewObject<UStaticMeshComponent>(Actor,NAME_None,RF_Transactional);Actor->AddInstanceComponent(Other);
    Other->SetStaticMesh(Mesh->GetStaticMesh());Other->SetMaterial(0,Source);Other->SetWorldLocation(FVector(150,0,0));Other->RegisterComponent();
    TArray<TWeakObjectPtr<AActor>> Previous;
    for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)if(auto* A=Cast<AActor>(*It))Previous.Add(A);
    GEditor->SelectNone(false,true,false);GEditor->SelectActor(Actor,true,false,true);
    TSharedPtr<SNPRToonEditor> Widget=SNew(SNPRToonEditor);
    ON_SCOPE_EXIT{Widget.Reset();GEditor->SelectNone(false,true,false);for(auto A:Previous)if(A.IsValid())GEditor->SelectActor(A.Get(),true,false,true);};
    auto& UI=*Widget->Impl;UI.AssetRoot=TEXT("/Game/NPR/ToonValidation/UI");UI.SemanticRoot=UI.AssetRoot/TEXT("Styles");UI.Select(Mesh,0);
    UI.SetScalar(0,.2);UI.EndEdit();UI.SetScalar(1,.08);UI.EndEdit();
    if(!TestTrue(*FString(TEXT("Preview update: ")+UI.Status),UI.Update()))return false;
    TestTrue(TEXT("Preview does not replace scene material"),Mesh->GetMaterial(0)==Source&&Other->GetMaterial(0)==Source);
    TestTrue(TEXT("Actual preview copy uses generated private material"),UI.Preview->PreviewMaterial(false,Mesh,0)==UI.Options->PreviewMaterial);
    TestTrue(TEXT("Original comparison retains original surface"),UI.Preview->PreviewMaterial(true,Mesh,0)==Source);
    auto* Material=UI.Options->PreviewMaterial.Get();auto* Profile=UI.Options->PrivateProfile.Get();
    UI.SetScalar(0,.3);UI.EndEdit();TestTrue(TEXT("Hot edit succeeds"),UI.Update());
    TestTrue(TEXT("Hot edit keeps compiled material and private profile identity"),UI.Options->PreviewMaterial==Material&&UI.Options->PrivateProfile==Profile);
    TestTrue(TEXT("Undo succeeds"),GEditor->UndoTransaction());UI.Update();
    TestTrue(TEXT("Undo restores previous unsaved parameter"),FMath::IsNearlyEqual(UI.Options->Parameters.TerminatorDotCenter,.2));
    TestTrue(TEXT("Undo does not recreate compiled preview material"),UI.Options->PreviewMaterial==Material);
    UI.SetScalar(0,0);UI.EndEdit();TestEqual(TEXT("Explicit zero remains set"),UI.Options->Parameters.TerminatorDotCenter,0.0);
    TestTrue(TEXT("Explicit zero patch retained"),UI.Options->LocalPatch.Values[NPRToonContract::CenterId].bIsSet);
    UI.Reset(0);TestFalse(TEXT("Reset deletes sparse override"),UI.Options->LocalPatch.Values.Contains(NPRToonContract::CenterId));
    UI.SetScalar(0,.25);UI.EndEdit();UI.Select(Other,0);UI.SetScalar(0,.6);UI.EndEdit();UI.Update();
    UI.Select(Mesh,0);TestEqual(TEXT("Changing target preserves slot-local draft"),UI.Options->Parameters.TerminatorDotCenter,.25);
    auto* Combined=NewObject<UNPRStyleProfile>();
    auto& ExistingRim=Combined->Defaults.Values.FindOrAdd(NPRRimContract::IntensityId);ExistingRim.bIsSet=true;ExistingRim.Value=.75;
    auto& ExistingCenter=Combined->MaterialOverrides.FindOrAdd(ENPRMaterialKind::Generic).Values.FindOrAdd(NPRToonContract::CenterId);ExistingCenter.bIsSet=true;ExistingCenter.Value=.1;
    UI.SetProfile(Combined);UI.Update();UI.Options->PrivateProfile->Settings.DiffuseIndirectScale=.37f;
    if(!TestTrue(TEXT("Save includes canonical style and native backend settings"),UI.SaveStyle()))return false;
    FNPRRimParameters SavedRim;FString SavedError;
    TestTrue(TEXT("Saving Toon preserves the selected unified style's Rim data"),UI.Options->StyleProfile->ResolveRimParameters(ENPRMaterialKind::Generic,SavedRim,SavedError)&&SavedRim.RimPeakScale==.75f);
    FNPRToonParameters SavedToon;
    TestTrue(TEXT("Saved Generic values override the inherited Generic patch correctly"),UI.Options->StyleProfile->ResolveToonParameters(ENPRMaterialKind::Generic,SavedToon,SavedError)&&SavedToon.TerminatorDotCenter==.25);
    TestEqual(TEXT("Saving a new style does not mutate the original selected style"),Combined->MaterialOverrides[ENPRMaterialKind::Generic].Values[NPRToonContract::CenterId].Value,.1);
    auto* SavedPreset=UI.Options->BackendPreset.Get();
    UI.Select(Other,0);UI.SetPreset(SavedPreset);
    TestTrue(TEXT("Saved preset can build a private preview on another target"),UI.Update());
    TestEqual(TEXT("Preset restores artistic center"),UI.Options->Parameters.TerminatorDotCenter,.25);
    TestEqual(TEXT("Preset restores advanced native controls"),UI.Options->PrivateProfile->Settings.DiffuseIndirectScale,.37f);
    TestTrue(TEXT("Preset reuse still creates independent profiles"),UI.Options->PrivateProfile!=SavedPreset->MaterialSlots[0].Material->GetToonProfileOverride_Internal());
    UI.Select(Mesh,0);
    TestTrue(TEXT("Isolation uses preview copies"),UI.Preview->Isolate(Mesh,0));
    TestFalse(TEXT("Other preview mesh hidden"),UI.Preview->IsMeshVisible(false,Other));
    TestTrue(TEXT("Source meshes retain visibility"),Mesh->IsVisible()&&Other->IsVisible());UI.Preview->Isolate(nullptr,INDEX_NONE);
    if(!TestTrue(*FString(TEXT("Apply persists generated bundle: ")+UI.Status),UI.Apply()))return false;
    auto* B=UI.Binding(Mesh,0);TestTrue(TEXT("Actor stores runtime configuration"),B&&B->ToonPackage&&B->ToonMesh==Mesh);
    TestTrue(TEXT("Applied bundle owns the actual scene MID"),B&&B->GetOwnedMaterial(Mesh,0)==Mesh->GetMaterial(0));
    TestTrue(TEXT("Other slot is unchanged by apply"),Other->GetMaterial(0)==Source);
    FNPRRimParameters Rim;Rim.RimPeakScale=1;Rim.RimColor=FLinearColor::Red;FString RimError;
    TestTrue(TEXT("Independent Rim can still update the shared Toon MID"),B->SetRimParameters(Rim,RimError));
    Widget->SyncSelection();UI.Update();FLinearColor Refreshed;
    UI.Options->PreviewMaterial->GetVectorParameterValue(FHashedMaterialParameterInfo(FName(NPRRimContract::ColorMaterialName)),Refreshed);
    TestTrue(TEXT("Returning to Toon refreshes the other page's current Rim parameters"),Refreshed.Equals(FLinearColor::Red));
    const auto Rows=UNPRModulationEditorLibrary::GetMeshEffects(Actor);
    TestTrue(TEXT("Overview discovers registered Toon effect"),Rows.ContainsByPredicate([Mesh](const auto& R){return R.Mesh==Mesh&&R.Effects.Contains(TEXT("Toon"));}));
    auto* External=UMaterial::GetDefaultMaterial(MD_Surface);Mesh->SetMaterial(0,External);B->RestoreMaterials();
    TestTrue(TEXT("Release keeps external scene replacement"),Mesh->GetMaterial(0)==External);
    return true;
}

namespace NPRToonWidgetValidation
{
struct FState
{
    FPreviewScene Scene{FPreviewScene::ConstructionValues().SetTransactional(true)};
    TStrongObjectPtr<UWorld> SourceWorld;
    TSharedPtr<SNPRToonEditor> Widget;
    TSharedPtr<SWindow> Window;
    TArray<TWeakObjectPtr<AActor>> PreviousSelection;
    TFunction<void(int32)> Stage;
    FString Directory;
    int32 Frame=0;
    void Close()
    {
        Window->RequestDestroyWindow();GEditor->SelectNone(false,true,false);
        for(auto Actor:PreviousSelection)if(Actor.IsValid())GEditor->SelectActor(Actor.Get(),true,false,true);
        Stage=nullptr;
    }
};
class FFrames:public IAutomationLatentCommand
{
    TSharedPtr<FState> State;
public:
    explicit FFrames(TSharedPtr<FState> S):State(S){}
    bool Update()override
    {
        ++State->Frame;
        if(State->Frame==25)State->Stage(0);
        if(State->Frame==40)State->Stage(1);
        if(State->Frame==55)State->Stage(2);
        if(State->Frame==70){State->Stage(3);State->Close();State.Reset();return true;}
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRToonWidgetRenderTest,"NPR.Toon.WidgetRender",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FNPRToonWidgetRenderTest::RunTest(const FString&)
{
    using namespace NPRToonWidgetValidation;
    auto State=MakeShared<FState>();
    State->SourceWorld.Reset(LoadObject<UWorld>(nullptr,TEXT("/Game/NPR/Maps/L_Outline_MultiScope_v1.L_Outline_MultiScope_v1")));
    if(!TestNotNull(TEXT("Existing character map loads read-only"),State->SourceWorld.Get()))return false;
    USkeletalMeshComponent* Source=nullptr;
    for(auto Actor:State->SourceWorld->PersistentLevel->Actors)if(IsValid(Actor)&&Actor->GetActorLabel()==TEXT("Outline_TexturedCharacter"))
        Source=Actor->FindComponentByClass<USkeletalMeshComponent>();
    if(!TestNotNull(TEXT("Existing character mesh located"),Source))return false;
    FActorSpawnParameters Spawn;Spawn.ObjectFlags=RF_Transient|RF_Transactional;
    auto* Host=State->Scene.GetWorld()->SpawnActor<ASkeletalMeshActor>(Spawn);auto* Mesh=Host->GetSkeletalMeshComponent();
    // Actor default subobjects inherit RF_Transient in a preview world. Inventory
    // deliberately excludes those helper components, so expose only this fixture.
    Mesh->ClearFlags(RF_Transient);
    Mesh->SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset());Mesh->SetWorldRotation(Source->GetComponentRotation());
    Mesh->SetWorldScale3D(Source->GetComponentScale());
    for(int32 I=0;I<Source->GetNumMaterials();++I)Mesh->SetMaterial(I,Source->GetMaterial(I));
    // Include the real actor's outline configuration. Its attached mask/hull
    // helpers can receive hit proxies before the visible source surface.
    if(auto* OriginalOutline=Source->GetOwner()->FindComponentByClass<UNPROutlineActorComponent>())
    {
        auto* Outline=NewObject<UNPROutlineActorComponent>(Host);
        Outline->bEnabled=OriginalOutline->bEnabled;Outline->Scope=OriginalOutline->Scope;
        Outline->BaseSettings=OriginalOutline->BaseSettings;Outline->Meshes=OriginalOutline->Meshes;
        Outline->HullTemplate=OriginalOutline->HullTemplate;Outline->MaskTemplate=OriginalOutline->MaskTemplate;Outline->ScreenTemplate=OriginalOutline->ScreenTemplate;
        for(auto& Rule:Outline->Meshes)if(Rule.Mesh==Source){Rule.Mesh=Mesh;Rule.SourceName=Mesh->GetFName();}
        Host->AddInstanceComponent(Outline);Outline->RegisterComponent();
        TestTrue(TEXT("Native input fixture includes real outline render helpers"),Outline->RefreshOutline()&&!Outline->GetOutlineMeshes().IsEmpty());
    }
    else AddError(TEXT("Native input fixture requires the existing outline configuration"));
    for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)if(auto* A=Cast<AActor>(*It))State->PreviousSelection.Add(A);
    GEditor->SelectNone(false,true,false);GEditor->SelectActor(Host,true,false,true);
    State->Widget=SNew(SNPRToonEditor);State->Widget->EditTarget(Mesh,0);
    State->Window=SNew(SWindow).Title(FText::FromString(TEXT("Toon Light · 原生窗口验证"))).ClientSize(FVector2D(1440,900))[State->Widget.ToSharedRef()];
    FSlateApplication::Get().AddWindow(State->Window.ToSharedRef());
    State->Directory=FPaths::ProjectSavedDir()/TEXT("ToonValidation/Widget");IFileManager::Get().MakeDirectory(*State->Directory,true);
    TWeakPtr<FState> Weak=State;
    State->Stage=[this,Weak,Mesh](int32 Stage)
    {
        auto S=Weak.Pin();if(!S)return;auto& UI=*S->Widget->Impl;
        auto Capture=[&](const TCHAR* Name)
        {
            FlushRenderingCommands();TArray<FColor> Pixels;FIntVector Size;
            if(TestTrue(TEXT("Native Slate window exports actual pixels"),FSlateApplication::Get().TakeScreenshot(S->Window.ToSharedRef(),Pixels,Size)))
            {TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);TestTrue(TEXT("Actual UI export saved"),FFileHelper::SaveArrayToFile(PNG,*(S->Directory/(FString(Name)+TEXT(".png")))));}
        };
        if(Stage==0)
        {
            TestTrue(TEXT("Native window has a real three-dimensional preview"),UI.Preview->MeshCount()>0);
            TestTrue(TEXT("Compact curve remains a sidebar card"),UI.MainCurve->GetCachedGeometry().GetLocalSize().Y<=160);
            TestTrue(TEXT("Full-size UI has usable inspector width"),UI.MainCurve->GetCachedGeometry().GetLocalSize().X>250);
            TSet<int32> Picked;
            for(float Y:{.15f,.25f,.4f,.55f,.7f,.85f})for(float X:{.35f,.5f,.65f})
                if(UI.Preview->PickRegionAtNormalized(X,Y)&&UI.Options.IsValid())Picked.Add(UI.Options->SlotIndex);
            TestTrue(TEXT("Real viewport hit proxies select multiple character material regions"),Picked.Num()>=2);
            FString Picks;for(int32 I:Picked)Picks+=FString::FromInt(I)+TEXT(" ");
            FFileHelper::SaveStringToFile(Picks,*(S->Directory/TEXT("PickedSlots.txt")));
            UI.Select(Mesh,0);UI.SetScalar(0,.2);UI.EndEdit();UI.SetScalar(1,.08);UI.EndEdit();
            TestTrue(*FString(TEXT("Visible character preview builds: ")+UI.Status),UI.Update());
        }
        if(Stage==1)
        {
            Capture(TEXT("Wide"));
            const FGeometry Geometry=UI.MainCurve->GetCachedGeometry();FWidgetPath Path;
            const bool Found=FSlateApplication::Get().GeneratePathToWidgetUnchecked(UI.MainCurve.ToSharedRef(),Path);
            TestTrue(TEXT("Curve is present in the actual Slate hit-test path"),Found);
            if(Found&&Path.IsValid()&&UI.Options.IsValid())
            {
                // GeneratePathToWidgetUnchecked fills geometry only. Event routing
                // also indexes one optional virtual pointer entry per widget.
                TArray<FWidgetAndPointer> RoutedWidgets;
                for(int32 I=0;I<Path.Widgets.Num();++I)RoutedWidgets.Emplace(Path.Widgets[I]);
                Path=FWidgetPath(MakeArrayView(RoutedWidgets));
                auto Pos=[&](double Q){return Geometry.LocalToAbsolute(FVector2D(30+(Q+1)*.5*(Geometry.GetLocalSize().X-42),Geometry.GetLocalSize().Y*.5));};
                const auto A=Pos(.2),B=Pos(.45);TSet<FKey> Down{EKeys::LeftMouseButton},Up;
                // Seed the Slate user's previous hit path before the first drag.
                // Real platform mouse input supplies this preceding move event.
                FSlateApplication::Get().RoutePointerMoveEvent(Path,FPointerEvent(0,A,A,Up,FKey(),0,FModifierKeysState()),false);
                FSlateApplication::Get().RoutePointerDownEvent(Path,FPointerEvent(0,A,A,Down,EKeys::LeftMouseButton,0,FModifierKeysState()));
                FSlateApplication::Get().RoutePointerMoveEvent(Path,FPointerEvent(0,B,A,Down,FKey(),0,FModifierKeysState()),false);
                FSlateApplication::Get().RoutePointerUpEvent(Path,FPointerEvent(0,B,B,Up,EKeys::LeftMouseButton,0,FModifierKeysState()));
                TestTrue(TEXT("Native curve drag changes the real parameter to 0.45"),FMath::IsNearlyEqual(UI.Options->Parameters.TerminatorDotCenter,.45,1.e-4));
            }
        }
        if(Stage==2){Capture(TEXT("CurveDragged"));S->Window->Resize(FVector2D(1024,800));UI.Preview->Focus();}
        if(Stage==3){Capture(TEXT("Narrow"));TestTrue(TEXT("Narrow window keeps the compact curve usable"),UI.MainCurve->GetCachedGeometry().GetLocalSize().X>150);}
    };
    ADD_LATENT_AUTOMATION_COMMAND(FFrames(State));return true;
}
#endif
