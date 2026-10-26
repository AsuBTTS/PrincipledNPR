# Rim Light：按材质槽添加边缘光

更新日期：2026-10-26。我们在每个 Mesh 的材质槽内启用 Rim，默认保留原材质图、贴图和表面参数。Surface Recipe 是底层组合能力，不再是使用 Rim 的必填选项。

## 操作顺序

1. 在关卡选中角色，打开顶部 **Rim Light**，点击 **读取选中角色**。
2. 在按 Mesh 分组的卡片中找到对应材质槽。卡片直接显示槽名和原材质名，不需要输入 Index。
3. 勾选该槽的 **启用 Rim**，需要时点 **编辑参数**。只调整颜色、亮边宽度、柔和度和强度；底色、Normal 和原有自发光继续由原材质负责。
4. 新槽从宽度 8°、柔和度 12°、白色开始；启用时强度为 1。柔和度滑条可到 90°，它仍是角度过渡半宽，不是像素模糊半径。旧配置保留原值，旧程序结构默认的 2° 不变。
5. 用 **隔离**、**隔离当前 Mesh** 和 **原材质对比** 检查效果。切换卡片时，各槽未保存的参数和预览分别保留。
6. 默认 **保持原着色模型**。确实需要改变表面着色时，才选 **Subsurface · 次表面**。这个选择会改变表面光照模型，独立于四项 Rim 参数。
7. 点击 **保存角色 Rim**，保存本次编辑的槽位及生成材质。关闭窗口或改读其他角色时，未保存预览会恢复，已保存绑定会重新应用。

顶部只保留读取和保存。各参数使用 UE 属性行自带的重置箭头；不再排列四个独立重置按钮。可折叠的 **风格资产（可选）** 用来读取已有 Style Profile，它不再是启用前提。保存的是每个槽自己的值，不通过共享 Profile 把其他槽一起改掉。

## 原材质怎样保留

`NPRRimMaterialBuilder` 复制原母材质图，并复制原实例的有效纹理、数值、静态参数和沿父链继承的基础属性覆盖。在副本上追加既有 `MF_RimLight`，将贡献与原有自发光相加。Rim 默认使用顶点世界法线；不会要求用户重填原法线或自发光，也不会给原材质补一张默认凹凸图。首次启用或切换着色模型需要编译新副本，之后四项参数在同一 MID 上实时修改。

生成材质保存到 `/Game/NPR/Generated/Rim/`。原材质资产不改写。保存关卡时先恢复原槽材质，再写绑定配置，避免把临时 MID 保存成源材质。重新打开关卡时，运行时直接使用已生成材质，不在游戏中复制或编译编辑器材质图。

当前支持普通 Surface 的属性引脚、传统 Material Attributes（材质属性集合）的 Rim 叠加，以及直接连接的 Substrate Toon、Slab、Shading Models 根节点。复杂 Substrate 层组合或函数根节点会给出未适配提示并保留原材质；不自动猜测整个材质的合成语义。支持 Opaque 和 Masked，透明材质仍需专门适配。

## Subsurface 选择

传统独立引脚材质使用 UE `MSM_Subsurface`。直接 Toon 根节点转为 Substrate Shading Models 的 Subsurface，沿用原 BaseColor、Metallic、Specular、Roughness、Normal、Emissive、Tangent 和 Anisotropy；原 ToonProfile 不再参与该副本的光照。原 Slab 使用 Wrap 次表面近似，缺少散射距离时使用 1 cm 的 RGB 平均自由程。Material Attributes 的 Subsurface 切换暂未适配，仍可保持原模型添加 Rim。此选项用于明确改变模型，不代表皮肤散射已自动标定。

## 数据兼容与检查

旧的 `RimRecipe`、`RimSurfaceInputs`、Profile 解析和 `ApplyRimPreview` 接口保留。新绑定保存 `RimGeneratedMaterial`、`RimShadingModel` 和槽位局部 `RimLocalParameters`；旧绑定没有局部值时，继续按 Profile 默认值和 Material Kind 覆盖解析。两个角度存弧度，UI 用度，写材质时只转换一次。0 强度、0 宽度、0 柔和度及黑色均是有效值。

构建、真实 GPU 图像和 UI 检查的范围见 [原材质 Rim 检查](OriginalSurface-2026-10-26.md)。此前手动填写源输入的工作区及联调记录保留为历史证据，不能用于证明本次新流程已经通过。阴影、动画、多 LOD、Cook、性能及任意复杂材质仍需独立验收。
