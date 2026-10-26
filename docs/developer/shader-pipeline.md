# 渲染管线

本插件当前没有独立的 `Shaders/` 源码目录。Outline 通过 UE 材质资产实现；`Tools/create_outline_backend_assets.py` 是其可读构造来源，Screen HLSL 写在材质 Custom 节点中。HLSL 是 GPU 执行的着色程序语言；Custom 节点允许把一段 HLSL 放入材质图。

## Outline 数据流

这个功能从角色设置进入区域解析，再交给现有的两种描边材质路径：

角色基础设置与预设 → 读取 Mesh / Material / Section 覆写 → `ResolveRegions` 按层应用已勾选的参数 → 检查参数、目标和后端。检查失败时停止刷新并显示错误；通过后按 Mesh、材质槽和样式整理区域，并创建辅助网格与材质实例（MID）。最后按描边方法分流：Inverted Hull 用反向壳材质沿顶点法线外扩；Screen Space 让辅助网格写入 CustomDepth 和 Stencil，再由屏幕后处理材质采样场景边缘。两条路径都由 UE 显示最终描边。

图中的 MID 是运行中可调参数的材质实例。Screen Space 的屏幕后处理材质仍属于 UE 材质资产；当前插件没有独立的 `.usf` 渲染 Pass。

WPO 是 World Position Offset，即移动顶点的位置。Hull 用它沿法线外扩，然后显示壳的背面。Screen 则借 CustomDepth/Stencil 保存角色区域的深度和编号，再向区域外采样画线。Stencil 可理解为“给区域写一个整数标签”。二者的宽度单位与算法不同，详见[Outline](../features/outline.md)。

Screen 放在景深之后、时间抗锯齿之前。TSR 是时间超分辨率，TAA 是时间抗锯齿，都使用前后帧信息；若在它们之后才画硬边，深度采样的逐帧偏移可能直接变成闪动。既有修复保留正常 jitter（采样微偏移），让描边一起进入时域处理。

## Rim Light 数据流

我们把四项控制实现为独立的角度外观层：`MI_RimLight_Preview → M_RimLight → MF_RimLight → Emissive`。材质实例用度数编辑亮边宽度与边缘柔和度，在调用前转为弧度；颜色与强度分别作为 RGB 和线性倍率输入。基础材质强度为 0，预览实例覆盖为 1。

`MF_RimLight` 对世界法线与视线归一化，使用点积、Arcsine、像素导数和三次平滑过渡计算亮边；几何法线另外提供正面门控。整个网络使用材质节点，重复 Band 网络封装在同一函数内。计算不需要材质以外的资源或阶段，因此没有新增 Custom HLSL、`.ush` 或独立 Pass。这个函数可接到普通表面材质或 Substrate BSDF 的 Emissive，不引用 ToonProfile。

构造来源是 `Tools/create_rim_light_assets.py`，实际资产位于 `Plugins/PrincipledNPR/Content/RimLight`。四项绑定来自 `Documentation/RimLight/MaterialParameters.v1.json`；`FNPRRimParameters` 提供校验与弧度转材质度数。颜色 `RimColor` 通过独立 RGB 覆盖表保存，旧标量表继续兼容。实例参数编辑、函数调用与完整 Runtime Resolver 是不同接入范围。

第②步保留原节点算法和已有二进制资产，新增参数契约与交接工具。2026-10-22 在 UE 5.8.3 / Windows 独立 NullRHI 进程对实际保存资产完成 34 项参数检查，确认三个 Scalar、一个 Vector、默认值、滑杆、各自函数输入与 Emissive 连接；不保存资产，也不代表新画面验收。源码/JSON 校验与 C++ 执行范围见[参数交接](/generated/source/Documentation/RimLight/ParameterHandoff.md.txt)。

第③步增加运行时入口 `UNPRStyleBindingComponent::SetRimParameters`：对统一数据和全部已绑定材质槽进行预检查，通过后把弧度换成材质度数，再写入三个 Scalar 与一个 Vector。计算继续走原有 `MID → M_RimLight（或已接函数的角色材质）→ MF_RimLight → Emissive`，没有改变节点算法或增加 Shader Pass。任何槽失败时都不写入；外部换掉材质的槽不会被继续调参或恢复覆盖。

2026-10-22 Game 和完整 Editor 构建通过，UE 5.8.3 / Windows / DX12 SM6 通过三项 C++ 用例、八项图像/自动化门禁及当前编辑器十项反射检查。真实图像使用 `NPREditor/Private/Tests/NPRRimRenderTests.cpp` 的独立 `FPreviewScene`，跨帧等待资源就绪，再重复更新同一个 MID；不在用户关卡添加测试角色。范围与指标见[动态调参](/generated/source/Documentation/RimLight/RuntimeBinding.md.txt)。

默认策略是独立自发光贡献，不自动获得原生灯光方向和阴影可见度。函数仅能在像素阶段使用，不能接顶点位移。创建、编译与图像验证范围见[记录](/generated/source/Documentation/RimLight/MaterialCreation.json)和[功能页](../features/rim-light.md)；仅编译成功仍归为已实现但待验证。

## Surface 家族与组合入口

新增 Surface 家族使用 `MF_NPRSurfaceInputs` 保留基础颜色、切线法线和原有自发光，直接调用已有 `MF_RimLight`，并用 `MF_NPRAdditiveColor` 表达第二份独立贡献。`MF_NPRComposeEmissive` 将三份贡献相加，Default Lit 输出与原生 Toon BSDF 接口复用相同函数。这些计算可以由材质节点清楚表达，没有引入 HLSL 文件或独立 Pass。

`UNPRSurfaceRecipe` 描述预制材质的效果、依赖、全局参数类型和归属，Composer 只选兼容家族并生成旧绑定包，再由 `ApplyPackage` 与 `SetRimParameters` 执行。当前选择器不生成任意材质图，不替代统一 Style Resolver。Outline 的 Post Process 与 Geometry 控制器不接入 Surface Master。操作、算法与验证边界见 [Surface 材质组合](../features/surface-material.md)。

## 后续实现原则：材质节点优先

2026-10-26 的工作区只增加编辑器侧共享视口与预览过滤。`NPROutlineEditorUI.cpp` 复制角色网格到临时世界；Rim/Surface 过滤通过独立预览 MID 的现有强度参数完成，几何/屏幕描边通过预览规则与原控制器完成。不新增材质图、HLSL 文件、渲染资源阶段或独立 Pass，也不改写关卡 MID 来实现隔离。构建、自动化和实际视口证据见[工作区验证](/generated/source/Documentation/Editor/WorkspaceValidation.json)。

我们把 UE 材质节点图看作 Shader 的一种写法。UE 会把节点组合编译成 HLSL，再生成显卡可执行的程序；所以用材质节点实现，也是我们在写 Shader。[Epic 的材质说明](https://dev.epicgames.com/documentation/unreal-engine/essential-unreal-engine-material-concepts?lang=en-US)介绍了这个过程。

我们先选能让效果容易看懂、方便美术调节的实现：简单计算用节点；一段节点网络要在多处使用时，优先整理成 Material Function（材质函数，一组可重复使用的节点）。Base Color、颜色调整和常见的遮罩混合都适合从这里开始。

只有遇到明确需要时，我们才把计算写成 HLSL。比如所需计算没有合适的材质节点，节点连线已经难以读懂，或者某段数学算法需要在多种材质间共用并单独维护。Custom 节点可以直接写 HLSL，也能引用外部 Shader 文件；但 Epic 提醒，Custom 节点可能挡住引擎对常量的简化，导致指令数高于等价的内置节点，因此不能只为“源码看起来独立”而改写。[Custom 节点说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/custom-material-expressions-in-unreal-engine)

`.ush` 可以按本项目约定存放共用 HLSL 函数，再由材质 Custom 节点引用；它仍然属于材质 Shader 的一部分，自己不会新增渲染阶段。若效果需要材质之外的屏幕图像、计算着色器或独立绘制阶段，才考虑 `.usf` 和独立 Shader Pass。`.usf` 文件本身也不会自动运行：UE 还需要 C++ 注册 Shader、传入参数和资源，并把工作加入渲染流程。RDG（渲染依赖图）是 UE 用来登记和安排这些渲染工作及资源的接口。[Global Shader 说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/adding-global-shaders-to-unreal-engine)和[RDG 指南](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine)有相应的接入示例。

对我们来说，判断顺序是：材质节点能清楚表达，就保留节点实现；节点图重复或难读，先看材质函数是否合适；确有算法复用或表达能力需求，再抽出 HLSL；效果需要材质无法提供的渲染阶段和资源时，才研究独立 Pass。当前 Outline 继续使用已有材质资产和 Custom 节点。未来若抽出其中的计算，应逐段验证编译结果与画面，不因建立独立源码目录而重写整套实现。

## 构建与验证边界

材质脚本只操作带本项目归属标记的资产。修改脚本后应按现有工具更新实际 `.uasset`，确认编译完成，再做画面回归；不能仅因文本代码改变就宣布材质已更新。普通文档构建不会执行该脚本。

跨引擎版本升级时重点核对后处理插入位置、SceneTexture 编号/缓冲尺寸、预曝光和时间上采样顺序。预曝光是引擎为控制亮度数值采用的缩放，不应在材质已自动处理时再次手工乘。

S58 语义后端的曲线生成、标定和包构造仍待实现；不可把这里的 Outline Pass 说明套到表面 Toon Lighting。
