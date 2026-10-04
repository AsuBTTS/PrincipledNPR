# UE5.8 Toon专项修订摘要

日期：2026-10-04。主报告版本：1.0-S58专项修订。

**决策已修改：S58是V1首选生产候选，A0是数学参考后端。** 优先级改变依据是Epic公开的原生灯光集成与实际API；是否通过生产验收仍须目标构建实验。

| 问题 | 修订结论 |
|---|---|
| Epic替我们完成什么 | Toon closure、原生直接/多灯/衰减/阴影接入、Sky/Lumen、镜面/间接贡献与Ramp基础；复用而非默认重造 |
| 系统仍完成什么 | 参数语义/标定、Material×Style、资产协议、Profile生成/映射质量、Face/Hair特化、Preset/实例/bundle、Outline/Rim与验收 |
| A0是否继续做 | 保留golden、消融和严格Face/Hair研究；不作为全角色生产默认 |
| S58能否做主后端 | 首选候选；Generic优先，Hair流向/近似映射与Face入口分别验收；目前未获渲染测试证据 |
| Face是否直接支持 | 无公开逐灯FaceMask/NoLOverride；12bit PatternUV经frac/Wrap，原Clamp identity端点方案不成立；内部采样逆映射与off外乘透射分支可作key-only近似候选 |
| Hair是否等价 | CL58210709的MIR未传Tangent/Anisotropy，legacy basis仅Normal；方向Gate仍待渲染；无shift瓣可研究标定，独立shift缺专用入口 |
| Style运行时切换 | 预生成不可变ToonProfile与共Master的MIC，预载MID bundle切换；普通参数可动态改，Profile Settings不在runtime改 |

新增第10.8节`UE 5.8 Toon BSDF Backend Evaluation`，同步重写路线比较、架构、技术探针、风险与28项开发任务。第4–6节数学reference完整保留。

关键证据：[官方节点API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UMaterialExpressionSubstrateToon-?lang=en-US)、[Profile字段](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FToonProfileStruct)、[MI Profile覆写](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UMaterialInstance)、[Epic Face/Tangent回复](https://forums.unrealengine.com/t/substrate-feedback-thread/691314?page=41)、[历史阴影修复](https://forums.unrealengine.com/t/5-8-preview-toon-bsdf-shadows-cast-on-surface-are-blurry/2736077)、[UE-388460缓存缺陷](https://issues.unrealengine.com/issue/UE-388460)。

**核心源码状态：SourceVerified；渲染实测：NotRun。** 已读取本机UE5.8.3、CL58210709的节点/Profile、legacy/MIR、Toon common/atlas Shader。确认signed NoL先remap到[0,1]；IncludeShadow开启时先乘透射再offset/Ramp、关闭时Ramp后外乘透射；非Rect直接高光为GGX D×NoV查表归一化，再送Ramp，直接Vis/Fresnel乘子被注释。详见[本机源码审计](UE5.8.3_本机源码审计.md)。

截至源码审计Launcher仍校验中、Editor未运行；后续安装/工程状态以独立[项目创建记录](项目创建记录.md)为准。Cast/Receive/Self、VSM/CSM、Lumen与间接镜面、Runtime/PSO/DDC仍未执行，API01等探针保持NotRun。源代码事实不等同画面通过，也不保证历史阴影修复已经完成回归。

下一轮顺序：锁环境→最小S58 Generic/源码审计→参数标定/Adapter→原生光照与Face/Hair门槛→Runtime/DDC/Cook→生产后端Gate→工具产品化。不得恢复“A0先做完，S58可选测测”的旧路线。
