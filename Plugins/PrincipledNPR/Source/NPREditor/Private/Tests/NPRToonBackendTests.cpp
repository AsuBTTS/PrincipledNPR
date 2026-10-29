#include "NPRToonBackend.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AssetCompilingManager.h"
#include "Engine/ToonProfile.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "MaterialShared.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "NPRBackendStylePackage.h"
#include "RHI.h"
#include "ShaderCompiler.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "GameFramework/PlayerStart.h"
#include "NPRStyleBindingComponent.h"
#include "AssetToolsModule.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRToonAdapterTest,"NPR.Toon.Adapter",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FNPRToonAdapterTest::RunTest(const FString&)
{
    auto* Source=LoadObject<UMaterialInterface>(nullptr,TEXT("/PrincipledNPR/Surface/M_NPRSurface_Toon.M_NPRSurface_Toon"));
    if(!TestNotNull(TEXT("Existing Surface Toon source loads"),Source))return false;
    auto* SourceRoot=Cast<UMaterialExpressionSubstrateToonBSDF>(Source->GetMaterial()->GetEditorOnlyData()->FrontMaterial.Expression);
    if(!TestNotNull(TEXT("Source actually owns native Toon BSDF"),SourceRoot))return false;
    UToonProfile* SourceProfile=SourceRoot->ToonProfile;
    const FGuid SourceGuid=SourceProfile->Guid;const FLinearColor Original=SourceProfile->Settings.DiffuseRamp.GetLinearColorValue(.6f);
    FNPRToonParameters P;P.TerminatorDotCenter=.2;P.TerminatorDotHalfWidth=.08;
    FNPRToonResponseSettings S;S.bUseArtisticSpecular=false;
    FString Error;UToonProfile* Profile=nullptr;
    TStrongObjectPtr<UMaterialInstanceConstant> Preview(NPRToonBackend::BuildPreview(Source,ENPRShadingModel::SubstrateToon,P,S,Profile,Error));
    if(!TestNotNull(*FString(TEXT("Preview builds: ")+Error),Preview.Get()))return false;
    TestTrue(TEXT("Preview and source profiles are independent"),Profile&&Profile!=SourceProfile&&Profile->Guid!=SourceGuid);
    TestTrue(TEXT("Only private transient resource edited"),Profile->HasAnyFlags(RF_Transient));
    TestFalse(TEXT("Production profile mutation rejected"),NPRToonBackend::UpdateProfile(SourceProfile,P,S,Error));
    for(auto Shape:{ENPRToonResponseShape::Linear,ENPRToonResponseShape::Hermite})
    {
        S.Shape=Shape;TestTrue(TEXT("Private curve update"),NPRToonBackend::UpdateProfile(Profile,P,S,Error));
        double MaxError=0;
        for(int32 I=0;I<=2000;++I)
        {
            const double Q=I/1000.0-1.0;
            const double Expected=.1+.9*P.EvaluateDiffuse(Q,Shape);
            MaxError=FMath::Max(MaxError,FMath::Abs(double(Profile->Settings.DiffuseRamp.GetLinearColorValue(float((Q+1)*.5)).R)-Expected));
        }
        TestTrue(TEXT("Generated curve agrees with canonical reference throughout signed domain"),MaxError<1.e-5);
        TestTrue(TEXT("Exact cubic/linear generator avoids 257-key update overhead"),Profile->Settings.DiffuseRamp.ColorCurves[0].GetNumKeys()<=5);
    }
    P.TerminatorDotCenter=.95;P.TerminatorDotHalfWidth=.2;S.Shape=ENPRToonResponseShape::Linear;
    TestTrue(TEXT("Clipped transition updates"),NPRToonBackend::UpdateProfile(Profile,P,S,Error));
    TestTrue(TEXT("Clipped upper endpoint preserves 0.6625 response"),FMath::IsNearlyEqual(Profile->Settings.DiffuseRamp.GetLinearColorValue(1).R,.6625f,1.e-6f));
    P.TerminatorDotCenter=.2;P.TerminatorDotHalfWidth=0;
    TestTrue(TEXT("Zero width creates finite curve"),NPRToonBackend::UpdateProfile(Profile,P,S,Error));
    TestTrue(TEXT("Hard lower plateau"),FMath::IsNearlyEqual(Profile->Settings.DiffuseRamp.GetLinearColorValue(.55f).R,.1f,1.e-6f));
    TestTrue(TEXT("Hard upper plateau"),FMath::IsNearlyEqual(Profile->Settings.DiffuseRamp.GetLinearColorValue(.65f).R,1.f,1.e-6f));
    S.bUseArtisticDiffuse=false;Profile->Settings.DiffuseIndirectScale=.37f;
    const auto Before=Profile->Settings.DiffuseRamp.GetLinearColorValue(.6f);
    P.TerminatorDotCenter=.7;NPRToonBackend::UpdateProfile(Profile,P,S,Error);
    TestTrue(TEXT("Raw diffuse curve retained when generation disabled"),Profile->Settings.DiffuseRamp.GetLinearColorValue(.6f).Equals(Before));
    TestEqual(TEXT("Advanced backend control retained"),Profile->Settings.DiffuseIndirectScale,.37f);
    TestTrue(TEXT("Source profile GUID unchanged"),SourceProfile->Guid==SourceGuid);
    TestTrue(TEXT("Source response unchanged"),SourceProfile->Settings.DiffuseRamp.GetLinearColorValue(.6f).Equals(Original));
    UToonProfile* LitProfile=nullptr;
    TStrongObjectPtr<UMaterialInstanceConstant> Lit(NPRToonBackend::BuildPreview(Source,ENPRShadingModel::DefaultLit,P,S,LitProfile,Error));
    if(!TestNotNull(TEXT("Model is a working discrete backend selection"),Lit.Get()))return false;
    TestTrue(TEXT("DefaultLit selection changes BSDF, not a cosmetic enum"),Cast<UMaterialExpressionSubstrateShadingModels>(Lit->GetMaterial()->GetEditorOnlyData()->FrontMaterial.Expression)!=nullptr);
    TestTrue(TEXT("Model switch preserves source"),Source->GetMaterial()->GetEditorOnlyData()->FrontMaterial.Expression==SourceRoot);

    S.bUseArtisticSpecular=true;UToonProfile* ArtisticProfile=nullptr;
    TStrongObjectPtr<UMaterialInstanceConstant> Artistic(NPRToonBackend::BuildPreview(Source,ENPRShadingModel::SubstrateToon,P,S,ArtisticProfile,Error));
    if(!TestNotNull(TEXT("Angular reference lobe builds"),Artistic.Get()))return false;
    S.bUseArtisticSpecular=false;UToonProfile* RestoredProfile=nullptr;
    TStrongObjectPtr<UMaterialInstanceConstant> Restored(NPRToonBackend::BuildPreview(Artistic.Get(),ENPRShadingModel::SubstrateToon,P,S,RestoredProfile,Error));
    if(!TestNotNull(TEXT("Disabling angular authoring restores native source path"),Restored.Get()))return false;
    auto* RestoredRoot=CastChecked<UMaterialExpressionSubstrateToonBSDF>(Restored->GetMaterial()->GetEditorOnlyData()->FrontMaterial.Expression);
    auto* OriginalInput=Cast<UMaterialExpressionScalarParameter>(RestoredRoot->Roughness.Expression);
    TestTrue(TEXT("Original NPR_Roughness network survives a saved/derived reference-lobe source"),OriginalInput&&OriginalInput->ParameterName==TEXT("NPR_Roughness"));

    auto* Legacy=NewObject<UMaterial>();auto* LegacyData=Legacy->GetEditorOnlyData();
    LegacyData->Roughness.UseConstant=true;LegacyData->Roughness.Constant=.8f;
    LegacyData->Normal.UseConstant=true;LegacyData->Normal.Constant=FVector3f(0,1,0);
    LegacyData->EmissiveColor.UseConstant=true;LegacyData->EmissiveColor.Constant=FLinearColor(.2f,.3f,.4f);
    Legacy->PostEditChange();UToonProfile* LegacyProfile=nullptr;
    TStrongObjectPtr<UMaterialInstanceConstant> LegacyCopy(NPRToonBackend::BuildPreview(Legacy,ENPRShadingModel::SubstrateToon,P,S,LegacyProfile,Error));
    if(!TestNotNull(TEXT("Legacy inline inputs convert without invented defaults"),LegacyCopy.Get()))return false;
    auto* LegacyRoot=CastChecked<UMaterialExpressionSubstrateToonBSDF>(LegacyCopy->GetMaterial()->GetEditorOnlyData()->FrontMaterial.Expression);
    auto* BaseConstant=Cast<UMaterialExpressionConstant3Vector>(LegacyRoot->BaseColor.Expression);
    const auto EngineBase=FMaterialAttributeDefinitionMap::GetDefaultValue(MP_BaseColor);
    TestTrue(TEXT("Legacy unconnected base color uses the engine attribute default, not the Toon pin default"),BaseConstant&&BaseConstant->Constant.Equals(FLinearColor(EngineBase.X,EngineBase.Y,EngineBase.Z)));
    auto* RoughConstant=Cast<UMaterialExpressionConstant>(LegacyRoot->Roughness.Expression);
    TestTrue(TEXT("Inline roughness is preserved"),RoughConstant&&RoughConstant->R==.8f);
    auto* NormalConstant=Cast<UMaterialExpressionConstant3Vector>(LegacyRoot->Normal.Expression);
    TestTrue(TEXT("Inline normal is preserved"),NormalConstant&&NormalConstant->Constant.Equals(FLinearColor(0,1,0)));
    auto* EmissionConstant=Cast<UMaterialExpressionConstant3Vector>(LegacyRoot->EmissiveColor.Expression);
    TestTrue(TEXT("Inline emission is preserved"),EmissionConstant&&EmissionConstant->Constant.Equals(FLinearColor(.2f,.3f,.4f)));

    auto* InheritedSource=NewObject<UMaterialInstanceConstant>();InheritedSource->SetParentEditorOnly(Legacy);
    InheritedSource->BasePropertyOverrides.bOverride_ShadingModel=true;InheritedSource->BasePropertyOverrides.ShadingModel=MSM_Subsurface;
    InheritedSource->PostEditChange();UToonProfile* InheritedProfile=nullptr;
    TStrongObjectPtr<UMaterialInstanceConstant> Inherited(NPRToonBackend::BuildPreview(InheritedSource,ENPRShadingModel::Inherit,P,S,InheritedProfile,Error));
    if(!TestNotNull(TEXT("Inherit preserves a source instance model override"),Inherited.Get()))return false;
    TestTrue(TEXT("Inherited Subsurface override remains active"),Inherited->BasePropertyOverrides.bOverride_ShadingModel&&Inherited->BasePropertyOverrides.ShadingModel==MSM_Subsurface);
    TestTrue(TEXT("Inherit retains legacy graph and inline values"),!Inherited->GetMaterial()->GetEditorOnlyData()->FrontMaterial.Expression&&Inherited->GetMaterial()->GetEditorOnlyData()->Roughness.Constant==.8f);

    auto* DirectSource=DuplicateObject<UMaterial>(Lit->GetMaterial(),GetTransientPackage());
    auto* DirectRoot=CastChecked<UMaterialExpressionSubstrateShadingModels>(DirectSource->GetEditorOnlyData()->FrontMaterial.Expression);
    DirectRoot->ShadingModelOverride=MSM_Subsurface;
    auto* SubsurfaceColor=NewObject<UMaterialExpressionConstant3Vector>(DirectSource);SubsurfaceColor->Material=DirectSource;SubsurfaceColor->Constant=FLinearColor(.8f,.2f,.1f);
    DirectSource->GetExpressionCollection().AddExpression(SubsurfaceColor);DirectRoot->SubSurfaceColor.Connect(0,SubsurfaceColor);DirectSource->PostEditChange();
    TStrongObjectPtr<UMaterialInstanceConstant> DirectInherited(NPRToonBackend::BuildPreview(DirectSource,ENPRShadingModel::Inherit,P,S,InheritedProfile,Error));
    if(!TestNotNull(TEXT("Inherit retains a direct Substrate model"),DirectInherited.Get()))return false;
    const auto* PreservedRoot=CastChecked<UMaterialExpressionSubstrateShadingModels>(DirectInherited->GetMaterial()->GetEditorOnlyData()->FrontMaterial.Expression);
    TestTrue(TEXT("Inherit preserves Subsurface model and its model-specific input"),PreservedRoot->ShadingModelOverride==MSM_Subsurface&&PreservedRoot->SubSurfaceColor.Expression!=nullptr);

    S.bUseArtisticDiffuse=true;S.bUseArtisticSpecular=true;P.TerminatorDotCenter=.2;P.TerminatorDotHalfWidth=.08;
    UToonProfile* RuntimeProfile=nullptr;
    TStrongObjectPtr<UMaterialInstanceConstant> RuntimePreview(NPRToonBackend::BuildPreview(Source,ENPRShadingModel::SubstrateToon,P,S,RuntimeProfile,Error,Profile));
    if(!TestNotNull(TEXT("All-five-parameter runtime candidate builds"),RuntimePreview.Get()))return false;
    auto* Bundle=NPRToonBackend::Bake(RuntimePreview.Get(),RuntimeProfile,ENPRShadingModel::SubstrateToon,P,S,0,TEXT("/Game/NPR/ToonValidation"),Error);
    if(!TestNotNull(*FString(TEXT("Immutable bundle bakes: ")+Error),Bundle))return false;
    TestTrue(TEXT("Baked material is persistent"),Bundle->MaterialSlots[0].Material&&!Bundle->MaterialSlots[0].Material->HasAnyFlags(RF_Transient));
    auto* SavedInstance=Cast<UMaterialInstanceConstant>(Bundle->MaterialSlots[0].Material);
    TestTrue(TEXT("Baked profile detached from mutable preview"),SavedInstance&&SavedInstance->ToonProfileOverride!=RuntimeProfile);
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("ToonValidation");IFileManager::Get().MakeDirectory(*Dir,true);
    TestTrue(TEXT("Persist package identity for fresh-process and runtime checks"),FFileHelper::SaveStringToFile(Bundle->GetPathName(),*(Dir/TEXT("LatestPackage.txt"))));
    UToonProfile* RuntimeLitProfile=nullptr;
    TStrongObjectPtr<UMaterialInstanceConstant> RuntimeLit(NPRToonBackend::BuildPreview(Source,ENPRShadingModel::DefaultLit,P,S,RuntimeLitProfile,Error));
    auto* LitBundle=RuntimeLit.IsValid()?NPRToonBackend::Bake(RuntimeLit.Get(),RuntimeLitProfile,ENPRShadingModel::DefaultLit,P,S,0,TEXT("/Game/NPR/ToonValidation/DefaultLit"),Error):nullptr;
    if(!TestNotNull(TEXT("Alternate DefaultLit model bakes for runtime switching"),LitBundle))return false;
    TestTrue(TEXT("Inactive Toon parameters have explicit mapping diagnostics"),LitBundle->MappingQuality.ContainsByPredicate([](const auto& Q){return Q.Level==ENPRMappingLevel::Unsupported;}));
    TestTrue(TEXT("Persist alternate model identity"),FFileHelper::SaveStringToFile(LitBundle->GetPathName(),*(Dir/TEXT("LatestLitPackage.txt"))));
    AddInfo(FString::Printf(TEXT("Baked package %s"),*Bundle->GetPathName()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRToonPersistenceTest,"NPR.Toon.Persistence",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FNPRToonPersistenceTest::RunTest(const FString&)
{
    FString Asset;FFileHelper::LoadFileToString(Asset,*(FPaths::ProjectSavedDir()/TEXT("ToonValidation/LatestPackage.txt")));
    auto* Bundle=LoadObject<UNPRBackendStylePackage>(nullptr,*Asset);
    if(!TestNotNull(TEXT("Baked style available for saved-map test"),Bundle))return false;
    FString PackageName,Name;FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(TEXT("/Game/NPR/ToonValidation/L_ToonRuntime"),TEXT(""),PackageName,Name);
    UPackage* Package=CreatePackage(*PackageName);Package->SetPackageFlags(PKG_ContainsMap);
    UWorld* World=UWorld::CreateWorld(EWorldType::Editor,false,FName(*Name),Package);World->SetFlags(RF_Public|RF_Standalone);
    FActorSpawnParameters Spawn;Spawn.Name=TEXT("ToonRuntimeActor");
    auto* Actor=World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
    auto* Mesh=Actor->GetStaticMeshComponent();Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    auto* B=NewObject<UNPRStyleBindingComponent>(Actor,TEXT("ToonRuntimeBinding"),RF_Transactional);
    Actor->AddInstanceComponent(B);B->ToonMesh=Mesh;B->ToonPackage=Bundle;B->RegisterComponent();
    TestNotNull(TEXT("Saved configuration creates actual MID on registration"),B->GetOwnedMaterial(Mesh,0));
    // Only the immutable package and original material enter the level, never a transient MID.
    B->RestoreMaterials();
    FString LitAsset;FFileHelper::LoadFileToString(LitAsset,*(FPaths::ProjectSavedDir()/TEXT("ToonValidation/LatestLitPackage.txt")));
    auto* LitBundle=LoadObject<UNPRBackendStylePackage>(nullptr,*LitAsset);
    if(!TestNotNull(TEXT("Alternate runtime model package loads"),LitBundle)){World->DestroyWorld(false);World->RemoveFromRoot();return false;}
    FActorSpawnParameters LitSpawn;LitSpawn.Name=TEXT("ToonDefaultLitActor");
    auto* LitActor=World->SpawnActor<AStaticMeshActor>(FVector(0,150,0),FRotator::ZeroRotator,LitSpawn);
    auto* LitMesh=LitActor->GetStaticMeshComponent();LitMesh->SetMobility(EComponentMobility::Movable);LitMesh->SetStaticMesh(Mesh->GetStaticMesh());
    auto* LitBinding=NewObject<UNPRStyleBindingComponent>(LitActor,TEXT("ToonDefaultLitBinding"),RF_Transactional);
    LitActor->AddInstanceComponent(LitBinding);LitBinding->ToonMesh=LitMesh;LitBinding->ToonPackage=LitBundle;LitBinding->RegisterComponent();LitBinding->RestoreMaterials();
    auto* Light=World->SpawnActor<ADirectionalLight>();Light->SetActorRotation(FRotator(-35,45,0));
    CastChecked<UDirectionalLightComponent>(Light->GetLightComponent())->SetIntensity(PI);
    World->SpawnActor<APlayerStart>(FVector(-250,0,60),FRotator(-12,0,0));
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    const FString File=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetMapPackageExtension());
    const bool Saved=UPackage::SavePackage(Package,World,*File,Args);TestTrue(TEXT("Validation map and component configuration save"),Saved);
    TestTrue(TEXT("Persist map identity for independent Game run"),FFileHelper::SaveStringToFile(PackageName,*(FPaths::ProjectSavedDir()/TEXT("ToonValidation/LatestMap.txt"))));
    World->DestroyWorld(false);World->RemoveFromRoot();
    AddInfo(FString::Printf(TEXT("Saved runtime map %s, immutable style %s"),*PackageName,*Asset));return Saved;
}
#endif
