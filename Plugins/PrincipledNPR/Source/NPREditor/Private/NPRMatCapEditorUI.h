#pragma once

#include "Widgets/SCompoundWidget.h"

class FNPRCharacterPreview;
class UMeshComponent;

class SNPRMatCapEditor final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SNPRMatCapEditor) {} SLATE_END_ARGS()
    SNPRMatCapEditor();
    virtual ~SNPRMatCapEditor() override;
    void Construct(const FArguments& Args);
    void SyncSelection();
    void EditTarget(UMeshComponent* Mesh, int32 Slot);
    TSharedPtr<FNPRCharacterPreview> GetPreview() const;
    virtual void Tick(const FGeometry& Geometry, double Time, float Delta) override;

private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
#if WITH_DEV_AUTOMATION_TESTS
    friend class FNPRMatCapEditorWorkflowTest;
    friend class FNPRMatCapGuideCaptureTest;
#endif
};
