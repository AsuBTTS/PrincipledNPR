# 参数映射与控制粒度

## 全局语义链路（开发中）

```text
资产事实 + PBR 表面输入 + Material Semantics + Style Semantics
→ 稀疏 Resolve → Capability Check → Backend Mapping / Calibration
→ Immutable Backend Package → MIC / MID Bundle
```

契约覆盖顺序是 `SchemaDefaults → MaterialDefaults → StyleDefaults → StyleMaterialPatch → InstancePatch → RuntimeTransientPatch`。该顺序是设计契约，完整解析器尚未实现；目前没有完整 MaterialDefaults 数值集合，不应自行补猜。

Patch 以稳定 ID 为键，`isSet=false` 或缺字段表示继承，`0` 是有效值，Reset 删除本层字段。硬域非法应报错，不静默 clamp；推荐域只给诊断。JSON 与 C++ 分别使用 `isSet/value` 和 `bIsSet/Value`，尚无完整自动导入转换。

```json
{ "TerminatorDotCenter": { "isSet": true, "value": 0 } }
```

S58 中交界坐标候选 `u=(tau+1)/2` 仅在限定条件下坐标精确，不证明 LUT、AA、最终外观精确。高光角宽需拟合 Roughness + SpecularRamp，禁止采用 `size=1-roughness`。Face 逐灯 terminator 替换受公开接口限制；主光 PatternUV offset bridge 是近似候选，需测试量化和 wrap 接缝。所有现有映射条件逐项列在[自动表](./parameters.md)。

## 独立 Outline 链路（已有实现）

`Actor.BaseSettings → Component.Override → Material Slot.Override → Section.Override` 按勾选字段逐级替换。它不是上述全局 Resolver 的替代实现。区域倍率由最深覆写替换，不逐层相乘。

```text
GetResolvedWidth = Method 对应基础宽度 × 当前边来源倍率 × 最终 LocalWidthScale
Hull 位移 = NormalWS × GetResolvedWidth × 顶点色因子 × UV0 Mask 因子
Screen 扩张半径 = GetResolvedWidth（输出像素）
```

`ApplyMaterialParameters` 将 Color 写入 `OutlineColor`，Hull 写 `WidthCm`、Screen 写 `WidthPixels`；不活动方法的宽度写 0。另写 `DepthBiasCm`、`UseVertexColorWidth`、`UseWidthMask`、`WidthMask`。组分配的 `StencilId` 和辅助显隐 `Visible` 属后端运行数据，不是美术风格语义。关闭 Mask 时复位为自有白纹理。

| 粒度 | 当前 Outline | 全局语义规划 |
| --- | --- | --- |
| 整角色 / 多 Mesh | Actor 配置、分组与总开关 | 风格解析入口和资源预载仍需实现 |
| Mesh / Component | 独立 Override；辅助组件跟随源组件 | 资产事实与实例 patch 应分离 |
| Material Slot | 唯一槽名；同槽 Section 一起生效 | 显式包目前按槽索引绑定单个 Mesh |
| Section | 仅单 LOD 且独占槽 | 稳定身份、LOD 映射需独立方案 |
| 顶点 / UV0 | Hull 可乘 R 通道局部因子 | 不能自动成为 Screen 属性缓冲 |

不能用像素宽度直接换算 Hull 厘米宽度：它依赖距离、投影和几何。后续若需要跨策略保持相同视觉语义，应新增显式校准策略及误差范围，保留这两个旧参数。
