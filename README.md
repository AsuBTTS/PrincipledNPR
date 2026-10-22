# Principled NPR

**[在线文档](https://asubtts.github.io/PrincipledNPR/)** · [文档首页](docs/index.md) · [使用说明](Documentation/NPR调制插件使用说明.md)

我们在 UE 5.8.3 中开发角色风格化材质编辑工具。目前 Outline 已有窗口、局部设置、预设与关卡保存；Rim 已有独立材质与四项动态控制，并接入可扩展 Surface 母材质。同槽 Toon + Rim 已通过受控球体验证，完整风格解析、角色自动转换和 Rim 调制窗口仍在开发。

## 当前维护的五份说明

独立原文保存在 `Documentation/`，网页阅读版保存在 `docs/`。先维护原文，再运行 `npm run docs:generate` 同步网页；OpenSpec 也直接引用原文。

| 文档 | 适合什么时候读 |
| --- | --- |
| [项目架构设计](Documentation/项目架构设计.md) | 理解模块分工、表面组合与独立描边 |
| [项目开发说明与约束](Documentation/项目开发说明与约束.md) | 开发、修改参数、更新资产和验证前 |
| [NPR调制插件使用说明](Documentation/NPR调制插件使用说明.md) | 选择角色、调整、预览、保存和导出 |
| [outline_introduction](Documentation/outline_introduction.md) | 先理解描边算法与公式，再实际使用 |
| [rim_light_introduction](Documentation/rim_light_introduction.md) | 先理解 Rim 节点公式，再调材质或运行时参数 |

## 打开工程与文档

工程入口是 `PrincipledNPR.uproject`。获取工程后先下载 Git LFS 资源，安装相应 UE 与 C++ 工具链。构建方式见[安装与环境](docs/guide/installation.md)，正常使用已保存资产不需要重跑 Python 创建脚本。

本地文档运行 `npm ci`、`npm run docs:dev`；检查使用 `npm run docs:build`。参数或相关源码变化后先执行 `npm run docs:generate`。线上入口由 dev 的 GitHub Actions 发布；本地构建和推送本身不等于已部署。

## 项目组织

- `Source/`：工程宿主；`Plugins/PrincipledNPR/Source/`：运行时与编辑器代码。
- 插件 `Content/Outline`、`RimLight`、`Surface`：实际材质与组合资源。
- `Content/NPR/`：工程配置、角色场景和测试探针。
- `Tools/`：构建、资产创建、验证和文档工具。
- `Documentation/`：五份独立说明原文、参数契约、研究依据与原始验收证据。
- `docs/`：网页文档，五份阅读版由原文同步；`openspec/`：约束、已有能力规范和真实开发变更。

12 份旧说明已合并迁往 D 盘，见[历史归档索引](docs/reference/legacy.md)。根目录 `AGENTS.md` 继续规定开发与文档维护约束。

## 角色资源与恢复

陈千语和流浪者是工程研究用角色，源模型、骨骼、贴图、材质与导入记录继续保留。双角色、原生 Toon 探针和风格初值不是完整 Face/Hair 或两套生产风格验收。详细范围见[功能状态](docs/reference/status.md)。

完整工程包含第三方角色资源，不作为可再分发角色包。来源授权、Git LFS、备份恢复及未纳入 Git 的本机产物见[备份与恢复说明](Documentation/GitHubBackup.md)。
