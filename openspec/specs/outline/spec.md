# Outline 现有功能规范

## Purpose

记录当前独立描边后端、参数、区域覆盖、预览和保存的行为基线。本文依据现有源码和历史验收记录，不声明新增渲染验证。通俗算法和公式见 Documentation/outline_introduction.md，窗口操作见 Documentation/NPR调制插件使用说明.md；新增宽度/遮罩、复杂动画及打包性能的未验收范围继续保留。

## Requirements

### Requirement: Independent outline backends

系统 MUST 保留 InvertedHull、ScreenSpace 和 None。两种描边由独立控制器和现有材质执行，并保留角色原表面；整角色与旧单槽入口继续存在。

#### Scenario: Surface effects coexist with outline
- **WHEN** 一个已配置 Surface 的对象启用任一种受支持的描边
- **THEN** 描边资源独立建立，不能替换该对象原有 Surface MID
- **AND** 停用描边时清理或停用对应辅助资源

### Requirement: Width units and final range

系统 MUST 保留屏幕基础宽度 1.25 px、硬域 0–8 px，壳基础宽度 0.1 cm、硬域 0–5 cm；最终宽度为基础值乘当前边缘来源倍率和最终区域倍率。屏幕像素与壳厘米不得混为同一单位。

#### Scenario: Multiplied width exceeds final range
- **WHEN** 单个字段合法但乘法结果超过 Screen 8 px 或 Hull 5 cm
- **THEN** 校验拒绝该设置并给出原因

### Requirement: Sparse region overrides

系统 MUST 按角色、组件、材质槽、Section 顺序解析勾选字段，最深层覆盖生效；未勾选字段继承。0 是有效值，恢复继承删除本层覆写，区域倍率不逐层相乘。

#### Scenario: Local override is reset
- **WHEN** 使用者恢复某材质槽的继承
- **THEN** 删除该层规则并重新使用父层值，而不是写入 0 作为替代

### Requirement: Supported region and method combinations

系统 MUST 拒绝歧义的 Section、多 LOD Section、透明或裁切表面等当前不支持的覆盖。UnifiedSilhouette 需要 Screen 和一致的组参数；Hull 不支持手工内部边界，Screen 不支持连续顶点色或贴图宽度。

#### Scenario: Unsupported continuous screen width
- **WHEN** ScreenSpace 设置启用顶点色或 UV 宽度贴图
- **THEN** 校验报告不支持，不能把该设置当成已生效

### Requirement: Preview isolation is temporary

编辑器 MUST 将材质槽隔离限制在临时预览中；实际角色、源网格和原始材质不因隔离被修改。原材质对比保持相同模型、相机和灯光条件。

#### Scenario: A material slot is isolated
- **WHEN** 使用者勾选某槽隔离后取消或显示全部材质
- **THEN** 预览恢复完整角色，关卡源组件可见性保持

### Requirement: Preset and level persistence

系统 MUST 区分基础风格预设和角色局部设置。保存基础预设与当前关卡时，基础设置进入预设，角色及区域覆写随关卡保存；PNG 导出不代替上述保存。

#### Scenario: Same preset is reapplied
- **WHEN** 使用者点击重新应用所选预设
- **THEN** 再次应用该资产当前基础值，并继续解析关卡局部覆写

### Requirement: Bounded validation statements

文档 MUST 区分基础描边、界面、时间稳定性与新增宽度遮罩的验证范围，保留原始证据和未运行项。历史通过不自动覆盖后续渲染改动。

#### Scenario: Only documentation is changed
- **WHEN** 本轮只重写说明或移动旧文档
- **THEN** 交付明确没有重新运行 UE 渲染，不将文档构建描述为图像验收
