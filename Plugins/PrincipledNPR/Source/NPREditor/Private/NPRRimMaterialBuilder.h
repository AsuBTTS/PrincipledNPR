#pragma once

#include "CoreMinimal.h"
#include "NPRStyleBindingComponent.h"

class UMaterialInstanceConstant;

/** Copy inherited parameters from editor data, even when the source shader map is unavailable. */
void CopyNPRSurfaceParameters(UMaterialInterface& Source, UMaterialInstanceConstant& Target, bool bIncludeStatic);

/** Adds the existing Rim material function to a copy of the source graph. Never edits the source. */
namespace NPRRimMaterialBuilder
{
    UMaterialInstanceConstant* Build(UMaterialInterface* Source, ENPRRimShadingModel Model, FString& Error);
    bool Save(UMaterialInstanceConstant* Material, FString& Error);
}

namespace NPRMatCapMaterialBuilder
{
    UMaterialInstanceConstant* Build(UMaterialInterface* Source, FString& Error);
    UMaterialInstanceConstant* Bake(UMaterialInstanceConstant* Preview, const FString& Root, FString& Error);
}
