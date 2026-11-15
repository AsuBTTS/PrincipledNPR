#pragma once

#include "CoreMinimal.h"
#include "NPRMatCapParameters.h"
#include "NPRMatCapEditorOptions.generated.h"

UCLASS(Transient)
class UNPRMatCapEditorOptions : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="MatCap", meta=(ShowOnlyInnerProperties))
    FNPRMatCapParameters Parameters;
};
