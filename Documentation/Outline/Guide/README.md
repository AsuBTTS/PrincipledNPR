# 操作流程配图

## 图片索引

| 编号 | 内容 | 原始来源 |
|---|---|---|
| 01 | 整角色界面总览 | 既有实机截图 `Buttons/Saved.png`，逐字节复制为 `Raw/01-overview.jpg` |
| 02 | 基础宽度与倍率 | 本次实机采集 `Raw/02-width.jpg` |
| 03 | 局部 Override、Values、Materials、Sections | 本次实机采集 `Raw/03-regions.jpg` |
| 04 | 预设与保存 | `Raw/01-overview.jpg` 的局部窗口 |
| 05 | 导出与目录 | 既有实机截图 `Buttons/Exported.png`，逐字节复制为 `Raw/05-export.jpg` |
| 06 | 关闭与重新启用 | 既有实机截图 `Buttons/ActorDisabled.png`，逐字节复制为 `Raw/06-disabled.jpg` |

截图日期为 2026-10-18，均来自当前已编译的 Outline 窗口。旧 `Buttons/*.png` 文件实际编码为 JPEG，本目录原图使用与编码一致的 `.jpg` 扩展名，没有重压缩或重绘。最终配图是 PNG。

## 标注原则

- 使用真实截图；生成式改图候选未采用。
- 只加独立的编号、连线、框选和说明，不替换控件文字、参数值或渲染画面。
- 局部截图保留对应完整原图。说明顺序与图中编号一致。
- 截图中的未勾选覆写、只读预设和灰色入口应按真实状态解释。
- 页面布局变化时重新截图，按新图修正坐标后重新导出，不沿用过期坐标。

## 更新方式

`Tools/build_outline_guide.cjs` 使用原图构造可编辑 SVG，再通过 Sharp 导出 PNG。`annotations.json` 记录来源、裁切范围、标注框和说明。

```powershell
node Tools/build_outline_guide.cjs "<包含 sharp 的 node_modules 绝对路径>"
```

不传依赖路径时仅输出 SVG 与标注清单。只重新排版文档配图，不修改 UE 参数或资产，也不运行项目测试。交付前逐图查看文字、框选位置和原图是否正确显示。
