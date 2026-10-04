#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogNPRRuntime, Log, All);

class FNPRRuntimeModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UE_LOG(LogNPRRuntime, Display, TEXT("NPR runtime skeleton loaded. S58 rendering gates remain unaccepted."));
    }
};

IMPLEMENT_MODULE(FNPRRuntimeModule, NPRRuntime);
