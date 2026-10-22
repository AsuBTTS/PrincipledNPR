#include "NPRSurfaceRecipe.h"

#include "Components/MeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "NPRBackendStylePackage.h"
#include "NPRRimParameters.h"

bool UNPRSurfaceRecipe::Validate(FString& OutError) const
{
    OutError.Reset();
    auto Fail = [&](const FString& Error) { OutError = Error; return false; };
    if (RecipeVersion != 1 || RecipeId.IsNone() || FamilyId.IsNone() || !IsValid(Material) ||
        Material->GetMaterial()->MaterialDomain != MD_Surface || Effects.IsEmpty())
        return Fail(TEXT("A v1 recipe, identity, loaded Surface material and effect definitions are required."));

    TSet<FName> EffectIds, ParameterNames, SemanticIds;
    int32 SurfaceInputCount = 0;
    for (const auto& Effect : Effects)
    {
        if (Effect.EffectId.IsNone() || Effect.OutputContract.IsNone() || EffectIds.Contains(Effect.EffectId))
            return Fail(TEXT("Effect identities and output contracts must be nonempty; duplicate effects are forbidden."));
        if (Effect.Composition == ENPRSurfaceComposition::SurfaceInputs) ++SurfaceInputCount;
        else if (Effect.Composition != ENPRSurfaceComposition::AdditiveEmissive && Effect.Composition != ENPRSurfaceComposition::NativeLighting)
            return Fail(TEXT("Unknown Surface composition rule."));
        EffectIds.Add(Effect.EffectId);
        for (const auto& Binding : Effect.Parameters)
        {
            if (Binding.MaterialParameter.IsNone() || ParameterNames.Contains(Binding.MaterialParameter) ||
                (!Binding.SemanticId.IsNone() && SemanticIds.Contains(Binding.SemanticId)))
                return Fail(FString::Printf(TEXT("Parameter '%s' has missing or conflicting ownership."), *Binding.MaterialParameter.ToString()));
            if (Binding.Mapping != ENPRSurfaceParameterMapping::Identity &&
                (Binding.Mapping != ENPRSurfaceParameterMapping::RadiansToDegrees || Binding.Type != ENPRSurfaceParameterType::Scalar))
                return Fail(TEXT("Unsupported parameter mapping/type combination."));
            ParameterNames.Add(Binding.MaterialParameter);
            if (!Binding.SemanticId.IsNone()) SemanticIds.Add(Binding.SemanticId);
            const FHashedMaterialParameterInfo Info(Binding.MaterialParameter);
            float Scalar = 0.f; FLinearColor Vector; UTexture* Texture = nullptr;
            const bool Present = Binding.Type == ENPRSurfaceParameterType::Scalar ? Material->GetScalarParameterValue(Info, Scalar) :
                Binding.Type == ENPRSurfaceParameterType::Vector ? Material->GetVectorParameterValue(Info, Vector) :
                Binding.Type == ENPRSurfaceParameterType::Texture && Material->GetTextureParameterValue(Info, Texture);
            if (!Present) return Fail(FString::Printf(TEXT("Effect '%s': material parameter '%s' is absent or has the wrong type."),
                *Effect.EffectId.ToString(), *Binding.MaterialParameter.ToString()));
        }
        // Existing SetRimParameters is the authoritative writer: recipes may not silently remap its frozen v1 contract.
        if (Effect.EffectId == TEXT("RimLight"))
        {
            const FName Ids[] = {NPRRimContract::WidthId, NPRRimContract::SoftnessId, NPRRimContract::IntensityId, NPRRimContract::ColorId};
            const FName Names[] = {NPRRimContract::WidthMaterialName, NPRRimContract::SoftnessMaterialName,
                NPRRimContract::IntensityMaterialName, NPRRimContract::ColorMaterialName};
            if (Effect.Composition != ENPRSurfaceComposition::AdditiveEmissive || Effect.OutputContract != TEXT("RimColorContribution") || Effect.Parameters.Num() != 4)
                return Fail(TEXT("RimLight must retain the four-control additive v1 output contract."));
            for (int32 I = 0; I < 4; ++I)
            {
                const auto* B = Effect.Parameters.FindByPredicate([&](const auto& P) { return P.SemanticId == Ids[I]; });
                if (!B || B->MaterialParameter != Names[I] ||
                    B->Type != (I == 3 ? ENPRSurfaceParameterType::Vector : ENPRSurfaceParameterType::Scalar) ||
                    B->Mapping != (I < 2 ? ENPRSurfaceParameterMapping::RadiansToDegrees : ENPRSurfaceParameterMapping::Identity))
                    return Fail(TEXT("RimLight recipe conflicts with NPRRimContract names, types or angle units."));
            }
        }
    }
    if (SurfaceInputCount != 1 || !EffectIds.Contains(TEXT("BaseSurface")))
        return Fail(TEXT("Exactly one SurfaceInputs producer named BaseSurface is required."));
    const auto* Base = Effects.FindByPredicate([](const auto& E) { return E.EffectId == TEXT("BaseSurface"); });
    if (Base->Composition != ENPRSurfaceComposition::SurfaceInputs) return Fail(TEXT("BaseSurface must own SurfaceInputs."));
    for (const auto& Effect : Effects)
        for (FName Dependency : Effect.RequiresEffects)
            if (Dependency == Effect.EffectId || !EffectIds.Contains(Dependency))
                return Fail(TEXT("Missing or self-referencing effect dependency."));
    // The recipe defines a fixed evaluation order: dependencies must precede their consumer (also rejects cycles).
    TSet<FName> Available;
    for (const auto& Effect : Effects)
    {
        for (FName Dependency : Effect.RequiresEffects)
            if (!Available.Contains(Dependency)) return Fail(TEXT("Recipe effects must be stored in dependency order."));
        Available.Add(Effect.EffectId);
    }
    return true;
}

UNPRBackendStylePackage* UNPRSurfaceComposerLibrary::ComposePackage(UMeshComponent* Mesh, FName FamilyId,
    const TArray<UNPRSurfaceRecipe*>& Candidates, const TArray<FName>& RequestedEffects,
    const TArray<int32>& TargetSlots, UNPRSurfaceRecipe*& SelectedRecipe, FString& OutError)
{
    SelectedRecipe = nullptr; OutError.Reset();
    if (!IsValid(Mesh) || FamilyId.IsNone() || TargetSlots.IsEmpty())
    { OutError = TEXT("A mesh, explicit Surface family and nonempty target slots are required."); return nullptr; }
    TSet<int32> Slots;
    for (int32 Slot : TargetSlots)
    {
        if (Slot < 0 || Slot >= Mesh->GetNumMaterials() || Slots.Contains(Slot))
        { OutError = TEXT("Invalid or duplicate Surface target slot; no mesh materials changed."); return nullptr; }
        Slots.Add(Slot);
    }
    TSet<FName> Requested;
    for (FName Effect : RequestedEffects)
    {
        if (Effect.IsNone() || Requested.Contains(Effect))
        { OutError = TEXT("Requested effects must have unique, nonempty identities."); return nullptr; }
        Requested.Add(Effect);
    }
    for (UNPRSurfaceRecipe* Recipe : Candidates)
    {
        if (!IsValid(Recipe) || Recipe->FamilyId != FamilyId) continue;
        FString Error;
        if (!Recipe->Validate(Error)) { OutError += Recipe->GetName() + TEXT(": ") + Error + TEXT("\n"); continue; }
        bool Supported = true;
        for (FName Effect : Requested)
            if (!Recipe->Effects.ContainsByPredicate([&](const auto& E) { return E.EffectId == Effect; }))
            { OutError += FString::Printf(TEXT("%s lacks requested effect %s.\n"), *Recipe->RecipeId.ToString(), *Effect.ToString()); Supported = false; }
        if (!Supported) continue;
        auto* Package = NewObject<UNPRBackendStylePackage>(Mesh);
        Package->PackageId = Recipe->RecipeId;
        Package->BackendId = Recipe->FamilyId;
        // Runtime selection checks contracts, not rendering acceptance. Keep inherited NotRun status.
        for (int32 Slot : TargetSlots)
        {
            FNPRBackendMaterialSlot Entry; Entry.SlotIndex = Slot; Entry.Material = Recipe->Material;
            Package->MaterialSlots.Add(Entry);
        }
        SelectedRecipe = Recipe; OutError.Reset(); return Package;
    }
    OutError = TEXT("No compatible prebuilt Surface recipe in requested family.\n") + OutError;
    return nullptr;
}
