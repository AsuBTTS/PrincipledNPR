#pragma once

#include "Widgets/SCompoundWidget.h"

class SNPRRimEditor final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SNPRRimEditor) {} SLATE_END_ARGS()
    SNPRRimEditor();
    void Construct(const FArguments& Args);
    virtual ~SNPRRimEditor() override;
    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override;
    void ReloadSelection();
    void SyncSelection();
    void EditTarget(class UMeshComponent* Mesh,int32 SlotIndex);
    TSharedPtr<class FNPRCharacterPreview> GetPreview() const;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
#if WITH_DEV_AUTOMATION_TESTS
    friend class FNPRRimEditorWorkflowTest;
#endif
};
