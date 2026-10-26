# 系统介绍

我们正在开发 Principled NPR，一套面向泛二次元角色的 UE5 材质渲染编辑工具。我们希望美术能直接调整明暗、反光和轮廓这些看得见的效果，再由系统把这些控制转换为具体的渲染参数。

```text
Principled Parameters → Parameter Mapping → Shader / Rendering Implementation
```

NPR 调制窗口提供[统一效果总览](../guides/npr-workspace.md)，可以查看各 Mesh/材质槽已登记的效果，再进入对应编辑页。[Outline 描边](../outline/outline_introduction.md) 支持两种方法、角色与局部设置、对比和预设保存，原有单槽流程继续保留。[Rim Light](../rim/rim_light_introduction.md) 支持 Profile、显式表面输入、实时调参、角色隔离预览和保存绑定；Surface 组合继续保留明确提供的基础表面，不自动分析任意原材质图。

完整参数体系已有数据格式、两份未标定风格草案和基础资产类型；完整自动转换仍未接通。[MatCap](../features/matcap.md) 仍在规划。各项进展和验证范围见[状态与证据](../reference/status.md)。

文档面向角色美术、技术美术和插件开发者。首次使用从[安装](./installation.md)和[快速开始](./quick-start.md)进入；开发扩展先读[架构](../developer/architecture.md)与[参数映射](../principled/mapping.md)。
