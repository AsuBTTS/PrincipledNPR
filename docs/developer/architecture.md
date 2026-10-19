# 模块架构

## 已有结构

| 位置 | 职责 |
| --- | --- |
| NPRRuntime | 运行时模块，保存数据类型、材质绑定及描边组件 |
| NPREditor | 编辑器模块，负责窗口、预览、资产及关卡保存 |
| 插件 Content/Outline | Hull、Mask、Screen 材质及白色宽度纹理 |
| Tools | 材质资产构造、验证、图像统计与文档工具 |
| Documentation | 原始设计、参数契约、研究报告和历史验证资料 |
| docs | 正式中文手册与 VitePress 配置 |

Runtime 是游戏运行时可用的部分；Editor 是只在编辑器使用的部分。不要为了增加文档或工具，把编辑器依赖带进 Runtime。

## 两条现有路径

语义路径已有 `UNPRStyleProfile`（风格）、`UNPRMaterialProfile`（材质语义和输入）、`UNPRBackendStylePackage`（预制材质包）和 `UNPRStyleBindingComponent`（应用到网格）。完整 Resolver 和 Adapter 尚缺。Resolver 指“按继承顺序得到最终参数”，Adapter 指“把统一视觉参数转换成具体后端输入”。

`ApplyPackage` 先检查目标 Mesh、槽范围、重复项和材质引用，再创建全部 MID，成功后替换绑定。MID 是运行中可改参数的材质实例；MIC 通常指在编辑器保存的材质实例。`SetScalarParameter` 先检查所有已绑定槽都存在目标参数，再统一修改；不会创建静态变体，也不会改 ToonProfile。恢复时只还原仍由本组件持有的材质，避免覆盖其他系统后来的修改。这里没有对 `ValidationStatus` 自动执行渲染验收门禁，调用方不能只因 Apply 成功就认为风格已标定。

Outline 则已经独立完成设置、区域解析、辅助网格、后处理、编辑器和预设链路。旧 `UNPROutlineComponent` 负责单槽，`UNPROutlineActorComponent` 负责整角色。保留两条入口，后续通过显式桥接接入统一语义，不改已有接口含义。

## 后续架构决策

继续采用[三层参数模型](../principled/overview.md)，在不改变旧 API 的前提下补充映射结果与能力诊断。渲染策略不同可有不同实现，但需记录单位、误差、适用条件和退化方式。ToonProfile 是预制的后端资源，不能直接作为艺术语义 Style，也不应在运行时改其设置。

Shader 实现采用材质节点优先：节点图本身就是 UE 编写材质 Shader 的方式。重复节点网络先考虑材质函数；只有表达不清、缺少合适节点或需要维护可复用 HLSL 算法时，才加入源码文件。独立 Shader Pass 只用于确实需要材质之外的渲染阶段和资源的效果。具体判断与 UE 接入边界见[渲染管线](./shader-pipeline.md#后续实现原则-材质节点优先)。

Face、Hair、完整风格切换和 Cook（打包游戏资源）验证都需独立验收。Outline 的通过记录不能替代这些门禁。对 lilToon 的可借鉴部分和引擎差异见[技术对照](../reference/liltoon-comparison.md)。
