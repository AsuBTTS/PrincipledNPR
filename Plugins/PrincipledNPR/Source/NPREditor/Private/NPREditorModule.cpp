#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogNPREditor, Log, All);

class FNPREditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UE_LOG(LogNPREditor, Display, TEXT("NPR editor skeleton loaded. Use built-in Data Asset and Details workflows."));
    }
};

IMPLEMENT_MODULE(FNPREditorModule, NPREditor);
