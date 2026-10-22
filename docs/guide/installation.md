# 安装与环境

本仓库是 UE 工程及本地插件，入口为 `PrincipledNPR.uproject`。已有记录使用 UE **5.8.3 / CL 58210709、Win64、DX12 / SM6**；这些是历史验证环境，不代表其他版本已兼容。本次文档任务未重新构建 UE。

1. 获取工程及 Git LFS 资产（`.uasset`、`.umap`、PNG 等由 LFS 管理），确认不是指针文本。
2. 安装该工程对应 UE 5.8 和 C++ 编译工具链；通过 `UE58_ENGINE_ROOT` 或脚本参数提供引擎根目录。
3. 在 PowerShell 工程根目录构建：

```powershell
./Tools/Build-PrincipledNPR.ps1 -EngineRoot '你的UE5.8根目录' -Target Editor
```

4. 打开 `.uproject`，确认 `PrincipledNPR`、PythonScriptPlugin、EditorScriptingUtilities 启用。Python 工具用于构建/验证资产，普通使用窗口不必执行重建脚本。
5. Screen Space 依赖 CustomDepth with Stencil。现有 `Config/DefaultEngine.ini` 已配置 `r.CustomDepth=3`，使用其他工程时需核对对应项目设置。

插件包含 `NPRRuntime` 和 `NPREditor`；材质分别位于 `/PrincipledNPR/Outline/`、`RimLight/`、`Surface/`。不要为了使用窗口重新生成或覆盖所有材质资产。后端资产确需更新时，按[开发约束](../developer/项目开发说明与约束.md)核对归属标记、编译状态及现有资源。

文档站点只需 Node.js 20+，独立于引擎；见[文档开发](../reference/documentation.md)。
