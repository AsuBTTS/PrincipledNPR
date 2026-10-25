# Rim Light 配置、角色绑定与编辑器工作流

更新日期：2026-10-25。本页说明本轮实现的 Profile、角色材质槽和 NPR 调制窗口链路。渲染效果仍需在目标角色上单独观察；C++ 编译成功不等同于所有角色资产通过视觉验收。

## 数据与优先级

Style Profile 以四个语义 ID 存储 Rim：`RimGrazingAngleRad`、`RimEdgeHalfWidthRad`、`RimPeakScale`、`RimColor`。前两项以弧度保存，窗口用度数显示；颜色以线性 RGB 保存，Alpha 不参与着色。解析顺序为编译默认值 → Profile Defaults → 当前 Material Kind 的覆盖。默认值为 8°、2°、0、白色。旧 Profile 没有 RGB 字段时继承白色。显式 0 强度和黑色是有效覆盖；“Reset” 删除该 Material Kind 的覆盖项，恢复继承。

## 在 NPR 调制窗口中使用

1. 在关卡中选中角色 Actor，打开 **窗口 → NPR 调制 → Rim Light**，点 **Read Selection**。界面自动选取第一个有材质槽的网格；如角色包含多个网格，在详情区改选 `Mesh`，并设置 `Slot Index`。范围为该网格现有的材质槽序号。
2. 选择已有 `Style Profile`，点击 **Load Profile**；或点击 **New Profile** 创建 `/Game/NPR/Profiles/SP_Rim*`。选择与原材质功能相容且含 RimLight 的 `Surface Recipe`，例如插件内容中的 `SR_DefaultLit_Rim` 或 `SR_Toon_Rim`。设置 `Material Kind`。
3. 展开 **Source Surface**，勾选 `bConfigured`，明确填写角色的基础颜色及贴图、法线贴图与强度、原有自发光颜色、贴图和倍率。此操作会将指定材质槽换成预制 Surface Master 的 MID；不会自动迁移任意原材质图。未配置输入时预览会报错且不会替换材质。
4. 点击 **Apply Preview**。随后改变宽度、柔和度、强度或颜色会在已有 MID 上实时更新。默认强度为 0；要观察亮边，请设置非零强度。改变 Recipe 或基础表面输入会重新组合该槽材质。
5. 每个 **Reset** 按钮取消对应的 Material Kind 覆盖并显示继承值。点击 **Save Profile and Actor Binding** 保存 Profile 资产和当前关卡。关卡保存前临时恢复原材质，保存后重新应用 Rim，以免把预览 MID 写入关卡。
6. 重开关卡时，角色上的 `UNPRStyleBindingComponent` 按保存的网格、槽位、Profile、Recipe 和基础表面输入重新绑定。再次选中角色并点击 **Read Selection** 可查看保存值。Undo 字段回滚已接入；测试负责人仍需验证实际操作。保存到磁盘后的撤销需要再次保存资产和关卡。

预览仅作用于选定角色的单个材质槽。关闭窗口时，未保存的新绑定会恢复原材质并销毁；已保存的绑定会重读 Profile。其他系统之后替换同一槽时，组件不会覆盖该系统的新材质。错误信息显示在 Rim 页面顶部。

## 范围与验证

本轮使用现有 `UNPRSurfaceComposerLibrary::ComposePackage`、`UNPRStyleBindingComponent::ApplyPackage` 和 `SetRimParameters`。预制 Recipe 决定材质能力，不修改 ToonProfile 或 Rim 函数。基础色、法线和原有自发光通过 Surface Master 的显式输入保留；任意第三方材质图、复杂材质层和未提供的贴图无法自动保留。

已完成 UE 5.8.3 Editor C++ 构建。`NPR.Rim.StyleProfile`、`NPR.Rim.ProfileBinding` 以及原有 `NPR.Rim.LiveBinding`、`NPR.Rim.MaterialContract` 用例通过。编辑器中对 `PNPR_ChenQianyu_EndfieldLike` 的单槽预览、Profile 与关卡保存、重开后读取 Profile / Recipe / 强度 1.0 通过；预览测试产生的 Profile 和关卡改动已从工作树移出并恢复。未配置源输入的失败保护也由运行时用例覆盖。角色切换清理、预览失败后的材质恢复与 Undo 回调的最新修正已通过 Editor 编译，尚待实际操作验证；四项控制的逐项画面效果、Profile 切换、Reset、阴影、动画、多 LOD、Cook、性能及角色材质观感仍需测试负责人验收。
