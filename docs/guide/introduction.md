# 系统介绍

我们正在开发 Principled NPR，一套面向泛二次元角色的 UE5 材质渲染编辑工具。我们希望美术能直接调整明暗、反光和轮廓这些看得见的效果，再由系统把这些控制转换为具体的渲染参数。

```text
Principled Parameters → Parameter Mapping → Shader / Rendering Implementation
```

我们目前可以使用的主要功能是 [Outline 描边](../features/outline.md)。这个功能支持两种描边方法，可以调整整个角色或局部区域，并提供左右对比预览和预设保存。原有的单材质槽操作流程也继续保留。

我们已经为完整参数体系准备了数据格式、两份风格草案和基础资产类型，但还没有接通完整的参数计算与渲染转换。[边缘光](../features/rim-light.md)和 [MatCap](../features/matcap.md) 也还不能通过这套工具开启。各项进展见[状态与证据](../reference/status.md)。

文档面向角色美术、技术美术和插件开发者。首次使用从[安装](./installation.md)和[快速开始](./quick-start.md)进入；开发扩展先读[架构](../developer/architecture.md)与[参数映射](../principled/mapping.md)。
