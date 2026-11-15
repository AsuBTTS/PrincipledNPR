#pragma once

#include "CoreMinimal.h"
#include "NPRMatCapParameters.h"

class UMaterial;
class UMaterialExpression;

namespace NPRMatCapGraph
{
    using namespace NPRMatCapContract;

    /** Builds native material expressions and returns the linear RGB contribution. */
    UMaterialExpression* Add(UMaterial& Material);

    bool NeedsSubstrateNormalUpgrade(const UMaterial& Material);
    void UpgradeSubstrateNormal(UMaterial& Material);
}
