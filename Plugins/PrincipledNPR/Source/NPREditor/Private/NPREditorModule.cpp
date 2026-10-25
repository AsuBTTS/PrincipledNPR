#include "Modules/ModuleManager.h"
#include "NPROutlineEditorUI.h"

DEFINE_LOG_CATEGORY_STATIC(LogNPREditor, Log, All);

class FNPREditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FNPROutlineEditorUI::Register();
        UE_LOG(LogNPREditor, Display, TEXT("NPR editor loaded. Window > NPR Modulation opens Outline and Rim Light."));
    }
    virtual void ShutdownModule() override { FNPROutlineEditorUI::Unregister(); }
};

IMPLEMENT_MODULE(FNPREditorModule, NPREditor);
