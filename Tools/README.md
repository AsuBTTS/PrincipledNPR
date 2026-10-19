# 开发工具

正常打开项目、查看角色和使用已保存的材质，不需要执行这些脚本。日常保存使用 UE 编辑器的“保存全部”。Python 不参与运行时逐帧渲染。

开发脚本只在相应工作需要时显式执行；普通使用不依赖 Python 验证会话。

多粒度 Outline：`audit_outline_assets.py` 只读导出二进制材质连接和引用；`verify_outline_multiscope.py` 验证独立拥有的多组件/区域场景，环境变量 `NPR_MULTI_STAGE=Create|Reopen`；`Measure-MultiScopeOutline.ps1` 测量实际 PNG。`run_outline_validation_session.py` 是开发期间的临时会话工具，只接受允许列表中的脚本，完成后必须提交 exit 请求关闭。`verify_outline_regression_and_audit.py` 串联原单槽回归与材质审计。

多层宽度：插件编译后，`update_outline_width_assets.py` 更新自有 Hull Mask 材质/默认白纹理，并收细自有演示角色（1.25 px / 0.1 cm）；它是资产创作脚本，不运行测试。普通使用直接在属性面板调节，见 [多层宽度控制](../Documentation/Outline/多层宽度控制.md)。

`Measure-MultiScopePreview.ps1` 检查整组双视口的实际人物、关闭描边、颜色与宽度变化。`open_outline_character_demo.py` 选择真实带纹理人物并打开整组预览，可通过普通编辑器的 `-ExecCmds="py <脚本绝对路径>"` 启动，不开启持续验证会话。

新增功能用法见 [多粒度 Outline 使用说明](../Documentation/Outline/MultiScope/使用说明.md)，范围与限制见 [审查与设计](../Documentation/Outline/多粒度审查与设计.md)。Create 只会重置带归属标记的专用测试关卡。

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
| `create_outline_workflow_fixture.py` | 创建独立的单角色、单 Opaque 槽描边测试场景；已有场景只读取 | UE 编辑器 Python |

流浪者重建流程按转换 → 导入 → 创建双风格场景 → 完成材质绑定的顺序执行。已有 `.blend`、FBX 和 UE 资产均已保存，协作者无需为打开项目重复转换或导入。

这些工具有项目身份和资产所有权检查，不作为启动脚本自动运行。`finalize_two_character_import.py` 会设置初始镜头、天空光与流浪者默认材质；已有手工调整时，不要把它当作日常保存命令。具体输入、资产路径与当前限制见 `Documentation/Assets/TwoCharacterSetup.md`。

## 验证

| 脚本 | 用途 |
|---|---|
| `verify_reference_math.py` | 检查参考模型的标量恒等式、连续性和边界案例；不是 GPU 渲染验收 |
| `verify_semantic_drafts.py` | 检查风格草案的 ID、数值域与报告来源；不是正式风格视觉验收 |
| `verify_outline_workflow.py` | 真 RHI 下调用与 Outline 窗口相同的服务，检查转换、参数变体导出、保存和独立进程重开；PNG 仍需检查 |

前两个使用普通 Python 3，不需要启动 UE。Outline 验证需要 UE Editor，使用 `NPR_OUTLINE_STAGE=Create` 和 `Reopen` 分别启动两个进程；不能加 `-nullrhi`。普通 Outline 使用不需要运行这些脚本，见 `Documentation/Outline/操作流程.md`。

`verify_outline_workflow.py` 通过 `SetComparisonCaptureSize` 将测试渲染缓冲固定为 351×493；普通窗口不设固定尺寸。Create 仅恢复有明确所有权标记的受控槽，使其可以重复执行。

`Measure-OutlineImages.ps1` 在两个阶段导出后测量 PNG：颜色面积随宽度增长、红绿变化、关闭描边的差异，以及独立进程的黑色描边掩码。使用 Windows PowerShell 5 的 `System.Drawing`：

```powershell
& 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe' -NoProfile -File .\Tools\Measure-OutlineImages.ps1
```

跨进程同时记录精确掩码 IoU 和 RGB 差异，并显式允许 1 px 的光栅/时域抖动：两向覆盖均须达到 95%，描边面积差须小于 5%。此测量不替代全视角、遮挡、性能或 Cook 验收。

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

`Build-PrincipledNPR.ps1` 是 C++ 构建 / IDE 工程生成入口，接收自己的 UE 安装目录；不参与运行时渲染。构建前保存并关闭编辑器，详见 `Documentation/C++骨架.md`。
