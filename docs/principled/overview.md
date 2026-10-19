# Principled 参数体系

实现状态：**开发中**。语义契约是 Draft / Uncalibrated / NotConnected；这与已经独立运行的 Outline 模块并行存在。

我们设计 Principled 参数，是希望先说清楚“画面要怎么变”，再决定底层怎么实现。例如，我们可以描述明暗分界的位置、高光覆盖的大小，以及边缘光向角色内部延伸的范围。

这些参数需要有固定的含义。不同渲染方法中的 roughness（粗糙度）对高光的影响可能不同，因此我们不能直接把它改名为“高光大小”。同样，表面角度范围和屏幕像素线宽也不能混用。准确单位和计算条件继续保留在参数表中，方便开发时核对。

| 层次 | 所有者与用途 | 当前状态 |
| --- | --- | --- |
| Principled Parameters | JSON Contract；Style/Material Profile；稳定 ID、单位、视觉响应 | 契约与宿主已有，尚未全链路求值 |
| Parameter Mapping | 稀疏层解析、资产事实、能力检查、后端标定 | 完整 Resolver / Adapter 尚未实现 |
| Shader / Rendering | S58 预制包候选；独立 Outline 后端 | 显式 MID 包绑定已有；S58 未验收；Outline 独立接入 |

当前契约字段见[完整参数表](./parameters.md)。常用视觉控制解释如下；精确默认值和硬域以自动表为准：

- `TerminatorDotCenter`：调整 Generic/Hair 的明暗交界；不等于 Shadow Map bias。`TerminatorDotHalfWidth` 描述过渡半宽，零仍需保留像素 AA。
- `SpecHalfAngleRad`、`SpecEdgeSoftness`、`SpecPeakScale`：分别描述半峰角宽、边缘软度和可见性/Fresnel 之前的峰值；峰值改变不保证积分能量不变。
- `MacroNormalBlend`：依赖资产提供引导法线；缺数据时不能声称支持形体抽象。
- Rim 三参数描述角域和线性幅度，不是后处理描边像素。其可见性策略仍需独立定义。
- Face/Hair 参数依赖头部坐标系、临界角场、根梢方向等资产事实；风格不能擅自修改这些事实。
- `OutlineWidthPx` 是语义契约条目；当前操作入口为独立强类型 `WidthPixels` / `HullWidthCm`，二者尚未通过全局 Resolver 自动映射。

原始后端参数和高级选项继续保留。后续兼容接入应先增加显式、版本化映射与诊断，不更名、不删除既有控制。[映射指南](./mapping.md)说明控制粒度和质量边界。
