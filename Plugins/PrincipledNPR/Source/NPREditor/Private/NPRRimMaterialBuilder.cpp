#include "NPRRimMaterialBuilder.h"
#include "NPRMatCapGraph.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialShared.h"
#include "RHI.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionGetMaterialAttributes.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

namespace
{
template<class T> T* Node(UMaterial* Material)
{
    auto* Result = NewObject<T>(Material, NAME_None, RF_Transactional);
    Result->Material = Material;
    Result->Desc = TEXT("NPR Rim · 保留原表面");
    Material->GetExpressionCollection().AddExpression(Result);
    return Result;
}

UMaterialExpression* Rim(UMaterial* Material, FString& Error)
{
    auto* Function = LoadObject<UMaterialFunctionInterface>(nullptr,
        TEXT("/PrincipledNPR/RimLight/MF_RimLight.MF_RimLight"));
    if (!Function) { Error = TEXT("找不到 MF_RimLight；原材质保持不变。"); return nullptr; }
    auto* Call = Node<UMaterialExpressionMaterialFunctionCall>(Material);
    Call->SetMaterialFunction(Function);
    const TCHAR* Names[] = { NPRRimContract::WidthMaterialName, NPRRimContract::SoftnessMaterialName, NPRRimContract::IntensityMaterialName };
    const TCHAR* Pins[] = { TEXT("RimGrazingAngleRad"), TEXT("RimEdgeHalfWidthRad"), TEXT("RimPeakScale") };
    const float Defaults[] = {8.f, 12.f, 0.f};
    for (int32 I=0; I<3; ++I)
    {
        auto* Parameter = Node<UMaterialExpressionScalarParameter>(Material);
        Parameter->ParameterName = Names[I]; Parameter->DefaultValue = Defaults[I];
        Parameter->Group = TEXT("边缘光"); Parameter->SortPriority = I;
        Parameter->SliderMin = 0; Parameter->SliderMax = I<2 ? 90.f : 2.f;
        UMaterialExpression* Value = Parameter;
        if (I<2)
        {
            auto* Radians = Node<UMaterialExpressionMultiply>(Material);
            Radians->A.Connect(0, Parameter); Radians->ConstB = PI/180.f; Value = Radians;
        }
        for (auto& Input : Call->FunctionInputs)
            if (Input.ExpressionInput && Input.ExpressionInput->InputName == Pins[I]) Input.Input.Connect(0, Value);
    }
    auto* Color = Node<UMaterialExpressionVectorParameter>(Material);
    Color->ParameterName = NPRRimContract::ColorMaterialName; Color->DefaultValue = FLinearColor::White;
    Color->Group = TEXT("边缘光"); Color->SortPriority = 3;
    for (auto& Input : Call->FunctionInputs)
        if (Input.ExpressionInput && Input.ExpressionInput->InputName == TEXT("RimColor")) Input.Input.Connect(0, Color);
    // The function defaults to the vertex normal. Source normals and textures stay untouched.
    return Call;
}

void Append(UMaterial* Material, FExpressionInput& Emission, UMaterialExpression* Contribution)
{
    if (Emission.Expression)
    {
        auto* Add = Node<UMaterialExpressionAdd>(Material);
        Add->A = Emission; Add->B.Connect(0, Contribution); Emission.Connect(0, Add);
    }
    else Emission.Connect(0, Contribution);
}

bool Extend(UMaterial* Material, ENPRRimShadingModel Model, UMaterialExpression* Contribution, FString& Error)
{
    auto* Data = Material->GetEditorOnlyData();
    auto* Root = Data->FrontMaterial.Expression;
    FExpressionInput* Emission = nullptr;
    if (auto* Toon = Cast<UMaterialExpressionSubstrateToonBSDF>(Root))
    {
        if (Model == ENPRRimShadingModel::Subsurface)
        {
            auto* SSS = Node<UMaterialExpressionSubstrateShadingModels>(Material);
            SSS->BaseColor=Toon->BaseColor; SSS->Metallic=Toon->Metallic; SSS->Specular=Toon->Specular;
            SSS->Roughness=Toon->Roughness; SSS->Normal=Toon->Normal; SSS->EmissiveColor=Toon->EmissiveColor;
            SSS->Tangent=Toon->Tangent; SSS->Anisotropy=Toon->Anisotropy;
            SSS->SubSurfaceColor=Toon->BaseColor; SSS->ShadingModelOverride=MSM_Subsurface;
            Data->FrontMaterial.Connect(0, SSS); Emission=&SSS->EmissiveColor;
        }
        else Emission=&Toon->EmissiveColor;
    }
    else if (auto* Models = Cast<UMaterialExpressionSubstrateShadingModels>(Root))
    {
        if (Model == ENPRRimShadingModel::Subsurface)
        {
            Models->ShadingModel.Expression=nullptr; Models->ShadingModelOverride=MSM_Subsurface;
            if (!Models->SubSurfaceColor.Expression) Models->SubSurfaceColor=Models->BaseColor;
        }
        Emission=&Models->EmissiveColor;
    }
    else if (auto* Slab = Cast<UMaterialExpressionSubstrateSlabBSDF>(Root))
    {
        if (Model == ENPRRimShadingModel::Subsurface)
        {
            Slab->SubSurfaceType=MSS_Wrap;
            if (!Slab->SSSMFP.Expression)
            {
                auto* MFP=Node<UMaterialExpressionConstant3Vector>(Material);
                MFP->Constant=FLinearColor(1,1,1); Slab->SSSMFP.Connect(0,MFP);
            }
        }
        Emission=&Slab->EmissiveColor;
    }
    else if (Root)
    {
        Error=TEXT("当前 Substrate 根节点尚不支持安全添加 Rim（支持直接 Toon、Slab 和 Shading Models）。原材质保持不变。");
        return false;
    }
    if (!Contribution && (Emission || Model==ENPRRimShadingModel::KeepOriginal)) return true;
    if (!Contribution) return false;
    if (Emission) { Append(Material,*Emission,Contribution); return true; }
    if (Model == ENPRRimShadingModel::Subsurface)
    {
        if (Material->bUseMaterialAttributes)
        {
            Error=TEXT("Material Attributes 的 Subsurface 切换尚未适配；可使用保持原模型添加 Rim。原材质保持不变。");
            return false;
        }
        Material->SetShadingModel(MSM_Subsurface); Data->ShadingModelFromMaterialExpression.Expression=nullptr;
        if (!Data->SubsurfaceColor.Expression && !Data->SubsurfaceColor.UseConstant)
            Data->SubsurfaceColor.Expression=Data->BaseColor.Expression;
    }
    if (Material->bUseMaterialAttributes)
    {
        const auto Original=Data->MaterialAttributes;
        auto* Get=Node<UMaterialExpressionGetMaterialAttributes>(Material);
        Get->MaterialAttributes=Original;
        auto* Add=Node<UMaterialExpressionAdd>(Material);
        Add->A.Connect(Get->CreateOrGetOutputAttribute(MP_EmissiveColor),Get);
        Add->B.Connect(0,Contribution);
        auto* Set=Node<UMaterialExpressionSetMaterialAttributes>(Material);
        Set->GetInput(0)->Connect(Original.OutputIndex,Original.Expression);
        Set->ConnectInputAttribute(MP_EmissiveColor,Add);
        Data->MaterialAttributes.Connect(0,Set);
    }
    else
    {
        if (!Data->EmissiveColor.Expression && Data->EmissiveColor.UseConstant)
        {
            auto* Constant=Node<UMaterialExpressionConstant3Vector>(Material);
            Constant->Constant=FLinearColor(Data->EmissiveColor.Constant);
            Data->EmissiveColor.Connect(0,Constant);
        }
        Data->EmissiveColor.UseConstant=false;
        Append(Material,Data->EmissiveColor,Contribution);
    }
    return true;
}

bool SaveDerivedSurfaceAsset(UObject* Asset, FString& Error)
{
    auto* Package=Asset->GetOutermost();
    const FString Filename=FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
    FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
    if (!UPackage::SavePackage(Package,Asset,*Filename,Args)) { Error=TEXT("无法保存派生材质：") + Filename; return false; }
    return true;
}

void CopyInheritedBaseOverrides(UMaterialInterface* Source, UMaterialInstanceConstant* Target)
{
    TArray<UMaterialInstance*> Chain;
    for(auto* Instance=Cast<UMaterialInstance>(Source); Instance; Instance=Cast<UMaterialInstance>(Instance->Parent)) Chain.Add(Instance);
    auto* Struct=FMaterialInstanceBasePropertyOverrides::StaticStruct();
    for(int32 I=Chain.Num()-1;I>=0;--I)
    {
        const auto& From=Chain[I]->BasePropertyOverrides; auto& To=Target->BasePropertyOverrides;
        for(TFieldIterator<FProperty> It(Struct);It;++It)
        {
            const FString Condition=It->GetMetaData(TEXT("EditCondition"));
            auto* Flag=FindFProperty<FBoolProperty>(Struct,*Condition);
            if(Flag && Flag->GetPropertyValue_InContainer(&From))
            {
                It->CopyCompleteValue_InContainer(&To,&From);
                Flag->SetPropertyValue_InContainer(&To,true);
            }
        }
        To.UsageFlags=(To.UsageFlags & ~From.bOverride_UsageFlags) | (From.UsageFlags & From.bOverride_UsageFlags);
        To.bOverride_UsageFlags|=From.bOverride_UsageFlags;
        if(Chain[I]->bOverrideSubsurfaceProfile){Target->bOverrideSubsurfaceProfile=true;Target->SubsurfaceProfile=Chain[I]->SubsurfaceProfile;}
        if(Chain[I]->bOverrideSpecularProfile){Target->bOverrideSpecularProfile=true;Target->SpecularProfileOverride=Chain[I]->SpecularProfileOverride;}
        if(Chain[I]->bOverrideToonProfile){Target->bOverrideToonProfile=true;Target->ToonProfileOverride=Chain[I]->ToonProfileOverride;}
    }
}

void CopySurfaceParameters(UMaterialInterface* Source, UMaterialInstanceConstant* Target)
{
    CopyNPRSurfaceParameters(*Source, *Target, true);
    CopyInheritedBaseOverrides(Source,Target);
}
}

void CopyNPRSurfaceParameters(UMaterialInterface& Source, UMaterialInstanceConstant& Target, bool bIncludeStatic)
{
    FMaterialInstanceParameterUpdateContext Update(&Target, EMaterialInstanceClearParameterFlag::AllNonStatic);
    TArray<FMaterialParameterInfo> Infos;
    TArray<FGuid> Ids;
    for (int32 Index = 0; Index < NumMaterialParameterTypes; ++Index)
    {
        const auto Type = static_cast<EMaterialParameterType>(Index);
        if (!bIncludeStatic && IsStaticMaterialParameter(Type)) continue;
        Source.GetAllParameterInfoOfType(Type, Infos, Ids);
        for (const auto& Info : Infos)
        {
            FMaterialParameterMetadata Value;
            if (Source.GetParameterValue(Type, FMemoryImageMaterialParameterInfo(Info), Value))
            {
                Value.bOverride = true;
                Update.SetParameterValueEditorOnly(Info, Value);
            }
        }
    }
}

UMaterialInstanceConstant* NPRRimMaterialBuilder::Build(UMaterialInterface* Source, ENPRRimShadingModel Model, FString& Error)
{
    Error.Reset();
    if (!IsValid(Source) || Source->GetMaterial()->MaterialDomain!=MD_Surface ||
        (Source->GetBlendMode()!=BLEND_Opaque && Source->GetBlendMode()!=BLEND_Masked))
    { Error=TEXT("请选择不透明或 Masked 表面的材质槽；原材质保持不变。"); return nullptr; }
    FString PackageName,Name;
    FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(TEXT("/Game/NPR/Generated/Rim/M_Rim_")+Source->GetName(),TEXT(""),PackageName,Name);
    auto* Material=DuplicateObject<UMaterial>(Source->GetMaterial(),CreatePackage(*PackageName),*Name);
    Material->ClearFlags(RF_Transient); Material->SetFlags(RF_Public|RF_Standalone);
    float Scalar;FLinearColor Color;
    bool HasRim=Source->GetVectorParameterValue(FHashedMaterialParameterInfo(FName(NPRRimContract::ColorMaterialName)),Color);
    for(const FName Parameter : {FName(NPRRimContract::WidthMaterialName),FName(NPRRimContract::SoftnessMaterialName),FName(NPRRimContract::IntensityMaterialName)})
        HasRim &= Source->GetScalarParameterValue(FHashedMaterialParameterInfo(Parameter),Scalar);
    auto* Contribution = HasRim ? nullptr : Rim(Material, Error);
    if ((!HasRim && !Contribution) || !Extend(Material,Model,Contribution,Error)) return nullptr;
    NPRMatCapGraph::UpgradeSubstrateNormal(*Material);
    Material->PostEditChange();
    FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(PackageName+TEXT("_Instance"),TEXT(""),PackageName,Name);
    auto* Instance=NewObject<UMaterialInstanceConstant>(CreatePackage(*PackageName),*Name,RF_Public|RF_Standalone);
    Instance->SetParentEditorOnly(Material);
    CopySurfaceParameters(Source,Instance);
    if (Model==ENPRRimShadingModel::Subsurface)
    { Instance->BasePropertyOverrides.bOverride_ShadingModel=false; }
    Instance->PostEditChange();
    // First-use compilation must finish before swapping the slot, otherwise the live preview
    // can show the engine checkerboard while the original surface is already available.
    Instance->EnsureIsComplete();
    const FMaterialResource* Resource=Instance->GetMaterialResource(GMaxRHIShaderPlatform);
    if(!Resource) Resource=Material->GetMaterialResource(GMaxRHIShaderPlatform);
    if(Resource && !Resource->GetCompileErrors().IsEmpty())
    { Error=TEXT("Rim 材质编译失败，原材质保持不变：")+FString::Join(Resource->GetCompileErrors(),TEXT("\n"));return nullptr; }
    return Instance;
}

bool NPRRimMaterialBuilder::Save(UMaterialInstanceConstant* Material, FString& Error)
{
    if (!Material) { Error=TEXT("没有可保存的 Rim 材质。"); return false; }
    if (!SaveDerivedSurfaceAsset(Material->GetMaterial(),Error) || !SaveDerivedSurfaceAsset(Material,Error)) return false;
    FAssetRegistryModule::AssetCreated(Material->GetMaterial()); FAssetRegistryModule::AssetCreated(Material);
    return true;
}

UMaterialInstanceConstant* NPRMatCapMaterialBuilder::Build(UMaterialInterface* Source, FString& Error)
{
    Error.Reset();
    if (!IsValid(Source) || !Source->GetMaterial() || Source->GetMaterial()->MaterialDomain != MD_Surface ||
        (Source->GetBlendMode() != BLEND_Opaque && Source->GetBlendMode() != BLEND_Masked))
    {
        Error = TEXT("MatCap 支持不透明和 Masked 表面材质。");
        return nullptr;
    }
    const auto* Root = Source->GetMaterial()->GetEditorOnlyData()->FrontMaterial.Expression;
    if (Root && !Root->IsA<UMaterialExpressionSubstrateToonBSDF>() &&
        !Root->IsA<UMaterialExpressionSubstrateShadingModels>() && !Root->IsA<UMaterialExpressionSubstrateSlabBSDF>())
    {
        Error = TEXT("MatCap 暂不支持从复杂 Substrate 分层派生材质。");
        return nullptr;
    }

    auto* Material = DuplicateObject<UMaterial>(Source->GetMaterial(), GetTransientPackage());
    Material->ClearFlags(RF_Public | RF_Standalone);
    Material->SetFlags(RF_Transient);
    if (!FNPRMatCapParameters::IsSupported(Source))
    {
        auto* Contribution = NPRMatCapGraph::Add(*Material);
        if (!Contribution)
        {
            Error = TEXT("无法加载 MatCap 的默认贴图。");
            return nullptr;
        }
        if (!Extend(Material, ENPRRimShadingModel::KeepOriginal, Contribution, Error)) return nullptr;
    }
    else
    {
        NPRMatCapGraph::UpgradeSubstrateNormal(*Material);
    }
    Material->PostEditChange();
    auto* Instance = NewObject<UMaterialInstanceConstant>(GetTransientPackage(), NAME_None, RF_Transient);
    Instance->SetParentEditorOnly(Material);
    CopySurfaceParameters(Source, Instance);
    Instance->PostEditChange();
    Instance->EnsureIsComplete();
    const FMaterialResource* Resource = Instance->GetMaterialResource(GMaxRHIShaderPlatform);
    if (!Resource || !Resource->GetCompileErrors().IsEmpty())
    {
        Error = TEXT("MatCap 材质编译失败：") + (Resource ? FString::Join(Resource->GetCompileErrors(), TEXT("\n")) : FString(TEXT("没有材质资源")));
        return nullptr;
    }
    return Instance;
}

UMaterialInstanceConstant* NPRMatCapMaterialBuilder::Bake(UMaterialInstanceConstant* Preview, const FString& Root, FString& Error)
{
    Error.Reset();
    if (!IsValid(Preview) || !Root.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Root))
    {
        Error = TEXT("保存 MatCap 需要有效预览和 /Game 下的目录。");
        return nullptr;
    }
    auto& AssetTools = FAssetToolsModule::GetModule().Get();
    FString Package, Name;
    AssetTools.CreateUniqueAssetName(Root / TEXT("M_MatCap"), TEXT(""), Package, Name);
    auto* Material = DuplicateObject<UMaterial>(Preview->GetMaterial(), CreatePackage(*Package), *Name);
    Material->ClearFlags(RF_Transient);
    Material->SetFlags(RF_Public | RF_Standalone);
    Material->PostEditChange();
    AssetTools.CreateUniqueAssetName(Root / TEXT("MI_MatCap"), TEXT(""), Package, Name);
    auto* Instance = DuplicateObject<UMaterialInstanceConstant>(Preview, CreatePackage(*Package), *Name);
    Instance->ClearFlags(RF_Transient);
    Instance->SetFlags(RF_Public | RF_Standalone);
    Instance->SetParentEditorOnly(Material);
    Instance->PostEditChange();
    Instance->EnsureIsComplete();
    const FMaterialResource* Resource = Instance->GetMaterialResource(GMaxRHIShaderPlatform);
    if (!Resource || !Resource->GetCompileErrors().IsEmpty())
    {
        Error = TEXT("已生成的 MatCap 材质无法编译。");
        return nullptr;
    }
    if (!SaveDerivedSurfaceAsset(Material, Error) || !SaveDerivedSurfaceAsset(Instance, Error))
    {
        Error = TEXT("无法保存生成的 MatCap 资产。");
        return nullptr;
    }
    FAssetRegistryModule::AssetCreated(Material);
    FAssetRegistryModule::AssetCreated(Instance);
    return Instance;
}
