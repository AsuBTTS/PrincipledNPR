<!-- Generated from Documentation/outline_introduction.md. Edit the original, then run npm run docs:generate. -->

# Outline 描边介绍

我们用描边强调人物轮廓，也可让头发、衣服等区域有不同线宽。本文先说明现有算法，再说明怎样使用。更新日期：2026-10-22；本轮只整理文档，没有重新运行 UE 渲染。

## 1. 算法：先理解两种描边

### 反向壳：在模型外画一层壳

我们复制一份临时辅助网格，把它沿着表面朝向向外推一点，再只显示外壳背面。原模型挡住中间的大部分外壳，边缘露出的部分就成为线条。表面朝向在图形学里叫“法线”。

外扩距离使用厘米，所以同样参数在远处通常显得细，在近处显得粗。它不修改原始模型资产或原表面材质。当前辅助网格对普通骨骼角色使用姿势跟随；复杂变形仍需独立检查。

### 屏幕空间：根据画面标记向外找边缘

我们让辅助网格在深度和编号缓冲中留下标记，可以把它理解为一张只有“哪里有角色、离相机多远、属于哪组”的辅助图。后处理材质在画面附近查找这些标记并向外扩张，形成轮廓。

宽度使用像素。当前计算在 4 圈、每圈 16 个方向上采样，并用深度容差减少画在前景遮挡物后面的线。它不会自动识别衣服褶皱、纹理线或所有物体相交边。

这一步发生在景深之后、时间抗锯齿之前，让描边一起参与引擎的时间稳定处理。当前图像证据不表示任意运动、分辨率和场景都不会闪动。

### 线宽与局部覆盖怎样计算

```text
区域宽度 = 当前方法的基础宽度 × 当前边缘来源倍率 × 最终区域倍率
反向壳还可乘顶点色和宽度贴图的局部因子
```

顶点色是模型顶点上保存的颜色；当前只用红色通道控制外扩。宽度贴图同样读取红色通道，0 隐去、1 保持全宽。贴图在顶点处读取，细节受模型顶点数量限制。

区域设置按“角色 → 组件 → 材质槽 → Section”继承。只替换勾选字段，以最深一层为准，不把所有层的区域倍率相乘。像素与厘米不能用固定比例换算。

### 对应 UE 材质输入的公式

先由运行时设置算出区域宽度，再写到材质的 `WidthCm` 或 `WidthPixels`：

```text
Screen 区域宽度(px) = WidthPixels × 当前边缘来源倍率 × 最终 LocalWidthScale
Hull 区域宽度(cm) = HullWidthCm × 当前边缘来源倍率 × 最终 LocalWidthScale

顶点色因子 = Lerp(1, VertexColor.R, UseVertexColorWidth)
贴图因子 = Lerp(1, Saturate(WidthMask(UV0).R), UseWidthMask)
World Position Offset = VertexNormalWS × Hull区域宽度(cm) × 顶点色因子 × 贴图因子
```

`Lerp(A,B,开关)` 是线性插值节点；这里开关写 0 或 1，关闭取 A，开启取 B。`VertexNormalWS` 是顶点世界法线。`World Position Offset` 是 UE 材质的“世界位置偏移”输入，因此整条公式就是“沿着表面朝向，把顶点向外推多少厘米”。

例如区域壳宽度 0.1 cm、顶点红色为 0.5、贴图红色为 0.8，两个局部开关都开启时，实际外扩为 `0.1 × 0.5 × 0.8 = 0.04 cm`。

Screen 的区域宽度还要换成当前内部渲染分辨率的采样半径：

```text
内部采样半径 = Clamp(Screen区域宽度, 0, 8) × View.ViewResolutionFraction
```

`View.ViewResolutionFraction` 是引擎的内部渲染比例。它让材质用最终输出像素来表达宽度；并不保证边缘方向、遮挡和采样条件下都呈现严格等厚线条。Screen 使用的是区域标记、深度比较和多方向采样，不能用一条反向壳位移公式替代。

继续展开采样和颜色合成，可以用接近引擎变量的表达写成：

```text
方向角 = dir × 2π / 16                         // dir 取 0..15
当前圈半径 = 内部采样半径 × ring / 4          // ring 取 1..4
采样UV = Clamp(当前UV + (Cos(方向角), Sin(方向角)) × 当前圈半径 × 缓冲尺寸倒数, UV边界)
命中 = 邻域内有 Abs(CustomStencil - StencilId) < 0.5 的采样点
最近角色深度 = 所有命中采样点的 CustomDepth 最小值
可见 = 最近角色深度 <= 当前 SceneDepth + DepthBiasCm
输出颜色 = Lerp(SceneColor.RGB, OutlineColor.RGB, 命中 × 可见)
```

`CustomStencil` 是区域编号，`CustomDepth` 是辅助网格深度，`SceneDepth` 是当前画面深度，`SceneColor` 是当前画面颜色。缓冲尺寸倒数来自 `GetSceneTextureBufferSize(25).zw`，不拿窗口尺寸代替纹理尺寸。当前像素已经属于本组，或采样宽度小于 0.01 时，直接保留原颜色；上面的采样公式只描述其他像素的分支。

两种方法继续由 Outline 独立管理，分别使用 `M_OutlineHull`、`M_OutlineScreen` 等现有材质。Surface 的颜色、法线和 Rim 不需要搬进描边算法。

## 2. 当前效果与状态

基础后端、角色分组、旧单槽流程以及材质槽界面为**已实现并验证**，但覆盖范围不同。新增分层宽度、顶点/贴图局部宽度仍归为**已实现但待验证**，已有编译和资产记录，尚缺对应的完整图像验收。

| 原材质 | 屏幕空间描边 |
| --- | --- |
| ![原材质导出图](/generated/source/Documentation/Outline/Temporal/Validation/Candidate1/Screen_Static100/000/Original.png) | ![Screen 描边导出图](/generated/source/Documentation/Outline/Temporal/Validation/Candidate1/Screen_Static100/000/Outline.png) |

来源：2026-10-17 的真实渲染导出，不是编辑器界面截图。颜色和线宽是当时的测试设置，不代表当前默认值；该组旧图未验证后来的遮罩功能。

## 3. 使用：整角色描边

1. 在关卡大纲选中一个角色，打开 **窗口 → NPR 调制**。
2. 进入 Outline，点击“使用关卡选中角色”，再点击“创建 / 打开角色级配置”。
3. 选择描边方法，先使用较小的基础宽度和容易辨认的颜色。
4. 在 `Meshes` 中展开组件和材质槽；用“描边”控制参与区域。
5. 需要不同线宽或颜色时，选择 IndependentRegions（独立区域），在本槽“编辑参数”中勾选并填写覆写。
6. 需要统一外轮廓时选择 Screen Space 与 UnifiedSilhouette（统一外轮廓）；同组设置必须一致，只允许局部关闭。
7. 用“隔离”检查单个槽，再恢复全角色观察遮挡与接缝。
8. 保存基础预设和当前关卡，重新打开检查设置。

完整窗口说明见 [NPR 调制插件使用说明](../guides/NPR调制插件使用说明.md)。普通使用不需要运行重建脚本。

## 4. 参数怎样调

| 控制 | 默认值 / 硬范围 | 使用提示 |
| --- | --- | --- |
| 描边方式 | 反向壳；可选关闭、Screen | 先决定算法，再改对应宽度 |
| 描边颜色 | 线性 RGB `(0.015, 0.008, 0.025)`；每通道 0–1 | 屏幕显示还受曝光和色调映射影响 |
| 屏幕宽度 | 1.25 px；0–8 px | 只用于 Screen |
| 壳宽度 | 0.1 cm；0–5 cm | 只用于反向壳 |
| 边缘来源倍率 | 轮廓和手工区域边界均为 1；0–4 | 手工边界不是自动识别的褶皱线 |
| 区域倍率 | 1；0–4 | 需要局部缩放时使用，0 有效 |
| 深度容差 | 0.1 cm；0–5 cm | Screen 遮挡判断的容差，不是另一种线宽 |
| 顶点红色通道控制 | 默认关闭 | 仅反向壳，先确认模型确实有相应顶点色 |
| UV0 宽度贴图 | 默认关闭，贴图为空 | 仅反向壳，使用线性 Masks 类型、非虚拟 Texture2D |

例如屏幕基础宽度 1.25 px、边缘倍率 1、区域倍率 0.8，计算宽度为 1 px。Reset/恢复继承删除本层覆写；输入 0 是一个保留下来的关闭或零宽值。

全部字段、单位与声明见[参数参考](../principled/parameters.md)。规划中的统一 `OutlineWidthPx` 尚未自动连接到当前 Outline，不要把它与现有两种宽度混用。

基础宽度乘完两项倍率后仍需在最终硬范围内：Screen 不超过 8 px，Hull 不超过 5 cm。超出时运行时校验会报错，不能只看每个单独字段是否在范围内。

## 5. 旧单材质槽流程仍然保留

旧流程用于一个主骨骼网格的受控不透明材质槽。练习场景为 `/Game/NPR/Maps/L_Outline_Workflow`，角色 `Outline_ControlledCharacter_Slot02`，受控衣物是 2 号槽。

载入角色和目标槽后，点击“转换 / 更新单槽 Outline 绑定”。系统创建原材质的子实例并保留原材质引用；只替换这个槽，重复执行复用已有绑定。生成资产在 `/Game/NPR/Outline/Generated`。

调整、对比、预设保存和重开流程与窗口说明一致。需要恢复时使用“恢复单槽原材质并移除绑定”；若别的系统已换掉目标槽，会给出诊断并保留外部修改。生成资产不会随恢复操作自动删除。

整角色模式使用“关闭角色描边（保留配置）/ 启用角色描边”，不需要单槽转换按钮。这两个流程不能混着理解。

## 6. 已知限制

- 当前主要面向普通不透明网格，透明或裁切材质的描边覆盖尚未接通。
- Section 是一块绘制区域，目前只支持单 LOD 且独占材质槽；共享槽和多 LOD 覆写会被拒绝。
- 统一外轮廓需要 Screen；不同颜色或线宽使用独立区域。
- Screen 不支持连续顶点色/贴图宽度；反向壳的细节受网格、UV 接缝和纹理就绪情况影响。
- 当前没有自动几何折痕或独立遮挡边分类。辅助网格、区域组和后处理也有额外成本。
- 移动平台、Nanite、复杂动画、Cook 打包和性能全面验收尚未完成。

## 7. 验证与维护入口

2026-10-17 的时间稳定性记录包含 67 项有界检查；2026-10-20 的界面记录覆盖材质槽隔离、参数展开、对比和部分窗口宽度。2026-10-22 Surface 球体测试证明两种 Outline 能与新表面共存，并保留同一个 Surface MID；它没有重做全部 Outline 交互和时域验收。

原始证据：[时间稳定性](/generated/source/Documentation/Outline/Temporal/Acceptance.json)、[界面记录](/generated/source/Documentation/Outline/MaterialSlotUI.md.txt)、[Surface 共存验证](/generated/source/Documentation/Surface/ImplementationValidation.json)。

源码主要位于 `NPROutlinePreset`、`NPROutlineActorComponent`、`NPROutlineComponent`、`NPROutlineEditorLibrary` 和 `NPROutlineEditorUI`。修改算法、参数或窗口后，同步本文、[开发约束](../developer/项目开发说明与约束.md)及[状态清单](../reference/status.md)。
