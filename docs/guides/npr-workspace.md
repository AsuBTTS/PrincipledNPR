<!-- Generated from Documentation/Editor/NPRWorkspace.md. Edit the original, then run npm run docs:generate. -->

# NPR 调制：统一效果工作区

更新日期：2026-10-29。我们从同一个入口选择角色、查看各个 Mesh 已应用的效果，再进入对应效果调参。Mesh 是角色的网格组件；材质槽是这个组件上使用一份材质的位置。Toon 新增流程与验证范围见 [Toon Light](../features/toon-shading.md)。

状态：已实现并验证，限于本页记录的编译、受控自动化与真实 GPU 捕获；不同角色资产仍需单独检查。

## 1. 先看角色，再选择效果

工具栏和 **窗口 → NPR 调制** 打开角色效果总览。窗口最上方保留 **NPR 调制**、**Outline**、**Rim Light**，并新增 **Toon · 光照**；NPR 调制用 16 号字，效果入口用较小的 12 号字。按钮的内容高度一致，文字垂直居中对齐；**NPR 调制** 本身是可点击的总入口。进入总览后，在页面内容中的分类区域按执行方式选择效果：

- **非后处理**：Rim Light、反向壳几何描边和 Surface 表面组合。
- **后处理**：Screen 屏幕空间描边。
- **Outline · 描边编辑**：保留原有的整角色、单槽、预设与高级参数编辑。分类中的描边入口先隔离查看已有结果；进入描边编辑器后才按需要修改方法。

总览按 Mesh 和材质槽列出实际登记的绑定，可以直接跳转对应的 Rim 或描边编辑器；Rim 跳转会定位到该 Mesh 的该槽。切换顶部页面不会清空同一角色尚未保存的 Rim 编辑。Rim 强度为 0 时仍会列出这份绑定，并显示强度 0；这不表示亮边正在发光。没有登记的第三方材质功能显示为未登记，不推断它已经被转换或支持独立开关。

![UE 5.8.3 最终总览：顶部三个入口，分类位于页面内](/generated/source/Documentation/Editor/WorkspaceEvidence/Final/Overview.jpg)

来源：2026-10-26，Windows 11 / UE 5.8.3 实机窗口，902×725。这是当时版本的导航记录；2026-10-28 新增 Toon 入口的界面另行验证。隔离测试工程中的 Qianyu 使用参考姿势；图中没有登记的槽位不会被标成已经应用 Rim，不作为角色光照验收图。

### Toon 页面与区域选择

Toon 页面左侧显示艺术参数和小曲线，中央复用角色视口，右侧显示 LOD、Section 与材质槽。可以点击模型选择区域；实际应用按材质槽生效，共用槽的区域会同步变化。私有预览不会直接更换关卡材质；“保存风格”保留艺术参数与后端高级设置，“应用到所选槽”才创建角色绑定，之后仍需保存关卡。原始高级 Profile 和普通受光/Toon 模型选择均保留，详见 [Toon Light 操作](../features/toon-shading.md)。

## 2. 完整角色、隔离与原材质对比

选择关卡角色后点“使用关卡选中角色”。右侧显示真实网格和当前材质的预览副本；骨骼网格采用参考姿势。

每行可以“隔离 Mesh”或“隔离材质槽”。“只看 RimLight / HullOutline / ScreenOutline / Surface”进一步选择单项效果；“查看全部效果组合”恢复组合画面，“显示全部材质”恢复完整角色。所有隔离均只作用于临时预览副本，不修改源网格、关卡可见性、关卡材质参数或已有描边配置。

“原材质对比”打开第二个视口。两侧使用共享相机、相同模型和灯光；左侧读取本插件绑定前记录的材质，右侧显示当前效果。Studio、Warm、Raking 只改变预览灯光。“聚焦当前角色”按预览范围重新构图。

单效果过滤继续复用现有材质和描边控制器：Rim 使用预览 MID 副本，关闭预览中的第二份附加贡献；Surface 预览关闭 Rim 和描边；两种描边分别过滤预览区域的实际描边方法，并保留基础表面。这里没有新增 Shader Pass，也不改写关卡中的效果开关。

MID 是运行时材质实例，可以理解为当前槽使用的一份可调材质。我们先把有效编辑值写入这个槽的生产 MID，再把参数变化同步到相应的预览副本。预览缓存按“Mesh＋槽位”分别保存，只在源参数变化时同步，避免每帧清空参数或让两个槽串用同一份颜色。原材质对比读取绑定前保留的材质；如果其他系统已经替换这个槽，我们保留外部修改。

| 效果 | 实际执行位置 | 隔离时怎样查看 |
| --- | --- | --- |
| Rim Light | `MF_RimLight` 输出贡献，默认追加到原材质副本的自发光；旧 Surface 母材质链路继续兼容 | 显示选定槽的当前 Rim 参数，移除预览中的独立第二份附加贡献 |
| Surface | Surface 母材质的基础颜色、法线、自发光输入 | 预览中关闭 Rim 与描边，查看已显式提供的基础表面 |
| 反向壳描边 | Outline 控制器建立并绘制外扩网格 | 仅保留反向壳区域方法，保留基础表面 |
| 屏幕空间描边 | Outline 控制器登记的屏幕后处理 | 仅保留屏幕空间区域方法，保留基础表面 |

“只看 Rim”用于检查当前表面上的亮边贡献，画面仍保留基础表面；它不是一张仅含发光像素的黑底贴图。Rim 算法、正面门控和抗锯齿公式见 [Rim Light 介绍](../rim/rim_light_introduction.md)，Outline 的厘米/像素宽度与继承规则见 [Outline 介绍](../outline/outline_introduction.md)。

## 3. Rim 的四项控制

窗口沿用既有参数，没有新增全局 Rim。宽度和柔和度在 UI 中用度数显示，Profile 与程序结构用弧度保存；写入材质时只转换一次。

| 控制 | 语义 ID | 默认值 | 硬范围 / 建议先尝试 | 可见变化 |
| --- | --- | --- | --- | --- |
| 亮边宽度 | `RimGrazingAngleRad` | 8° | 0°–90° / 1°–35° | 增大后更多侧向表面进入亮边 |
| 边缘柔和度 | `RimEdgeHalfWidthRad` | 新槽 12°；旧程序默认 2° | 0°–90° / 4°–25° | 增大后边界更缓；它是角度过渡的半宽 |
| 边缘光强度 | `RimPeakScale` | 0 | 有限非负 / 0–2 | 增大附加颜色贡献；0 是有效关闭值 |
| 边缘光颜色 | `RimColor` | 线性 RGB 白色 | 每通道有限非负 / 0–1 | 改变亮边颜色；黑色有效，Alpha 不参与计算 |

四项控制作用于当前 Mesh/材质槽。Profile 是可选的风格参数来源，Material Kind 是其中区分材质类型的键；新槽保存自己的局部值。旧的共享 Profile 绑定继续兼容，没有全局、角色、组件逐级继承的 Rim 开关。Surface Recipe 留在底层，不要求使用者手动指定。

## 4. Rim Light 的操作顺序

1. 读取选中角色，在 Mesh 分组中按槽名找到目标，勾选该卡片的 **启用 Rim**。
2. 在该卡片调整颜色、宽度、柔和度和强度。原材质的底色、法线和自发光自动保留，不需要选择 Recipe、输入 Index 或重新配置 Source Surface。
3. 新槽柔和度默认 12°，滑条可到 90°；已有配置保留原值。强度 0 关闭，再次启用恢复本槽上次非零值。
4. 用隔离和原材质对比观察。切换卡片时各槽的草稿与实时效果分别保留。
5. 默认保持原着色模型；需要时单独选择 Subsurface。模型切换会重新生成副本并编译，四项 Rim 参数调整只更新当前 MID。
6. 点击 **保存角色 Rim** 保存本次修改的材质槽。每槽局部值独立，不会因使用相同风格资产而联动保存。风格资产只作为可选参数来源。

顶部保留读取与保存两个按钮，并留出内边距和换行间距；重复的统一应用、加载和成排重置入口已移除。各参数使用属性行的重置箭头。源图支持范围、Subsurface 语义和旧数据兼容见 [槽位操作说明](/generated/source/Documentation/RimLight/EditorWorkflow.md.txt)。

Undo / Redo 恢复当前槽的显示值并更新 MID。外部系统换掉材质后自动刷新保留外部结果；需要明确重新应用时，在对应槽重新勾选启用。保存到磁盘后的 Undo 仍需再次保存。

## 5. 没有看到变化时

| 现象 | 我们先检查什么 |
| --- | --- |
| 角色不在画面中 | 关卡是否只选择一个角色、是否读取选择；点“聚焦当前角色”，检查隔离目标 |
| 颜色或强度变了，亮边仍不可见 | 顶部状态是否提示不支持当前材质根节点或编译失败；强度是否为 0、颜色是否为黑；Shader 是否完成编译 |
| 改了别的槽却在观察旧位置 | 当前卡片、当前槽配置和隔离目标是否相同；取消隔离再核对整个角色 |
| 关闭本槽 Rim 后仍有发光 | 原材质自发光或其他附加贡献可能仍存在；用原材质对比和单效果过滤分别确认 |
| 应用后原有表面变了 | 保持原模型时原表面应一致；先将强度归零，用原材质对比检查，并记录源材质与着色模型选择 |
| 透明槽不能勾选 Rim | 透明 Blend Mode 尚未适配；Opaque 和 Masked 可用；列表允许查看和隔离，不代表可以自动转换 |

## 6. 范围、源码与历史验证

本次原材质派生和精简入口的检查单独记录在[原材质 Rim 检查](/generated/source/Documentation/RimLight/OriginalSurface-2026-10-26.md.txt)。下文图片与工作区报告是同日较早版本的历史证据，图片中的 Source Surface 和统一应用按钮已被本次流程取代。

主要入口为 `NPROutlineEditorUI.cpp` 的工作区和共享视口、`NPRRimEditorUI.cpp` 的 Rim 编辑器，以及 `NPRModulationEditorLibrary` 的只读效果清单。旧的 `NPROutlineEditorLibrary::OpenEditor` 和 Outline 导出接口继续打开旧描边编辑路径；新的 `OpenWorkspace` 打开总览。

| 源码入口（相对于 `Plugins/PrincipledNPR/Source/`） | 负责什么 |
| --- | --- |
| `NPREditor/Private/NPROutlineEditorUI.cpp` | 顶部导航、总览分类、共享相机与预览副本 |
| `NPREditor/Private/NPRCharacterPreview.h` | 角色预览、隔离、过滤与导出接口 |
| `NPREditor/Private/NPRRimEditorUI.cpp` | 槽位卡片、交互更新、重置、Undo 与保存 |
| `NPREditor/Public/NPRModulationEditorLibrary.h` | 总览、编辑跳转和实际绑定清单的脚本入口 |
| `NPRRuntime/Private/NPRStyleBindingComponent.cpp` | 同槽 MID 调参与材质所有权检查 |
| `NPREditor/Private/Tests/NPRRimPreviewRenderTests.cpp` | 预览 MID 的多槽读回与真实 GPU 颜色/强度回归 |

2026-10-26，UE 5.8.3 / CL58210709、Windows 11、DX12 SM6：Editor/Game 构建及 10 项 Rim、Surface、编辑器自动化通过。新增预览材质测试导出 5 张真实 GPU 图像：红/绿色变化、强度提高、归零均通过，归零与初始画面的最大像素差为 0；多槽预览实例、Undo、外部材质保护和隔离均有独立用例。GPU 捕获直接使用共享预览返回的 MID，在独立球体场景中渲染，不冒充编辑器窗口截图。

编译、自动化、真实视口和图像的最终结果分别记录在 [工作区验证](/generated/source/Documentation/Editor/WorkspaceValidation.json)，对应源码/DLL 和原始图像的哈希见 [最终汇总](/generated/source/Documentation/Editor/WorkspaceEvidence/Final/Summary.json)。此前修复后的 [导航调整前记录](/generated/source/Documentation/Editor/WorkspaceEvidence/Summary.json) 与合并版本的 [HUA 联调基线](/generated/source/Documentation/RimLight/Integration-2026-10-26.md.txt) 分别保留，不覆盖此前通过或失败的记录。

同日最终原生窗口检查覆盖 902×725 布局、NPR 调制 16 号字与两个较小的 12 号字入口、总览内部分类、Rim 分类跳转及 Outline 入口。完整角色采用参考姿势，最终 Rim 页见下图。

![UE 5.8.3 最终 Rim 页面：较小的效果入口和完整角色](/generated/source/Documentation/Editor/WorkspaceEvidence/Final/RimNavigation.jpg)

来源：2026-10-26，隔离测试工程、Qianyu 11 槽骨骼网格。本图尚未给当前槽应用 Rim，用于展示实际布局与构图。同日早些时候的[脸部槽实时改色与隔离截图](/generated/source/Documentation/Editor/WorkspaceEvidence/RimCardsIsolatedGreen.jpg)显示绿色 Rim、强度 1.8；该图的分类位置属于导航修改前的版本，只作为隔离与颜色变化证据。

参考姿势、静态球体和受控绑定不代表动画、阴影、多 LOD、透明裁切、任意原材质迁移、Cook 或性能已经验收。完整语义 Resolver 和自动角色转换仍未完成。
