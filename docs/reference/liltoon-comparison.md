# lilToon 技术对照

调研日期：2026-10-19。源码基线为 lilxyzw/lilToon 提交 [4a3f868](https://github.com/lilxyzw/lilToon/tree/4a3f868398fda5b7f5a254e506f86e43207103c3)。本报告基于实际读取的源码和官方开发文档，不依据功能名推测实现。源码证据清单记录在 `docs/reference/liltoon-sources.json`。本项目对照基线为 `dev` 上的 Outline 提交 `df01a4c`。

这里的 Shader Core 指多种材质共用的着色代码；Pass 指一次渲染所执行的一组着色程序；Variant（变体）指按功能开关编译出的不同程序版本。

## 1. Shader Core 与功能组合

lilToon 的普通 `lts.shader` 通过 `UsePass` 引用隐藏 Shader 的 Forward、Forward Add、ShadowCaster、Meta 等 Pass，并指定 `lilToon.lilToonInspector` 为编辑器入口。[源码：lts.shader](https://github.com/lilxyzw/lilToon/blob/4a3f868398fda5b7f5a254e506f86e43207103c3/Assets/lilToon/Shader/lts.shader#L640)

官方结构文档说明，常见变体复用 `ltspass_xx.shader`，部分 Fur、Gem、Refraction、Multi 等有独立 Pass；Pass 再引用公共 HLSL，并通过宏选择路径。该说明也包括由脚本切换不同 Unity 渲染管线的机制。[官方结构文档](https://lilxyzw.github.io/lilToon/ja_JP/dev/shader_structure.html)

实际普通 Forward 片元流程在表面输入与明暗处理后，依次经过 RimShade、Backlight、Refraction、Reflection、MatCap、第二层 MatCap、Rim Light 等阶段；各阶段带 `BEFORE_*` 和 `OVERRIDE_*` 扩展入口，并受 `LIL_FEATURE_*` 控制。这些入口允许在明确位置增加或替换行为。[源码：lil_pass_forward_normal.hlsl](https://github.com/lilxyzw/lilToon/blob/4a3f868398fda5b7f5a254e506f86e43207103c3/Assets/lilToon/Shader/Includes/lil_pass_forward_normal.hlsl#L487)

公共片元文件中的明暗代码使用 ShadowBorder、ShadowBlur 和抗锯齿函数控制分界，MatCap 使用方向计算贴图坐标并混合，Rim 在运行期开关 `_UseRim` 内计算边缘响应；不是所有功能都只靠一个编译开关。Outline 则在公共顶点路径中计算宽度、遮罩和顶点色影响，属于几何路径。[公共片元代码](https://github.com/lilxyzw/lilToon/blob/4a3f868398fda5b7f5a254e506f86e43207103c3/Assets/lilToon/Shader/Includes/lil_common_frag.hlsl#L1517)，[公共顶点代码](https://github.com/lilxyzw/lilToon/blob/4a3f868398fda5b7f5a254e506f86e43207103c3/Assets/lilToon/Shader/Includes/lil_common_vert.hlsl#L353)

本项目可借鉴“公共输入—明确阶段—可定位扩展点”的组织方式。但当前 Outline 是 UE 材质图、辅助 Mesh 和后处理；没有可直接照搬的 ShaderLab UsePass。现阶段把各路径写清楚，比为匹配 lilToon 文件结构拆改现有代码更合适。

## 2. 参数定义与传递

lilToon 的 ShaderLab `Properties` 声明名称、类型、默认值和范围，例如 `_Color`、`_MainTex`、`_MainTexHSVG`。Inspector 中的 `lilMaterialProperty` 以相同名称组织属性并标记所属功能组；HLSL 的 `UnityPerMaterial` 常量缓冲和纹理声明承接这些输入。常量缓冲是 GPU 读取一组材质数值的地方。[Properties](https://github.com/lilxyzw/lilToon/blob/4a3f868398fda5b7f5a254e506f86e43207103c3/Assets/lilToon/Shader/lts.shader#L40)，[Inspector 属性表](https://github.com/lilxyzw/lilToon/blob/4a3f868398fda5b7f5a254e506f86e43207103c3/Assets/lilToon/Editor/lilInspector/lilMaterialProperties.cs)，[GPU 输入声明](https://github.com/lilxyzw/lilToon/blob/4a3f868398fda5b7f5a254e506f86e43207103c3/Assets/lilToon/Shader/Includes/lil_common_input.hlsl#L104)

本项目 Outline 的对应链路是 `FNPROutlineSettings → ApplyMaterialParameters → MID 参数 → 材质图`，全局语义则另有 JSON 契约。可借鉴集中属性清单，但必须增加“视觉语义 → 后端参数”这一层，不能让 Shader pin 名成为唯一艺术接口。[当前映射](../principled/mapping.md)与[自动参数参考](../principled/parameters.md)分别记录含义和客观数据。

## 3. Inspector 分组和依赖

`lilToonInspector::OnGUI` 收集材质目标，`DrawAllGUI` 初始化设置、绑定属性、检查 Shader 类型，随后切换 Advanced、Preset、Settings 页面。属性改变后还会处理版本标识、Multi 材质设置、关键词以及兼容颜色/贴图字段。[Inspector 主流程](https://github.com/lilxyzw/lilToon/blob/4a3f868398fda5b7f5a254e506f86e43207103c3/Assets/lilToon/Editor/lilInspector/lilInspector.cs#L35)

MatCap/Rim 面板先用 `ShouldDrawBlock` 判断功能组，再显示折叠面板和启用开关，启用后显示内部属性；也有针对特定混合组合的提示和修复入口。因此“是否显示、是否启用、依赖是否合法”是不同的判断。[功能面板源码](https://github.com/lilxyzw/lilToon/blob/4a3f868398fda5b7f5a254e506f86e43207103c3/Assets/lilToon/Editor/lilInspector/lilPropertyGroupDrawerNormalMapAndReflectionSetting.cs#L18)

UE 对应实现是 Slate 窗口和 Details 属性面板（UE 的自定义界面与属性编辑工具），借助 Category、EditCondition、AdvancedDisplay 加上 Validate/RefreshOutline。后续应逐步补足可读错误、参数来源和能力说明，保留现有双视口、按钮和样式；不直接重写成 Unity Inspector 布局。

## 4. Preset 保存、读取和应用

`lilToonPreset` 是 ScriptableObject（Unity 可保存的数据资产），包含按名称保存的颜色、向量、浮点、纹理数组，以及 Shader、渲染模式、队列和描边选择。纹理条目保存引用、偏移与缩放。`ApplyPreset` 记录撤销，处理模式，再调用材质 SetColor/SetVector/SetFloat/SetTexture 等；`LoadPresets` 按资产类型查找。[数据结构和应用](https://github.com/lilxyzw/lilToon/blob/4a3f868398fda5b7f5a254e506f86e43207103c3/Assets/lilToon/Editor/lilToonPreset.cs#L9)

保存界面可选择功能分组与纹理；随后创建 `.asset` 并保存到 AssetDatabase。这不是完整角色的 Mesh/Section 配置格式。[保存与属性筛选](https://github.com/lilxyzw/lilToon/blob/4a3f868398fda5b7f5a254e506f86e43207103c3/Assets/lilToon/Editor/lilToonPreset.cs#L357)

UE 对应为 `UNPROutlinePreset` / UDataAsset、Settings 校验及编辑器资产保存。当前预设保存基础设置，角色局部规则存关卡；不能声称已支持 lilToon 那样任意功能组的选择性预设。可考虑在兼容现有 v1 的前提下增加版本迁移、选择性字段和依赖清单，先写方案再实现。[本项目预设](../editor/presets.md)

## 5. Pass、变体和扩展机制

需要区分三层：Shader 变体选择使用哪些 Pass；`LIL_FEATURE_*` 决定哪些功能编译进程序；`_UseRim` 等材质数值在已编译路径中决定是否执行。Multi 还使用关键词，不能把所有开关当成同一套机制。[结构说明](https://lilxyzw.github.io/lilToon/ja_JP/dev/shader_structure.html)，[运行期开关源码](https://github.com/lilxyzw/lilToon/blob/4a3f868398fda5b7f5a254e506f86e43207103c3/Assets/lilToon/Shader/Includes/lil_common_frag.hlsl#L1643)

UE 则由材质编译、静态开关和引擎渲染阶段管理不同程序，MID 适合调整已有数值/纹理，不能任意改变静态编译路径。本项目 `SetScalarParameter` 就只更新已存在的浮点参数。当前 Outline 的额外绘制和后处理不是 lilToon 的 Forward Add。后续扩展需明确“需要重新编译还是可以运行时修改”，并分别测试性能和打包结果。

## 6. 多材质、多 Mesh / Section

lilToon Inspector 的 `materialEditor.targets` 和 Beta Multi-Editor 可对多个选中材质取得公共属性并统一编辑；不同 Shader 的选择会被记录为 `isMultiVariants`。这是材质批量编辑证据，不能据此推定已有角色范围的区域继承、跨 Mesh 外轮廓合并或 UE Section 隔离。[多材质入口](https://github.com/lilxyzw/lilToon/blob/4a3f868398fda5b7f5a254e506f86e43207103c3/Assets/lilToon/Editor/lilInspector/lilInspector.cs#L152)

本次所读文件未找到与本项目 Actor → Component → Material → Section 完全等价的角色解析器；这不是对 lilToon 全仓库或其他配套工具“绝无此能力”的断言。UE 的一个材质槽可对应多个 Section，当前隔离方案因此只支持单 LOD、独占槽；跨 Mesh 统一 Screen 轮廓由本项目 Stencil 分组处理。[当前粒度边界](../features/outline.md)

## 7. 改进顺序与自主研究

| 顺序 | 建议 | 原因与验收 |
| --- | --- | --- |
| 当前 | 完成真实手册、参数校验、状态清单 | 本次实施，不改变渲染或参数含义 |
| 下一步 | 补全语义 Resolver 和能力诊断 | 验证继承、显式零、Reset、缺输入；不只看 UI |
| 随后 | 标定 Generic 后端，再单独验证 Face/Hair | 角宽、能量、阴影和 GI（间接光）会相互影响 |
| 独立模块 | Rim、MatCap 与颜色控制 | 先验证算法和可见性，再定义兼容参数与界面 |
| 后续 | 选择性预设、版本迁移和批量操作 | 保留现有 v1 和角色本地规则 |

Principled 映射不能从 lilToon 参数名直接得到。需要自主研究视觉量的测量方法、不同策略下的对应关系、误差和适用范围，尤其是像素/几何宽度、角域高光、脸部逐灯控制、发丝方向与独立边缘光可见性。高层语义稳定，底层实现可以不同；差异须公开记录。

## 8. 文档体验与署名

官方 Main Color 页先解释用途，再列参数、处理结果和注意点。本项目采用相同的可读顺序，使用中文和真实截图；尚未实现的功能明确写规划。[官方页面参考](https://lilxyzw.github.io/lilToon/ja_JP/color/maincolor.html)

lilToon 作者为 lilxyzw，源码采用 [MIT License](https://github.com/lilxyzw/lilToon/blob/4a3f868398fda5b7f5a254e506f86e43207103c3/LICENSE)。本次只做架构研究和链接引用，没有复制其 Shader、Inspector、图片或样式到插件。后续复制代码/实质片段时，应随副本保留适用版权与许可全文；其他来源资源需分别核对，不能默认继承 lilToon 的许可。
