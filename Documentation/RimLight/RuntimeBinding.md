# ③ C++ / MID 动态调参

我们用一个入口同时更新亮边宽度、边缘柔和度、边缘光强度和边缘光颜色：`UNPRStyleBindingComponent::SetRimParameters`。它接收第②步的 `FNPRRimParameters`，先校验，再把值写入当前绑定的 MID（运行中可调的材质实例）。

## 调用顺序

1. 准备已加载的 `UNPRBackendStylePackage`，只列出这次要绑定的网格材质槽。各槽材质应已经包含第②步约定的三个 Scalar 和一个 Vector。
2. 调用 `ApplyPackage(Mesh, Package, Error)` 创建并绑定每槽独立的 MID。这个步骤会替换包中列出的槽，因此需要使用接好 Rim 函数的角色材质；独立灰色预览材质不保留角色原有配色。
3. 填入 `FNPRRimParameters`，调用 `SetRimParameters(Values, Error)`。在主线程按需要重复调用即可实时调参，不需要重新创建 MID，也不需要编译材质。
4. 结束这次绑定时调用 `RestoreMaterials()`；组件的 `EndPlay` 也会调用它。已被其他系统换掉的槽会保留外部材质。

```cpp
FNPRRimParameters Rim;
Rim.RimGrazingAngleRad = FMath::DegreesToRadians(25.0);
Rim.RimEdgeHalfWidthRad = FMath::DegreesToRadians(2.0);
Rim.RimPeakScale = 1.0f;
Rim.RimColor = FLinearColor(1.0f, 0.2f, 0.1f);
FString Error;
if (!Binding->SetRimParameters(Rim, Error))
{
    // 显示 Error；原有 MID 参数保持不变。
}
```

UE 蓝图可使用同名 `Set Rim Parameters` 节点，输入 `NPRRimParameters` 结构，读取返回值和 `Out Error`。结构中的角度是弧度；函数内部转换为材质中文参数所需的度数，材质再转回函数输入的弧度。不要重复转换。Blueprint 与 C++ 使用同一实现，不需要 S58 或 ToonProfile。

## 一次更新怎样生效

`四项统一数据 → Validate → 检查全部绑定槽与参数类型 → 弧度转度 → 写入三个 Scalar 和一个 Vector → 原 MID 继续渲染`

- 输入必须有限并处于硬域；非法角度、负值、NaN 和无穷报错，不截断、不修改传入结构。显式 0 和黑色可以正常写入，RGB 允许 HDR。
- 必须已有有效绑定；任何一个槽被外部替换、缺少所需参数或参数类型不符，整次更新失败，所有槽的旧值保持不变。
- 全部槽预检查完成后才开始写入。颜色 Alpha 统一写 1，Rim 函数只取 RGB；颜色亮度不归一化。
- 旧的通用 `SetScalarParameter` 继续按材质名称直接写标量，不自动做 Rim 单位转换或整组校验。Rim 业务应使用新的专用入口。
- `RestoreMaterials` 恢复本组件仍拥有的槽；不会保存材质或风格资产。Reset 删除哪一层的覆盖由后续 Profile/Resolver 负责，不能把“写 0”当作删除覆盖。

## 验证入口与范围

`NPR.Rim.MaterialContract` 检查默认值、单位转换、零值、HDR 和非法值；`NPR.Rim.LiveBinding` 检查实际保存材质、双槽 MID、读回、整组拒绝、外部替换、恢复和旧标量接口；`NPR.Rim.RenderBinding` 在独立 `FPreviewScene` 中使用同一个 MID 导出八张真实 GPU 图像。测试不在用户当前关卡中添加或替换角色，也不保存地图或材质。

完整 Editor 构建后，在 UE 自带命令框输入 `Automation RunTests NPR.Rim`。`RenderBinding` 要求真实显卡路径，不能用 NullRHI。图像导出在 `Documentation/RimLight/BindingValidation/`，图像门禁、人工查看和反射调用记录分别为同目录的 `ImageMetrics.json`、`ImageReview.json`、`PythonReadback.json`。

2026-10-22 在 Windows / UE 5.8.3（CL 58210709）/ DX12 SM6 / Intel UHD Graphics 验证通过：三项 C++ 用例零错误、零警告；`Tools/check_rim_binding_images.py` 的八项图像/自动化门禁通过；当前编辑器的 `Tools/verify_rim_binding_python.py` 十项反射调用检查通过，预览实例已打开。Game 与完整 Editor DLL 均已构建，具体记录见 `ImplementationValidation.json`、`BindingValidation/ImageMetrics.json`、`ImageReview.json` 和 `PythonReadback.json`。

图像门禁使用 512×512 的相同场景与相同 MID：宽度 8° → 25° 的亮边信号面积为 3240 → 18661 像素；硬边与软边的过渡区域为 2436 → 13734 像素；红色强度翻倍后，红通道平均增加约 48.73 个 8 bit 显示值；OffAgain 与初始 Off 的最大像素误差为 0。该测量受固定曝光与色调映射影响，不代表线性亮度的精确倍增。

为了避免后台限帧让自动化队列一直等待，最新启动仅为该进程覆盖了测试就绪阈值 `Engine.AutomationTestSettings.DefaultInteractiveFramerate=1`、持续时间 1 秒；没有修改编辑器的后台节能偏好，也不据此判断性能。捕获先等 45 帧，之后每项等 12 帧，确保测试实际材质而非尚未准备好的默认材质；C++ 用例同时检查开启亮边必须改变像素。

Python 调用这个 UFUNCTION 时，UE 把原生 bool 当作成功标记：成功返回 `OutError` 字符串（通常为空串），失败返回 `None`。不要用空串的真假判断成功；C++ 和蓝图仍读取原生 bool 与 `OutError`。

这个入口解决第③步的动态参数写入。2026-10-25 已加入 RGB Profile 存储、单槽角色绑定和 NPR 调制窗口，使用顺序见 [Rim 编辑器工作流](EditorWorkflow.md)。前述 2026-10-22 验证只覆盖受控网格及四项 Rim 参数，不能替代本轮角色多槽/多 LOD、阴影、动画、时域、Cook 或性能验收。

## 源码

- `Plugins/PrincipledNPR/Source/NPRRuntime/Public/NPRStyleBindingComponent.h`
- `Plugins/PrincipledNPR/Source/NPRRuntime/Private/NPRStyleBindingComponent.cpp`
- `Plugins/PrincipledNPR/Source/NPRRuntime/Private/Tests/NPRRimBindingTests.cpp`
- `Plugins/PrincipledNPR/Source/NPREditor/Private/Tests/NPRRimRenderTests.cpp`
- 单位、名称和默认值继续由 `MaterialParameters.v1.json` 与 [第②步交接](ParameterHandoff.md)约定。


## 原表面派生绑定

编辑器 `NPRRimMaterialBuilder` 在副本上连接既有 Rim 函数，生成材质后通过 `ApplyRimMaterial(Mesh, Slot, Material, Parameters, Error)` 绑定。运行时只加载已生成材质，不复制或编译编辑器图。保存配置中的 `bUseRimLocalParameters=true` 使用当前槽独立值，优先于可选 Profile；旧配置为 false 时继续 Profile 解析。`RimGeneratedMaterial` 为空时，旧 Recipe 路径继续兼容。

空 Normal Texture 在旧 `ApplyRimPreview` 路径映射为法线强度 0，避免继承母材质非平坦的 DefaultNormal 纹理。新路径完全保留原 Normal 图，不需要这一替代输入。数据约定见[原表面绑定契约](OriginalSurfaceBinding.v1.json)，操作见[槽位工作流](EditorWorkflow.md)。
