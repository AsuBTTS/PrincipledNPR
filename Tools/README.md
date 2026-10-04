# 开发工具

正常打开项目、查看角色和使用已保存的材质，不需要执行这些脚本。日常保存使用 UE 编辑器的“保存全部”。Python 不参与运行时逐帧渲染。

当前保留 12 个 Python 文件：7 个资产开发工具、2 个验证工具、3 个 Git 配置工具。它们只在相应开发工作需要时显式执行。

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

流浪者重建流程按转换 → 导入 → 创建双风格场景 → 完成材质绑定的顺序执行。已有 `.blend`、FBX 和 UE 资产均已保存，协作者无需为打开项目重复转换或导入。

这些工具有项目身份和资产所有权检查，不作为启动脚本自动运行。`finalize_two_character_import.py` 会设置初始镜头、天空光与流浪者默认材质；已有手工调整时，不要把它当作日常保存命令。具体输入、资产路径与当前限制见 `Documentation/Assets/TwoCharacterSetup.md`。

## 验证

| 脚本 | 用途 |
|---|---|
| `verify_reference_math.py` | 检查参考模型的标量恒等式、连续性和边界案例；不是 GPU 渲染验收 |
| `verify_semantic_drafts.py` | 检查风格草案的 ID、数值域与报告来源；不是正式风格视觉验收 |

两者使用普通 Python 3，不需要启动 UE。

## Git 协作

在新克隆中准备提交前，使用普通 Python 3 执行：

```sh
python Tools/configure_git_backup.py
```

`configure_git_backup.py` 配置本地 Git LFS 与 clean filter；它调用 `git_config_clean.py` 和 `git_record_clean.py`，在提交时过滤本机令牌、引擎注册 GUID 和本机文件路径。后两个文件是过滤器依赖，不是用户操作入口。配置只写入本地 `.git/config`，不会把本机 Python 或引擎安装路径上传。

## 已移除的临时脚本

`fix_probe_transforms.py`、`save_wanderer_skeleton.py`、`set_character_front_view.py` 和 `save_project_for_github.py` 已完成本次修复或保存任务，其必要逻辑已有正式工具或 UE 原生操作承接，因此从当前项目移除。历史实现可从 Git 历史查看，不在当前工具目录另存一份。

`Open-PrincipledNPR.ps1` 是可选启动助手；也可直接打开 `.uproject`。

`Build-PrincipledNPR.ps1` 是 C++ 构建 / IDE 工程生成入口，接收自己的 UE 安装目录；不参与运行时渲染。构建前保存并关闭编辑器，详见 `Documentation/C++骨架.md`。
