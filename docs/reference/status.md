# 实现状态与证据

状态用于回答“现在到底能不能用”。它不代表所有平台、资产和引擎版本都通过测试。

| 功能 | 状态 | 证据与边界 |
| --- | --- | --- |
| Outline 基础后端、角色分组、旧单槽流程 | 已实现并验证 | 2026-10-17 UE 5.8.3 / DX12 / SM6 历史记录；67 项时间稳定性检查、角色与旧单槽回归，范围见原报告 |
| Outline 分层宽度、UV0 Mask | 已实现但待验证 | 已有 Editor/Game 构建及资产编译记录；新增图像回归 NotRun |
| NPR 调制入口、材质槽隔离与参数展开 | 已实现并验证 | 2026-10-20 UE 5.8.3 / Windows；导航、隔离切换、参数展开、可选对比及窄窗口布局；未覆盖静态网格、多 LOD 和全部规则修改；见 [改进记录](/generated/source/Documentation/Outline/MaterialSlotUI.md.txt) |
| 完整 Principled 参数链路 | 开发中 | JSON 为 Draft / Uncalibrated / NotConnected；C++ 宿主已有，完整 Resolver/Adapter 缺失 |
| Toon 语义后端 | 开发中 | S58 候选未验收；映射项 NotImplemented / NotRun |
| Surface Master Family / Recipe 组合 | 已实现并验证 | 2026-10-22 UE 5.8.3 / DX12 SM6；主工程两项新自动化、90 项实际资产检查、17 张真实球体捕获；BaseColor/Normal 保持、HDR 贡献相加、Toon+Rim 单槽及两种 Outline 共存通过；未覆盖角色、完整语义 Resolver、Cook 或性能，见 [Surface](../features/surface-material.md)与[执行记录](/generated/source/Documentation/Surface/ImplementationValidation.json) |
| Rim Light 独立材质 | 已实现并验证 | 2026-10-22 Windows / UE 5.8.3 / DX12 SM6，静态球体四项变化的八张真实图像、八项门禁及人工查看；不覆盖角色、阴影、动画、时域或 Cook，见[图像记录](/generated/source/Documentation/RimLight/BindingValidation/ImageMetrics.json) |
| Rim Light ② 参数暴露与契约 | 已实现并验证 | 2026-10-22 Windows / UE 5.8.3；27 项离线交叉检查、34 项实际资产只读检查及 C++ MaterialContract 用例通过；Game/Editor 构建通过；早期 Game 启动失败记录保留，最新 Editor 验证已补齐用例执行 |
| Rim Light ③ C++ / MID 动态调参 | 已实现并验证 | 2026-10-22 UE 5.8.3 / DX12 SM6；双槽读回、失败保持原值、外部替换与恢复；同一个 MID 跨帧更新、八项图像/自动化门禁和当前编辑器十项反射检查，见[动态调参](/generated/source/Documentation/RimLight/RuntimeBinding.md.txt)与[执行记录](/generated/source/Documentation/RimLight/ImplementationValidation.json) |
| Rim Light 完整工作流 | 已实现待完整视觉验收 | 2026-10-25 UE 5.8.3 Editor 构建、Profile 与绑定自动化通过；角色单槽预览、Profile 和关卡保存及重开读取通过。Undo 完整 UI 操作、阴影、动画、多 LOD、Cook、性能和角色材质观感仍需单独验证；见 [工作流](/generated/source/Documentation/RimLight/EditorWorkflow.md.txt) |
| MatCap、统一颜色控制 | 规划中 | 无完整插件可用链路 |

[67 项原始记录](/generated/source/Documentation/Outline/Temporal/Acceptance.json)与[宽度层实现记录](/generated/source/Documentation/Outline/WidthLayers/Implementation.json)可离线查看。历史证据不能作为后续新功能的通过证明。

机器可读清单为 `docs/feature-status.json`。构建会检查状态值、对应页面与证据文件存在；不会自动判断画面是否正确，也不会把证据文件的存在等同于测试通过。

2026-10-19 的历史工作只修改文档及文档工具，没有重新运行 UE。2026-10-22 的 Rim 与 Surface 新验证按上表分别记录，不追溯覆盖此前未验收项。
