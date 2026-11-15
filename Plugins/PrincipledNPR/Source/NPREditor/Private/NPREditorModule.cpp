#include "Modules/ModuleManager.h"
#include "NPROutlineEditorUI.h"
#include "NPRStyleBindingComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "UObject/ObjectSaveContext.h"

DEFINE_LOG_CATEGORY_STATIC(LogNPREditor, Log, All);

class FNPREditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FNPROutlineEditorUI::Register();
        PreSave = FEditorDelegates::PreSaveWorldWithContext.AddRaw(this, &FNPREditorModule::BeforeSave);
        PostSave = FEditorDelegates::PostSaveWorldWithContext.AddRaw(this, &FNPREditorModule::AfterSave);
        UE_LOG(LogNPREditor, Display, TEXT("NPR editor loaded. Window > NPR Modulation opens Outline, Rim Light, Toon Light and MatCap."));
    }
    virtual void ShutdownModule() override
    {
        FEditorDelegates::PreSaveWorldWithContext.Remove(PreSave);
        FEditorDelegates::PostSaveWorldWithContext.Remove(PostSave);
        FNPROutlineEditorUI::Unregister();
    }

private:
    void BeforeSave(UWorld* World, FObjectPreSaveContext Context)
    {
        if (!World || Context.IsCooking()) return;
        auto& Bindings = ReapplyAfterSave.FindOrAdd(World);
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            for (auto* Binding : TInlineComponentArray<UNPRStyleBindingComponent*>(*It))
            {
                if (Binding->HasMatCapSettings() && Binding->HasTrackedMaterials())
                {
                    Bindings.Add(Binding);
                    Binding->SuspendMatCapForSave();
                }
            }
        }
    }

    void AfterSave(UWorld* World, FObjectPostSaveContext Context)
    {
        TArray<TWeakObjectPtr<UNPRStyleBindingComponent>> Bindings;
        if (!ReapplyAfterSave.RemoveAndCopyValue(World, Bindings)) return;
        for (const auto& Weak : Bindings)
            if (auto* Binding = Weak.Get()) Binding->ResumeMatCapAfterSave();
    }

    FDelegateHandle PreSave, PostSave;
    TMap<UWorld*, TArray<TWeakObjectPtr<UNPRStyleBindingComponent>>> ReapplyAfterSave;
};

IMPLEMENT_MODULE(FNPREditorModule, NPREditor);
