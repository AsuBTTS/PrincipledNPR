#include "NPRModulationEditorLibrary.h"
#include "NPROutlineEditorUI.h"
#include "NPRCharacterPreview.h"
#include "NPRStyleBindingComponent.h"
#include "NPRBackendStylePackage.h"
#include "NPRRimParameters.h"
#include "NPRSurfaceRecipe.h"
#include "NPROutlineActorComponent.h"
#include "NPROutlineComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Actor.h"

void UNPRModulationEditorLibrary::OpenWorkspace() { FNPROutlineEditorUI::OpenWorkspace(); }
bool UNPRModulationEditorLibrary::SelectFeature(FName Feature) { return FNPROutlineEditorUI::SelectFeature(Feature); }
bool UNPRModulationEditorLibrary::EditEffect(UMeshComponent* Mesh,int32 SlotIndex,FName Effect)
{return FNPROutlineEditorUI::EditEffect(Mesh,SlotIndex,Effect);}
bool UNPRModulationEditorLibrary::IsolatePreview(UMeshComponent* Mesh,int32 SlotIndex)
{ auto View=FNPROutlineEditorUI::CurrentPreview(); return View.IsValid() && View->Isolate(Mesh,SlotIndex); }
bool UNPRModulationEditorLibrary::ExportPreview(const FString& Directory)
{ auto View=FNPROutlineEditorUI::CurrentPreview(); return View.IsValid() && View->Export(Directory); }
int32 UNPRModulationEditorLibrary::GetPreviewMeshCount()
{ auto View=FNPROutlineEditorUI::CurrentPreview(); return View.IsValid()?View->MeshCount():0; }

TArray<FNPRMeshEffectInfo> UNPRModulationEditorLibrary::GetMeshEffects(AActor* Actor)
{
    TArray<FNPRMeshEffectInfo> Rows;
    if(!IsValid(Actor)) return Rows;
    TArray<FNPROutlineResolvedRegion> Regions;
    for(auto* Group:TInlineComponentArray<UNPROutlineActorComponent*>(Actor))
        if(Group->bEnabled) { TArray<FNPROutlineResolvedRegion> Part; FString Error; if(Group->ResolveRegions(Part,Error)) Regions.Append(Part); }
    for(auto* Mesh:TInlineComponentArray<UMeshComponent*>(Actor))
    {
        const bool HasAsset=(Cast<USkeletalMeshComponent>(Mesh) && CastChecked<USkeletalMeshComponent>(Mesh)->GetSkeletalMeshAsset()) ||
            (Cast<UStaticMeshComponent>(Mesh) && CastChecked<UStaticMeshComponent>(Mesh)->GetStaticMesh());
        if(Mesh->HasAnyFlags(RF_Transient) || !HasAsset || Mesh->IsA<UInstancedStaticMeshComponent>()) continue;
        const auto Names=Mesh->GetMaterialSlotNames();
        for(int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
        {
            auto& Row=Rows.AddDefaulted_GetRef(); Row.Mesh=Mesh; Row.SlotIndex=Slot;
            Row.SlotName=Names.IsValidIndex(Slot)?Names[Slot]:NAME_None;
            auto Add=[&](FName Effect,FName Category,const FString& Description)
            { Row.Effects.AddUnique(Effect); Row.Categories.AddUnique(Category); if(!Row.Description.IsEmpty())Row.Description+=TEXT(" · "); Row.Description+=Description; };
            for(auto* Binding:TInlineComponentArray<UNPRStyleBindingComponent*>(Actor))
                if(auto* MID=Binding->GetOwnedMaterial(Mesh,Slot))
                {
                    if(Binding->ToonPackage&&Binding->ToonPackage->bHasToonParameters)
                        Add(TEXT("Toon"),TEXT("NonPostProcess"),TEXT("Toon Light / 不可变后端风格包"));
                    float Intensity=0.f;
                    FLinearColor Color;
                    if(MID->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(NPRRimContract::IntensityMaterialName)),Intensity) &&
                        MID->GetVectorParameterValue(FHashedMaterialParameterInfo(FName(NPRRimContract::ColorMaterialName)),Color))
                        Add(TEXT("RimLight"),TEXT("NonPostProcess"),FString::Printf(TEXT("Rim Light（强度 %.3g）"),Intensity));
                    if(MID->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("NPR_BaseColor")),Color))
                        Add(TEXT("Surface"),TEXT("NonPostProcess"),IsValid(Binding->RimRecipe)?TEXT("Surface / ")+Binding->RimRecipe->RecipeId.ToString():FString(TEXT("Surface / 预览中的表面输入")));
                    if(Row.Effects.IsEmpty())Row.Description=TEXT("存在插件材质绑定，未声明可隔离效果");
                }
            auto AddOutline=[&](ENPROutlineMethod Method)
            {
                if(Method==ENPROutlineMethod::ScreenSpace) Add(TEXT("ScreenOutline"),TEXT("PostProcess"),TEXT("屏幕空间描边 / 后处理"));
                if(Method==ENPROutlineMethod::InvertedHull) Add(TEXT("HullOutline"),TEXT("NonPostProcess"),TEXT("反向壳描边 / 几何"));
            };
            for(const auto& R:Regions) if(R.Mesh==Mesh && R.MaterialSlotIndex==Slot && R.bEnabled) AddOutline(R.Settings.Method);
            for(auto* Old:TInlineComponentArray<UNPROutlineComponent*>(Actor))
                if(Old->TargetMesh==Mesh && Old->MaterialSlotIndex==Slot) AddOutline(Old->Settings.Method);
            if(Row.Description.IsEmpty()) Row.Description=TEXT("无本插件登记的效果；保留当前材质");
        }
    }
    return Rows;
}
