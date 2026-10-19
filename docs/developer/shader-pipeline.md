# 渲染管线

本插件当前没有独立的 `Shaders/` 源码目录。Outline 通过 UE 材质资产实现；`Tools/create_outline_backend_assets.py` 是其可读构造来源，Screen HLSL 写在材质 Custom 节点中。HLSL 是 GPU 执行的着色程序语言；Custom 节点允许把一段 HLSL 放入材质图。

## Outline 数据流

```text
Settings / Preset + Actor 区域规则
→ ResolveRegions（逐层替换勾选字段）
→ Validate（检查参数和后端能力）
→ 按区域/样式分组 → 辅助 Mesh + MID
→ Hull WPO 或 CustomDepth/Stencil + Screen 后处理
```

WPO 是 World Position Offset，即移动顶点的位置。Hull 用它沿法线外扩，然后显示壳的背面。Screen 则借 CustomDepth/Stencil 保存角色区域的深度和编号，再向区域外采样画线。Stencil 可理解为“给区域写一个整数标签”。二者的宽度单位与算法不同，详见[Outline](../features/outline.md)。

Screen 放在景深之后、时间抗锯齿之前。TSR 是时间超分辨率，TAA 是时间抗锯齿，都使用前后帧信息；若在它们之后才画硬边，深度采样的逐帧偏移可能直接变成闪动。既有修复保留正常 jitter（采样微偏移），让描边一起进入时域处理。

## 构建与验证边界

材质脚本只操作带本项目归属标记的资产。修改脚本后应按现有工具更新实际 `.uasset`，确认编译完成，再做画面回归；不能仅因文本代码改变就宣布材质已更新。普通文档构建不会执行该脚本。

跨引擎版本升级时重点核对后处理插入位置、SceneTexture 编号/缓冲尺寸、预曝光和时间上采样顺序。预曝光是引擎为控制亮度数值采用的缩放，不应在材质已自动处理时再次手工乘。

S58 语义后端的曲线生成、标定和包构造仍待实现；不可把这里的 Outline Pass 说明套到表面 Toon Lighting。
