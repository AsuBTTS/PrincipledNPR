# Rim Light 边缘光

## 功能概述

同一套函数和 C++ 入口现在也可以接入 [Surface Master 家族](./surface-material.md)，与基础颜色、法线和原有自发光共存。独立 Rim 材质及本页历史验证继续保留；新增组合的测试范围另行记录。

我们用边缘光提亮角色靠近轮廓的表面，让头发、衣服和背景更容易分开。现在可以直接在材质实例中调整 **亮边宽度、边缘柔和度、边缘光强度、边缘光颜色**。这个实现按表面朝向与视线的夹角计算，不是固定像素宽度的屏幕描边。

## 实现状态与效果

完整模块处于 **开发中**：独立材质、四项参数契约与③ C++ / MID 动态调参 **已实现并验证**，范围是受控网格和静态球体。NPR 调制窗口中的 Rim Light 仍是占位页；Profile 存储、自动角色绑定、完整风格解析和原生阴影可见度还未接入。

2026-10-22 在 Windows / UE 5.8.3 / DX12 SM6 验证，同一个 MID 连续更新参数并导出八张真实 GPU 图像，三项 C++ 用例与八项图像/自动化门禁通过。下图来自受控球体渲染目标导出，分别为 25° 白色亮边和 25° 红色亮边；不是界面截图，也不代表角色验收。原始数据见[执行记录](/generated/source/Documentation/RimLight/ImplementationValidation.json)与[图像指标](/generated/source/Documentation/RimLight/BindingValidation/ImageMetrics.json)。

| 25° 白色亮边 | 25° 红色亮边 |
| --- | --- |
| ![受控球体白色边缘光实测导出](/generated/source/Documentation/RimLight/BindingValidation/Wide25.png) | ![受控球体红色边缘光实测导出](/generated/source/Documentation/RimLight/BindingValidation/Red25.png) |

## 使用流程

1. 在 UE 内容浏览器的设置中勾选“显示插件内容”。
2. 打开 `Principled NPR Content / RimLight`，双击 `MI_RimLight_Preview`。
3. 在“边缘光”分组勾选需要覆盖的参数，再修改数值或颜色。预览实例初始为 8°、2°、强度 1、白色；强度改为 0 可关闭亮边。
4. 使用材质编辑器的球体预览观察变化。这个独立预览材质带有固定灰色基础表面，直接替换角色原材质会改变原有配色；接入角色时，应在原材质中调用 `MF_RimLight`，把它的 `RimColorContribution` 与原 Emissive 相加。Substrate 材质可把贡献接到相应 BSDF 的 Emissive 输入，不需要 ToonProfile。

`M_RimLight` 的强度默认仍为 0，保持现有契约。预览实例单独覆盖为 1，方便看到结果。

## 四项控制

| 界面名称 | 用途 | 默认值 | 支持域 / 推荐范围 |
| --- | --- | --- | --- |
| 亮边宽度（延伸角） | 向表面内部延伸多宽 | 8° | 0–90° / 建议 1–35° |
| 边缘柔和度（过渡半宽） | 亮暗交界两侧各有多宽的过渡 | 2° | 0–90° / 建议 0–10° |
| 边缘光强度 | 额外边缘光贡献的线性倍率，0 关闭 | 基础材质 0；预览实例 1 | 有限非负值 / 建议 0–2 |
| 边缘光颜色 | 额外贡献的线性 RGB，Alpha 不参与计算 | 白色 | 每通道有限非负值 / 建议 0–1，允许 HDR |

前两项在界面中用度数，进入函数前乘 π/180 转为弧度；`RimGrazingAngleRad`、`RimEdgeHalfWidthRad` 的已有语义不变。强度对应 `RimPeakScale`。颜色现在有正式可选语义 ID `RimColor`，材质中为 Vector；旧风格缺字段时用白色，现有只存标量的 C++ Patch 尚不能保存它，Scalar 更新接口也不能设置颜色。

支持域由 `FNPRRimParameters::Validate` 校验，非法数据报错且不修改原值。材质实例面板不会自动调用这个方法，直接编辑时仍须遵守范围。完整[单位与映射记录](/generated/source/Documentation/RimLight/MaterialParameters.v1.json)可离线查看。

## ② 参数暴露与契约

四项材质绑定名称已固定：`亮边宽度`、`边缘柔和度`、`边缘光强度`、`边缘光颜色`。后续 C++ 使用 `NPRRimContract` 中的名字和 `FNPRRimParameters` 对接，先校验，再将前两项弧度转为材质度数。参数类型、兼容、显式零值、Reset 和第③/④步的接口约定见[交接说明](/generated/source/Documentation/RimLight/ParameterHandoff.md.txt)及[参数映射](../principled/mapping.md)。

参数契约 **已实现并验证**：2026-10-22 在 Windows / UE 5.8.3 完成 27 项离线交叉检查、34 项实际资产只读检查，并通过 `NPR.Rim.MaterialContract` C++ 用例。Game 与完整 Editor 构建通过。具体范围见[离线记录](/generated/source/Documentation/RimLight/ContractValidation.json)、[资产记录](/generated/source/Documentation/RimLight/ParameterExposureValidation.json)和[构建与执行记录](/generated/source/Documentation/RimLight/ImplementationValidation.json)。

## ③ C++ / MID 动态调参

我们先用 `ApplyPackage` 给指定网格的材质槽创建 MID，再调用 `SetRimParameters`。这个入口接收 `FNPRRimParameters`，一次更新四项控制，内部完成角度转换与 RGB 写入；UE 蓝图也可调用同名节点。后续调参复用原 MID，不需要重新编译材质。

输入非法、任一绑定槽缺少参数/类型不符，或材质已被外部系统替换时，整次更新失败并保留所有旧值。`RestoreMaterials` 恢复本组件仍拥有的原材质。具体示例见[③ 动态调参说明](/generated/source/Documentation/RimLight/RuntimeBinding.md.txt)。

这个入口 **已实现并验证**：2026-10-22 完成双槽 C++ 读回与整组拒绝、八张跨帧 GPU 导出及图像指标、当前编辑器十项反射调用检查。亮边面积由 8° 时的 3240 像素增至 25° 时的 18661 像素；柔和过渡区由 2436 增至 13734 像素；回到强度 0 后与初始 Off 图像完全一致。数字只适用于本次 512×512 受控球体；颜色强度的显示响应仍受色调映射影响。

## 算法与渲染阶段

先将法线与视线方向归一化，计算 `gamma = asin(saturate(N·V))`：侧对相机为 0°，正对相机为 90°。再计算：

`权重 = 1 - smoothstep(亮边宽度 - 有效柔和度, 亮边宽度 + 有效柔和度, gamma)`

`额外颜色 = 边缘光颜色 × 边缘光强度 × 权重 × 正面门控`

例如宽度 8°、柔和度 2°，忽略像素过滤时，6°以内保持完整形状权重，6°–10°平滑减弱，10°以外为 0。有效柔和度取艺术半宽、半个像素变化量和微小正数的最大值；因此 0 柔和度保留抗锯齿并避免除以零。几何法线额外控制正反面，避免把背面全部照亮。

这个算法通过 UE 内置材质节点实现，重复计算封装在 Material Function（材质函数）中，输出接入像素阶段的 Emissive（自发光贡献）。没有新增 Custom HLSL 或独立渲染 Pass。调用关系与选择理由见[渲染管线](../developer/shader-pipeline.md)。

## 限制与依赖

- 默认使用顶点世界法线生成亮边；函数调用处可输入自己的 Rim 法线，几何正面门控独立保留。自定义向量必须非零，不包含双面翻法线工作流。
- 角宽与像素厚度不成正比。柔和度大于亮边宽度时，完整强度区可能消失；颜色亮度与强度也存在乘法耦合。
- 默认贡献不受灯光方向或原生投射阴影控制。普通深度测试会遮住不可见表面，但可见表面上的阴影不会自动抑制这份自发光；最终还受曝光、色调映射、雾和 GI 路径影响。
- 函数使用像素导数进行抗锯齿，不能接 WPO 或其他顶点着色输入。
- 第③步已有显式四项 MID 更新入口，包含颜色；Draft JSON 到画面的完整 Resolver、Profile 的 RGB 存储和角色自动绑定仍待实现。

## 源码入口与记录

可读构造来源：`Tools/create_rim_light_assets.py`；资产目录：`Plugins/PrincipledNPR/Content/RimLight`。算法来自数学报告第 4.1、4.5 节，没有复制参考项目的实现。

[原始实现说明](/generated/source/Documentation/RimLight/Materials.md.txt)保留调用、兼容和验证边界。S58 表中的 Unsupported 仍表示没有对应 ToonProfile 字段；这个材质函数可以独立调用。
