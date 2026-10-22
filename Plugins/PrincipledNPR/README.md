# Principled NPR 插件

插件包含 `NPRRuntime` 与 `NPREditor` 两个 C++ 模块。前者管理数据、绑定、Surface 选择和独立描边；后者提供 NPR 调制窗口、预览与保存服务。Runtime 不依赖 UnrealEd，也不在运行时编辑 ToonProfile。

材质资源位于 `/PrincipledNPR/Outline`、`/PrincipledNPR/RimLight`、`/PrincipledNPR/Surface`。角色和工程测试场景位于 `/Game/NPR`，不属于通用插件发行资产。

- [架构与实际状态](../../Documentation/项目架构设计.md)
- [模块开发约束](../../Documentation/项目开发说明与约束.md)
- [NPR 调制操作](../../Documentation/NPR调制插件使用说明.md)
- [Outline 算法与使用](../../Documentation/outline_introduction.md)
- [Rim 算法、公式与使用](../../Documentation/rim_light_introduction.md)

Outline 保留整角色和旧单槽流程；Rim 的窗口页仍是占位，实际材质与程序接口另有验证。完整语义 Resolver、S58 标定、Face/Hair、Cook 和性能仍需各自验收。
