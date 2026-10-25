#pragma once

#include "Widgets/SCompoundWidget.h"

class SNPRRimEditor final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SNPRRimEditor) {} SLATE_END_ARGS()
    SNPRRimEditor();
    void Construct(const FArguments& Args);
    virtual ~SNPRRimEditor() override;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
};
