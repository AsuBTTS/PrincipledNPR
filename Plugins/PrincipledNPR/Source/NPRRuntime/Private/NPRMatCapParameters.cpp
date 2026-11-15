#include "NPRMatCapParameters.h"

#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

bool FNPRMatCapParameters::Validate(FString& Error) const
{
    Error.Reset();
    const auto Unit = [](float Value) { return FMath::IsFinite(Value) && Value >= 0.f && Value <= 1.f; };
    if (!Unit(Weight) || !Unit(NormalInfluence) || !Unit(Perspective) || !Unit(Tint.R) || !Unit(Tint.G) || !Unit(Tint.B))
    {
        Error = TEXT("MatCap 权重、法线影响、透视修正和染色 RGB 必须为 0–1 的有限值。");
        return false;
    }
    if (bEnabled && !IsValid(Texture))
    {
        Error = TEXT("启用 MatCap 前请选择光泽贴图。");
        return false;
    }
    for (const UTexture2D* Input : {Texture.Get(), Mask.Get()})
    {
        if (Input && (!IsValid(Input) || !Input->SRGB || Input->VirtualTextureStreaming || Input->IsNormalMap()))
        {
            Error = TEXT("MatCap 需要普通 sRGB 颜色贴图。当前版本使用红通道转线性后的值作为遮罩。");
            return false;
        }
    }
    return true;
}

bool FNPRMatCapParameters::IsSupported(const UMaterialInterface* Material)
{
    if (!IsValid(Material)) return false;
    float Scalar = 0.f;
    FLinearColor RGB;
    UTexture* TextureValue = nullptr;
    return Material->GetScalarParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Weight), Scalar)
        && Material->GetScalarParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::NormalInfluence), Scalar)
        && Material->GetScalarParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Perspective), Scalar)
        && Material->GetVectorParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Tint), RGB)
        && Material->GetTextureParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Texture), TextureValue)
        && Material->GetTextureParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Mask), TextureValue);
}

bool FNPRMatCapParameters::Apply(UMaterialInstanceDynamic& Instance, FString& Error) const
{
    if (!Validate(Error)) return false;
    if (!IsSupported(&Instance))
    {
        Error = TEXT("当前材质缺少 MatCap 参数。请在编辑器生成兼容材质。");
        return false;
    }
    const auto SetTexture = [&Instance](FName Name, UTexture2D* Value)
    {
        UTexture* Resolved = Value;
        if (!Resolved)
        {
            // The parent instance can contain a baked override from a combined Toon or Rim material.
            Instance.GetMaterial()->GetTextureParameterDefaultValue(FHashedMaterialParameterInfo(Name), Resolved);
        }
        Instance.SetTextureParameterValue(Name, Resolved);
    };
    SetTexture(NPRMatCapContract::Texture, Texture);
    SetTexture(NPRMatCapContract::Mask, Mask);
    Instance.SetVectorParameterValue(NPRMatCapContract::Tint, FLinearColor(Tint.R, Tint.G, Tint.B));
    Instance.SetScalarParameterValue(NPRMatCapContract::NormalInfluence, NormalInfluence);
    Instance.SetScalarParameterValue(NPRMatCapContract::Perspective, Perspective);
    Instance.SetScalarParameterValue(NPRMatCapContract::Weight, bEnabled ? Weight : 0.f);
    return true;
}
