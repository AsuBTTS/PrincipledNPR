# Outline 描边

## 功能与实现状态

我们用描边来强调角色的轮廓，让角色更容易从背景中突出。这个功能会在角色边缘画线，我们可以调整线的颜色、宽度，也可以只调整头发、衣服等局部区域。

我们目前提供两种方法：反向壳把模型外扩一点，用外壳背面形成轮廓；Screen Space（屏幕空间）在画面上找角色区域的边缘，再向外画线，适合统一整角色的外轮廓。

基础路径为**已实现并验证**（历史环境和有界证据见[状态页](../reference/status.md)）；新增分层宽度、UV0 Mask 为**已实现但待验证**，已有构建/资产编译记录，新增图像回归未运行。本次仅验证文档。UV0 Mask 是一张按模型第一套贴图坐标读取的遮罩图，红通道越暗，该处的壳宽度越小。

初次使用先按[快速开始](../guide/quick-start.md)操作。下表保留代码中的字段名，便于在界面和报错中对照；后面的“渲染实现”主要供开发者阅读。

## 效果展示

| 原材质 | Screen Space 描边 |
| --- | --- |
| ![原材质](/generated/source/Documentation/Outline/Temporal/Validation/Candidate1/Screen_Static100/000/Original.png) | ![Screen 描边](/generated/source/Documentation/Outline/Temporal/Validation/Candidate1/Screen_Static100/000/Outline.png) |

来源是 2026-10-17 时间稳定性实验 Candidate1 / Screen_Static100 / 000。该历史图用于说明描边位置，颜色、宽度和当前默认值可能不同；不能作为最新 Mask 功能验收。真实连续帧指标见[历史数据](/generated/source/Documentation/Outline/Temporal/TemporalMetrics.json)。

## 参数与依赖

完整 C++ 类型、默认值、声明范围和编辑条件见[自动生成表](../principled/parameters.md)。主要入口：

| 控制 | 默认值 / 范围 | 映射及用途 |
| --- | --- | --- |
| Method | InvertedHull；None / InvertedHull / ScreenSpace | 选择后端，None 显式关闭 |
| Color | 线性 RGBA (0.015, 0.008, 0.025, 1)；RGB [0,1] | OutlineColor；隐藏 alpha，不支持透明描边合成 |
| WidthPixels | 1.25；[0,8] px | Screen → WidthPixels；关联语义 OutlineWidthPx，尚无自动桥接 |
| HullWidthCm | 0.1；[0,5] cm | Hull → WidthCm；不能当作像素宽度 |
| WidthEdgeSource | Silhouette | 选择轮廓或手工 FeatureBoundary；并不自动分类 |
| EdgeWidthScales | Silhouette=1、FeatureBoundary=1；各 [0,4] | 当前来源倍率 |
| LocalWidthScale | 1；[0,4] | 最深层勾选覆写替换 |
| DepthBiasCm | 0.1；[0,5] cm | Screen 深度容差；不是遮挡边宽度 |
| bUseVertexColorWidth | false | Hull，资产顶点色 R；不复制组件 Mesh Paint 覆盖 |
| bUseWidthMask / WidthMask | false / 空引用 | Hull，UV0、Masks、线性、非 VT 的 Texture2D R 通道 |

Validate 同时检查有限数值、来源支持、纹理类型和组合后最终宽度上限（Screen 8 px / Hull 5 cm）。0 宽度会移除相应辅助描边。Screen 拒绝顶点色/Mask 连续宽度；Hull 拒绝 FeatureBoundary。Crease/Occlusion 枚举虽保留用于能力诊断，但不可作为有效编辑来源。

## 渲染实现

Hull 由独立辅助 Static/Skeletal Mesh 执行：`VertexNormalWS × WidthCm` 写 WPO，顶点色和 UV0 Mask 进一步乘宽度；双面 Masked Unlit 材质通过 TwoSidedSign 丢弃正面，背面壳与常规深度测试形成轮廓。Mask 在顶点阶段显式 mip 0 采样，细节受顶点密度、UV 接缝和纹理驻留影响。

Screen 通过辅助网格写 CustomDepth/Stencil；后处理读 SceneColor、SceneDepth、CustomDepth、CustomStencil。中心属于本组时保留原色，在外部执行 4 圈 × 16 方向采样，找到本组邻域后以最近深度和 DepthBiasCm 抑制被遮挡线。宽度乘 `View.ViewResolutionFraction`；采样步长来自纹理缓冲尺寸。采样半径不保证任意视角严格等厚。

当前 Screen 位于 **Scene Color After DOF，TSR/TAA 之前**。与深度共享 jitter 后交由时间抗锯齿处理；不是关闭时域 AA。不同 Stencil 组独立后处理 Pass，数量随样式/区域分组增加。最多可用 ID 受 1–255 与其他场景使用者约束，耗尽报错。

## 使用流程

按[快速开始](../guide/quick-start.md)建立 Actor 配置。统一外轮廓选择 Screen + UnifiedSilhouette，要求全组样式一致，可局部关闭。局部不同颜色/宽度选择 IndependentRegions，再在组件、Materials 或受支持 Sections 中勾选覆写。

需要手工区域边界时使用 Screen + IndependentRegions + FeatureBoundary；相邻区域可能在共享边形成双侧线。保存[基础预设及当前关卡](../editor/presets.md)后重新打开验证。旧单槽转换、恢复入口仍保留。

## 限制与性能

- 没有自动几何折痕或遮挡边分类。Depth 比较只能抑制遮挡，不生成独立 Occlusion 线。
- Unified+Hull 被拒绝，几何外扩可能出现内部壳轮廓。Unified 不支持同组不同局部样式/宽度。
- Section 仅限单 LOD 且独占材质槽，共享槽/多 LOD 必须报错。不可把槽隔离说成任意 Section 隔离。
- 透明/Masked 原材质的透明形状不由通用辅助 Mask 自动复制；细发片、缝隙、法线接缝及非均匀缩放需资产实测。未声明 Nanite、移动平台和 Cook 的全面兼容。
- 多辅助网格、蒙皮、CustomDepth 和多后处理 Pass 有成本；Skeletal 使用 Leader Pose，运行时动画须在关卡检查。

## 开发者入口

`NPROutlinePreset.h/.cpp`：Settings、Validate、GetResolvedWidth、ApplyMaterialParameters。`NPROutlineActorComponent.cpp`：ResolveRegions、RefreshOutline、GetSectionMapping、组与 Stencil 分配。`NPROutlineComponent.cpp`：旧单槽绑定。

`Tools/create_outline_backend_assets.py`：材质图及嵌入 HLSL；插件当前没有独立 Shaders 目录。`NPROutlineEditorUI.cpp`：双视口和交互；`NPROutlineEditorLibrary.cpp`：资产/关卡保存入口。这些源码均可从[参数页来源指纹](../principled/parameters.md)下载核对。
