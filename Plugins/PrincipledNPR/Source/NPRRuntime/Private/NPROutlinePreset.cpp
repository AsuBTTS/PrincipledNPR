#include "NPROutlinePreset.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"

float FNPROutlineSettings::GetEdgeWidthScale() const
{
    if(WidthEdgeSource==ENPROutlineEdgeSource::Silhouette) return EdgeWidthScales.Silhouette;
    if(WidthEdgeSource==ENPROutlineEdgeSource::FeatureBoundary) return EdgeWidthScales.FeatureBoundary;
    return 0.f; // Unsupported provenance must never masquerade as a silhouette.
}

float FNPROutlineSettings::GetResolvedWidth() const
{
    if(Method==ENPROutlineMethod::None) return 0.f;
    return (Method==ENPROutlineMethod::ScreenSpace?WidthPixels:HullWidthCm)*GetEdgeWidthScale()*LocalWidthScale;
}

void FNPROutlineSettings::ApplyMaterialParameters(UMaterialInstanceDynamic* MID) const
{
    if(!MID) return;
    MID->SetVectorParameterValue(TEXT("OutlineColor"),Color);
    MID->SetScalarParameterValue(TEXT("WidthCm"),Method==ENPROutlineMethod::InvertedHull?GetResolvedWidth():0.f);
    MID->SetScalarParameterValue(TEXT("WidthPixels"),Method==ENPROutlineMethod::ScreenSpace?GetResolvedWidth():0.f);
    MID->SetScalarParameterValue(TEXT("DepthBiasCm"),DepthBiasCm);
    MID->SetScalarParameterValue(TEXT("UseVertexColorWidth"),bUseVertexColorWidth?1.f:0.f);
    MID->SetScalarParameterValue(TEXT("UseWidthMask"),bUseWidthMask?1.f:0.f);
    // Reset a previous mask override when switching presets back to uniform width.
    UTexture* Texture=bUseWidthMask?WidthMask.Get():LoadObject<UTexture2D>(nullptr,TEXT("/PrincipledNPR/Outline/T_OutlineWidthMaskWhite.T_OutlineWidthMaskWhite"));
    if(Texture) MID->SetTextureParameterValue(TEXT("WidthMask"),Texture);
}

bool FNPROutlineSettings::Validate(FString& OutError) const
{
    OutError.Reset();
    if (Method != ENPROutlineMethod::None && Method != ENPROutlineMethod::InvertedHull && Method != ENPROutlineMethod::ScreenSpace)
        OutError = TEXT("Unknown outline method.");
    else if (!FMath::IsFinite(WidthPixels) || WidthPixels < 0 || WidthPixels > 8 ||
             !FMath::IsFinite(HullWidthCm) || HullWidthCm < 0 || HullWidthCm > 5 ||
             !FMath::IsFinite(DepthBiasCm) || DepthBiasCm < 0 || DepthBiasCm > 5)
        OutError = TEXT("Outline width/depth values are outside their finite supported ranges.");
    else if (!FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B) ||
             Color.R < 0 || Color.R > 1 || Color.G < 0 || Color.G > 1 || Color.B < 0 || Color.B > 1)
        OutError = TEXT("Outline RGB must be finite linear color in [0,1].");
    else if(WidthEdgeSource!=ENPROutlineEdgeSource::Silhouette && WidthEdgeSource!=ENPROutlineEdgeSource::FeatureBoundary)
        OutError=TEXT("Crease/Occlusion edge classification is not implemented by the Hull/Stencil backends. Use authored regions to control existing contours; this does not generate those edge types.");
    else if(Method==ENPROutlineMethod::InvertedHull && WidthEdgeSource==ENPROutlineEdgeSource::FeatureBoundary)
        OutError=TEXT("Hull cannot generate interior material/feature boundaries. Use Silhouette + local scale/vertex/mask for Hull width, or Screen Space Independent Regions for authored boundary contours.");
    else if(!FMath::IsFinite(EdgeWidthScales.Silhouette) || EdgeWidthScales.Silhouette<0 || EdgeWidthScales.Silhouette>4 ||
            !FMath::IsFinite(EdgeWidthScales.FeatureBoundary) || EdgeWidthScales.FeatureBoundary<0 || EdgeWidthScales.FeatureBoundary>4 ||
            !FMath::IsFinite(LocalWidthScale) || LocalWidthScale<0 || LocalWidthScale>4)
        OutError=TEXT("Edge/local width scales must be finite in [0,4].");
    else if(GetResolvedWidth()>(Method==ENPROutlineMethod::ScreenSpace?8.f:5.f))
        OutError=TEXT("Base width x edge scale x local scale exceeds the supported final width (8 px / 5 cm). Reduce one factor.");
    else if(Method==ENPROutlineMethod::ScreenSpace && (bUseVertexColorWidth || bUseWidthMask))
        OutError=TEXT("Screen Space CustomDepth/Stencil carries no continuous vertex/mask width. Use Material Slot / Section region widths, or Hull for Vertex Color / UV0 Mask.");
    else if(Method==ENPROutlineMethod::InvertedHull && bUseWidthMask && (!IsValid(WidthMask) || WidthMask->SRGB || WidthMask->VirtualTextureStreaming || WidthMask->CompressionSettings!=TC_Masks))
        OutError=TEXT("Width Mask requires a non-virtual Texture2D: Compression=Masks, sRGB=false. It is sampled in UV0 at vertices.");
    return OutError.IsEmpty();
}

bool FNPROutlineSettings::operator==(const FNPROutlineSettings& Other) const
{
    return Method == Other.Method && Color == Other.Color && WidthPixels == Other.WidthPixels &&
        HullWidthCm == Other.HullWidthCm && DepthBiasCm == Other.DepthBiasCm && bUseVertexColorWidth == Other.bUseVertexColorWidth &&
        WidthEdgeSource==Other.WidthEdgeSource && EdgeWidthScales==Other.EdgeWidthScales && LocalWidthScale==Other.LocalWidthScale &&
        bUseWidthMask==Other.bUseWidthMask && WidthMask==Other.WidthMask;
}
