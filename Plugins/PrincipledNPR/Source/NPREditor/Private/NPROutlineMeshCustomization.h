#pragma once

#include "IPropertyTypeCustomization.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "IPropertyUtilities.h"
#include "NPROutlineActorComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/AppStyle.h"

struct FNPROutlineSlotUIState
{
    TMap<TWeakObjectPtr<UMeshComponent>, FName> ExpandedSlots;
    TSet<TWeakObjectPtr<UMeshComponent>> ExpandedComponents;
    TSet<TWeakObjectPtr<UMeshComponent>> ExpandedSections;
};

// This is an editor presentation of the existing slot-name contract. No asset schema changes.
class FNPROutlineMeshCustomization final : public IPropertyTypeCustomization
{
public:
    FNPROutlineMeshCustomization(TSharedRef<FNPROutlineSlotUIState> InState, TFunction<void(UMeshComponent*,int32,bool)> InIsolate, TFunction<bool(UMeshComponent*,int32)> InIsIsolated)
        : State(InState), Isolate(MoveTemp(InIsolate)), IsIsolated(MoveTemp(InIsIsolated)) {}
    virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InHandle, FDetailWidgetRow& Row, IPropertyTypeCustomizationUtils&) override
    {
        Handle=InHandle;
        Row.NameContent()[InHandle->CreatePropertyNameWidget()]
            .ValueContent()[SNew(STextBlock).Text(FText::FromString(TEXT("组件与材质槽")))];
    }
    FNPROutlineMeshRule* Rule() const
    {
        TArray<void*> Raw; Handle->AccessRawData(Raw);
        return Raw.Num()==1?static_cast<FNPROutlineMeshRule*>(Raw[0]):nullptr;
    }
    void Edit(TFunction<void(FNPROutlineMeshRule&)> Change)
    {
        if(!Rule()) return;
        const FScopedTransaction Transaction(NSLOCTEXT("NPR","SlotRule","Edit Outline material slot"));
        TArray<UObject*> Owners; Handle->GetOuterObjects(Owners);
        for(auto* Owner:Owners) if(Owner) Owner->Modify();
        Handle->NotifyPreChange(); Change(*Rule());
        Handle->NotifyPostChange(EPropertyChangeType::ValueSet);
        Handle->NotifyFinishedChangingProperties();
        if(Utilities) Utilities->RequestForceRefresh();
    }
    static FNPROutlineMaterialRule& FindOrAdd(FNPROutlineMeshRule& R,FName Name)
    {
        if(auto* Found=R.Materials.FindByPredicate([Name](const auto& M){return M.MaterialSlotName==Name;})) return *Found;
        FNPROutlineMaterialRule M; M.MaterialSlotName=Name;
        return R.Materials.Add_GetRef(M);
    }
    virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InHandle, IDetailChildrenBuilder& Children, IPropertyTypeCustomizationUtils& Utils) override
    {
        Handle=InHandle; Utilities=Utils.GetPropertyUtilities();
        auto* R=Rule(); if(!R || !IsValid(R->Mesh)) return;
        auto* Mesh=R->Mesh.Get();
        Children.AddCustomRow(FText::FromString(TEXT("源组件"))).WholeRowContent()
            [SNew(STextBlock).Text(FText::FromString(Mesh->GetName())).ToolTipText(FText::FromString(Mesh->GetPathName()))];
        Children.AddCustomRow(FText::FromString(TEXT("高级设置"))).WholeRowContent()
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0,4,8,4)
                [SNew(SCheckBox).Style(FAppStyle::Get(),"ToggleButtonCheckbox")
                    .IsChecked(State->ExpandedComponents.Contains(Mesh)?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this,Mesh](ECheckBoxState Value){if(Value==ECheckBoxState::Checked)State->ExpandedComponents.Add(Mesh);else State->ExpandedComponents.Remove(Mesh);Utilities->RequestForceRefresh();})
                    [SNew(STextBlock).Text(FText::FromString(TEXT("组件参数")))]]
                + SHorizontalBox::Slot().AutoWidth().Padding(0,4)
                [SNew(SCheckBox).Style(FAppStyle::Get(),"ToggleButtonCheckbox")
                    .IsChecked(State->ExpandedSections.Contains(Mesh)?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda([this,Mesh](ECheckBoxState Value){if(Value==ECheckBoxState::Checked)State->ExpandedSections.Add(Mesh);else State->ExpandedSections.Remove(Mesh);Utilities->RequestForceRefresh();})
                    [SNew(STextBlock).Text(FText::FromString(TEXT("Section 高级")))]]];
        if(State->ExpandedComponents.Contains(Mesh))
            Children.AddProperty(InHandle->GetChildHandle(TEXT("Override")).ToSharedRef()).DisplayName(FText::FromString(TEXT("组件级覆写"))).ShouldAutoExpand(true);
        TArray<FNPROutlineSectionInfo> Mapping; FString Error;
        UNPROutlineActorComponent::GetSectionMapping(Mesh,Mapping,Error);
        TSet<int32> Used; for(const auto& S:Mapping) Used.Add(S.MaterialSlotIndex);
        auto Slots=Used.Array(); Slots.Sort();
        const auto Names=Mesh->GetMaterialSlotNames();
        auto MaterialsHandle=InHandle->GetChildHandle(TEXT("Materials"))->AsArray();
        int32 OpaqueCount=0;
        for(int32 Slot:Slots) if(auto* M=Mesh->GetMaterial(Slot)) if(M->GetBlendMode()==BLEND_Opaque) ++OpaqueCount;
        const FString Summary=FString::Printf(TEXT("%d 个使用中材质槽 · %d 个支持描边 · %d 个尚不支持"),Slots.Num(),OpaqueCount,Slots.Num()-OpaqueCount);
        Children.AddCustomRow(FText::FromString(TEXT("覆盖能力"))).WholeRowContent()
            [SNew(STextBlock).Text(FText::FromString(Summary+(OpaqueCount==0?TEXT("\n整个网格当前无法生成描边；仍可隔离查看各材质槽。需要为这些材质适配覆盖。不是宽度设置的问题。"):FString()))).AutoWrapText(true)];
        Children.AddCustomRow(FText::FromString(TEXT("材质槽"))).WholeRowContent()
            [SNew(STextBlock).Text(FText::FromString(TEXT("勾选描边 · 隔离仅影响预览 · 重置恢复继承"))).AutoWrapText(true)];
        for(int32 Slot:Slots)
        {
            if(!Names.IsValidIndex(Slot)) continue;
            const FName Name=Names[Slot];
            const bool Unique=Names.FilterByPredicate([Name](FName N){return N==Name;}).Num()==1;
            auto* Mat=Mesh->GetMaterial(Slot);
            const bool Opaque=Mat && Mat->GetBlendMode()==BLEND_Opaque;
            const TCHAR* Blend=Mat?(Mat->GetBlendMode()==BLEND_Opaque?TEXT("Opaque（不透明）"):Mat->GetBlendMode()==BLEND_Masked?TEXT("Masked（遮罩）"):Mat->GetBlendMode()==BLEND_Translucent?TEXT("Translucent（半透明）"):TEXT("其他混合模式")):TEXT("未指定材质");
            const FString SlotLabel=FString::Printf(TEXT("[%d]  %s"),Slot,*Name.ToString());
            const FString MaterialLabel=Mat?Mat->GetName():TEXT("未指定材质");
            const FString Capability=FString(Blend)+(Opaque?TEXT(" · 支持描边"):TEXT(" · 描边未适配"));
            Children.AddCustomRow(FText::FromString(Name.ToString())).WholeRowContent()
            [SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this,Mesh,Slot]{return IsIsolated && IsIsolated(Mesh,Slot)?FLinearColor(0.045f,0.095f,0.085f):FLinearColor(0.035f,0.038f,0.042f);})
                .Padding(FMargin(10,8))
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [SNew(STextBlock).Text(FText::FromString(SlotLabel)).ToolTipText(FText::FromString(SlotLabel))
                        .Font(FAppStyle::GetFontStyle("PropertyWindow.BoldFont")).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)
                    [SNew(STextBlock).Text(FText::FromString(MaterialLabel)).ToolTipText(FText::FromString(Mat?Mat->GetPathName():MaterialLabel))
                        .ColorAndOpacity(FSlateColor::UseSubduedForeground()).OverflowPolicy(ETextOverflowPolicy::Ellipsis)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,2,0,6)
                    [SNew(STextBlock).Text(FText::FromString(Capability)).Font(FCoreStyle::GetDefaultFontStyle("Regular",9))
                        .ColorAndOpacity(Opaque?FLinearColor(0.55f,0.70f,0.64f):FLinearColor(0.86f,0.63f,0.34f)).AutoWrapText(true)]
                    + SVerticalBox::Slot().AutoHeight()
                    [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,12,0)
                        [SNew(SCheckBox).IsEnabled(Unique)
                            .ToolTipText(FText::FromString(TEXT("本材质槽是否参与描边；Actor 总开关和更深层 Section 覆写仍优先。恢复继承可删除本槽规则。")))
                            .IsChecked_Lambda([this,Name]{auto* V=Rule(); if(!V) return ECheckBoxState::Undetermined; bool Enabled=true; FNPROutlineSettings S; V->Override.Apply(S,Enabled); if(auto* M=V->Materials.FindByPredicate([Name](const auto& X){return X.MaterialSlotName==Name;})) M->Override.Apply(S,Enabled); return Enabled?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                            .OnCheckStateChanged_Lambda([this,Name](ECheckBoxState Value){Edit([Name,Value](auto& V){auto& O=FindOrAdd(V,Name).Override; O.bOverrideEnabled=true; O.bEnabled=Value==ECheckBoxState::Checked;});})
                            [SNew(STextBlock).Text(FText::FromString(TEXT("描边")))]]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,12,0)
                        [SNew(SCheckBox)
                            .ToolTipText(FText::FromString(TEXT("勾选只预览此材质槽；取消勾选恢复全部材质。隔离不修改关卡，切到另一槽时本项会自动取消。")))
                            .IsChecked_Lambda([this,Mesh,Slot]{return IsIsolated && IsIsolated(Mesh,Slot)?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                            .OnCheckStateChanged_Lambda([this,Mesh,Slot](ECheckBoxState Value){if(Isolate) Isolate(Mesh,Slot,Value==ECheckBoxState::Checked);})
                            [SNew(STextBlock).Text_Lambda([this,Mesh,Slot]{return FText::FromString(IsIsolated && IsIsolated(Mesh,Slot)?TEXT("隔离中"):TEXT("隔离"));})]]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)
                        [SNew(SButton).ButtonStyle(FAppStyle::Get(),"PrimaryButton").IsEnabled(Unique).ContentPadding(FMargin(10,4))
                            .ButtonColorAndOpacity(State->ExpandedSlots.FindRef(Mesh)==Name?FLinearColor(0.35f,0.65f,0.58f):FLinearColor::White)
                            .ToolTipText(FText::FromString(TEXT("展开本槽的局部描边参数；再次点击收起。勾选对应覆写后，局部值才会替换继承值。")))
                            .OnClicked_Lambda([this,Name,Mesh]{if(State->ExpandedSlots.FindRef(Mesh)==Name){State->ExpandedSlots.Remove(Mesh);Utilities->RequestForceRefresh();}else{State->ExpandedSlots.Add(Mesh,Name);auto* V=Rule();if(V && V->Materials.ContainsByPredicate([Name](const auto& M){return M.MaterialSlotName==Name;})) Utilities->RequestForceRefresh();else Edit([Name](auto& R){FindOrAdd(R,Name);});}return FReply::Handled();})
                            [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",10))
                                .Text(FText::FromString(State->ExpandedSlots.FindRef(Mesh)==Name?TEXT("收起参数"):TEXT("编辑参数")))]]
                        + SHorizontalBox::Slot().FillWidth(1)
                        [SNullWidget::NullWidget]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [SNew(SButton).ButtonStyle(FAppStyle::Get(),"SimpleButton").ContentPadding(FMargin(4,2))
                            .Text(FText::FromString(TEXT("恢复继承")))
                            .ToolTipText(FText::FromString(TEXT("删除本材质槽的全部覆写，恢复使用父级参数。")))
                            .OnClicked_Lambda([this,Name,Mesh]{State->ExpandedSlots.Remove(Mesh);Edit([Name](auto& V){V.Materials.RemoveAll([Name](const auto& M){return M.MaterialSlotName==Name;});}); return FReply::Handled();})]
                    ]
                ]
            ];
            for(int32 I=0;I<R->Materials.Num();++I) if(State->ExpandedSlots.FindRef(Mesh)==Name && R->Materials[I].MaterialSlotName==Name)
                Children.AddProperty(MaterialsHandle->GetElement(I)->GetChildHandle(TEXT("Override")).ToSharedRef())
                    .DisplayName(FText::FromString(FString::Printf(TEXT("[%d] 局部参数"),Slot))).ShouldAutoExpand(true);
        }
        // Old malformed/duplicate rows remain data until the user explicitly cleans them.
        TSet<FName> Seen; int32 Invalid=0;
        for(const auto& M:R->Materials)
        {
            const int32 Slot=Mesh->GetMaterialIndex(M.MaterialSlotName);
            if(!Used.Contains(Slot) || Seen.Contains(M.MaterialSlotName) || Names.FilterByPredicate([&](FName N){return N==M.MaterialSlotName;}).Num()!=1) ++Invalid;
            else Seen.Add(M.MaterialSlotName);
        }
        if(Invalid)
            Children.AddCustomRow(FText::FromString(TEXT("无效规则"))).WholeRowContent()
            [SNew(SButton).Text(FText::FromString(FString::Printf(TEXT("移除 %d 条无效/重复规则（同名保留第一条）"),Invalid)))
                .OnClicked_Lambda([this,Used,Names,Mesh]{Edit([&](auto& V){TSet<FName> Kept; V.Materials.RemoveAll([&](const auto& M){const FName N=M.MaterialSlotName; if(!Used.Contains(Mesh->GetMaterialIndex(N)) || Kept.Contains(N) || Names.FilterByPredicate([N](FName X){return X==N;}).Num()!=1) return true; Kept.Add(N); return false;});}); return FReply::Handled();})];
        if(!Error.IsEmpty()) Children.AddCustomRow(FText::FromString(TEXT("映射错误"))).WholeRowContent()[SNew(STextBlock).Text(FText::FromString(Error)).AutoWrapText(true)];
        if(State->ExpandedSections.Contains(Mesh)) Children.AddProperty(InHandle->GetChildHandle(TEXT("Sections")).ToSharedRef()).DisplayName(FText::FromString(TEXT("Section 覆写（高级）"))).ShouldAutoExpand(false);
    }
private:
    TSharedPtr<IPropertyHandle> Handle;
    TSharedPtr<IPropertyUtilities> Utilities;
    TSharedRef<FNPROutlineSlotUIState> State;
    TFunction<void(UMeshComponent*,int32,bool)> Isolate;
    TFunction<bool(UMeshComponent*,int32)> IsIsolated;
};
