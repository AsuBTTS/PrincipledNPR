# 实现状态与证据

状态用于回答“现在到底能不能用”。它不代表所有平台、资产和引擎版本都通过测试。

| 功能 | 状态 | 证据与边界 |
| --- | --- | --- |
| Outline 基础后端、角色分组、旧单槽流程 | 已实现并验证 | 2026-10-17 UE 5.8.3 / DX12 / SM6 历史记录；67 项时间稳定性检查、角色与旧单槽回归，范围见原报告 |
| Outline 分层宽度、UV0 Mask | 已实现但待验证 | 已有 Editor/Game 构建及资产编译记录；新增图像回归 NotRun |
| 完整 Principled 参数链路 | 开发中 | JSON 为 Draft / Uncalibrated / NotConnected；C++ 宿主已有，完整 Resolver/Adapter 缺失 |
| Toon 语义后端 | 开发中 | S58 候选未验收；映射项 NotImplemented / NotRun |
| Rim Light、MatCap、统一颜色控制 | 规划中 | Rim 有语义设计，三者均无完整插件可用链路 |

[67 项原始记录](/generated/source/Documentation/Outline/Temporal/Acceptance.json)与[宽度层实现记录](/generated/source/Documentation/Outline/WidthLayers/Implementation.json)可离线查看。历史证据不能作为后续新功能的通过证明。

机器可读清单为 `docs/feature-status.json`。构建会检查状态值、对应页面与证据文件存在；不会自动判断画面是否正确，也不会把证据文件的存在等同于测试通过。

2026-10-19 本次工作只修改文档及文档工具，不重新运行 UE、材质编译或渲染实验。文档站点的构建结果记录在交付报告中。
