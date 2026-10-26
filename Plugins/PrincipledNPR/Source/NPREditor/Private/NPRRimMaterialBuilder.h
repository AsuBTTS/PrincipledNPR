#pragma once

#include "CoreMinimal.h"
#include "NPRStyleBindingComponent.h"

class UMaterialInstanceConstant;

/** Adds the existing Rim material function to a copy of the source graph. Never edits the source. */
namespace NPRRimMaterialBuilder
{
    UMaterialInstanceConstant* Build(UMaterialInterface* Source, ENPRRimShadingModel Model, FString& Error);
    bool Save(UMaterialInstanceConstant* Material, FString& Error);
}
