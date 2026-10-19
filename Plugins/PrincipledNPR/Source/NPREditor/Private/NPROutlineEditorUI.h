#pragma once

#include "CoreMinimal.h"

class FNPROutlineEditorUI
{
public:
    static void Register();
    static void Unregister();
    static void Open();
    static bool ExportComparison(const FString& Directory);
    static bool SetComparisonCaptureSize(int32 Width, int32 Height);
    static int32 GetComparisonMeshCount(bool bOutlined);
    static bool SetComparisonCamera(const FVector& Location, const FRotator& Rotation);
    static bool GetComparisonCamera(FVector& Location, FRotator& Rotation);
    static bool SetComparisonScreenPercentage(int32 Percentage);
    static FString GetComparisonTemporalState();
};
