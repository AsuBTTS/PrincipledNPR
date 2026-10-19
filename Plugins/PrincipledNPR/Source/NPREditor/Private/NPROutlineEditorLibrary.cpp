#include "NPROutlineEditorLibrary.h"

#include "NPROutlineComponent.h"
#include "NPROutlineActorComponent.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunctionInterface.h"
#include "UObject/UnrealType.h"
#include "NPROutlineEditorUI.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Level.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "ScopedTransaction.h"
#include "FileHelpers.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "RenderingThread.h"

namespace
{
bool SaveAsset(UObject* Asset, FString& OutError)
{
    UPackage* Package = Asset->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    Args.SaveFlags = SAVE_NoError;
    if (!UPackage::SavePackage(Package, Asset, *Filename, Args))
    {
        OutError = TEXT("Unable to save asset: ") + Filename;
        return false;
    }
    return true;
}
}

bool UNPROutlineEditorLibrary::SampleValidationAnimation(USkeletalMeshComponent* Mesh,float TimeSeconds)
{
    if(!Mesh || !Mesh->GetOwner() || !Mesh->GetOwner()->ActorHasTag(TEXT("PrincipledNPR.MultiScope.v1")) || !FMath::IsFinite(TimeSeconds)) return false;
    Mesh->SetPosition(TimeSeconds,false);
    Mesh->TickAnimation(0.f,false);
    Mesh->RefreshBoneTransforms();
    return true;
}

USkeletalMeshComponent* UNPROutlineEditorLibrary::AddValidationSkeletalMesh(AActor* Actor,USkeletalMesh* Mesh)
{
    if(!Actor || !Mesh || !Actor->ActorHasTag(TEXT("PrincipledNPR.MultiScope.v1"))) return nullptr;
    auto* Component=NewObject<USkeletalMeshComponent>(Actor,NAME_None,RF_Transactional);
    Component->SetupAttachment(Actor->GetRootComponent()); Component->SetSkeletalMeshAsset(Mesh);
    Actor->AddInstanceComponent(Component); Component->RegisterComponent(); Actor->MarkPackageDirty();
    return Component;
}

UStaticMeshComponent* UNPROutlineEditorLibrary::AddValidationStaticMesh(AActor* Actor,UStaticMesh* Mesh,const FTransform& RelativeTransform)
{
    if(!Actor || !Mesh || !Actor->ActorHasTag(TEXT("PrincipledNPR.MultiScope.v1"))) return nullptr;
    auto* Component=NewObject<UStaticMeshComponent>(Actor,NAME_None,RF_Transactional);
    Component->SetupAttachment(Actor->GetRootComponent()); Component->SetRelativeTransform(RelativeTransform);
    Component->SetStaticMesh(Mesh); Actor->AddInstanceComponent(Component); Component->RegisterComponent();
    Actor->MarkPackageDirty(); return Component;
}

UNPROutlineActorComponent* UNPROutlineEditorLibrary::ConfigureActor(AActor* Actor, FString& OutError)
{
    OutError.Reset();
    if(!IsValid(Actor)) { OutError=TEXT("Select an actor first."); return nullptr; }
    for(auto* Existing:TInlineComponentArray<UNPROutlineActorComponent*>(Actor)) return Existing;
    for(auto* Legacy:TInlineComponentArray<UNPROutlineComponent*>(Actor))
        if(Legacy->Settings.Method!=ENPROutlineMethod::None)
        { OutError=TEXT("Disable the legacy slot outline first (Method=None), then configure the actor."); return nullptr; }
    const FScopedTransaction Transaction(NSLOCTEXT("NPR","ConfigureActorOutline","Configure actor Outline"));
    Actor->Modify();
    auto* Component=NewObject<UNPROutlineActorComponent>(Actor,NAME_None,RF_Transactional);
    Component->HullTemplate=LoadObject<UMaterialInterface>(nullptr,TEXT("/PrincipledNPR/Outline/M_OutlineHull.M_OutlineHull"));
    Component->MaskTemplate=LoadObject<UMaterialInterface>(nullptr,TEXT("/PrincipledNPR/Outline/M_OutlineMask.M_OutlineMask"));
    Component->ScreenTemplate=LoadObject<UMaterialInterface>(nullptr,TEXT("/PrincipledNPR/Outline/M_OutlineScreen.M_OutlineScreen"));
    Component->CollectOwnerMeshes();
    Actor->AddInstanceComponent(Component); Component->RegisterComponent();
    // Keep the configuration editable if source coverage needs adapting; never enable a partial group.
    Component->RefreshOutline(); OutError=Component->GetLastError();
    Actor->MarkPackageDirty(); return Component;
}

bool UNPROutlineEditorLibrary::SaveActorOutline(UNPROutlineActorComponent* Component,FString& OutError)
{
    OutError.Reset();
    if(!IsValid(Component)||!Component->GetOwner()) {OutError=TEXT("No actor Outline configuration.");return false;}
    if(!Component->RefreshOutline()) {OutError=Component->GetLastError();return false;}
    if(!FEditorFileUtils::SaveLevel(Component->GetOwner()->GetLevel())) {OutError=TEXT("Level save failed or canceled.");return false;}
    return true;
}

bool UNPROutlineEditorLibrary::SetActorOutlineEnabled(UNPROutlineActorComponent* Component,bool bEnabled,FString& OutError)
{
    OutError.Reset();
    if(!IsValid(Component) || !Component->GetOwner()) { OutError=TEXT("No actor Outline configuration selected."); return false; }
    const FScopedTransaction Transaction(NSLOCTEXT("NPR","ToggleActorOutline","Enable or disable actor Outline"));
    Component->Modify();
    const bool Previous=Component->bEnabled;
    Component->bEnabled=bEnabled;
    if(!Component->RefreshOutline())
    {
        OutError=Component->GetLastError();
        if(OutError.IsEmpty()) OutError=TEXT("Unable to refresh actor Outline.");
        Component->bEnabled=Previous; Component->RefreshOutline();
        return false;
    }
    Component->MarkPackageDirty();
    return true;
}

bool UNPROutlineEditorLibrary::ApplyActorPreset(UNPROutlineActorComponent* Component,UNPROutlinePreset* PresetAsset,FString& OutError)
{
    OutError.Reset();
    if(!IsValid(Component) || !IsValid(PresetAsset)) { OutError=TEXT("Select an actor Outline configuration and a preset first."); return false; }
    const FScopedTransaction Transaction(NSLOCTEXT("NPR","ApplyActorOutlinePreset","Apply actor Outline preset"));
    Component->Modify();
    if(!Component->ApplyPreset(PresetAsset,OutError)) return false;
    Component->MarkPackageDirty();
    return true;
}

FString UNPROutlineEditorLibrary::AuditMaterial(UMaterialInterface* Material)
{
    if(!Material) return TEXT("Missing material");
    FString Text;
    auto Dump=[&Text](UObject* Object)
    {
        Text+=TEXT("\nOBJECT ")+Object->GetPathName()+TEXT(" CLASS ")+Object->GetClass()->GetName()+TEXT("\n");
        for(TFieldIterator<FProperty> It(Object->GetClass());It;++It)
        {
            if(It->HasAnyPropertyFlags(CPF_Transient)) continue;
            FString Value; It->ExportText_InContainer(0,Value,Object,nullptr,Object,PPF_None);
            Text+=It->GetName()+TEXT("=")+Value+TEXT("\n");
        }
    };
    for(UMaterialInterface* Current=Material;Current;)
    {
        Dump(Current);
        auto* Instance=Cast<UMaterialInstance>(Current);
        Current=Instance?Instance->Parent.Get():nullptr;
    }
    UMaterial* Base=Material->GetMaterial();
    if(Base)
        for(int32 P=0;P<MP_MAX;++P)
            if(auto* Input=Base->GetExpressionInputForProperty(static_cast<EMaterialProperty>(P)))
                if(Input->Expression) Text+=FString::Printf(TEXT("ROOT_OUTPUT %d -> %s[%d]\n"),P,*Input->Expression->GetPathName(),Input->OutputIndex);
    TSet<UMaterialFunctionInterface*> Seen;
    TFunction<void(TConstArrayView<TObjectPtr<UMaterialExpression>>)> Walk;
    Walk=[&](TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions)
    {
        for(UMaterialExpression* Expression:Expressions)
        {
            Dump(Expression);
            for(FExpressionInputIterator Input(Expression);Input;++Input)
                if(Input->Expression)
                    Text+=TEXT("INPUT ")+Expression->GetPathName()+TEXT(".")+FString::FromInt(Input.Index)+TEXT(" <- ")+Input->Expression->GetPathName()+FString::Printf(TEXT("[%d]\n"),Input->OutputIndex);
            if(auto* Call=Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
                if(auto* Function=Call->MaterialFunction.Get();Function && !Seen.Contains(Function))
                { Seen.Add(Function); Dump(Function); Walk(Function->GetExpressions()); }
        }
    };
    if(Base) Walk(Base->GetExpressions());
    return Text;
}

FNPROutlineConversionResult UNPROutlineEditorLibrary::ConvertMesh(USkeletalMeshComponent* Mesh, int32 SlotIndex,
    const FNPROutlineSettings& Settings, const FString& AssetFolder)
{
    FNPROutlineConversionResult Result;
    if (!Settings.Validate(Result.Error)) return Result;
    if (!IsValid(Mesh) || !Mesh->GetOwner() || !Mesh->GetSkeletalMeshAsset() ||
        SlotIndex < 0 || SlotIndex >= Mesh->GetNumMaterials())
    {
        Result.Error = TEXT("Select a skeletal mesh actor and a valid material slot.");
        return Result;
    }
    if (!AssetFolder.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(AssetFolder))
    {
        Result.Error = TEXT("Generated assets must use a valid /Game/ folder.");
        return Result;
    }
    for (UNPROutlineComponent* Existing : TInlineComponentArray<UNPROutlineComponent*>(Mesh->GetOwner()))
    {
        if (Existing->TargetMesh == Mesh && Existing->MaterialSlotIndex == SlotIndex)
        {
            if (Mesh->GetMaterial(SlotIndex) != Existing->ConvertedMaterial)
            {
                Result.Error = TEXT("The slot was changed by another system. Restore or reselect its source explicitly.");
                return Result;
            }
            const FScopedTransaction UpdateTransaction(NSLOCTEXT("NPR", "ReuseOutline", "Update existing NPR Outline binding"));
            Existing->Modify();
            Existing->Settings = Settings;
            Result.Binding = Existing;
            Result.bSuccess = Existing->RefreshOutline();
            Result.Error = Existing->GetLastError();
            return Result;
        }
    }
    UMaterialInterface* Original = Mesh->GetMaterial(SlotIndex);
    if (!IsValid(Original) || !Original->IsAsset() || Original->IsA<UMaterialInstanceDynamic>() || Original->GetBlendMode() != BLEND_Opaque)
    {
        Result.Error = TEXT("This controlled slice supports saved Opaque surface materials; masked/translucent sources require an explicit coverage adapter.");
        return Result;
    }
    UMaterialInterface* Hull = LoadObject<UMaterialInterface>(nullptr, TEXT("/PrincipledNPR/Outline/M_OutlineHull.M_OutlineHull"));
    UMaterialInterface* Mask = LoadObject<UMaterialInterface>(nullptr, TEXT("/PrincipledNPR/Outline/M_OutlineMask.M_OutlineMask"));
    UMaterialInterface* Screen = LoadObject<UMaterialInterface>(nullptr, TEXT("/PrincipledNPR/Outline/M_OutlineScreen.M_OutlineScreen"));
    if (!Hull || !Mask || !Screen)
    {
        Result.Error = TEXT("The plugin Outline master assets are missing.");
        return Result;
    }
    FString PackageName, AssetName;
    FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(
        AssetFolder / FString::Printf(TEXT("MI_%s_Slot%02d_Outline"), *Mesh->GetSkeletalMeshAsset()->GetName(), SlotIndex),
        TEXT(""), PackageName, AssetName);
    UPackage* Package = CreatePackage(*PackageName);
    UMaterialInstanceConstant* Converted = NewObject<UMaterialInstanceConstant>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    // Inherit the entire source graph, texture references, channels and static parameters without guessing their names.
    Converted->SetParentEditorOnly(Original);
    Converted->PostEditChange();
    FAssetRegistryModule::AssetCreated(Converted);
    if (!SaveAsset(Converted, Result.Error)) return Result;

    const FScopedTransaction Transaction(NSLOCTEXT("NPR", "ConvertOutline", "Convert material slot to NPR Outline"));
    AActor* Actor = Mesh->GetOwner();
    Actor->Modify();
    Mesh->Modify();
    UNPROutlineComponent* Binding = NewObject<UNPROutlineComponent>(Actor, NAME_None, RF_Transactional);
    Binding->TargetMesh = Mesh;
    Binding->MaterialSlotIndex = SlotIndex;
    Binding->MaterialSlotName = Mesh->GetMaterialSlotNames()[SlotIndex];
    Binding->OriginalMaterial = Original;
    Binding->ConvertedMaterial = Converted;
    Binding->HullTemplate = Hull;
    Binding->MaskTemplate = Mask;
    Binding->ScreenTemplate = Screen;
    Binding->Settings = Settings;
    Actor->AddInstanceComponent(Binding);
    Binding->RegisterComponent();
    if (!Binding->RefreshOutline())
    {
        Result.Error = Binding->GetLastError();
        Actor->RemoveInstanceComponent(Binding);
        Binding->DestroyComponent();
        return Result;
    }
    Mesh->SetMaterial(SlotIndex, Converted);
    Actor->MarkPackageDirty();
    Result.Binding = Binding;
    Result.bSuccess = true;
    return Result;
}

UNPROutlinePreset* UNPROutlineEditorLibrary::SavePreset(const FString& PackagePath, const FNPROutlineSettings& Settings, FString& OutError)
{
    OutError.Reset();
    if (!Settings.Validate(OutError)) return nullptr;
    if (!PackagePath.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackagePath))
    {
        OutError = TEXT("Preset requires a valid /Game/ package path without the .asset suffix.");
        return nullptr;
    }
    UObject* Existing = LoadObject<UObject>(nullptr, *(PackagePath + TEXT(".") + FPackageName::GetLongPackageAssetName(PackagePath)), nullptr, LOAD_NoWarn);
    UNPROutlinePreset* PresetAsset = Cast<UNPROutlinePreset>(Existing);
    if (Existing && !PresetAsset) { OutError = TEXT("A different asset type already occupies that path."); return nullptr; }
    const FScopedTransaction Transaction(NSLOCTEXT("NPR", "SaveOutlinePreset", "Save NPR Outline preset"));
    if (!PresetAsset)
    {
        PresetAsset = NewObject<UNPROutlinePreset>(CreatePackage(*PackagePath), *FPackageName::GetLongPackageAssetName(PackagePath), RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(PresetAsset);
    }
    PresetAsset->Modify();
    PresetAsset->Settings = Settings;
    PresetAsset->MarkPackageDirty();
    return SaveAsset(PresetAsset, OutError) ? PresetAsset : nullptr;
}

bool UNPROutlineEditorLibrary::ApplyPreset(UNPROutlineComponent* Binding, UNPROutlinePreset* PresetAsset, FString& OutError)
{
    if (!IsValid(Binding) || !IsValid(PresetAsset)) { OutError = TEXT("Select a converted slot and preset first."); return false; }
    const FScopedTransaction Transaction(NSLOCTEXT("NPR", "ApplyOutlinePreset", "Apply NPR Outline preset"));
    Binding->Modify();
    const bool bResult = Binding->ApplyPreset(PresetAsset, OutError);
    if (bResult) Binding->MarkPackageDirty();
    return bResult;
}

bool UNPROutlineEditorLibrary::SaveBinding(UNPROutlineComponent* Binding, FString& OutError)
{
    OutError.Reset();
    if (!IsValid(Binding) || !Binding->GetOwner()) { OutError = TEXT("No converted binding to save."); return false; }
    if (!Binding->Settings.Validate(OutError)) return false;
    if (!Binding->RefreshOutline()) { OutError = Binding->GetLastError(); return false; }
    if (IsValid(Binding->ConvertedMaterial) && !SaveAsset(Binding->ConvertedMaterial, OutError)) return false;
    if (!FEditorFileUtils::SaveLevel(Binding->GetOwner()->GetLevel()))
    {
        OutError = TEXT("Level save was canceled or failed."); return false;
    }
    return true;
}

bool UNPROutlineEditorLibrary::RestoreSource(UNPROutlineComponent* Binding, FString& OutError)
{
    OutError.Reset();
    if (!IsValid(Binding) || !IsValid(Binding->TargetMesh)) { OutError = TEXT("No valid binding selected."); return false; }
    if (Binding->TargetMesh->GetMaterial(Binding->MaterialSlotIndex) != Binding->ConvertedMaterial)
    {
        OutError = TEXT("Another system changed the slot; source restoration was refused."); return false;
    }
    const FScopedTransaction Transaction(NSLOCTEXT("NPR", "RestoreOutline", "Restore source material and remove NPR Outline"));
    Binding->GetOwner()->Modify();
    Binding->TargetMesh->Modify();
    Binding->Modify();
    Binding->TargetMesh->SetMaterial(Binding->MaterialSlotIndex, Binding->OriginalMaterial);
    Binding->GetOwner()->RemoveInstanceComponent(Binding);
    Binding->DestroyComponent();
    return true;
}

void UNPROutlineEditorLibrary::OpenEditor() { FNPROutlineEditorUI::Open(); }
bool UNPROutlineEditorLibrary::ExportComparison(const FString& Directory) { return FNPROutlineEditorUI::ExportComparison(Directory); }
bool UNPROutlineEditorLibrary::SetComparisonCaptureSize(int32 Width, int32 Height)
{
    if (!((Width == 0 && Height == 0) || (Width >= 16 && Width <= 4096 && Height >= 16 && Height <= 4096))) return false;
    return FNPROutlineEditorUI::SetComparisonCaptureSize(Width, Height);
}

int32 UNPROutlineEditorLibrary::GetComparisonMeshCount(bool bOutlined)
{ return FNPROutlineEditorUI::GetComparisonMeshCount(bOutlined); }

bool UNPROutlineEditorLibrary::SetComparisonCamera(FVector Location, FRotator Rotation)
{ return FNPROutlineEditorUI::SetComparisonCamera(Location,Rotation); }
bool UNPROutlineEditorLibrary::GetComparisonCamera(FVector& Location, FRotator& Rotation)
{ return FNPROutlineEditorUI::GetComparisonCamera(Location,Rotation); }
bool UNPROutlineEditorLibrary::SetComparisonScreenPercentage(int32 Percentage)
{ return FNPROutlineEditorUI::SetComparisonScreenPercentage(Percentage); }
FString UNPROutlineEditorLibrary::GetComparisonTemporalState()
{ return FNPROutlineEditorUI::GetComparisonTemporalState(); }
bool UNPROutlineEditorLibrary::SetValidationScreenTemplate(UNPROutlineActorComponent* Component,UMaterialInterface* Material)
{
    if(!IsValid(Component) || !IsValid(Material) || !Component->GetOwner() ||
        !Component->GetOwner()->ActorHasTag(TEXT("PrincipledNPR.MultiScope.v1"))) return false;
    Component->ScreenTemplate=Material;
    return Component->RefreshOutline();
}

FString UNPROutlineEditorLibrary::FinishBackendCompilation()
{
    FAssetCompilingManager::Get().FinishAllCompilation();
    if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
    FlushRenderingCommands();
    FString Errors;
    for (const TCHAR* Name : {TEXT("M_OutlineHull"), TEXT("M_OutlineMask"), TEXT("M_OutlineScreen")})
    {
        UMaterial* Material = LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("/PrincipledNPR/Outline/%s.%s"),Name,Name));
        FMaterialResource* Resource = Material ? Material->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr;
        if (!Resource || !Resource->GetGameThreadShaderMap()) Errors += FString(Name) + TEXT(": no compiled shader map.\n");
        if (Resource) for (const FString& Error : Resource->GetCompileErrors()) Errors += FString(Name) + TEXT(": ") + Error + TEXT("\n");
    }
    return Errors;
}
