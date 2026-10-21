# 既有技术资料

正式手册放在 `docs/`，原来的设计与实验资料继续保存在 `Documentation/`。没有删除、搬走或改写历史记录；以下是阅读顺序和仓库内位置。

| 内容 | 原始位置 | 正式手册入口 |
| --- | --- | --- |
| 完整技术设计与研究背景 | Documentation/Principled_NPR_技术设计报告_V1.0.md | [参数体系](../principled/overview.md) |
| 语义骨架与 C++ 宿主 | Documentation/Architecture.md；Documentation/C++骨架.md | [模块架构](../developer/architecture.md) |
| 参数契约与风格草案 | Documentation/Schemas/；Documentation/Styles/ | [自动参数参考](../principled/parameters.md) |
| 描边模块与操作 | Documentation/Outline/技术模块报告.md；操作流程.md；Guide/README.md | [Outline 描边](../features/outline.md) |
| 角色、材质与 Section 边界 | Documentation/Outline/MultiScope/使用说明.md；审查与优化报告.md | [Outline](../features/outline.md) |
| 逐帧闪动修复和回归 | Documentation/Outline/Temporal/抖动修复报告.md | [渲染管线](../developer/shader-pipeline.md) |
| 新增宽度层与 UV0 Mask | Documentation/Outline/多层宽度控制.md；WidthLayers/Implementation.json | [参数映射](../principled/mapping.md) |

本地源仓库可直接打开上述文件。站点构建另外提供精选源码、原始参数、历史指标和真实截图，见[状态证据](./status.md)与[参数页来源](../principled/parameters.md)，不把数百 MB 的逐帧实验全部复制进网站。

旧文档记录的是各自编写时的实现阶段。如与现有源码不同，先核实时间与变更，不覆盖历史结论来伪装为新验收。例如早期 Architecture 中的报告指纹可能是旧记录，当前机器数据从实际契约读取。
