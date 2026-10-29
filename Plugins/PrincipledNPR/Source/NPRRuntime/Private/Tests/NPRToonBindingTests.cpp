#include "NPRStyleBindingComponent.h"
#include "NPRBackendStylePackage.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRToonBindingTest,"NPR.Toon.RuntimeBinding",
    EAutomationTestFlags_ApplicationContextMask|EAutomationTestFlags::EngineFilter)

bool FNPRToonBindingTest::RunTest(const FString&)
{
    FString Asset;
    if(!FParse::Value(FCommandLine::Get(),TEXT("ToonPackage="),Asset))
        FFileHelper::LoadFileToString(Asset,*(FPaths::ProjectSavedDir()/TEXT("ToonValidation/LatestPackage.txt")));
    auto* Saved=LoadObject<UNPRBackendStylePackage>(nullptr,*Asset);
    if(!TestNotNull(TEXT("Editor-baked package loads in this process"),Saved))return false;
    if(!TestTrue(TEXT("Baked package declares Toon semantics"),Saved->bHasToonParameters&&!Saved->MaterialSlots.IsEmpty()))return false;
    auto* Surface=Saved->MaterialSlots[0].Material.Get();auto* Profile=Surface->GetToonProfileOverride_Internal();
    if(!TestNotNull(TEXT("Runtime material references persistent ToonProfile"),Profile))return false;
    auto* Original=UMaterial::GetDefaultMaterial(MD_Surface);
    auto* Geometry=NewObject<UStaticMesh>();Geometry->GetStaticMaterials().Add(FStaticMaterial(Original));Geometry->GetStaticMaterials().Add(FStaticMaterial(Original));
    auto* Mesh=NewObject<UStaticMeshComponent>();Mesh->SetStaticMesh(Geometry);
    auto* B=NewObject<UNPRStyleBindingComponent>();
    auto* Package=DuplicateObject<UNPRBackendStylePackage>(Saved,GetTransientPackage());
    Package->MaterialSlots.Reset();
    for(int32 I=0;I<2;++I){auto& Slot=Package->MaterialSlots.AddDefaulted_GetRef();Slot.SlotIndex=I;Slot.Material=Surface;}
    FString Error;B->ToonMesh=Mesh;B->ToonPackage=Package;
    if(!TestTrue(*FString(TEXT("Configured runtime bind: ")+Error),B->ApplyConfiguredToon(Error)))return false;
    auto* A=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));auto* C=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(1));
    TestTrue(TEXT("Each slot receives an independent MID"),A&&C&&A!=C);
    TestTrue(TEXT("MID creation does not replace immutable Profile"),A&&C&&A->GetToonProfileOverride_Internal()==Profile&&C->GetToonProfileOverride_Internal()==Profile);
    Package->MaterialSlots[1].SlotIndex=0;
    TestFalse(TEXT("Duplicate slots rejected atomically"),B->ApplyConfiguredToon(Error));
    TestTrue(TEXT("Failed binding leaves both active materials"),Mesh->GetMaterial(0)==A&&Mesh->GetMaterial(1)==C);
    Package->MaterialSlots[1].SlotIndex=99;
    TestFalse(TEXT("Out-of-range slot rejected"),B->ApplyConfiguredToon(Error));
    Package->MaterialSlots[1].SlotIndex=1;Package->ToonParameters.TerminatorDotCenter=2;
    TestFalse(TEXT("Invalid semantic bundle rejected before mutation"),B->ApplyConfiguredToon(Error));
    TestTrue(TEXT("Invalid semantics keep active MIDs"),Mesh->GetMaterial(0)==A&&Mesh->GetMaterial(1)==C);
    Package->ToonParameters=Saved->ToonParameters;
    Mesh->SetMaterial(1,Original);B->RestoreMaterials();
    TestTrue(TEXT("Restore preserves external replacement"),Mesh->GetMaterial(0)==Original&&Mesh->GetMaterial(1)==Original);
    TestTrue(TEXT("Fresh rebind succeeds after previous rejection"),B->ApplyConfiguredToon(Error));
    TestTrue(TEXT("Success clears prior diagnostic"),Error.IsEmpty());
    B->RestoreMaterials();
    if(!GIsEditor)
    {
        UNPRStyleBindingComponent* Restored=nullptr;UNPRStyleBindingComponent* Alternative=nullptr;
        for(const auto& Context:GEngine->GetWorldContexts())if(Context.World()&&Context.World()->IsGameWorld())
            for(TActorIterator<AActor> It(Context.World());It;++It)
            {
                if(It->GetFName()==TEXT("ToonRuntimeActor"))Restored=It->FindComponentByClass<UNPRStyleBindingComponent>();
                if(It->GetFName()==TEXT("ToonDefaultLitActor"))Alternative=It->FindComponentByClass<UNPRStyleBindingComponent>();
            }
        TestNotNull(TEXT("Game map contains the persisted Toon actor component"),Restored);
        TestTrue(TEXT("Game registration restores the actual mesh MID from baked configuration"),Restored&&Restored->GetOwnedMaterial(Restored->ToonMesh,0)!=nullptr);
        TestNotNull(TEXT("Alternate DefaultLit package was cooked and restored too"),Alternative);
        if(Restored&&Alternative)
        {
            auto* Toon=Restored->ToonPackage.Get();Restored->ToonPackage=Alternative->ToonPackage;
            TestTrue(TEXT("Runtime model selection switches to the prebuilt DefaultLit bundle"),Restored->ApplyConfiguredToon(Error));
            auto* LitMID=Restored->GetOwnedMaterial(Restored->ToonMesh,0);
            TestTrue(TEXT("Actual runtime parent is the selected DefaultLit material"),LitMID&&LitMID->Parent==Alternative->ToonPackage->MaterialSlots[0].Material);
            Restored->ToonPackage=Toon;
            TestTrue(TEXT("Runtime model selection switches back to the Toon bundle"),Restored->ApplyConfiguredToon(Error));
            auto* ToonMID=Restored->GetOwnedMaterial(Restored->ToonMesh,0);
            TestTrue(TEXT("Actual runtime parent is the selected Toon material"),ToonMID&&ToonMID->Parent==Toon->MaterialSlots[0].Material);
        }
    }
    AddInfo(FString::Printf(TEXT("Immutable runtime bundle %s; world=%s"),*Asset,GIsEditor?TEXT("Editor"):TEXT("Game")));
    return true;
}
#endif
