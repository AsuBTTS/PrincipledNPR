# Outline 描边

我们用描边让角色轮廓更清楚，也可以按头发、衣服等区域分别调整。下面先说明如何在编辑器里使用，再介绍两种描边方法怎样工作。

我们目前的实现状态是：基础描边、角色分组和旧单槽流程为**已实现并验证**；分层宽度和 UV0 遮罩为**已实现但待验证**；NPR 调制窗口、材质槽隔离与参数展开为**已实现并验证**。这些结论有各自的验证范围，详见[状态与证据](../reference/status.md)。

## 效果和当前状态

下面是 2026-10-17 时间稳定性实验留下的一组真实画面对比。它能帮助我们看懂屏幕空间描边的位置，但颜色、线宽和当前默认值可能已经不同，也没有验证后来增加的遮罩功能。

| 原材质 | Screen Space 描边 |
| --- | --- |
| ![原材质](/generated/source/Documentation/Outline/Temporal/Validation/Candidate1/Screen_Static100/000/Original.png) | ![Screen 描边](/generated/source/Documentation/Outline/Temporal/Validation/Candidate1/Screen_Static100/000/Outline.png) |

## 在 UE 编辑器里使用

先按[安装与环境](../guide/installation.md)打开工程，并在关卡大纲中选中一个角色。然后从 **窗口 → NPR 调制** 打开工具，点击“使用关卡选中角色 / Use Selection”，再点击“创建 / 打开角色级配置”。这会读取角色的静态网格或骨骼网格组件；不需要先把每个材质槽单独转换。

![UE 5.8.3 中的 NPR 调制窗口，左侧显示角色材质槽，右侧显示描边预览](/generated/source/Documentation/Outline/MaterialSlotUI/NPRModulation-Outline-UE5.8.3.jpg)

在“描边方式 / Method”中选择反向壳或屏幕空间。要给整个人物使用统一外轮廓，选择 Screen Space 和“统一外轮廓 / UnifiedSilhouette”；要让头发、衣服等部分分别调整，选择“独立区域 / IndependentRegions”。从默认细线开始：Screen Space 的基础宽度是 1.25 px，反向壳是 0.1 cm。

在 `Meshes` 中展开要调整的组件，再从实际材质槽卡片勾选“描边”。点击“编辑参数”可调本槽设置：勾选对应的覆写项后，`Values` 中的值才会替换继承值。“覆写基础宽度”直接换成本区域的基础数值；“覆写区域宽度倍率”则按比例调整已有宽度。勾选“隔离”可临时只看这个槽；取消隔离或点击“显示全部材质”即可恢复整个人物。

工具默认显示描边预览。打开“原材质对比”可并排查看原材质和描边结果；“聚焦当前角色”以及 Studio、Warm、Raking 灯光只改变预览，不会改关卡灯光。需要留图时可以导出双视口 PNG。最后保存所选描边预设和当前关卡，重新打开关卡检查区域设置。角色预览使用参考姿势，动画效果还要在实际场景中确认。

2026-10-20 的真实界面导出如下。左图展示完整角色，右图只隔离了衣服材质槽；隔离画面只用于检查该槽，不能代替整个人物验收。原始参数是当时已有的粉色描边，不代表当前默认颜色。

<div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:16px;">
  <figure style="margin:0;">
    <img src="/generated/source/Documentation/Outline/MaterialSlotUI/FullCharacter.png" alt="UE 预览导出的完整角色描边画面" style="display:block;width:100%;height:auto;border:1px solid var(--vp-c-divider);border-radius:8px;" />
    <figcaption>完整角色预览：所有区域一起显示。</figcaption>
  </figure>
  <figure style="margin:0;">
    <img src="/generated/source/Documentation/Outline/MaterialSlotUI/IsolatedCloth.png" alt="UE 预览导出的单个衣服材质槽隔离画面" style="display:block;width:100%;height:auto;border:1px solid var(--vp-c-divider);border-radius:8px;" />
    <figcaption>衣服槽隔离预览：只检查当前槽。</figcaption>
  </figure>
</div>

## 参数怎样影响线宽

屏幕空间描边按像素扩张，反向壳按模型空间的厘米数外扩，所以二者不是同一个单位。当前宽度由“基础宽度 × 边缘类型倍率 × 区域倍率”算出；区域层级从角色、组件、材质槽到 Section 逐级继承，只有最深处勾选的覆写生效，并不会把各层倍率连乘。

| 控制 | 默认值和范围 | 我们用它做什么 |
| --- | --- | --- |
| 描边方式 `Method` | InvertedHull；也可关闭或选 ScreenSpace | 选择外扩模型或在画面中扩张边缘 |
| 描边颜色 `Color` | 线性 RGB `(0.015, 0.008, 0.025)` | 设置线条颜色；当前不合成透明描边 |
| 屏幕宽度 `WidthPixels` | 1.25；0–8 px | 设置 Screen Space 的基础宽度 |
| 壳宽度 `HullWidthCm` | 0.1；0–5 cm | 设置反向壳的基础外扩距离 |
| 边缘来源倍率 `EdgeWidthScales` | Silhouette 和 FeatureBoundary 默认均为 1；各 0–4 | 调整当前边缘来源的宽度比例 |
| 区域倍率 `LocalWidthScale` | 1；0–4 | 按组件、材质槽或 Section 缩放宽度 |
| 深度容差 `DepthBiasCm` | 0.1；0–5 cm | Screen Space 用来减少被其他物体挡住的线 |
| 顶点色宽度 `bUseVertexColorWidth` | 关闭 | 反向壳读取模型顶点色 R 通道控制局部外扩 |
| UV0 遮罩 `bUseWidthMask` / `WidthMask` | 关闭 / 空 | 反向壳读取宽度贴图的 R 通道；需要符合格式要求的 Texture2D |

例如 Screen Space 基础宽度为 1.25 px、边缘倍率为 1、区域倍率为 0.8 时，区域计算宽度为 1 px。反向壳还可以继续乘顶点色和遮罩因子。像素和厘米会受相机、距离及模型大小影响，不能用固定比例换算。

完整字段、声明范围和对应源码见[参数参考](../principled/parameters.md)。其中规划中的 Principled `OutlineWidthPx` 尚未自动连接到当前 Outline；目前没有一个跨两种方法通用的最终视觉宽度旋钮。

## 两种方法怎样画出描边

**反向壳（Inverted Hull）** 会为角色组件创建辅助网格，把模型沿表面朝向稍微向外推，再只留下外壳背面形成线条。推开的距离由 `HullWidthCm` 和各层倍率决定，顶点色和 UV0 遮罩还能继续控制局部宽度。这里的表面朝向也叫法线；WPO（世界位置偏移）是材质在渲染时移动顶点的位置。它使用插件里的 `M_OutlineHull` 材质，不修改角色原始网格和材质资产。

**屏幕空间（Screen Space）** 会先让辅助网格写入 UE 的 CustomDepth / Stencil 标记，再由后处理材质读取画面颜色、深度和标记，在角色边缘附近采样并扩张出线条。材质里的 Custom HLSL（材质自定义代码）执行采样计算。当前按 4 圈、每圈 16 个方向采样，并用深度容差减少画在遮挡物后面的线；后处理位置为 Scene Color After DOF、TSR/TAA 之前。采样半径不保证所有视角都呈现严格等厚线条。

这两种方式目前都通过 UE 材质资产接入：反向壳使用 `M_OutlineHull`，屏幕空间使用 `M_OutlineScreen` 和后处理配置。可复用的 HLSL 仍嵌在材质 Custom 节点中，插件目前没有独立的 `Shaders/` 源码目录或单独注册的全局 Shader Pass。

## 限制、源码和验证范围

- 没有自动识别几何折痕或遮挡边。FeatureBoundary 只在 Screen Space + IndependentRegions 下使用人工划分区域；相邻区域可能在共享边形成两条线。
- UnifiedSilhouette 要求整组样式和宽度一致，只支持局部关闭；需要不同颜色或线宽时要用 IndependentRegions。统一模式下不支持 Hull。
- Section 仅支持单 LOD 且独占材质槽；共享槽或多 LOD 组合会报错。Masked / Translucent 原材质目前不能生成正确的描边覆盖。
- 反向壳的遮罩细节受网格顶点密度、UV 接缝和纹理驻留影响；Screen Space 不支持连续顶点色或 UV 遮罩宽度。
- 辅助网格、蒙皮、CustomDepth 和不同 Stencil 组的后处理都有运行成本。移动平台、Nanite、Cook 和复杂动画的全面兼容尚未验证。

开发入口在 `NPROutlinePreset.h/.cpp`（设置、校验和宽度计算）、`NPROutlineActorComponent.cpp`（区域解析和渲染资源）、`NPROutlineEditorUI.cpp`（工具窗口）及 `Tools/create_outline_backend_assets.py`（材质资产构造脚本）。这些材质资产的真实编译状态与用户操作记录见[历史资料索引](../reference/legacy.md)和[状态与证据](../reference/status.md)。

基础描边的历史图像与时间稳定性记录仍在 [2026-10-17 实验资料](/generated/source/Documentation/Outline/Temporal/TemporalMetrics.json)。分层宽度和 UV0 遮罩有编译及资产更新记录，但本次没有运行 UE 或新的渲染回归；当前 UI 的验证范围见[材质槽界面记录](/generated/source/Documentation/Outline/MaterialSlotUI.md.txt)与对应的[机器记录](/generated/source/Documentation/Outline/MaterialSlotUI/Validation.json)。
