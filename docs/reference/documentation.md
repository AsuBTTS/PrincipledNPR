# 文档站点开发

采用 VitePress 默认主题，中文分级导航、页内目录、亮暗主题和本地搜索已配置。本地搜索随静态构建生成，不需要外部搜索服务。功能参考：[VitePress 入门](https://vitepress.dev/guide/getting-started)和[站内搜索](https://vitepress.dev/reference/default-theme-search)。

## 本地运行

在工程根目录运行，Windows 若受 PowerShell 脚本策略影响可改用 `npm.cmd`。

```powershell
npm ci
npm run docs:dev
```

打开终端显示的本地地址，通常为 `http://127.0.0.1:5173`。

```powershell
npm run docs:build
npm run docs:preview
```

构建产物在 `docs/.vitepress/dist/`。预览通常为 `http://127.0.0.1:4173`；端口占用时以终端为准。部署到子目录时先设置 `DOCS_BASE`，例如 `/PrincipledNPR/`，再构建。

## 在线发布

我们使用 GitHub Pages 托管[在线文档](https://asubtts.github.io/PrincipledNPR/)。`.github/workflows/docs.yml` 会在 `dev` 的相关文件更新后构建、检查并发布网站；构建失败时不会发布新版本。`main` 和 Pull Request 只做构建检查。

发布时使用 `DOCS_BASE=/PrincipledNPR/`，确保导航、搜索、图片和源码下载都指向正确的子目录。仓库的 Pages 来源设置为 GitHub Actions，`github-pages` 环境允许 `dev` 部署。我们只上传构建出的静态文档，不上传完整 UE 工程和游戏资产。

推送后可以在仓库 Actions 中查看 Documentation 工作流。需要看到部署成功，再打开在线地址确认页面；仅有“代码已推送”或“构建成功”不代表网站已经发布。

## 日常维护

1. 五份独立说明在 `Documentation/` 修改原文，其他网页在 `docs/` 修改；增加页面时在 `.vitepress/config.mts` 配导航。
2. 原文、参数或相关源码变化后执行 `npm run docs:generate`；它会同步五份网页阅读版，更新参数表、离线源码/证据资源和参数契约变化示意图。
3. 人工检查参数的视觉解释、功能状态、截图来源和操作步骤。自动表不可代替这一步。
4. 执行 `npm run docs:build`，再预览检查桌面/窄屏导航、搜索和图片。首次安装由 lockfile 固定依赖，用 `npm ci` 复现。

`docs/public/generated/`、构建产物、缓存和 node_modules 都不提交。生成参数页与参数示意图需要提交，确保代码审查能看到数据变化；文档工具按需从仓库原始文件复制精选资源。机器检查不访问所有外链，第三方链接应在变更时核查。

独立说明的原文分别为 `Documentation/项目架构设计.md`、`Documentation/项目开发说明与约束.md`、`Documentation/NPR调制插件使用说明.md`、`Documentation/outline_introduction.md` 和 `Documentation/rim_light_introduction.md`，文件名保持用户指定名称。网页阅读版仍在 `docs/` 中按 architecture、developer、guides、outline、rim 分组，页面开头的注释记录原文路径。`Tools/docs.mjs` 生成时转换图片、证据和页面的相对链接；`docs:check` 会拒绝原文和网页内容不一致的版本。直接修改网页阅读版会在下次生成时被原文覆盖。

旧开发者、编辑器和功能 URL 留作简短入口，导航进入对应阅读版。OpenSpec 直接引用 `Documentation/` 原文；`openspec/specs/` 记录已有行为，不替代手册。工程中的非网页阅读从 `Documentation/README.md` 开始。

用户指定迁出的 12 份旧说明已离开日常文档层，原文和随附资料在 D 盘归档；仍被工具使用的契约和最终证据继续留在工程中。位置和哈希索引见[历史资料](./legacy.md)。D 盘阅读副本在 `Doc_current`，独立原文更新后应一同刷新；该目录不随 Git 或 Pages 发布。

## 页面写法

先说明用途和可见变化；单项功能按算法、公式、使用的顺序展开。术语第一次出现要用白话解释；用户页面尽量少放内部类名，开发者页面保留必要的精确信息。每个功能应写清：用途、效果图或待补、参数、实现、流程、限制、源码入口和状态。没有实际图片时不要造图。
