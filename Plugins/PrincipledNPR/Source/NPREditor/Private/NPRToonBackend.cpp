#include "NPRToonBackend.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Engine/ToonProfile.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionGetMaterialAttributes.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "MaterialShared.h"
#include "NPRBackendStylePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "RHI.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include <cmath>

namespace NPRToonBackend::Private
{
template<class T> T* Node(UMaterial* M)
{
    T* N=NewObject<T>(M,NAME_None,RF_Transactional);N->Material=M;
    N->Desc=TEXT("NPR Toon Adapter v1");M->GetExpressionCollection().AddExpression(N);return N;
}
void Notify(UToonProfile* P)
{
    FPropertyChangedEvent Event(FindFProperty<FProperty>(UToonProfile::StaticClass(),TEXT("Settings")),EPropertyChangeType::Interactive);
    P->PostEditChangeProperty(Event);
}
void DiffuseCurve(UToonProfile* P,const FNPRToonParameters& V,const FNPRToonResponseSettings& S)
{
    // Restrict the mathematical function to the reachable domain before generating knots.
    // A clipped cubic Hermite interval remains cubic with its original derivative.
    TArray<float> Times{0.f,1.f};
    const double T=(V.TerminatorDotCenter+1.0)*.5,H=V.TerminatorDotHalfWidth*.5;
    if(H>0)
    {
        for(double U:{T-H,T,T+H}) if(U>0&&U<1) Times.AddUnique(float(U));
    }
    else
    {
        const float Center=float(T);
        for(float U:{std::nextafter(Center,-1.f),Center,std::nextafter(Center,2.f)})
            if(U>=0&&U<=1) Times.AddUnique(U);
    }
    Times.Sort();
    auto& Curve=P->Settings.DiffuseRamp;Curve.ExternalCurve=nullptr;
    for(int32 Channel=0;Channel<4;++Channel)
    {
        FRichCurve& C=Curve.ColorCurves[Channel];C.Reset();
        const double Dark=Channel==3?1.0:S.DarkResponse.Component(Channel);
        const double Delta=Channel==3?0.0:double(S.LightResponse.Component(Channel))-Dark;
        for(float U:Times)
        {
            const double B=H==0&&U==float(T)?.5:V.EvaluateDiffuse(2.0*U-1.0,S.Shape);
            auto Handle=C.AddKey(U,float(Dark+Delta*B));
            if(H>0&&S.Shape==ENPRToonResponseShape::Hermite)
            {
                auto& K=C.GetKey(Handle);K.InterpMode=RCIM_Cubic;K.TangentMode=RCTM_User;
                const double Z=FMath::Clamp((double(U)-T+H)/(2.0*H),0.0,1.0);
                const float Derivative=float(Delta*6.0*Z*(1.0-Z)/(2.0*H));
                K.ArriveTangent=Derivative;K.LeaveTangent=Derivative;
            }
            else C.SetKeyInterpMode(Handle,RCIM_Linear);
        }
    }
}
double GGXCoordinate(double Angle)
{
    // Reference normalisation D(theta)/D(0), isotropic r=.5. The engine also applies
    // a NoV-dependent LUT, area-light integration and outer visibility: Approximate.
    constexpr double A2=.0625;
    const double C=FMath::Cos(Angle),D=1.0+(A2-1.0)*C*C;
    return FMath::Clamp(A2*A2/(D*D),0.0,1.0);
}
void SpecularCurve(UToonProfile* P,const FNPRToonParameters& V)
{
    TArray<float> Times{0.f,1.f};
    for(int32 I=1;I<256;++I) Times.Add(float(I)/256.f);
    const double Delta=.95*V.SpecEdgeSoftness*FMath::Min(V.SpecHalfAngleRad,UE_DOUBLE_HALF_PI-V.SpecHalfAngleRad);
    for(double A:{V.SpecHalfAngleRad-Delta,V.SpecHalfAngleRad,V.SpecHalfAngleRad+Delta}) Times.AddUnique(float(GGXCoordinate(A)));
    Times.Sort();auto& Curve=P->Settings.SpecularRamp;Curve.ExternalCurve=nullptr;
    for(auto& C:Curve.ColorCurves) C.Reset();
    for(float X:Times)
    {
        constexpr double A2=.0625;
        const double Cos2=X>0?FMath::Clamp((1.0-A2/FMath::Sqrt(double(X)))/(1.0-A2),0.0,1.0):0.0;
        const float Y=float(V.EvaluateSpecular(FMath::Acos(FMath::Sqrt(Cos2))));
        for(int32 Ch=0;Ch<4;++Ch)
        {auto H=Curve.ColorCurves[Ch].AddKey(X,Ch==3?1.f:Y);Curve.ColorCurves[Ch].SetKeyInterpMode(H,RCIM_Linear);}
    }
}
FExpressionInput Scalar(UMaterial* M,const FExpressionInput& Input,float Value)
{
    if(Input.Expression)return Input;
    auto* C=Node<UMaterialExpressionConstant>(M);C->R=Value;FExpressionInput Out;Out.Connect(0,C);return Out;
}
FExpressionInput Color(UMaterial* M,const FExpressionInput& Input,const FLinearColor& Value)
{
    if(Input.Expression)return Input;
    auto* C=Node<UMaterialExpressionConstant3Vector>(M);C->Constant=Value;FExpressionInput Out;Out.Connect(0,C);return Out;
}
bool Convert(UMaterial* M,ENPRShadingModel Model,UToonProfile* P,bool ArtisticSpecular,FString& Error)
{
    auto* D=M->GetEditorOnlyData();auto* Old=D->FrontMaterial.Expression;
    auto* Toon=Cast<UMaterialExpressionSubstrateToonBSDF>(Old);
    auto* Lit=Cast<UMaterialExpressionSubstrateShadingModels>(Old);
    if(Old&&!Toon&&!Lit)
    {Error=TEXT("当前 Substrate 组合根节点无法安全转换为 Toon；请使用直接 Toon/Shading Models 根节点或显式 Surface 输入。");return false;}
    // Inherit keeps the complete original non-Toon model and graph, including
    // Subsurface and model-selection inputs. It must not silently become Lit.
    if(Model==ENPRShadingModel::Inherit&&!Toon)return true;
    if(Model==ENPRShadingModel::Inherit) Model=Toon?ENPRShadingModel::SubstrateToon:ENPRShadingModel::DefaultLit;
    FExpressionInput Base,Metal,Spec,Rough,Normal,Emission,Tangent,Anisotropy;
    if(Toon)
    {Base=Toon->BaseColor;Metal=Toon->Metallic;Spec=Toon->Specular;Rough=Toon->Roughness;Normal=Toon->Normal;Emission=Toon->EmissiveColor;Tangent=Toon->Tangent;Anisotropy=Toon->Anisotropy;}
    else if(Lit)
    {Base=Lit->BaseColor;Metal=Lit->Metallic;Spec=Lit->Specular;Rough=Lit->Roughness;Normal=Lit->Normal;Emission=Lit->EmissiveColor;Tangent=Lit->Tangent;Anisotropy=Lit->Anisotropy;}
    else if(M->bUseMaterialAttributes)
    {
        auto* Get=Node<UMaterialExpressionGetMaterialAttributes>(M);Get->MaterialAttributes=D->MaterialAttributes;
        auto Read=[&](EMaterialProperty Property){FExpressionInput Input;Input.Connect(Get->CreateOrGetOutputAttribute(Property),Get);return Input;};
        Base=Read(MP_BaseColor);Metal=Read(MP_Metallic);Spec=Read(MP_Specular);Rough=Read(MP_Roughness);
        Normal=Read(MP_Normal);Emission=Read(MP_EmissiveColor);Tangent=Read(MP_Tangent);Anisotropy=Read(MP_Anisotropy);
        D->OpacityMask.Connect(Get->CreateOrGetOutputAttribute(MP_OpacityMask),Get);
        D->WorldPositionOffset.Connect(Get->CreateOrGetOutputAttribute(MP_WorldPositionOffset),Get);
        D->PixelDepthOffset.Connect(Get->CreateOrGetOutputAttribute(MP_PixelDepthOffset),Get);
        D->Displacement.Connect(Get->CreateOrGetOutputAttribute(MP_Displacement),Get);
        D->AmbientOcclusion.Connect(Get->CreateOrGetOutputAttribute(MP_AmbientOcclusion),Get);
        for(int32 I=0;I<M->NumCustomizedUVs&&I<8;++I)
            D->CustomizedUVs[I].Connect(Get->CreateOrGetOutputAttribute(EMaterialProperty(int32(MP_CustomizedUVs0)+I)),Get);
        M->bUseMaterialAttributes=false;
    }
    else
    {
        const FExpressionInput Empty;
        const auto BaseDefault=FMaterialAttributeDefinitionMap::GetDefaultValue(MP_BaseColor);
        Base=Color(M,D->BaseColor.UseConstant?Empty:D->BaseColor,D->BaseColor.UseConstant?D->BaseColor.Constant:FLinearColor(BaseDefault.X,BaseDefault.Y,BaseDefault.Z));
        Metal=Scalar(M,D->Metallic.UseConstant?Empty:D->Metallic,D->Metallic.UseConstant?D->Metallic.Constant:FMaterialAttributeDefinitionMap::GetDefaultValue(MP_Metallic).X);
        Spec=Scalar(M,D->Specular.UseConstant?Empty:D->Specular,D->Specular.UseConstant?D->Specular.Constant:FMaterialAttributeDefinitionMap::GetDefaultValue(MP_Specular).X);
        Rough=Scalar(M,D->Roughness.UseConstant?Empty:D->Roughness,D->Roughness.UseConstant?D->Roughness.Constant:FMaterialAttributeDefinitionMap::GetDefaultValue(MP_Roughness).X);
        Normal=D->Normal.UseConstant?Color(M,Empty,FLinearColor(D->Normal.Constant.X,D->Normal.Constant.Y,D->Normal.Constant.Z)):FExpressionInput(D->Normal);
        Emission=Color(M,D->EmissiveColor.UseConstant?Empty:D->EmissiveColor,D->EmissiveColor.UseConstant?D->EmissiveColor.Constant:FLinearColor::Black);
        Tangent=D->Tangent.UseConstant?Color(M,Empty,FLinearColor(D->Tangent.Constant.X,D->Tangent.Constant.Y,D->Tangent.Constant.Z)):FExpressionInput(D->Tangent);
        Anisotropy=D->Anisotropy; // UE explicitly ignores UseConstant for this property.
    }
    UMaterialExpressionReroute* OriginalRoughness=nullptr;
    UMaterialExpressionScalarParameter* ReferenceRoughness=nullptr;
    for(const auto& Entry:M->GetExpressionCollection().Expressions)
    {
        auto* Expression=Entry.Get();
        if(auto* R=Cast<UMaterialExpressionReroute>(Expression);R&&R->Desc==TEXT("NPR.Toon.OriginalRoughness.v1"))OriginalRoughness=R;
        if(auto* R=Cast<UMaterialExpressionScalarParameter>(Expression);R&&R->ParameterName==TEXT("NPR_ToonReferenceRoughness"))ReferenceRoughness=R;
    }
    if(OriginalRoughness&&Rough.Expression==ReferenceRoughness)Rough=OriginalRoughness->Input;
    if(Model==ENPRShadingModel::SubstrateToon)
    {
        auto* Root=Toon?Toon:Node<UMaterialExpressionSubstrateToonBSDF>(M);
        Root->BaseColor=Base;Root->Metallic=Metal;Root->Specular=Spec;Root->Roughness=Rough;Root->Normal=Normal;
        Root->EmissiveColor=Emission;Root->Tangent=Tangent;Root->Anisotropy=Anisotropy;Root->ToonProfile=P;
        if(ArtisticSpecular)
        {
            if(!OriginalRoughness){OriginalRoughness=Node<UMaterialExpressionReroute>(M);OriginalRoughness->Desc=TEXT("NPR.Toon.OriginalRoughness.v1");OriginalRoughness->Input=Rough;}
            auto* R=ReferenceRoughness?ReferenceRoughness:Node<UMaterialExpressionScalarParameter>(M);
            R->ParameterName=TEXT("NPR_ToonReferenceRoughness");R->DefaultValue=NPRToonBackend::SpecularReferenceRoughness;
            R->Group=TEXT("Toon Backend · 高级");R->SliderMin=0;R->SliderMax=1;
            R->Desc=TEXT("艺术高光参考粗糙度固定为 0.5；手动修改需要重新标定。关闭艺术高光后恢复原输入。");
            Root->Roughness.Connect(0,R);
        }
        D->FrontMaterial.Connect(0,Root);
    }
    else
    {
        auto* Root=Lit?Lit:Node<UMaterialExpressionSubstrateShadingModels>(M);
        Root->ShadingModelOverride=MSM_DefaultLit;Root->ShadingModel.Expression=nullptr;
        Root->BaseColor=Base;Root->Metallic=Metal;Root->Specular=Spec;Root->Roughness=Rough;Root->Normal=Normal;
        Root->EmissiveColor=Emission;Root->Tangent=Tangent;Root->Anisotropy=Anisotropy;D->FrontMaterial.Connect(0,Root);
    }
    return true;
}
bool Save(UObject* O,FString& Error)
{
    UPackage* P=O->GetOutermost();const FString File=FPackageName::LongPackageNameToFilename(P->GetName(),FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs Args;
    Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    if(!UPackage::SavePackage(P,O,*File,Args)){Error=TEXT("无法保存 Toon 资源：")+File;return false;}
    FAssetRegistryModule::AssetCreated(O);return true;
}
void NewAssetName(const FString& Base,FString& Package,FString& Name)
{FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(Base,TEXT(""),Package,Name);}
}

namespace NPRToonBackend { using namespace Private; }

bool NPRToonBackend::UpdateProfile(UToonProfile* P,const FNPRToonParameters& V,const FNPRToonResponseSettings& S,FString& Error)
{
    Error.Reset();
    if(!IsValid(P)||!P->HasAnyFlags(RF_Transient)||P->GetOutermost()!=GetTransientPackage())
    {Error=TEXT("实时调参只允许当前编辑器拥有的私有临时 Profile。");return false;}
    if(!V.Validate(Error)||!S.Validate(Error))return false;
    if(S.bUseArtisticSpecular&&V.SpecPeakScale>65504.0){Error=TEXT("高光倍率超出 S58 FP16 曲线表示范围；语义数据保持不变。");return false;}
    if(S.bUseArtisticDiffuse)
        for(int32 C=0;C<3;++C)if(S.DarkResponse.Component(C)>65504.f||S.LightResponse.Component(C)>65504.f)
        {Error=TEXT("响应 RGB 超出 S58 FP16 曲线表示范围；语义数据保持不变。");return false;}
    if(S.bUseArtisticDiffuse&&V.TerminatorDotHalfWidth>0)
    {
        const double T=(V.TerminatorDotCenter+1)*.5,H=V.TerminatorDotHalfWidth*.5;
        if(float(T-H)==float(T+H))
        {Error=TEXT("过渡半宽小于 S58 曲线时间精度；请使用明确的零宽或增大半宽。");return false;}
        if(S.Shape==ENPRToonResponseShape::Hermite)
            for(int32 C=0;C<3;++C)if(FMath::Abs(double(S.LightResponse.Component(C))-S.DarkResponse.Component(C))*1.5/V.TerminatorDotHalfWidth>double(TNumericLimits<float>::Max()))
            {Error=TEXT("响应斜率超出 S58 浮点表示范围。");return false;}
    }
    if(S.bUseArtisticDiffuse) DiffuseCurve(P,V,S);
    if(S.bUseArtisticSpecular) SpecularCurve(P,V);
    P->Settings.bDiffuseRampIncludeShadow=S.ShadowMode==ENPRToonShadowMode::InsideRamp;
    Notify(P);return true;
}

UMaterialInstanceConstant* NPRToonBackend::BuildPreview(UMaterialInterface* Source,ENPRShadingModel Model,
    const FNPRToonParameters& V,const FNPRToonResponseSettings& S,UToonProfile*& OutProfile,FString& Error,UToonProfile* TemplateOverride)
{
    Error.Reset();
    if(!V.Validate(Error)||!S.Validate(Error))return nullptr;
    if(!IsValid(Source)||!Source->GetMaterial()||Source->GetMaterial()->MaterialDomain!=MD_Surface||
        (Source->GetBlendMode()!=BLEND_Opaque&&Source->GetBlendMode()!=BLEND_Masked))
    {Error=TEXT("请选择不透明或 Masked 的表面材质槽。");return nullptr;}
    if(Model!=ENPRShadingModel::Inherit&&Model!=ENPRShadingModel::DefaultLit&&Model!=ENPRShadingModel::SubstrateToon)
    {Error=TEXT("未知着色模型。");return nullptr;}
    auto* Parent=DuplicateObject<UMaterial>(Source->GetMaterial(),GetTransientPackage());
    Parent->ClearFlags(RF_Public|RF_Standalone);Parent->SetFlags(RF_Transient);
    UToonProfile* Template=nullptr;
    if(auto* T=Cast<UMaterialExpressionSubstrateToonBSDF>(Parent->GetEditorOnlyData()->FrontMaterial.Expression))Template=T->ToonProfile;
    for(auto* I=Cast<UMaterialInstance>(Source);I;I=Cast<UMaterialInstance>(I->Parent))
        if(I->bOverrideToonProfile){Template=I->ToonProfileOverride;break;}
    if(IsValid(TemplateOverride))Template=TemplateOverride;
    auto* Profile=Template?DuplicateObject<UToonProfile>(Template,GetTransientPackage()):NewObject<UToonProfile>();
    Profile->ClearFlags(RF_Public|RF_Standalone);Profile->SetFlags(RF_Transient|RF_Transactional);Profile->Guid=FGuid::NewGuid();
    if(!UpdateProfile(Profile,V,S,Error)||!Convert(Parent,Model,Profile,S.bUseArtisticSpecular,Error))return nullptr;
    Parent->PostEditChange();
    auto* Result=NewObject<UMaterialInstanceConstant>(GetTransientPackage(),NAME_None,RF_Transient|RF_Transactional);
    Result->SetParentEditorOnly(Parent);Result->CopyMaterialUniformParametersEditorOnly(Source,true);
    // Preserve inherited base overrides, but bind the private profile rather than a shared source override.
    TArray<UMaterialInstance*> Chain;for(auto* I=Cast<UMaterialInstance>(Source);I;I=Cast<UMaterialInstance>(I->Parent))Chain.Add(I);
    for(int32 Index=Chain.Num()-1;Index>=0;--Index)
    {
        const auto& From=Chain[Index]->BasePropertyOverrides;auto& To=Result->BasePropertyOverrides;
        // Keep authored overrides even when the reference lobe temporarily disconnects
        // their original input. They become active again when the original path is restored.
        for(const auto& Override:Chain[Index]->ScalarParameterValues)Result->SetScalarParameterValueEditorOnly(Override.ParameterInfo,Override.ParameterValue);
        for(const auto& Override:Chain[Index]->VectorParameterValues)Result->SetVectorParameterValueEditorOnly(Override.ParameterInfo,Override.ParameterValue);
        for(const auto& Override:Chain[Index]->TextureParameterValues)Result->SetTextureParameterValueEditorOnly(Override.ParameterInfo,Override.ParameterValue);
        for(TFieldIterator<FProperty> It(FMaterialInstanceBasePropertyOverrides::StaticStruct());It;++It)
        {
            const FString Condition=It->GetMetaData(TEXT("EditCondition"));
            auto* Flag=FindFProperty<FBoolProperty>(FMaterialInstanceBasePropertyOverrides::StaticStruct(),*Condition);
            if(Flag&&Flag->GetPropertyValue_InContainer(&From)){It->CopyCompleteValue_InContainer(&To,&From);Flag->SetPropertyValue_InContainer(&To,true);}
        }
        if(Chain[Index]->bOverrideSubsurfaceProfile){Result->bOverrideSubsurfaceProfile=true;Result->SubsurfaceProfile=Chain[Index]->SubsurfaceProfile;}
        if(Chain[Index]->bOverrideSpecularProfile){Result->bOverrideSpecularProfile=true;Result->SpecularProfileOverride=Chain[Index]->SpecularProfileOverride;}
        Result->BasePropertyOverrides.UsageFlags=(Result->BasePropertyOverrides.UsageFlags&~From.bOverride_UsageFlags)|(From.UsageFlags&From.bOverride_UsageFlags);
        Result->BasePropertyOverrides.bOverride_UsageFlags|=From.bOverride_UsageFlags;
    }
    TArray<FMaterialParameterInfo> FontInfos;TArray<FGuid> FontIds;Source->GetAllFontParameterInfo(FontInfos,FontIds);
    for(const auto& Info:FontInfos){UFont* Font=nullptr;int32 Page=0;if(Source->GetFontParameterValue(Info,Font,Page))Result->SetFontParameterValueEditorOnly(Info,Font,Page);}
    if(S.bUseArtisticSpecular&&Cast<UMaterialExpressionSubstrateToonBSDF>(Parent->GetEditorOnlyData()->FrontMaterial.Expression))
        Result->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("NPR_ToonReferenceRoughness")),SpecularReferenceRoughness);
    if(Model!=ENPRShadingModel::Inherit)Result->BasePropertyOverrides.bOverride_ShadingModel=false;
    Result->bOverrideToonProfile=true;Result->ToonProfileOverride=Profile;
    Result->PostEditChange();Result->EnsureIsComplete();
    const FMaterialResource* Resource=Result->GetMaterialResource(GMaxRHIShaderPlatform);
    if(!Resource)Resource=Parent->GetMaterialResource(GMaxRHIShaderPlatform);
    if(Resource&&!Resource->GetCompileErrors().IsEmpty())
    {Error=TEXT("Toon 材质编译失败：")+FString::Join(Resource->GetCompileErrors(),TEXT("\n"));return nullptr;}
    OutProfile=Profile;return Result;
}

UNPRBackendStylePackage* NPRToonBackend::Bake(UMaterialInstanceConstant* Preview,UToonProfile* PrivateProfile,
    ENPRShadingModel Model,const FNPRToonParameters& V,const FNPRToonResponseSettings& S,int32 SlotIndex,
    const FString& Root,FString& Error)
{
    Error.Reset();
    if(!IsValid(Preview)||!IsValid(PrivateProfile)||SlotIndex<0||!V.Validate(Error)||!S.Validate(Error)||
        !Root.StartsWith(TEXT("/Game/"))||!FPackageName::IsValidLongPackageName(Root))
    {if(Error.IsEmpty())Error=TEXT("请选择有效预览、槽位和 /Game 下的保存目录。");return nullptr;}
    FString Package,Name;NewAssetName(Root/TEXT("TP_Toon"),Package,Name);
    auto* Profile=DuplicateObject<UToonProfile>(PrivateProfile,CreatePackage(*Package),*Name);
    Profile->ClearFlags(RF_Transient);Profile->SetFlags(RF_Public|RF_Standalone);Profile->Guid=FGuid::NewGuid();
    NewAssetName(Root/TEXT("M_Toon"),Package,Name);
    auto* Material=DuplicateObject<UMaterial>(Preview->GetMaterial(),CreatePackage(*Package),*Name);
    Material->ClearFlags(RF_Transient);Material->SetFlags(RF_Public|RF_Standalone);
    if(auto* T=Cast<UMaterialExpressionSubstrateToonBSDF>(Material->GetEditorOnlyData()->FrontMaterial.Expression))T->ToonProfile=Profile;
    Material->PostEditChange();NewAssetName(Root/TEXT("MI_Toon"),Package,Name);
    auto* Instance=DuplicateObject<UMaterialInstanceConstant>(Preview,CreatePackage(*Package),*Name);
    Instance->ClearFlags(RF_Transient);Instance->SetFlags(RF_Public|RF_Standalone);Instance->SetParentEditorOnly(Material);
    Instance->ToonProfileOverride=Profile;Instance->PostEditChange();Instance->EnsureIsComplete();
    const FMaterialResource* Compiled=Instance->GetMaterialResource(GMaxRHIShaderPlatform);
    if(!Compiled)Compiled=Material->GetMaterialResource(GMaxRHIShaderPlatform);
    if(Compiled&&!Compiled->GetCompileErrors().IsEmpty())
    {Error=TEXT("保存前材质编译失败：")+FString::Join(Compiled->GetCompileErrors(),TEXT("\n"));return nullptr;}
    NewAssetName(Root/TEXT("BP_ToonStyle"),Package,Name);
    auto* Bundle=NewObject<UNPRBackendStylePackage>(CreatePackage(*Package),*Name,RF_Public|RF_Standalone);
    Bundle->PackageId=FName(*Name);Bundle->BackendId=TEXT("S58.Toon.v1");Bundle->ShadingModel=Model;
    Bundle->bHasToonParameters=true;Bundle->ToonParameters=V;Bundle->ToonResponse=S;
    auto& Slot=Bundle->MaterialSlots.AddDefaulted_GetRef();Slot.SlotIndex=SlotIndex;Slot.Material=Instance;
    for(const TCHAR* Id:{NPRToonContract::CenterId,NPRToonContract::HalfWidthId,NPRToonContract::SpecAngleId,NPRToonContract::SpecSoftnessId,NPRToonContract::SpecPeakId})
    {
        auto& Q=Bundle->MappingQuality.AddDefaulted_GetRef();Q.SemanticId=Id;Q.Level=ENPRMappingLevel::Approximate;
        Q.Scope=TEXT("S58 原生 Toon LUT");Q.Conditions=FString(Id).StartsWith(TEXT("Spec"))?
            TEXT("各向同性 r=0.5 的 NDF 参考映射；NoV 能量 LUT、矩形光和外层受光另影响结果，未承诺角域全等价。"):
            TEXT("坐标恒等；实际 LUT、阴影与过滤近似，负阈值自影保留。");
        if((FString(Id).StartsWith(TEXT("Spec"))&&!S.bUseArtisticSpecular)||(!FString(Id).StartsWith(TEXT("Spec"))&&!S.bUseArtisticDiffuse))
        {Q.Level=ENPRMappingLevel::Unsupported;Q.Conditions=TEXT("该 Ramp 使用原始高级曲线；保留艺术值但不执行本参数映射。");}
        if(!Cast<UMaterialExpressionSubstrateToonBSDF>(Material->GetEditorOnlyData()->FrontMaterial.Expression))
        {Q.Level=ENPRMappingLevel::Unsupported;Q.Conditions=TEXT("当前表面不是直接 Toon BSDF；保留 Toon 参数供切回模型后使用，当前不执行 Toon Ramp。");}
    }
    if(!Save(Profile,Error)||!Save(Material,Error)||!Save(Instance,Error)||!Save(Bundle,Error))return nullptr;
    return Bundle;
}
