# Outline 描边

## 功能与实现状态

我们用描边来强调角色的轮廓，让角色更容易从背景中突出。这个功能会在角色边缘画线，我们可以调整线的颜色、宽度，也可以只调整头发、衣服等局部区域。

我们目前提供两种方法：反向壳把模型外扩一点，用外壳背面形成轮廓；Screen Space（屏幕空间）在画面上找角色区域的边缘，再向外画线，适合统一整角色的外轮廓。

基础路径为**已实现并验证**（历史环境和有界证据见[状态页](../reference/status.md)）；新增分层宽度、UV0 Mask 为**已实现但待验证**，已有构建/资产编译记录，新增图像回归未运行。2026-10-20 的新增界面验证单独记录在本文末尾。UV0 Mask 是一张按模型第一套贴图坐标读取的遮罩图，红通道越暗，该处的壳宽度越小。

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

## 材质槽勾选与隔离预览（2026-10-20）

我们在角色配置的 `Meshes` 中直接读取真实使用中的材质槽，显示编号、槽名、当前材质和 Opaque 支持状态。展开组件后，取消某行的“描边”即可排除这一槽；不再添加空规则或输入槽名。勾选会创建或更新本槽启用覆写，“恢复继承”删除本槽规则并恢复继承。Actor 总开关和更深层 Section 规则仍然生效。

勾选“隔离”可只看该组件的当前材质槽，选中时显示“隔离中”；取消勾选或点击工具栏“显示全部材质”恢复全角色。切换到另一槽时，原槽的隔离勾选会同步取消。隔离只作用于临时预览，不改变关卡组件可见性、原始材质或源网格。隔离预览会让其他区域不参与临时描边，所以它不能代替完整角色验收。Masked / Translucent 槽仍无法生成正确描边覆盖，但可以单独查看原表面、取消该槽描边。

工具栏“原材质对比”默认关闭，开启后恢复原材质与 Outline 双视口；相机和灯光继续共享。PNG 导出仍输出 Original.png 和 Outline.png，隔离状态会体现在导出画面中。

组件源引用改为名称提示，避免展开整套 UE 网格组件属性。组件级覆写、材质槽“编辑参数”和 Section 高级覆写继续保留。材质槽按钮带明显背景和加粗文字，展开后改为“收起参数”。已有 None、未使用或重复槽名规则会显示清理按钮；点击后移除无效项，同名规则保留第一条，因此清理前应确认不需要后续重复条目的参数。

实现与验证边界见 [材质槽界面改进记录](/generated/source/Documentation/Outline/MaterialSlotUI.md.txt)。新增 UI 与隔离流程不沿用旧时间稳定性验收结果。

2026-10-20：顶级入口改为“窗口 → NPR 调制”，窗口内切换 Outline 和 Rim Light 占位页。材质槽改用原生 Slate 分组卡片；名称、材质与覆盖能力分行显示，描边/隔离使用勾选框，“编辑参数”按需展开，“恢复继承”删除本槽覆写。隔离状态直接读取当前预览目标，避免按钮文字与画面状态不一致。编辑属性只更新窗口内反馈，同一提示去重。

已在 UE 5.8.3 / Windows 的当前角色中检查导航、隔离切换、参数按钮、原材质对比与窄窗口布局。真实导出对照：[仅衣服槽](/generated/source/Documentation/Outline/MaterialSlotUI/IsolatedCloth.png)、[取消隔离后的角色](/generated/source/Documentation/Outline/MaterialSlotUI/FullCharacter.png)。[机器可读记录](/generated/source/Documentation/Outline/MaterialSlotUI/Validation.json)列明未覆盖的操作和资产类型；这次界面检查不扩展渲染后端的验收范围。

这两张原图来自 UE 预览视口导出，保留当时已有的粉色描边参数；左图查看完整角色，右图只隔离当前衣服材质槽。隔离图中未显示的部件是预览筛选结果，不能据此判断完整角色效果。

<div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:16px;">
  <figure style="margin:0;">
    <img src="/generated/source/Documentation/Outline/MaterialSlotUI/FullCharacter.png" alt="UE 预览导出的完整角色描边画面" style="display:block;width:100%;height:auto;border:1px solid var(--vp-c-divider);border-radius:8px;" />
    <figcaption>图 1 · 完整角色预览：所有区域一起显示。</figcaption>
  </figure>
  <figure style="margin:0;">
    <img src="/generated/source/Documentation/Outline/MaterialSlotUI/IsolatedCloth.png" alt="UE 预览导出的单个衣服材质槽隔离画面" style="display:block;width:100%;height:auto;border:1px solid var(--vp-c-divider);border-radius:8px;" />
    <figcaption>图 2 · 衣服槽隔离预览：只用于检查当前槽，不能代替完整角色验收。</figcaption>
  </figure>
</div>
