# 表面材质组合现有功能规范

## Purpose

记录方案 B 已实现的 Surface 母材质家族、预制 Recipe 选择、Rim 参数、显式绑定和统一效果工作区。规范与易读说明分工：Documentation/项目架构设计.md 解释架构，Documentation/项目开发说明与约束.md 说明模块约束，Documentation/rim_light_introduction.md 解释算法和使用。完整风格解析与任意角色自动转换仍未完成。

## Requirements

### Requirement: Preserve base surface and additive contributions

Surface 材质 MUST 保留基础颜色、切线法线与原有自发光。最终自发光按 BaseEmissive + RimColorContribution + AdditionalEmissiveContribution 相加，不用 Rim 覆盖原表面，也不对最终 HDR 贡献做 0–1 截断。

#### Scenario: Rim is changed on a composed surface
- **WHEN** 同一个兼容 Surface MID 更新 Rim 四项参数
- **THEN** 原 BaseColor、Normal 和基础自发光输入保持原值
- **AND** Rim 仅改变其独立附加贡献

### Requirement: Prebuilt family selection

Composer MUST 在调用方明确指定的家族中按候选顺序选择第一个兼容 Recipe，返回现有类型的绑定包；不能静默丢弃所请求效果、跨家族回退或在运行时生成任意材质图。

#### Scenario: Requested effect is unavailable
- **WHEN** 请求 MatCap 等当前 Recipe 不支持的效果
- **THEN** 返回错误且不改变目标网格的材质

### Requirement: Independent effect ownership

Recipe MUST 声明效果标识、输出契约、组合规则、依赖和参数归属，检查实际参数类型与唯一归属。多个 Surface 效果通过同一个兼容材质槽 MID 更新，不能互相占用参数名。

#### Scenario: Two effects claim the same parameter
- **WHEN** 两个效果声明相同实际材质参数
- **THEN** Recipe 校验拒绝冲突

### Requirement: Frozen rim parameter contract

Rim MUST 保留 MF_RimLight、FNPRRimParameters、SetRimParameters 与既有映射。材质控制为亮边宽度 8°、边缘柔和度 2°、边缘光强度 0、边缘光颜色白色；两个程序角度为弧度，写入中文材质参数前转为度。0、黑色和有限非负 HDR 颜色是有效值。

#### Scenario: Canonical rim values are written
- **WHEN** 调用者提供合法弧度角度、强度与 RGB
- **THEN** 专用入口检查全部绑定槽，再转换两个角度并一起写入四项参数

### Requirement: All-slot validation and ownership-safe restore

显式绑定与 Rim 更新 MUST 在改变目标槽或写入参数前完成所需检查。任一目标失败时不产生部分 Rim 写入；恢复时只恢复仍由该组件持有的材质，不覆盖其他系统替换的材质。

#### Scenario: Another system replaces a bound slot
- **WHEN** 外部系统替换某个绑定槽的材质
- **THEN** 后续 Rim 更新拒绝该绑定状态，恢复操作保留外部材质

### Requirement: Separate surface inputs and semantic mapping

系统 MUST 区分 NPR_ 开头的 Backend 材质输入和统一 Principled 语义 ID。新技术输入使用原生 MID 写入时由调用者校验数值域；Recipe 类型检查不能声称已校验任意原生写入或任意图的实际算法。

#### Scenario: A surface texture is assigned
- **WHEN** 角色接入 Surface 家族
- **THEN** 明确提供对应纹理和数值，不推断任意原材质图已经自动转换

### Requirement: Preserve independent outline execution

Surface 组合 MUST 保留 Screen Outline 的后处理和 Inverted Hull 的几何后端，由原控制器管理，不强制将它们接入 Surface 母材质。

#### Scenario: A surface recipe is selected
- **WHEN** Composer 选择兼容的表面 Recipe
- **THEN** 不接管 Outline 的 Stencil、后处理 MID 或辅助几何设置

### Requirement: Report current capability and evidence limits

文档 MUST 说明统一工作区与显式槽位绑定的实际范围，保留完整风格解析与任意角色自动转换未完成的边界；Toon+Rim 的既有证明限于未标定探针和受控球体。编译、二进制资产检查、图像、Cook 和性能分别记录，不相互代替。

#### Scenario: A runtime package is composed successfully
- **WHEN** Recipe 检查和绑定包构造成功
- **THEN** 包保持既有 NotRun 渲染验收状态，不因选择成功自动升级为画面通过

### Requirement: Unified workspace and isolated character preview

NPR 调制 MUST 作为统一入口，按后处理和非后处理分类现有效果，按 Mesh/材质槽列出实际登记的绑定。Rim 与总览 MUST 提供角色视口、原材质对比、Mesh/槽位隔离和单效果过滤，复用现有渲染路径；不得通过修改关卡可见性或关卡参数实现隔离。

顶部导航 MUST 只保留 NPR 调制、Outline、Rim Light；NPR 调制使用 16 号字，Outline 和 Rim Light 使用较小的 12 号字，内容高度一致，文字垂直居中。后处理/非后处理分类 MUST 位于 NPR 调制总览的页面内容中，不占用顶部导航栏。

#### Scenario: One slot or effect is isolated
- **WHEN** 使用者选择隔离一个 Mesh、槽位或已登记效果
- **THEN** 仅临时预览副本改变，源角色可见性和材质不变
- **AND** 原材质与效果视口共享相机与灯光

### Requirement: Editor transactions and ownership-safe cleanup

Rim 编辑器 MUST 将显示值和稀疏覆盖意图一起撤销；已保存目标的连续未保存编辑不得通过重读 Profile 跳回保存值。释放预览和切换角色时 MUST 保留外部替换的材质。

#### Scenario: Undo a second unsaved edit
- **WHEN** 保存强度 0.4 后依次编辑为 0.8、1.6，再撤销一次
- **THEN** 显示值和当前 MID 回到 0.8，并保留对应覆盖意图

#### Scenario: Switch away after another system replaces the material
- **WHEN** 外部替换当前槽后，使用者读取另一个角色
- **THEN** 旧角色保留外部材质，清理不能重新应用保存配置覆盖它
