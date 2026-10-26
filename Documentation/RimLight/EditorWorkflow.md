# Rim Light 配置、角色绑定与编辑器工作流

更新日期：2026-10-26。本页说明 Profile、角色材质槽和 NPR 调制窗口链路。统一入口、角色双视口、单效果过滤及隔离操作见[统一效果工作区](../Editor/NPRWorkspace.md)。渲染效果仍需在目标角色上单独观察；C++ 编译成功不等同于所有角色资产通过视觉验收。

## 数据与优先级

Style Profile 以四个语义 ID 存储 Rim：`RimGrazingAngleRad`、`RimEdgeHalfWidthRad`、`RimPeakScale`、`RimColor`。前两项以弧度保存，窗口用度数显示；颜色以线性 RGB 保存，Alpha 不参与着色。解析顺序为编译默认值 → Profile Defaults → 当前 Material Kind 的覆盖。默认值为 8°、2°、0、白色。旧 Profile 没有 RGB 字段时使用白色。显式 0 强度和黑色是有效覆盖。“重置参数”重置本槽的编辑值，并在保存时删除对应材质类型覆盖；没有全局或角色级 Rim 继承链，不使用“恢复继承”的界面名称。

## 在 NPR 调制窗口中使用

1. 在关卡中选中角色 Actor，点击顶部 **Rim Light**，或从 **NPR 调制** 总览中的 **非后处理 → Rim Light** 进入，点 **使用关卡选中角色 / Read Selection**。角色视口显示当前网格与材质；在槽位卡片点“编辑参数”展开本槽控制。已有保存绑定会载入自己的配置；新槽需明确配置源输入。
2. 选择已有 `Style Profile`，点击 **Load Profile**；或点击 **New Profile** 创建 `/Game/NPR/Profiles/SP_Rim*`。选择与原材质功能相容且含 RimLight 的 `Surface Recipe`，例如插件内容中的 `SR_DefaultLit_Rim` 或 `SR_Toon_Rim`。设置 `Material Kind`。
3. 展开 **Source Surface**，勾选 `bConfigured`，明确填写角色的基础颜色及贴图、法线贴图与强度、原有自发光颜色、贴图和倍率。此操作会将指定材质槽换成预制 Surface Master 的 MID；不会自动迁移任意原材质图。未配置输入时预览会报错且不会替换材质。
4. 配置齐全后勾选本槽 **Rim** 或点击 **Apply Preview**。已加载的有效槽也会在修改颜色或强度时自动开始实时预览，不要求重复点击应用。拖动与颜色选择器的交互更新直接同步到当前 MID；默认强度为 0，要观察亮边请设置非零强度。改变 Recipe 或基础表面输入会重新组合该槽材质。
5. **重置本槽**按钮将对应参数恢复为所选 Profile 的默认值，**重置参数**恢复本槽四项参数，并在保存时删除对应的 Material Kind 覆盖。点击 **Save Profile and Actor Binding** 保存 Profile 资产和当前关卡。关卡保存前临时恢复原材质，保存后重新应用 Rim，以免把预览 MID 写入关卡。
6. 重开关卡时，角色上的 `UNPRStyleBindingComponent` 按保存的网格、槽位、Profile、Recipe 和基础表面输入重新绑定。再次选中角色并点击 **Read Selection** 查看保存值。Undo 同时恢复未保存值和覆盖意图，不重新载入 Profile 覆盖上一步。保存到磁盘后的撤销需要再次保存资产和关卡。

每次调参作用于选定角色的一个材质槽，各槽可以保留自己的绑定。关闭窗口或切换目标时，未保存的新绑定恢复原材质并销毁；仍由本组件持有的已保存绑定恢复保存配置。外部系统替换同一槽后，清理和自动刷新均保留外部材质。错误信息显示在 Rim 页面顶部。

Profile 是可复用的资产，不是绑定组件的私有副本。多个槽或角色共用同一 Profile 与 Material Kind 时，保存 Profile 会改变这些引用者下次解析的值；这不等于建立了全局 Rim 继承链。需要独立外观时选择独立 Profile，或明确区分材质类型。

“隔离 Mesh”“隔离此槽”和“只看当前 Rim 效果”仅影响预览世界。原材质对比共享相机与灯光，源角色可见性不变。

## 范围与验证

本轮使用现有 `UNPRSurfaceComposerLibrary::ComposePackage`、`UNPRStyleBindingComponent::ApplyPackage` 和 `SetRimParameters`。预制 Recipe 决定材质能力，不修改 ToonProfile 或 Rim 函数。基础色、法线和原有自发光通过 Surface Master 的显式输入保留；任意第三方材质图、复杂材质层和未提供的贴图无法自动保留。

2026-10-26 联调先保存了合并版本的[失败基线](Integration-2026-10-26.md)，随后修复 Undo、外部材质保护和图像门禁，并补齐统一工作区与角色预览。新增 `NPR.Editor.RimWorkflow` 与既有 Rim/Surface 用例的范围分别记录在[工作区验证](../Editor/WorkspaceValidation.json)。历史 2026-10-25 的单槽预览与保存记录保留；阴影、动画、多 LOD、Cook、性能和完整角色材质观感仍需独立验收。
