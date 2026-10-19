# Toon 明暗

## 功能概述

我们用 Toon 明暗来把角色的亮部和暗部分得更清楚。这个功能把连续的明暗变化整理成几块较明确的颜色，让我们能调整分界的位置和过渡的软硬。

## 实现状态与效果

**开发中**：我们已经准备了参数设计、参考计算工具和 UE 渲染方案，还没有接通完整的参数计算与转换，也没有完成实际画面验收。效果对比图待补。工程里已有角色材质能够显示，但我们还不能通过这套统一参数直接控制它们。

## 参数说明

| 参数 | 类型 | 默认值 | 硬范围 | 视觉作用 |
| --- | --- | --- | --- | --- |
| TerminatorDotCenter | 数值 | 0 | [-1,1] | Generic/Hair 明暗分界的位置 |
| TerminatorDotHalfWidth | 数值 | 0.05 | [0,0.5] | 分界过渡的半宽；0 仍需抗锯齿 |

N·L 是表面朝向和光线方向的点积，可理解为“这个表面有多朝向灯光”。这两个参数采用其有符号坐标。高光、法线、Face 和 Hair 的其他参数见[完整参考](../principled/parameters.md)。当前没有已经接通的插件 Shader 参数绑定。

## 渲染实现

S58 是本项目给 UE 5.8 Substrate Toon 候选后端起的名称。Substrate 是引擎组织表面材质的一套系统；ToonProfile 是该候选路径使用的引擎资源。计划把视觉参数转换为引擎曲线/资源，再生成材质包。候选关系 `(tau+1)/2` 只转换坐标；实际画面还受曲线采样、抗锯齿、灯光和阴影影响，必须实测。

## 使用流程与限制

目前可以阅读/校验 `Documentation/Styles` 中两份 Draft 风格；修改它们不会自动改变场景。不能把明暗分界参数当成阴影贴图偏移；脸部逐灯明暗、头发高光需要各自的资产数据和验证。[映射页](../principled/mapping.md)记录了这些边界。

## 开发者信息

契约：`NPRParameterContract.v1.json`；宿主：`NPRStyleProfile.h`、`NPRMaterialProfile.h`、`NPRBackendStylePackage.h`；数据校验：`Tools/verify_semantic_drafts.py`。这些类型和工具不等于完整渲染实现。
