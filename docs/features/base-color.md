# 基础颜色

## 功能概述

我们用基础颜色来确定皮肤、头发和衣服的主要颜色。这个功能通常读取颜色贴图，再乘一个颜色值。[Surface Master](./surface-material.md) 已提供这两个基础输入；角色接入时需显式配置原贴图和颜色。

## 实现状态与效果

统一颜色控制为**规划中**。Surface 的 `NPR_BaseColorTexture × NPR_BaseColor` 已在静态球体上验证，与 Rim 同槽组合时 BaseColor 缓冲保持不变，真实图像见 [Surface](./surface-material.md)。角色原材质可以继续使用；统一换色 UI 与角色转换尚未接入，Outline 的颜色只控制描边。

## 参数说明

| 输入 | 类型 | 默认值 / 范围 | 当前用途 |
| --- | --- | --- | --- |
| BaseColor | 材质表面颜色输入 | 由源材质提供，无插件统一数值 | Generic / Face / Hair 所需输入之一 |
| `NPR_BaseColor` / `NPR_BaseColorTexture` | Surface 技术参数 | 线性 RGB 0.18 / 白纹理 | 新 Surface 家族中颜色乘贴图，非全局语义 ID |

当前契约没有已接通的统一“色相、饱和度、明度”参数，也没有对应的插件 Shader 参数。不能把源材质中的任意同名参数当作统一接口。

## 渲染实现

原材质继续使用自己的计算；Surface 家族在共用 `MF_NPRSurfaceInputs` 中计算颜色。`UNPRStyleBindingComponent::ApplyPackage` 把预先准备好的材质包应用到网格材质槽，不自动分析或迁移原图。

## 使用流程

在 UE 原材质中调整颜色可保持原工作流；使用新 Surface 时，在预览实例配置纹理和颜色，运行时由 Composer 选择 Recipe、调用 `ApplyPackage`，再通过 MID 调整。步骤见 [Surface](./surface-material.md)，完整参数域见[参数参考](../principled/parameters.md)。

## 限制与开发信息

不同材质使用的参数名和计算方式可能不同；透明度也由原材质负责。未来统一颜色控制应先约定颜色空间、叠加顺序和遮罩规则，保留源材质高级选项。来源为契约的 `materialProfiles.*.requiredInputs` 与 `NPRStyleBindingComponent.cpp`，见[参数来源](../principled/parameters.md)。
