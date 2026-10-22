#include "NPRSurfaceRecipe.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "NPRBackendStylePackage.h"
#include "NPRStyleBindingComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRSurfaceComposerTest, "NPR.Surface.Composer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPRSurfaceComposerTest::RunTest(const FString& Arguments)
{
    auto* Recipe = LoadObject<UNPRSurfaceRecipe>(nullptr,TEXT("/PrincipledNPR/Surface/SR_DefaultLit_Rim.SR_DefaultLit_Rim"));
    auto* Toon = LoadObject<UNPRSurfaceRecipe>(nullptr,TEXT("/PrincipledNPR/Surface/SR_Toon_Rim.SR_Toon_Rim"));
    if (!TestTrue(TEXT("Both saved family recipes load"),Recipe && Toon)) return false;
    FString Error;
    TestTrue(TEXT("DefaultLit recipe validates actual parameter types"),Recipe->Validate(Error));
    TestTrue(TEXT("Toon recipe validates actual parameter types"),Toon->Validate(Error));
    auto* Mesh=NewObject<UStaticMeshComponent>();
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    auto* Original=Mesh->GetMaterial(0);
    UNPRSurfaceRecipe* Selected=nullptr;
    const TArray<UNPRSurfaceRecipe*> Candidates{Toon,Recipe};
    auto Compose=[&](FName Family,const TArray<FName>& Effects,const TArray<int32>& Slots)
    { return UNPRSurfaceComposerLibrary::ComposePackage(Mesh,Family,Candidates,Effects,Slots,Selected,Error); };
    TestNull(TEXT("Unsupported effect rejected"),Compose(TEXT("DefaultLit"),{TEXT("MatCap")},{0}));
    TestNull(TEXT("Unknown family rejected without fallback"),Compose(TEXT("Unknown"),{TEXT("RimLight")},{0}));
    TestNull(TEXT("Duplicate slots rejected"),Compose(TEXT("DefaultLit"),{TEXT("RimLight")},{0,0}));
    TestNull(TEXT("Invalid slot rejected"),Compose(TEXT("DefaultLit"),{TEXT("RimLight")},{5}));
    TestNull(TEXT("Duplicate effects rejected"),Compose(TEXT("DefaultLit"),{TEXT("RimLight"),TEXT("RimLight")},{0}));
    TestTrue(TEXT("Composition never modifies mesh"),Mesh->GetMaterial(0)==Original);
    auto* Package=Compose(TEXT("DefaultLit"),{TEXT("RimLight"),TEXT("AdditionalEmission")},{0});
    if (!TestNotNull(TEXT("Compatible family selected"),Package)) return false;
    TestTrue(TEXT("Explicit family wins over candidate ordering"),Selected==Recipe);
    TestEqual(TEXT("Selection is not rendering certification"),Package->ValidationStatus,FName(TEXT("NotRun")));
    auto* Binding=NewObject<UNPRStyleBindingComponent>();
    if (!TestTrue(TEXT("Existing ApplyPackage accepts composed package"),Binding->ApplyPackage(Mesh,Package,Error))) return false;
    auto* MID=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
    if (!TestNotNull(TEXT("Single slot owns one MID"),MID)) return false;
    const FLinearColor Base(.12f,.24f,.38f,1), Emission(.05f,.1f,.2f,1), Additional(.2f,.05f,.01f,1);
    MID->SetVectorParameterValue(TEXT("NPR_BaseColor"),Base);
    MID->SetVectorParameterValue(TEXT("NPR_EmissiveColor"),Emission);
    MID->SetVectorParameterValue(TEXT("NPR_AdditionalEmissiveColor"),Additional);
    MID->SetScalarParameterValue(TEXT("NPR_NormalStrength"),.6f);
    MID->SetScalarParameterValue(TEXT("NPR_AdditionalEmissiveScale"),.7f);
    UTexture* NormalBefore=nullptr;
    MID->GetTextureParameterValue(FHashedMaterialParameterInfo(FName(TEXT("NPR_NormalTexture"))),NormalBefore);
    FNPRRimParameters Rim; Rim.RimPeakScale=1; Rim.RimColor=FLinearColor::Red;
    TestTrue(TEXT("Frozen production Rim setter works through Surface Master"),Binding->SetRimParameters(Rim,Error));
    TestTrue(TEXT("No second MID created for another effect"),Mesh->GetMaterial(0)==MID);
    for(const auto& Pair:TArray<TPair<FName,FLinearColor>>{{TEXT("NPR_BaseColor"),Base},{TEXT("NPR_EmissiveColor"),Emission},{TEXT("NPR_AdditionalEmissiveColor"),Additional}})
    {
        FLinearColor Read;
        TestTrue(TEXT("Surface vector preserved"),MID->GetVectorParameterValue(FHashedMaterialParameterInfo(Pair.Key),Read)&&Read.Equals(Pair.Value));
    }
    float Value=0;
    MID->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(TEXT("NPR_NormalStrength"))),Value);
    TestEqual(TEXT("Normal strength preserved"),Value,.6f);
    UTexture* NormalAfter=nullptr;
    MID->GetTextureParameterValue(FHashedMaterialParameterInfo(FName(TEXT("NPR_NormalTexture"))),NormalAfter);
    TestTrue(TEXT("Normal texture preserved"),NormalBefore && NormalBefore==NormalAfter);
    TestTrue(TEXT("Second effect dynamically updates on same MID"),Binding->SetScalarParameter(TEXT("NPR_AdditionalEmissiveScale"),1.2f,Error));
    MID->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(NPRRimContract::IntensityMaterialName)),Value);
    TestEqual(TEXT("Second effect does not overwrite Rim"),Value,1.f);
    Binding->RestoreMaterials();
    TestTrue(TEXT("Existing restore semantics retained"),Mesh->GetMaterial(0)==Original);

    auto* Bad=DuplicateObject<UNPRSurfaceRecipe>(Recipe,GetTransientPackage());
    Bad->Effects.Last().Parameters.Add(Bad->Effects[0].Parameters[0]);
    TestFalse(TEXT("Cross-effect parameter ownership conflict rejected"),Bad->Validate(Error));
    Bad=DuplicateObject<UNPRSurfaceRecipe>(Recipe,GetTransientPackage());
    Bad->Effects.Last().Parameters[0].Type=ENPRSurfaceParameterType::Texture;
    TestFalse(TEXT("Incorrect actual parameter type rejected"),Bad->Validate(Error));
    Bad=DuplicateObject<UNPRSurfaceRecipe>(Recipe,GetTransientPackage());
    Bad->Effects[1].Parameters[0].Mapping=ENPRSurfaceParameterMapping::Identity;
    TestFalse(TEXT("Changed Rim angle mapping rejected"),Bad->Validate(Error));
    Bad=DuplicateObject<UNPRSurfaceRecipe>(Recipe,GetTransientPackage());
    Bad->Effects[0].RequiresEffects.Add(TEXT("RimLight"));
    TestFalse(TEXT("Cycle/out-of-order dependency rejected"),Bad->Validate(Error));
    return true;
}
#endif
