# Principled 参数体系

实现状态：**开发中**。语义契约是 Draft / Uncalibrated / NotConnected；这与已经独立运行的 Outline 模块并行存在。

我们设计 Principled 参数，是希望先说清楚“画面要怎么变”，再决定底层怎么实现。例如，我们可以描述明暗分界的位置、高光覆盖的大小，以及边缘光向角色内部延伸的范围。

这些参数需要有固定的含义。不同渲染方法中的 roughness（粗糙度）对高光的影响可能不同，因此我们不能直接把它改名为“高光大小”。同样，表面角度范围和屏幕像素线宽也不能混用。准确单位和计算条件继续保留在参数表中，方便开发时核对。

## 参数变化方向示意

![Principled NPR 参数沿各自建议范围变化的概念示意图；不是 UE 实测画面](/images/principled-parameter-chart.svg)

我们按每项参数自己的建议范围取五个档位，并标出对应单位和默认值。球体只帮助我们理解参数大致控制哪种变化；当前统一参数契约仍是草案，尚未完成标定和 Shader 接入。已有的 Outline 功能走独立链路，图中的 `OutlineWidthPx` 还没有自动映射过去。具体依赖和状态见[完整参数表](./parameters.md)与[实现状态](../reference/status.md)。

| 层次 | 所有者与用途 | 当前状态 |
| --- | --- | --- |
| Principled Parameters | JSON Contract；Style/Material Profile；稳定 ID、单位、视觉响应 | 契约与宿主已有，尚未全链路求值 |
| Parameter Mapping | 稀疏层解析、资产事实、能力检查、后端标定 | 完整 Resolver / Adapter 尚未实现 |
| Shader / Rendering | S58 预制包候选；独立 Outline 后端 | 显式 MID 包绑定已有；S58 未验收；Outline 独立接入 |

当前契约字段见[完整参数表](./parameters.md)。常用视觉控制解释如下；精确默认值和硬域以自动表为准：

- `TerminatorDotCenter` 和 `TerminatorDotHalfWidth` 调整通用材质、头发材质的明暗交界位置与过渡宽度。它们控制表面受光变化，不是阴影贴图的偏移值。宽度为 0 时仍会做抗锯齿，也就是减少边缘锯齿的处理。
- `SpecHalfAngleRad`、`SpecEdgeSoftness` 和 `SpecPeakScale` 分别控制高光覆盖角度、边缘柔和程度和峰值亮度。Fresnel（菲涅耳效应）指反光会随观看角度改变；调整峰值时，高光总亮度也可能跟着变化。
- `NormalDetailStrength` 控制法线贴图里的细小凹凸对光照的影响。`MacroNormalBlend` 则需要模型另外提供一份引导法线，帮助我们简化大形状的受光变化。
- `RimGrazingAngleRad`、`RimEdgeHalfWidthRad` 和 `RimPeakScale` 控制边缘光向内延伸的角度、过渡宽度和亮度。这里使用表面角度，不是屏幕像素；边缘光被遮挡时如何处理还没有定下来。
- Face 参数只用于面部，并依赖头部方向标定和面部角度数据；Hair 参数只用于头发，并依赖模型记录的发根到发梢方向。风格设置不能代替这些模型数据。
- `OutlineWidthPx` 描述输出画面里的像素宽度。当前可用的描边功能有自己的 `WidthPixels`（像素宽度）和 `HullWidthCm`（模型外扩厘米数）设置；两者还没有通过统一参数映射自动连接。

原始后端参数和高级选项继续保留。后续兼容接入应先增加显式、版本化映射与诊断，不更名、不删除既有控制。[映射指南](./mapping.md)说明控制粒度和质量边界。
