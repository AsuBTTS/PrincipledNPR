#include "NPRMatCapGraph.h"

#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionPixelNormalWS.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"

namespace NPRMatCapGraph
{
namespace
{
class FNodes
{
public:
    explicit FNodes(UMaterial& InMaterial) : Material(InMaterial) {}

    template<class T>
    T* Create(int32 Column, const TCHAR* Description = TEXT(""))
    {
        auto* Node = NewObject<T>(&Material, NAME_None, RF_Transactional);
        Node->Material = &Material;
        Node->Desc = Description;
        Node->MaterialExpressionEditorX = -2560 + 240 * Column;
        Node->MaterialExpressionEditorY = 640 + 185 * Rows[Column]++;
        Material.GetExpressionCollection().AddExpression(Node);
        return Node;
    }

    template<class T>
    T* Binary(int32 Column, UMaterialExpression* A, UMaterialExpression* B)
    {
        auto* Node = Create<T>(Column);
        Node->A.Connect(0, A);
        Node->B.Connect(0, B);
        return Node;
    }

    UMaterialExpression* Channels(int32 Column, UMaterialExpression* Source, bool R, bool G, bool B)
    {
        auto* Node = Create<UMaterialExpressionComponentMask>(Column);
        Node->Input.Connect(0, Source);
        Node->R = R;
        Node->G = G;
        Node->B = B;
        Node->A = false;
        return Node;
    }

    UMaterialExpression* Scalar(const TCHAR* Name, float Default)
    {
        auto* Node = Create<UMaterialExpressionScalarParameter>(0);
        Node->Group = TEXT("MatCap");
        Node->ParameterName = Name;
        Node->DefaultValue = Default;
        Node->SliderMin = 0;
        Node->SliderMax = 1;
        return Node;
    }

    UMaterialExpression* ToCamera(int32 Column, UMaterialExpression* Source)
    {
        auto* Node = Create<UMaterialExpressionTransform>(Column);
        Node->Input.Connect(0, Source);
        Node->TransformSourceType = TRANSFORMSOURCE_World;
        Node->TransformType = TRANSFORM_Camera;
        return Node;
    }

    UMaterialExpressionLinearInterpolate* Lerp(int32 Column, UMaterialExpression* A, UMaterialExpression* B, UMaterialExpression* Alpha)
    {
        auto* Node = Create<UMaterialExpressionLinearInterpolate>(Column);
        Node->A.Connect(0, A);
        Node->B.Connect(0, B);
        Node->Alpha.Connect(0, Alpha);
        return Node;
    }

    UMaterialExpression* Sample(int32 Column, const TCHAR* Name, UTexture2D* Default, UMaterialExpression* UV)
    {
        auto* Node = Create<UMaterialExpressionTextureSampleParameter2D>(Column);
        Node->Group = TEXT("MatCap");
        Node->ParameterName = Name;
        Node->Texture = Default;
        Node->SamplerType = SAMPLERTYPE_Color;
        Node->SamplerSource = SSM_Clamp_WorldGroupSettings;
        if (UV)
        {
            Node->Coordinates.Connect(0, UV);
        }
        return Node;
    }

private:
    UMaterial& Material;
    int32 Rows[12] = {};
};

const FExpressionInput* GetSubstrateNormal(const UMaterial& Material)
{
    const auto* Root = Material.GetEditorOnlyData()->FrontMaterial.Expression;
    if (const auto* Toon = Cast<UMaterialExpressionSubstrateToonBSDF>(Root)) return &Toon->Normal;
    if (const auto* Slab = Cast<UMaterialExpressionSubstrateSlabBSDF>(Root)) return &Slab->Normal;
    if (const auto* Models = Cast<UMaterialExpressionSubstrateShadingModels>(Root)) return &Models->Normal;
    return nullptr;
}

UMaterialExpressionLinearInterpolate* FindLegacyNormalBlend(const UMaterial& Material)
{
    if (!GetSubstrateNormal(Material)) return nullptr;
    for (UMaterialExpression* Expression : Material.GetExpressions())
    {
        auto* Blend = Cast<UMaterialExpressionLinearInterpolate>(Expression);
        if (!Blend || !Blend->A.Expression || !Blend->A.Expression->IsA<UMaterialExpressionVertexNormalWS>() ||
            !Blend->B.Expression || !Blend->B.Expression->IsA<UMaterialExpressionPixelNormalWS>()) continue;
        const auto* Influence = Cast<UMaterialExpressionScalarParameter>(Blend->Alpha.Expression);
        if (Influence && Influence->ParameterName == NormalInfluence) return Blend;
    }
    return nullptr;
}

void ConnectSubstrateNormal(FNodes& Nodes, const UMaterial& Material, const FExpressionInput& Normal,
    UMaterialExpressionLinearInterpolate& Blend)
{
    if (Normal.GetTracedInput().Expression)
    {
        auto* ToWorld = Nodes.Create<UMaterialExpressionTransform>(0);
        ToWorld->Input = Normal;
        ToWorld->TransformSourceType = Material.bTangentSpaceNormal ? TRANSFORMSOURCE_Tangent : TRANSFORMSOURCE_World;
        ToWorld->TransformType = TRANSFORM_World;
        ToWorld->MaterialExpressionEditorY = Blend.MaterialExpressionEditorY - 185;
        Blend.B.Connect(0, ToWorld);
    }
    else
    {
        Blend.B = Blend.A;
    }
}
}

bool NeedsSubstrateNormalUpgrade(const UMaterial& Material)
{
    return FindLegacyNormalBlend(Material) != nullptr;
}

void UpgradeSubstrateNormal(UMaterial& Material)
{
    if (auto* Blend = FindLegacyNormalBlend(Material))
    {
        FNodes Nodes(Material);
        ConnectSubstrateNormal(Nodes, Material, *GetSubstrateNormal(Material), *Blend);
    }
}

UMaterialExpression* Add(UMaterial& Material)
{
    FNodes Nodes(Material);
    auto* White = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
    auto* Black = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
    if (!White || !Black)
    {
        return nullptr;
    }

    auto* SmoothNormal = Nodes.Create<UMaterialExpressionVertexNormalWS>(0);
    auto* NormalBlend = Nodes.Lerp(1, SmoothNormal, SmoothNormal, Nodes.Scalar(NormalInfluence, 1));
    if (const auto* SubstrateNormal = GetSubstrateNormal(Material))
    {
        // A Substrate BSDF owns its normal. PixelNormalWS does not read the BSDF input.
        ConnectSubstrateNormal(Nodes, Material, *SubstrateNormal, *NormalBlend);
    }
    else
    {
        NormalBlend->B.Connect(0, Nodes.Create<UMaterialExpressionPixelNormalWS>(0));
    }
    auto* Normalize = Nodes.Create<UMaterialExpressionNormalize>(2);
    Normalize->VectorInput.Connect(0, NormalBlend);
    auto* N = Nodes.ToCamera(3, Normalize);
    auto* NormalXY = Nodes.Channels(4, N, true, true, false);
    auto* NormalZ = Nodes.Channels(4, N, false, false, true);

    // Minimal rotation from the camera's forward axis to the per-pixel view ray.
    auto* Ray = Nodes.Create<UMaterialExpressionMultiply>(3);
    Ray->A.Connect(0, Nodes.ToCamera(2, Nodes.Create<UMaterialExpressionCameraVectorWS>(1)));
    Ray->ConstB = -1.f;
    auto* RayXY = Nodes.Channels(4, Ray, true, true, false);
    auto* RayZ = Nodes.Channels(4, Ray, false, false, true);
    auto* Facing = Nodes.Binary<UMaterialExpressionDotProduct>(4, N, Ray);
    auto* Numerator = Nodes.Binary<UMaterialExpressionAdd>(5, Facing, NormalZ);
    auto* RayScale = Nodes.Create<UMaterialExpressionAdd>(5);
    RayScale->A.Connect(0, RayZ);
    RayScale->ConstB = 1.f;
    auto* Denominator = Nodes.Create<UMaterialExpressionMax>(6);
    Denominator->A.Connect(0, RayScale);
    Denominator->ConstB = .001f;
    auto* Factor = Nodes.Binary<UMaterialExpressionDivide>(7, Numerator, Denominator);
    auto* Correction = Nodes.Binary<UMaterialExpressionMultiply>(8, RayXY, Factor);
    auto* Corrected = Nodes.Binary<UMaterialExpressionSubtract>(9, NormalXY, Correction);
    auto* Projected = Nodes.Lerp(9, NormalXY, Corrected, Nodes.Scalar(Perspective, 0));

    auto* SignedScale = Nodes.Create<UMaterialExpressionConstant2Vector>(8);
    SignedScale->R = .5f;
    SignedScale->G = -.5f;
    auto* UV = Nodes.Create<UMaterialExpressionAdd>(10, TEXT("相机右方对应 U 增大，上方对应 V 减小"));
    UV->A.Connect(0, Nodes.Binary<UMaterialExpressionMultiply>(9, Projected, SignedScale));
    UV->ConstB = .5f;

    auto* Color = Nodes.Channels(10, Nodes.Sample(10, Texture, Black, UV), true, true, true);
    auto* MaskRed = Nodes.Channels(10, Nodes.Sample(9, Mask, White, nullptr), true, false, false);
    auto* TintValue = Nodes.Create<UMaterialExpressionVectorParameter>(0);
    TintValue->ParameterName = Tint;
    TintValue->Group = TEXT("MatCap");
    TintValue->DefaultValue = FLinearColor::White;
    auto* TintRGB = Nodes.Channels(1, TintValue, true, true, true);
    auto* Colored = Nodes.Binary<UMaterialExpressionMultiply>(11, Color, TintRGB);
    auto* Blend = Nodes.Binary<UMaterialExpressionMultiply>(11, MaskRed, Nodes.Scalar(Weight, 0));
    return Nodes.Binary<UMaterialExpressionMultiply>(11, Colored, Blend);
}
}
