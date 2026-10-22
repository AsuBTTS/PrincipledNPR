# 开发工具

正常打开项目、查看角色和使用已保存的材质，不需要执行这些脚本。日常保存使用 UE 编辑器的“保存全部”。Python 不参与运行时逐帧渲染。

开发脚本只在相应工作需要时显式执行；普通使用不依赖 Python 验证会话。

多粒度 Outline：`audit_outline_assets.py` 只读导出二进制材质连接和引用；`verify_outline_multiscope.py` 验证独立拥有的多组件/区域场景，环境变量 `NPR_MULTI_STAGE=Create|Reopen`；`Measure-MultiScopeOutline.ps1` 测量实际 PNG。`run_outline_validation_session.py` 是开发期间的临时会话工具，只接受允许列表中的脚本，完成后必须提交 exit 请求关闭。`verify_outline_regression_and_audit.py` 串联原单槽回归与材质审计。

多层宽度：插件编译后，`update_outline_width_assets.py` 更新自有 Hull Mask 材质/默认白纹理，并收细自有演示角色（1.25 px / 0.1 cm）；它是资产创作脚本，不运行测试。普通使用直接在属性面板调节，见 [多层宽度控制](../Documentation/Outline/多层宽度控制.md)。

`Measure-MultiScopePreview.ps1` 检查整组双视口的实际人物、关闭描边、颜色与宽度变化。`open_outline_character_demo.py` 选择真实带纹理人物并打开整组预览，可通过普通编辑器的 `-ExecCmds="py <脚本绝对路径>"` 启动，不开启持续验证会话。

新增功能用法见 [多粒度 Outline 使用说明](../Documentation/outline_introduction.md)，范围与限制见 [审查与设计](../Documentation/Outline/多粒度审查与设计.md)。Create 只会重置带归属标记的专用测试关卡。

## 资产开发

| 脚本 | 保留原因 | 执行环境 |
|---|---|---|
| `create_principled_npr_project_assets.py` | 重建独立 S58 光照探针、诊断 ToonProfile 和材质 | UE 编辑器 Python |
| `audit_character_assets.py` | 只读检查陈千语资产、依赖和材质槽 | UE 编辑器 Python |
| `create_character_asset_probe.py` | 重建陈千语诊断场景；已包含正确朝向与正面镜头 | UE 编辑器 Python |
| `convert_wanderer_to_fbx.py` | 从原始 PMX 重建可检查的 Blender/FBX 源资产 | Blender Python，需 MMD Tools |
| `import_wanderer_into_ue.py` | 导入流浪者网格、骨骼和贴图；已显式保存生成的骨骼 | UE 编辑器 Python |
| `create_two_style_character_probe.py` | 创建两种初始风格的原生资源与双角色场景 | UE 编辑器 Python |
| `finalize_two_character_import.py` | 完成流浪者默认材质槽绑定，并保存、重载检查双角色场景 | UE 编辑器 Python |
| `create_outline_backend_assets.py` | 重建归属本插件的 Hull / Mask / Screen 描边材质 | UE 编辑器 Python |
| `create_rim_light_assets.py` | 创建角度边缘光材质函数、四项控制的表面材质和预览实例；已有资产保留人工编辑 | UE 编辑器 Python |
| `open_rim_light_material.py` | 刷新当前编辑器的 Rim 目录并打开预览实例，不保存关卡 | UE 编辑器 Python |
| `verify_rim_light_materials.py` | 独立进程重载、编译检查及真实 GPU 球体参数变体导出 | UE 编辑器 Python，真实 RHI |
| `create_outline_workflow_fixture.py` | 创建独立的单角色、单 Opaque 槽描边测试场景；已有场景只读取 | UE 编辑器 Python |

流浪者重建流程按转换 → 导入 → 创建双风格场景 → 完成材质绑定的顺序执行。已有 `.blend`、FBX 和 UE 资产均已保存，协作者无需为打开项目重复转换或导入。

这些工具有项目身份和资产所有权检查，不作为启动脚本自动运行。`finalize_two_character_import.py` 会设置初始镜头、天空光与流浪者默认材质；已有手工调整时，不要把它当作日常保存命令。具体输入、资产路径与当前限制见 `Documentation/Assets/TwoCharacterSetup.md`。

## 验证

| 脚本 | 用途 |
|---|---|
| `verify_reference_math.py` | 检查参考模型的标量恒等式、连续性和边界案例；不是 GPU 渲染验收 |
| `verify_semantic_drafts.py` | 检查风格草案的 ID、数值域与报告来源；不是正式风格视觉验收 |
| `verify_rim_light_contract.py` | 交叉检查 Rim 的 JSON、C++ 声明、默认值、颜色形状、转换、零值与非法输入；普通 Python 3 |
| `audit_rim_light_contract.py` | 在独立 UE 进程只读重载实际材质/函数/实例，检查四项类型、名称、默认值与函数连接；不保存资产 |
| `verify_outline_workflow.py` | 真 RHI 下调用与 Outline 窗口相同的服务，检查转换、参数变体导出、保存和独立进程重开；PNG 仍需检查 |

前两个使用普通 Python 3，不需要启动 UE。Outline 验证需要 UE Editor，使用 `NPR_OUTLINE_STAGE=Create` 和 `Reopen` 分别启动两个进程；不能加 `-nullrhi`。普通 Outline 使用不需要运行这些脚本，见 `Documentation/NPR调制插件使用说明.md`。

`verify_outline_workflow.py` 通过 `SetComparisonCaptureSize` 将测试渲染缓冲固定为 351×493；普通窗口不设固定尺寸。Create 仅恢复有明确所有权标记的受控槽，使其可以重复执行。

`Measure-OutlineImages.ps1` 在两个阶段导出后测量 PNG：颜色面积随宽度增长、红绿变化、关闭描边的差异，以及独立进程的黑色描边掩码。使用 Windows PowerShell 5 的 `System.Drawing`：

```powershell
& 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' -NoProfile -File .\Tools\Measure-OutlineImages.ps1
```

跨进程同时记录精确掩码 IoU 和 RGB 差异，并显式允许 1 px 的光栅/时域抖动：两向覆盖均须达到 95%，描边面积差须小于 5%。此测量不替代全视角、遮挡、性能或 Cook 验收。

### Rim Light 第②步参数交接

`rim_light_contract.py` 提供纯 Python 校验与弧度转材质度数，不写材质。`create_rim_light_assets.py` 从同一映射表读取默认值，但已有自有资产保留人工编辑，因此脚本通过后仍要单独检查实际 `.uasset`。稳定 ID、中文 FName、RGB 存储边界和 C++ 接法见 [参数交接](../Documentation/RimLight/ParameterHandoff.md)。

```powershell
python Tools/verify_rim_light_contract.py
python Tools/verify_semantic_drafts.py
& 'E:\Unreal Engine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'E:\unrealProjects\PrincipledNPR\PrincipledNPR.uproject' -run=pythonscript '-script=E:\unrealProjects\PrincipledNPR\Tools\audit_rim_light_contract.py' -unattended -NullRHI -NoSound -NoSplash -NoSourceControl
```

引擎与项目路径按本机调整。2026-10-22 离线检查 27 项、资产检查 34 项通过；C++ 用例名为 `NPR.Rim.MaterialContract`，执行状态单独见 `Documentation/RimLight/ImplementationValidation.json`。NullRHI 不生成画面，不能替代 GPU 渲染验收。

### Rim Light 第③步动态调参验证

完整 Editor 构建后，在 UE 命令框执行 `Automation RunTests NPR.Rim`，运行参数契约、双槽绑定和真实 GPU 跨帧球体三项用例。`RenderBinding` 使用独立 `FPreviewScene`，不会替换当前关卡的角色材质。每项参数更新复用同一个 MID，导出八张 PNG 到 `Documentation/RimLight/BindingValidation`；`check_rim_binding_images.py` 需要 Pillow / NumPy，用于图像数值门禁。启动时通过 `-ReportExportPath=<project>/Saved/Automation/RimBinding` 保存报告，图像检查读取该报告。

`verify_rim_binding_python.py` 在已打开工程内对临时 Mesh / Binding / Package 调用实际 C++ 反射接口，读回角度、强度和 HDR RGB，检查非法值拒绝、MID 保持与恢复，最后刷新 `MI_RimLight_Preview`；不保存资产、地图或配置。只能在已完整编译新接口的编辑器中执行。UE 的该 Python 方法成功返回字符串、失败返回 None，空串也是成功。

2026-10-22 三项 C++、八项图像/自动化和十项反射检查通过；这些结果只覆盖记录中的受控网格和静态球体。调用和后续 Profile 对接见 [③ 动态调参](../Documentation/RimLight/RuntimeBinding.md)。

## Git 协作

在新克隆中准备提交前，使用普通 Python 3 执行：

```sh
python Tools/configure_git_backup.py
```

`configure_git_backup.py` 配置本地 Git LFS 与 clean filter；它调用 `git_config_clean.py` 和 `git_record_clean.py`，在提交时过滤本机令牌、引擎注册 GUID 和本机文件路径。后两个文件是过滤器依赖，不是用户操作入口。配置只写入本地 `.git/config`，不会把本机 Python 或引擎安装路径上传。

## Outline 连续帧验证

`verify_outline_temporal.py` 在独立真实 RHI 编辑器里采集静止和移动镜头连续帧，比较保留的旧 Screen 材质与修复版。`measure_outline_temporal.py` 用 numpy / Pillow 计算信号变化并输出 GIF；`check_outline_temporal.py` 检查 67 项有界验收条件。依赖诊断相机 API，仅用于开发验证，不参与普通插件启动或运行时。

Screen 材质当前位于 Scene Color After DOF，在 TSR/TAA 之前；保持默认 `r.CustomDepthTemporalAAJitter=1`。完整使用范围及可重现结果见 [抖动修复报告](../Documentation/Outline/Temporal/抖动修复报告.md)。

## 已移除的临时脚本

`fix_probe_transforms.py`、`save_wanderer_skeleton.py`、`set_character_front_view.py` 和 `save_project_for_github.py` 已完成本次修复或保存任务，其必要逻辑已有正式工具或 UE 原生操作承接，因此从当前项目移除。历史实现可从 Git 历史查看，不在当前工具目录另存一份。

`Open-PrincipledNPR.ps1` 是可选启动助手；也可直接打开 `.uproject`。

`Build-PrincipledNPR.ps1` 是 C++ 构建 / IDE 工程生成入口，接收自己的 UE 安装目录；不参与运行时渲染。构建前保存并关闭编辑器，详见 `Documentation/项目开发说明与约束.md`。
## Surface Master Family

`create_surface_master_assets.py` 在 UE 5.8.3 中创建带归属标签的共享函数、两个 Master 家族、Recipe、预览实例与测试纹理；复用原有 `MF_RimLight` 和未修改的 Toon 探针 Profile。已有同名归属资产保留，不重建覆盖。`audit_surface_master_assets.py` 独立只读检查实际保存资产的类型、默认值、图连接及参数归属。

先构建 Runtime / Editor，再在独立 UE 进程创建资产与审查。`NPR.Surface.Composer` 与 `NPR.Surface.RenderComposition` 验证同槽 MID、多效果贡献、BaseColor/Normal 保持及 Outline/Toon 共存；`NPR.Rim` 原有三项测试继续运行。不要在主工作区重跑会覆盖历史图像的 Rim 用例，使用隔离输出并保留旧证据。契约与实施边界见 `Documentation/Surface/`。

`check_surface_validation.py` 使用带 Pillow 的 Python 汇总实际报告、90 项资产审查、HDR 指标、构建日志与开工基线，输出 `Documentation/Surface/ImplementationValidation.json`。它需要本轮 `Saved` 日志和基线，不会凭空证明另一工作区的保留性。新 GPU 用例只更新 Surface 证据目录；完整操作与交付清单见 [实施记录](../Documentation/Surface/Implementation.md)。
