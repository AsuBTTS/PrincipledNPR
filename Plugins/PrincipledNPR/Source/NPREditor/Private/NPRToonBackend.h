#pragma once

#include "CoreMinimal.h"
#include "NPRToonParameters.h"

class UMaterialInterface;
class UMaterialInstanceConstant;
class UToonProfile;
class UNPRBackendStylePackage;

/** S58 Editor adapter. Preview objects are private; saved resources are immutable at runtime. */
namespace NPRToonBackend
{
    inline constexpr int32 AdapterVersion = 1;
    inline constexpr float SpecularReferenceRoughness = 0.5f;

    bool UpdateProfile(UToonProfile* PrivateProfile,const FNPRToonParameters& Parameters,
        const FNPRToonResponseSettings& Response,FString& OutError);

    /** Never changes Source; keeps its input graph, parameters, WPO, alpha mask and emission. */
    UMaterialInstanceConstant* BuildPreview(UMaterialInterface* Source,ENPRShadingModel Model,
        const FNPRToonParameters& Parameters,const FNPRToonResponseSettings& Response,
        UToonProfile*& OutPrivateProfile,FString& OutError,UToonProfile* TemplateOverride=nullptr);

    /** Unique asset bundle; no existing resource is overwritten. */
    UNPRBackendStylePackage* Bake(UMaterialInstanceConstant* Preview,UToonProfile* PrivateProfile,
        ENPRShadingModel Model,const FNPRToonParameters& Parameters,const FNPRToonResponseSettings& Response,
        int32 SlotIndex,const FString& AssetRoot,FString& OutError);
}
