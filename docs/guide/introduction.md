# 系统介绍

我们正在开发 Principled NPR，一套面向泛二次元角色的 UE5 材质渲染编辑工具。我们希望美术能直接调整明暗、反光和轮廓这些看得见的效果，再由系统把这些控制转换为具体的渲染参数。

```text
Principled Parameters → Parameter Mapping → Shader / Rendering Implementation
```

NPR 调制窗口目前可用的主要功能是 [Outline 描边](../outline/outline_introduction.md)，支持两种方法、角色与局部设置、对比和预设保存。原有单材质槽流程继续保留。[Rim Light](../rim/rim_light_introduction.md) 已有材质和运行时调参，Surface 已能保留原表面并组合 Rim，但它们还没有窗口内的完整调制流程。

完整参数体系已有数据格式、两份未标定风格草案和基础资产类型；完整自动转换仍未接通。[MatCap](../features/matcap.md) 仍在规划。各项进展和验证范围见[状态与证据](../reference/status.md)。

文档面向角色美术、技术美术和插件开发者。首次使用从[安装](./installation.md)和[快速开始](./quick-start.md)进入；开发扩展先读[架构](../developer/architecture.md)与[参数映射](../principled/mapping.md)。
