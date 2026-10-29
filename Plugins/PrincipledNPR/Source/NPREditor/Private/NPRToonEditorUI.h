#pragma once

#include "Widgets/SCompoundWidget.h"

class SNPRToonEditor final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SNPRToonEditor) {} SLATE_END_ARGS()
    SNPRToonEditor();
    void Construct(const FArguments& Args);
    virtual ~SNPRToonEditor() override;
    virtual void Tick(const FGeometry& Geometry,double Time,float Delta) override;
    void SyncSelection();
    void EditTarget(class UMeshComponent* Mesh,int32 SlotIndex);
    TSharedPtr<class FNPRCharacterPreview> GetPreview() const;
private:
    struct FImpl;
    TUniquePtr<FImpl> Impl;
#if WITH_DEV_AUTOMATION_TESTS
    friend class FNPRToonEditorWorkflowTest;
    friend class FNPRToonWidgetRenderTest;
#endif
};
