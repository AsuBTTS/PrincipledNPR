#include "NPRStyleBindingComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/MeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPRMatCapRestoredMapTest, "NPR.MatCap.RestoredMap",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FNPRMatCapRestoredMapTest::RunTest(const FString&)
{
    int32 VerifiedActors = 0;
    for (const auto& Context : GEngine->GetWorldContexts())
    {
        auto* World = Context.World();
        if (!World || !World->IsGameWorld()) continue;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            const bool bCool = It->GetFName() == TEXT("MatCapRuntimeActor");
            if (!bCool && It->GetFName() != TEXT("WarmBeveledModel")) continue;
            auto* Mesh = It->FindComponentByClass<UMeshComponent>();
            auto* Binding = It->FindComponentByClass<UNPRStyleBindingComponent>();
            if (!TestTrue(TEXT("Map has persisted MatCap configuration"), Mesh && Binding && Binding->FindMatCap(Mesh, 0))) return false;
            auto* MID = Binding->GetOwnedMaterial(Mesh, 0);
            float Weight = -1.f;
            if (!TestTrue(TEXT("Registration loaded the cooked material and weight"), MID &&
                MID->GetScalarParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Weight), Weight) && FMath::IsNearlyEqual(Weight, .65f))) return false;
            TestTrue(TEXT("Original material was loaded separately from generated material"), Binding->GetSourceMaterial(Mesh, 0) != MID->Parent);
            auto Settings = Binding->FindMatCap(Mesh, 0)->Parameters;
            UTexture* Sample = nullptr;
            TestTrue(TEXT("Each mesh restores its own cooked MatCap texture"),
                MID->GetTextureParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Texture), Sample) &&
                Sample && Sample == Settings.Texture && Sample->GetFName() == (bCool ? FName(TEXT("T_MatCapCool")) : FName(TEXT("T_MatCapWarm"))));
            FString Error;
            auto Invalid = Settings;
            Invalid.Texture = nullptr;
            TestFalse(TEXT("An enabled runtime layer rejects a missing texture"), Binding->SetMatCapParameters(Mesh, 0, Invalid, Error));
            TestTrue(TEXT("Rejecting an edit keeps the previous MID and weight"), Mesh->GetMaterial(0) == MID &&
                MID->GetScalarParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Weight), Weight) && FMath::IsNearlyEqual(Weight, .65f));

            auto* Black = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
            if (!TestNotNull(TEXT("Cooked mask test asset"), Black)) return false;
            Settings.Mask = Black;
            TestTrue(TEXT("Runtime sets a mask"), Binding->SetMatCapParameters(Mesh, 0, Settings, Error) &&
                MID->GetTextureParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Mask), Sample) && Sample == Black);
            Settings.Mask = nullptr;
            Settings.Weight = 0;
            TestTrue(TEXT("Game changes weight on the existing MID"), Binding->SetMatCapParameters(Mesh, 0, Settings, Error) && Mesh->GetMaterial(0) == MID);
            TestTrue(TEXT("Weight zero is written"), MID->GetScalarParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Weight), Weight) && Weight == 0.f);
            TestTrue(TEXT("Clearing the mask restores white"),
                MID->GetTextureParameterValue(FHashedMaterialParameterInfo(NPRMatCapContract::Mask), Sample) && Sample && Sample->GetFName() == TEXT("WhiteSquareTexture"));
            ++VerifiedActors;
        }
    }
    return TestEqual(TEXT("Both demo actors loaded from the cooked validation map"), VerifiedActors, 2);
}
#endif
