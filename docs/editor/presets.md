# 预设与保存

我们用预设保存一组基础描边设置，方便下次使用或应用到其他角色。这个功能保存的是基础风格；具体角色上哪些部位单独调整过，还需要保存在关卡的角色配置中。

## 选择和重新应用

选择一个 `UNPROutlinePreset` 后立即应用。“重新应用所选预设”会重新读取该预设的已保存设置。只读 Preset 引用只是来源记录，直接改显示引用不等于执行应用。

角色模式中，应用预设会把基础设置复制到 `BaseSettings`；本地区域覆写继续参与最终结果。若“换预设后某区域没变化”，先检查该区域是否覆写了同一字段。清空候选选择不会清除当前参数。

## 保存流程

在窗口填写项目内资产路径，例如 `/Game/NPR/Outline/Presets/OP_Custom`，点击保存。实现会先校验当前设置，写入预设，再应用并保存角色/单槽绑定所在关卡。若后一步失败，窗口可能提示“预设已保存，但关卡保存未完成”，应分别检查，不要把部分成功当作全部成功。

普通调参不会自动改写原预设。角色局部规则不打包进基础预设；要分享完整角色配置，还需提供关卡/Actor 配置和引用资产。

## 数据结构与限制

`UNPROutlinePreset` 是 UE 的 UDataAsset（可保存为工程资产的数据对象），包含 `SchemaVersion=1` 和 `FNPROutlineSettings Settings`，由 UE 资产序列化保存。窗口拒绝非 v1 预设；新增宽度字段通过声明默认值兼容旧 v1 资产，但跨版本实际载入仍需回归。

WidthMask 是设置中的可选纹理引用，因此预设可携带该引用；它仍依赖角色 UV0，换角色后不能保证效果一致。网格、槽身份、源材质等绑定数据不是基础风格数值。

全局 `UNPRStyleProfile` 是另一套语义资产宿主，不等于 OutlinePreset，也不等于 ToonProfile。其自动转换和标定仍待实现。

源码入口：`NPROutlineEditorLibrary.cpp::SavePreset / ApplyActorPreset / SaveActorOutline / SaveBinding`；`NPROutlineActorComponent.cpp::ApplyPreset` 失败时尝试恢复旧基础设置。相关文件见[参数来源](../principled/parameters.md)。
