# 历史资料与归档索引

2026-10-22，我们按用户要求合并并迁出 12 份旧说明。当前五份独立原文保存在工程 `Documentation/` 中，本网站同步为阅读版：[项目架构设计](../architecture/项目架构设计.md)、[开发约束](../developer/项目开发说明与约束.md)、[插件使用说明](../guides/NPR调制插件使用说明.md)、[Outline](../outline/outline_introduction.md)、[Rim](../rim/rim_light_introduction.md)。

## D 盘历史归档

本机目录：`D:\学期作业\大创\Doc_history\2026-10-22_历史文档整合`。

归档包含两份合并阅读稿、迁出原文，以及一份带图片和数据的原始 Documentation 快照。快照共有 1371 个文件，均按迁移前 SHA-256 校验；仍被工程工具依赖的契约和最终证据在项目中继续保留。外部目录不随 Git 克隆，线上站点不提供这个本机目录的下载。

| 迁出的旧说明 | 当前承接内容 |
| --- | --- |
| 下一轮开发交接_V1.0.md | 架构设计与开发约束；历史待办不自动视为当前状态 |
| 项目结构Tree.md | 架构设计中的当前职责与目录 |
| UE5.8_Toon_专项修订摘要.md | 历史技术路线；当前实现范围在架构设计和状态页 |
| 项目创建记录.md | 当前打开方式在插件使用说明；早期过程归档 |
| Assets/CharacterIntegration.md | 早期角色接入过程；后续结果仍见 TwoCharacterSetup |
| C++骨架.md | 当前模块与构建方式进入架构设计、开发约束 |
| BootstrapInterfaces.md | 一次性工程创建接口核查保存在历史阅读稿 |
| ProbeGuide.md | 探针的用途与验收边界进入架构和开发约束 |
| Outline/操作流程.md | 插件使用说明与 Outline 介绍 |
| Outline/按钮说明与修复.md | 当前用法进入插件说明；修复经过归档 |
| Outline/MultiScope/使用说明.md | 当前区域操作和边界进入 Outline 介绍 |
| Outline/后续开发链路指导.md | 当前共用开发要求进入开发约束 |

机器可读的迁出清单、原路径、大小和哈希见[HistoryArchive.json](/generated/source/Documentation/HistoryArchive.json)。完整快照清单在 D 盘归档目录；五份当前文档的离线阅读副本放在同级 `Doc_current` 目录。

## 为什么保留一处兼容说明

`Documentation/项目创建记录.md` 的原文已经迁出，原位置仅保留简短跳转。早期技术设计报告按内容哈希被参数契约和风格草案引用，因此没有为了修复一个链接改写整份报告及其指纹。其他日常入口已改为当前文档。

## 项目中继续保留的资料

| 内容 | 位置与作用 |
| --- | --- |
| 完整技术设计与引擎源码审计 | `Documentation/Principled_NPR_技术设计报告_V1.0.md`、`UE5.8.3_本机源码审计.md`；保留研究推导和证据 |
| 参数契约与风格草案 | `Documentation/Schemas/`、`Styles/`；部分是工具实际输入 |
| 角色导入及绑定事实 | `Documentation/Assets/`；保留来源、槽位和导入记录 |
| 描边实现与验收 | `Documentation/Outline/`；保留技术报告、宽度说明、界面与时间稳定性证据 |
| Rim 实现与验收 | `Documentation/RimLight/`；保留四项契约、动态写入和真实图像 |
| Surface 实现与验收 | `Documentation/Surface/`；保留组合契约、实际资产审查和 GPU 捕获 |

历史资料中的“当前”指各自编写日期；当前功能状态见[状态页](./status.md)。旧图、旧计划和失败尝试不会被重命名为本轮新验收。
