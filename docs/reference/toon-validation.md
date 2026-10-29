<!-- Generated from Documentation/ToonLight/ValidationReport.md. Edit the original, then run npm run docs:generate. -->

# Toon Light 本轮验证

2026-10-28 至 29，我们完成了五个 Generic Toon 参数、模型配置、原图派生、私有 Profile 预览和不可变包绑定的产品链路验证。结果限于 UE 5.8.3 / CL 58210709、Windows 11、NVIDIA RTX 4060 Laptop、DX12 SM6；完整 S58 生产候选仍为未验收。

机器可读结果在 [Validation.json](/generated/source/Documentation/ToonLight/Validation.json)。[源码哈希](/generated/source/Documentation/ToonLight/Validation/SourceHashes.json)、[资产哈希](/generated/source/Documentation/ToonLight/Validation/AssetHashes.json)和[二进制哈希](/generated/source/Documentation/ToonLight/Validation/BinaryHashes.json)对应本轮交付；旧研究、旧 GPU 报告与原始数学文件保留原状。

源码哈希记录实测 Windows 工作区的原始字节。Git 会规范化普通源码换行，跨平台核对时可结合参数页的 LF 规范化指纹；研究与验收记录、用户提供的数学原文则配置为保留原始字节，图片仍使用 LFS。

## 已有实测结果

| 检查 | 本轮结果 | 证据与边界 |
| --- | --- | --- |
| Editor / Game Development 构建 | 成功 | [Editor](/generated/source/Documentation/ToonLight/Validation/FinalEditorBuild.log.txt)、[Game](/generated/source/Documentation/ToonLight/Validation/FinalGameBuild.log.txt)；有引擎头文件弃用和工具链版本提示，不将其描述为零编译警告 |
| Cook / Stage / Archive | 成功 | [UAT 日志](/generated/source/Documentation/ToonLight/Validation/FinalCook.log.txt)，最终一轮 140.11 秒；这是本机有缓存的单次执行，不是性能基准 |
| Editor 自动化 | 13 成功，0 错误/警告 | [原始结果](/generated/source/Documentation/ToonLight/Validation/EditorAutomation.json)：7 项 Toon，6 项既有 Rim/Surface 回归 |
| 最后 UI 改动复测 | 2 成功，0 错误/警告 | [原始结果](/generated/source/Documentation/ToonLight/Validation/UIAutomation.json)：最终 Slate 输入、描边辅助网格回溯及私有预览源码复测；包含长名称换行和取色器弱引用回调 |
| 独立打包 Development Game | 2 成功，0 错误/警告 | [原始结果](/generated/source/Documentation/ToonLight/Validation/GameAutomation.json)：契约、保存关卡的注册恢复、实际 MID、Toon → Default Lit → Toon 材质父级切换 |
| 跨源码契约检查 | 25 项通过 | [ContractChecks](/generated/source/Documentation/ToonLight/ContractChecks.json)，只证明 JSON/C++ 的 ID、默认、范围及资源守卫对应；不替代渲染 |

运行时用例另测双槽各自 MID、共享不可变 Profile、重复/越界槽及非法语义整体失败、失败后保持原材质、外部材质替换保护。Editor 用例另测稀疏 Reset、明确的 0、Undo、逐槽草稿、高级原生 Ramp、源风格 Rim 数据保留、原始粗糙度恢复及不改变源材质。继承模式另测 Subsurface 模型、专用输入及材质实例模型覆写保持，不用仅有枚举值相同代替这些结构检查。

保存与重开使用独立打包 Game 读取 `/Game/NPR/ToonValidation/L_ToonRuntime3`；两个角色分别硬引用 Toon 和 Default Lit 包，使 Cook 包含二者。主要 Toon 包为 `/Game/NPR/ToonValidation/BP_ToonStyle6.BP_ToonStyle6`。包的逐参数质量记录仍保留近似与未完整验收状态。

## GPU 响应与误差

通过实际 `NPRToonBackend` 创建材质，并通过 `NPRStyleBindingComponent::ApplyPackage` 绑定平面。使用无自影、零光源角度的方向光、白 BaseColor、固定相机、关闭 GI/反射/时域 AA 的 HDR 夹具。原生 Profile 分辨率 R=64，目标为 RGBA16f，读回保存 float32 RGBA；统计使用中心区域。

| 项目 | 实测 | 判据与含义 |
| --- | --- | --- |
| 捕获 | 23 张：14 漫反射、9 高光，含各自归一化参考 | PNG 只用于显示；数值来自 HDR 原始读回 |
| 漫反射相对理想函数的最大绝对误差 | 0.04306640625 | 如实保留；主要包含 R64 端点/转折过滤差异，不宣称理想函数精确复现 |
| 漫反射相对原生采样参考的最大残差 | 0.0072998046875 | 门限 0.025；参考独立按源码的 `(i+0.5)/R`、FP16、双线性钳位计算 |
| 高光相对角域参考的最大绝对误差 | 0.0222891381133 | 门限 0.12；仅含本夹具 8°/18°、峰值 0/1/2 等指定样本，仍属近似 |
| 热更新新增 Shader jobs / 材质翻译 | 0 / 0 | 在材质准备后，累计 JobsAssigned 与 MaterialTranslateCalls 前后相同；ShaderMap 保持同一对象 |

原始数据：[RenderMetrics.json](/generated/source/Documentation/ToonLight/Validation/RenderMetrics.json)、[PNG 与 HDR 原始数据 ZIP](/generated/source/Documentation/ToonLight/Validation/RenderData.zip)。ZIP 中 `.rgba32f` 为 256×256、小端 float32 RGBA，不含文件头。

最初按理想函数给端点设置 0.04 门限时失败，保留在 [R64Initial](/generated/source/Documentation/ToonLight/Attempts/R64Initial/Automation.json)。后续依据已经确认的原生采样规则增加过滤参考，理想误差继续单列，没有通过放宽“精确”定义隐藏差异。原生过滤参考和艺术目标分别用于判断实现错误与后端限制。

## 原生页面与区域指定

![真实 Toon 页面，参数曲线放在左侧](/generated/source/Documentation/ToonLight/Validation/UI/Wide.png)

![真实窄窗口，曲线拖动后中心为 0.45](/generated/source/Documentation/ToonLight/Validation/UI/Narrow.png)

来源均为 `NPR.Toon.WidgetRender` 的实际 Slate 窗口离屏导出，没有使用生成图或后期合成。最终夹具包含既有角色的真实描边配置和辅助网格；区域通过 Slate down/up、视口几何转换、真实 Hit Proxy 和源网格回溯选择到槽 6、2，见 [PickedSlots](/generated/source/Documentation/ToonLight/Validation/UI/PickedSlots.txt.txt)。曲线通过 Slate down/move/up 路由把中心从 0.20 改到 0.45；左右栏具有独立滚动。

测试夹具的首次空角色与输入断言保留在 [WidgetInitial](/generated/source/Documentation/ToonLight/Attempts/WidgetInitial/README.md.txt)。最终通过前修正了夹具默认子组件的临时标记与仅包含几何的 Slate 路径；没有放宽生产网格清单过滤。

实机中也保留描边直接点击脸部、服装，分别选择到槽 6、2；文本框把中心和半宽改到 0.20、0.08，曲线及私有预览随之变化。原生取色器接受 sRGB `285EAAFF`，显示线性 RGB 约 0.021/0.112/0.402。桌面操作的最终范围与保存证据见 [ManualChecks](/generated/source/Documentation/ToonLight/Validation/ManualChecks.json)。动画、多 LOD 交互和角色美术成片不在这个测试范围。

![实机最终窗口：左侧小曲线、中央预览、右侧可视区域指定](/generated/source/Documentation/ToonLight/Validation/UI/ManualWorkspace.jpg)

来源为最终源码运行的原生 UE 桌面窗口截图。通过窗口按钮保存艺术风格 `/Game/NPR/Styles/Style_Toon`、完整后端预设 `/Game/NPR/Generated/Toon/Presets/BP_ToonStyle`，应用时另存不可变包 `/Game/NPR/Generated/Toon/BP_ToonStyle`，随后保存独立测试关卡 `/Game/NPR/ToonValidation/L_ToonWorkspace_20261029_204340`。保存日志见 [实机摘录](/generated/source/Documentation/ToonLight/Validation/ManualRunExcerpt.log.txt)，取色器原图见 [ManualColorPicker](/generated/source/Documentation/ToonLight/Validation/UI/ManualColorPicker.jpg)。此关卡的按钮操作记录与上面的独立 Cook/Game 恢复测试分别列出，不混用其覆盖范围。

首次实机点选失败及两次中间输入尝试保留在 [ManualInput](/generated/source/Documentation/ToonLight/Attempts/ManualInput.md.txt)。诊断显示描边辅助网格收到了命中，旧映射没有找到源副本；最终沿附着关系回溯，保持原描边算法与效果。中间一次成功的八项用例包含一条 UE Google 联网检测超时警告，原始 JSON 保留；最终 13 项与最后两项复测均为零警告，不覆盖或隐藏中间记录。

## 数学推导、源码确认与未验证假设

- **数学推导**：有符号 N·L 转为 u=(q+1)/2，中心 t=(τ+1)/2，半宽 h=w/2；Hermite 的导数及裁剪区间独立推导并用密集 CPU 样本检查。零宽具有明确 0/0.5/1 参考极限。
- **源码确认**：UE 原生曲线以 `(i+0.5)/R` 烘焙 FP16 并双线性钳位读取；Profile 改动与材质结构编译属于不同路径。原始引擎路径、函数与先前确认见[实时研究](../research/toon-live.md)，本轮没有新增项目 HLSL 或 Shader Pass。
- **已有实测**：上面的 HDR 误差、实际资源身份、累计编译计数、保存后 Game 的父材质切换及 UI 事件；它们不能外推到任意场景。
- **尚未验证**：全视角高光等价、矩形光/各向异性、动画、多 LOD 实际角色操作、大量 Profile 的实时帧率、冷启动 PSO 和完整 S58/Face/Hair 门禁。首次 Game 启动已有 PSO miss，不能承诺无卡顿。

30Hz 合并只降低输入通知次数；原生 LUT/图集仍可能整体更新。Runtime 使用预制资源包，不在线修改共享 Profile。精确帧时间、GPU timestamp 和 Shipping 目标需要另设测试；本轮不借用旧隔离工程的时间数字作为产品性能。

## 复现入口

在本工程完成 Editor 构建后，用 `Automation RunTests NPR.Toon` 执行 Toon 七项用例；用例会在 `Saved/ToonValidation` 写出最新包与关卡路径。`NPR.Toon.Persistence` 应在 Adapter 成功后运行，Game 测试应使用该次生成并 Cook 的关卡与包。

BuildCookRun 的完整参数保留在 UAT 日志。Game 以 `-ToonPackage=/Game/NPR/ToonValidation/BP_ToonStyle6.BP_ToonStyle6` 和该保存关卡启动，执行 `Automation RunTests NPR.Toon`，读取出的两项用例必须均成功；Editor 专用测试不会出现在 Game 中。各自动化摘录见 [Editor](/generated/source/Documentation/ToonLight/Validation/EditorRunExcerpt.log.txt)、[UI](/generated/source/Documentation/ToonLight/Validation/UIRunExcerpt.log.txt)、[Game](/generated/source/Documentation/ToonLight/Validation/GameRunExcerpt.log.txt)。

原有 Outline/Rim/Surface 的全部历史图像门禁没有在本轮重跑；六项现有回归只是明确列出的有限覆盖。文档检查与构建结果另见 [DocsChecks](/generated/source/Documentation/ToonLight/Validation/DocsChecks.json)，不能替代上述 UE 检查。
