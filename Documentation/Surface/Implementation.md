# Surface Master Family 实施记录

本轮正式采用方案 B，新增固定 Default Lit / S58 Toon 家族、共享表面输入和贡献组合函数、预制 Recipe 与小型选择器。没有运行时材质图生成器，没有改写 Outline 或原有 Rim。

`UNPRSurfaceRecipe → ComposePackage → UNPRBackendStylePackage → ApplyPackage → 同一槽 MID` 是真实调用链；原有 `SetRimParameters` 在这个 MID 上校验并更新四项 Rim 参数。完整 Style Resolver 和跨后端 Execution Plan 尚未实现，旧数据结构和接口保留。

`MF_NPRSurfaceInputs` 输出 BaseColor、NormalTS、BaseEmissive；`MF_RimLight` 输出原有贡献；`MF_NPRAdditiveColor` 输出第二份贡献。`MF_NPRComposeEmissive` 固定相加，两个 Master 只暴露参数、连接共用函数和选择输出。技术参数见 `SurfaceContract.v1.json`；它们不是新的全局语义 ID。

资产位于 `/PrincipledNPR/Surface`。构造工具使用 `NPRSurfaceOwner` 标签，拒绝覆盖其他来源资产；已完成资产复用，不覆盖美术改动。原有 Rim、Outline 和 ToonProfile 不保存、不修改。Toon 家族引用 `/Game/NPR/Styles/AnimeFlat/Profiles/TP_S58_InitialDiffuseSeed`，依赖当前工程未标定探针资源，不是脱离本工程的生产默认资产；Default Lit 无此依赖。

## 验证方式

- Game 在主工作区构建；旧编辑器占用 DLL 时，在 `work/SurfaceValidation` 构建相同源码的 Editor。共享 Content 仅用于创建获授权的新 Surface 资产。
- 所有 GPU 测试使用独立 FPreviewScene，不保存共享角色、地图或旧资产。
- 创建资产后，单独只读审查真实默认值、参数类型、图连接、Recipe 归属和 ToonProfile 引用。
- `NPR.Surface.Composer` 验证选择、失败诊断、归属、原有绑定和同 MID 调参；`NPR.Surface.RenderComposition` 验证真实缓冲、HDR 贡献相加、Outline 与 Toon 共存。
- 原有三项 `NPR.Rim` 用例在隔离目录重跑，保留原有历史证据。

最终状态、环境、时间、结果和限制见 `ImplementationValidation.json`。脚本通过或空编译错误列表不能代替 GPU 验收。

## 本轮结果（2026-10-22）

最终 GPU 捕获于 2026-10-22 12:44 UTC（北京时间 20:44）。环境为 UE 5.8.3 / CL 58210709，Windows 11，Intel UHD Graphics，D3D12 SM6，Deferred / Substrate Blendable。范围是独立预览场景中的普通静态球体、单材质槽、512 × 512 捕获，不保存原工程地图。

| 要求 | 结果与证据 |
| --- | --- |
| 基础材质正常渲染 | 带颜色纹理与切线法线的真实球体；`Validation/SurfaceBase.png` |
| Rim 显示及旧接口调参 | Composer 选择材质，经旧 ApplyPackage 创建 MID，旧 SetRimParameters 写入后图像变化；新 Runtime 与 GPU 两项用例均通过 |
| 原表面属性保留 | 切换 Rim 前后 BaseColor / Normal 缓冲平均差均为 0；平坦法线对照差约 0.3064，证明法线纹理实际使用 |
| 原自发光和多贡献 | BaseEmissive、Rim、AdditionalEmission 独立开启均可见；真实 HDR 浮点读回的相加相对误差 0.0088481，小于 0.01 门限；不宣称零误差 |
| 第二效果扩展 | 独立 MF_NPRAdditiveColor、参数归属、输出契约与组合输入，已在同一 MID 上动态更新；MatCap 等算法未实现 |
| Toon + Rim 同槽 | M_NPRSurface_Toon 使用真实原生 Toon BSDF 和既有 Profile；同槽稳定 MID 与 GPU 差异通过，非 Toon 全语义/质量验收 |
| Outline 兼容 | 真实 Screen 与 Hull 共存捕获可见，Surface MID 未被替换；未重跑全部交互、Preset、区域或时间稳定性流程 |
| 独立 Rim 回归 | 隔离工程三项旧自动化和八项图像/自动化门禁通过；新结果在 RimRegression，不覆盖旧 Documentation/RimLight |
| 构建及资产 | 主工程 Game / Editor 构建成功；90 项真实资产只读检查、17 张最终 GPU PNG、18 项汇总门禁全部通过 |

第一次捕获出现默认材质，之后增加资产/Shader 编译完成等待；Screen 还需 SceneCapture 保留渲染状态。最终修正只涉及新测试夹具，没有修改旧 Outline。失败证据在 `Attempts/InitialCaptures`、`Attempts/InitialAutomation`、`Attempts/ReadyCaptures`、`Attempts/ReadyAutomation` 保留，不作为通过图像。

旧编辑器进程持有原 DLL，本轮使用 UBT 正式支持的 ModuleWithSuffix 编译主工程 Editor，最终为后缀 6202；没有手改模块清单。新主工程命令行进程成功加载新类和资产并通过两项 Surface 自动化。已经打开的旧编辑器需要重开工程以加载新增反射类型。

## 新增资产（10 个）

全部位于 `Plugins/PrincipledNPR/Content/Surface`，UE 路径前缀 `/PrincipledNPR/Surface/`：

- `MF_NPRSurfaceInputs`、`MF_NPRAdditiveColor`、`MF_NPRComposeEmissive`：共享函数。
- `M_NPRSurface_DefaultLit`、`M_NPRSurface_Toon`：两个输出家族。
- `SR_DefaultLit_Rim`、`SR_Toon_Rim`：能力、参数归属、组合规则和预制材质引用。
- `MI_NPRSurface_Preview`：Default Lit 预览实例。
- `T_SurfaceValidationChecker`、`T_SurfaceValidationNormal`：编写的测试输入，不是生成的验收画面。

`MF_RimLight` 与 Toon 探针 Profile 是复用资产，不在新增清单内。二进制真实连接、默认值、依赖与哈希见 `AssetAudit.json`，创作记录见 `AssetCreation.json`。

## 本轮源码与文档文件

新增源码：

- `Plugins/PrincipledNPR/Source/NPRRuntime/Public/NPRSurfaceRecipe.h`
- `Plugins/PrincipledNPR/Source/NPRRuntime/Private/NPRSurfaceRecipe.cpp`
- `Plugins/PrincipledNPR/Source/NPRRuntime/Private/Tests/NPRSurfaceComposerTests.cpp`
- `Plugins/PrincipledNPR/Source/NPREditor/Private/Tests/NPRSurfaceRenderTests.cpp`
- `Tools/create_surface_master_assets.py`、`Tools/audit_surface_master_assets.py`、`Tools/check_surface_validation.py`

新增契约与证据：`Documentation/Surface/SurfaceContract.v1.json`、本记录、AssetCreation / AssetAudit / ImplementationValidation JSON、Validation / RimRegression / Attempts 目录。没有修改之前已经存在的插件源码或资产；工作区原本未提交的 Rim/Outline 改动也按开工基线保留。

修改或新增文档入口：`docs/features/surface-material.md`、`docs/features/base-color.md`、`docs/features/toon-shading.md`、`docs/features/rim-light.md`、`docs/developer/architecture.md`、`docs/developer/shader-pipeline.md`、`docs/principled/mapping.md`、`docs/reference/status.md`、`docs/feature-status.json`、`docs/changelog.md`、`docs/index.md`、`docs/.vitepress/config.mts`、`Documentation/Architecture.md`、`Tools/README.md`、`Tools/docs.mjs`，以及自动生成的参数页、离线来源和契约示意图。沿用既有样式，仅增加 Surface 导航入口。

## 复核方式与边界

先构建 Game / Editor。资产创建脚本仅对新资产执行一次，后续使用只读审计；修改脚本本身不更新已经保存的图。自动化执行 `Automation RunTests NPR.Surface`，使用真实 D3D12 RHI，导出报告到 `Saved/Automation/SurfaceFinal`。GPU 用例会更新新 `Documentation/Surface/Validation` 图像，不会保存地图。

`Tools/check_surface_validation.py` 用带 Pillow 的 Python 读取真实报告、资产哈希、GPU 指标和本次保存在 `Saved/NPR/Surface/Before.json` 的开工基线。它不是脱离本地日志即可运行的通用 CI；缺少基线不能证明保留性。构建日志同在 `Saved/NPR/Surface`；汇总中保存环境、指标、源码/资产/图像哈希以便追溯。旧 Rim 图像门禁在隔离目录运行，避免旧工具覆盖原证据。

还未完成：完整 Style Profile / Backend Resolver / Execution Plan；Surface 调制 UI；角色原图自动转换；MatCap、风格化 Specular；角色动画、透明裁切、多 LOD、逐灯阴影、GI、Cook 和性能验收。新增技术参数的原生 MID setter 不校验数值域，调用方按契约验证；Recipe 校验只保证类型、唯一归属、依赖、已声明参数和 Rim 固定 Mapping，不对任意图的实际组合语义作推断。

## 文档检查

本轮 `npm run docs:generate`、`npm run docs:check`、`npm run docs:build` 通过：18 项原 Principled 语义参数、13 项原 Outline 字段、24 篇正文、12 项功能状态；静态产物 26 个 HTML、2171 个本地链接/资源/锚点检查通过。`python Tools/verify_semantic_drafts.py` 的两份草案数据检查通过，不代表完整 Schema 或渲染验收。构建保留既有大于 500 kB 的前端 bundle 提示，没有以此改变现有 UI。

应用内浏览器打开本地构建 `/features/surface-material.html`，已查看侧栏、页内导航和真实 GPU 图片正常显示；使用既有样式，没有新增 Mermaid。该预览不等于 GitHub Pages 发布，本轮没有提交或推送。
