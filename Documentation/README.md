# Documentation：独立文档与原始资料

我们在这里维护五份可以直接阅读的 Markdown 原文，文件名保持不变。网页目录为 `docs/`；网页阅读版参照这里的内容生成，不在两处分别修改正文。

| 文档 | 内容 |
| --- | --- |
| [项目架构设计](项目架构设计.md) | 模块分工、Surface 组合、独立描边与当前进度 |
| [项目开发说明与约束](项目开发说明与约束.md) | 每个模块的开发要求、兼容、构建与验证 |
| [NPR调制插件使用说明](NPR调制插件使用说明.md) | 当前窗口、操作、预览、保存与未接通入口 |
| [outline_introduction](outline_introduction.md) | 先描边算法和引擎公式，再使用和限制 |
| [rim_light_introduction](rim_light_introduction.md) | 先 Rim 节点公式和数值例子，再材质及 MID 使用 |

修改原文后，在工程根目录运行 `npm run docs:generate`、`npm run docs:check` 和 `npm run docs:build`。网页内容不一致时检查会报错；OpenSpec 配置直接引用这里的原文。

公式和操作不依赖在线服务。图片使用现有 UE 实机或渲染证据的相对路径；Mermaid 流程图需支持它的 Markdown 阅读器。图像的来源、日期与适用范围在各篇正文中说明，本轮没有重新运行 UE。

`Schemas/`、`Styles/`、`Outline/`、`RimLight/` 和 `Surface/` 等目录继续保留实际契约、技术资料和原始验收证据。十二份旧说明已按要求合并迁往 `D:\学期作业\大创\Doc_history\2026-10-22_历史文档整合`；迁出文件和原文哈希见 [HistoryArchive.json](HistoryArchive.json)。本机阅读副本在同级 `Doc_current`，不作为第二份维护源。
