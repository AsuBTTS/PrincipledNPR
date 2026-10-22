# Principled NPR 开发与文档规则

保留全部已有参数、算法、Shader 入口、UI 样式、资产与工具。先调查工作区和用户改动；不因参考 lilToon 而重构或改变参数语义。用户未要求时不提交、推送或改写历史。

## 文档是开发交付的一部分

以下变更必须同步检查 `docs/`、原始 `Documentation/` 证据及 `docs/changelog.md`：Shader 参数增删或默认值/范围修改、Principled 映射、渲染算法、编辑器 UI/操作、Preset/Asset 数据结构、模块接口、影响用户操作或渲染结果的 Bug。

- 保持 `Principled Parameters → Parameter Mapping → Shader / Rendering Implementation`。语义 ID 不等于 Shader pin；原始高级参数继续保留。
- Shader 实现默认优先使用 UE 材质节点；重复节点网络先考虑 Material Function。只有缺少合适节点、图难以读懂或需要维护可复用 HLSL 算法时才新增 HLSL 文件；只有需要材质之外的渲染阶段或资源时才设计独立 Shader Pass。`.ush` 引用不等于独立 Pass；`.usf` 也不会自行注册和执行。新增实现时更新 `docs/developer/shader-pipeline.md`，说明选择理由、材质调用关系和验证情况。
- 新参数先设计兼容方案，注明单位、硬域、推荐范围、默认值、依赖、控制粒度、后端质量与限制；不得悄悄重定义已有 ID。覆盖中的 0 有效，Reset 删除本层值。
- JSON 契约、C++ 声明、运行时 Validate、材质构造脚本与实际二进制资产分别检查；修改脚本不证明资产已更新。
- `npm run docs:generate` 更新自动参数页、离线证据和由参数契约生成的变化示意图。新增语义 ID 时，先补齐 `Tools/docs.mjs` 中的中文名称和解释；生成器会检查注解完整，再人工审查视觉说明、映射、算法、状态与操作步骤。Outline 字段也要有中文用途。生成器是有限语法解析器，遇到新声明形式应扩展它，不得跳过字段。
- `npm run docs:build` 必须通过参数新鲜度、源码快照、文档链接、功能状态清单及 VitePress 构建检查；首次安装用 `npm ci`。
- Mermaid 流程图用真实代码路径和当前操作步骤编写；使用本地预览确认 VitePress 将图形正常渲染。图片标注来源、展示状态和适用边界；不能把真实渲染导出图称为界面截图。
- 功能页包含概述、真实效果/待补标记、参数、算法与阶段、操作流程、限制、源码入口和实现状态。规划功能不可描述为可用。
- 仅有编译通过归为“已实现但待验证”；“已实现并验证”注明环境、时间、证据和范围。历史验证不覆盖后续新增功能。本次未运行 UE 时明确说明。
- 真实图像保留来源；禁止生成或伪造渲染验收图。独立文档原文、工具依赖的契约及原始验收证据留在 `Documentation/`；`docs/` 是网页文档目录，参照原文同步。经用户明确要求迁出的历史说明保留原文、来源、哈希与归档索引，不删除历史证据。
- 文档使用通俗中文，以团队口吻写“我们……”和“这个功能……”，先说明用途和可见变化，单项功能再按算法、使用顺序展开，避免写成旁观者审计报告；专业术语首次出现要解释。技术对照仍保留证据与准确边界。主题参考用户提供的 #42f4dc 色相，但用户要求整体更暗、去掉荧光感；当前正文强调 #14685f、点缀 #1b8c7d，暗色背景采用柔和青绿 #64baab，同时保证可读。
- 文档首页和正文使用左侧层级导航，分组可展开/收起，条目可跳转；在桌面及应用内中等宽度预览中直接显示目录，极窄手机屏幕保留抽屉入口。
- 在线文档由 `dev` 经 GitHub Actions 发布到 GitHub Pages，部署基础路径为 `/PrincipledNPR/`。发布任务需确认部署成功与在线页面正常；上传源码或构建成功不等于网站已发布。README 顶部保持真实在线入口。
- 第三方结论链接固定 commit 和函数/行；复制代码或图片时保留适用许可证及署名。当前调研不引入 lilToon 代码。

## 当前文档维护入口

- `Documentation/项目架构设计.md`：模块分工、现有组合方式和实现范围。
- `Documentation/项目开发说明与约束.md`：每个模块的开发约束、兼容和检查方式。
- `Documentation/NPR调制插件使用说明.md`：实际窗口、预览、保存和未接通入口。
- `Documentation/outline_introduction.md` 与 `Documentation/rim_light_introduction.md`：先算法与公式，再使用、限制与验证。
- 五份原文只在 `Documentation/` 维护；运行 `npm run docs:generate` 同步网页阅读版，`docs:check` 检查两者一致。
- 涉及数学时使用可对应 UE 材质节点或现有变量的公式，解释输入、单位、节点用途及数值例子，避免只写抽象符号。
- `openspec/config.yaml` 引用上述文档；`openspec/specs/` 记录已有行为；`openspec/changes/` 只放真实进行中的变更。进入工作流仍需实际读取对应正文。
- 本次历史迁移索引为 `Documentation/HistoryArchive.json` 和 `docs/reference/legacy.md`，旧路径必要兼容说明不再维护第二份正文。

## 本地检查

`npm run docs:check`；`npm run docs:build`；涉及语义草案时运行 `python Tools/verify_semantic_drafts.py` 与相关既有验证工具。渲染改动使用对应 UE 构建/图像门禁；文档构建不能替代它们。
