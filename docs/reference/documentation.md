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

构建产物在 `docs/.vitepress/dist/`。预览通常为 `http://127.0.0.1:4173`；端口占用时以终端为准。产物可以部署到静态托管服务，本任务仅在本地构建，没有发布。部署到子目录时先设置 `DOCS_BASE`，例如 `/PrincipledNPR/`，再构建。

## 日常维护

1. 改正文、增加页面，并在 `.vitepress/config.mts` 配导航。
2. 参数或相关源码变化后执行 `npm run docs:generate`；它同时准备离线源码/证据资源。
3. 人工检查参数的视觉解释、功能状态、截图来源和操作步骤。自动表不可代替这一步。
4. 执行 `npm run docs:build`，再预览检查桌面/窄屏导航、搜索和图片。首次安装由 lockfile 固定依赖，用 `npm ci` 复现。

`docs/public/generated/`、构建产物、缓存和 node_modules 都不提交。生成参数页需要提交，确保代码审查能看到数据变化；文档工具按需从仓库原始文件复制精选资源。机器检查不访问所有外链，第三方链接应在变更时核查。

## 页面写法

先写用途和操作，再解释技术。术语第一次出现要用白话解释；用户页面尽量少放内部类名，开发者页面保留必要的精确信息。每个功能应写清：用途、效果图或待补、参数、实现、流程、限制、源码入口和状态。没有实际图片时不要造图。
