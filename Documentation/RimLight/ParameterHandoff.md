# ② Rim Light 材质参数暴露与契约

我们把四项控制固定成一份共同约定：材质、C++ 调参和风格存储都按这份表对接。这个步骤交付参数名称、类型、单位、默认值、有效范围、校验与转换方法。MID（运行中可调参数的材质实例）的写入属于第③步，Style Profile（风格资产）的保存属于第④步。

## 四项控制怎么对应

| 美术看到的名称 / 实际材质 FName | 稳定语义 ID | 材质参数类型 | 统一数据中的单位 / 默认值 | 材质中的单位 / 默认值 | 硬域 / 推荐范围 |
| --- | --- | --- | --- | --- | --- |
| 亮边宽度 | `RimGrazingAngleRad` | Scalar | 弧度 / 0.13962634015954636 | 度 / 8 | 0–π/2 rad（0–90°）/ 建议 1–35° |
| 边缘柔和度 | `RimEdgeHalfWidthRad` | Scalar | 弧度半宽 / 0.03490658503988659 | 度半宽 / 2 | 0–π/2 rad（0–90°）/ 建议 0–10° |
| 边缘光强度 | `RimPeakScale` | Scalar | 线性倍率 / 0 | 线性倍率 / 0 | 有限非负值 / 建议 0–2 |
| 边缘光颜色 | `RimColor` | Vector，取 RGB | 线性 RGB / [1,1,1] | 线性 RGB / 白色 | 每通道有限非负值 / 建议 0–1，允许 HDR |

语义 ID 是存储、解析用的稳定键；FName 是 UE 查找材质参数时使用的名字。当前四个中文材质名就是绑定键，不能只改中文名字而不做显式迁移。材质参数都放在“边缘光”分组。颜色 Alpha 不参与渲染，RGB 不做亮度归一化。

基础材质与函数的强度默认值始终为 0；`MI_RimLight_Preview` 单独覆盖为 1，以便打开时看到亮边。材质的滑杆分别是 0–35°、0–10°、0–2；滑杆不是硬域，宽度和柔和度仍可在有效范围内手动输入到 90°。

## 第③步如何使用这份约定

第③步现已实现为 `UNPRStyleBindingComponent::SetRimParameters`。下面的检查与转换由这个方法整组执行，调用方式和验证范围见 [③ 动态调参](RuntimeBinding.md)。`FNPRRimParameters` 本身仍只负责值与校验，不持有材质。

1. 先把统一数据填入 `FNPRRimParameters`。
2. 调用 `Validate(Error)`，发现 NaN、无穷、负值或越界角度就停止本次更新并显示错误；校验不修改原值、不静默截断。
3. 确认目标材质具有表中全部四项参数，且为三个 Scalar、一个 Vector。确认完成后再整组更新，避免只写成功一部分。
4. 通过 `GetMaterialWidthDegrees()`、`GetMaterialSoftnessDegrees()` 将弧度转换为当前材质所需的度数；强度直接传递，颜色作为线性 RGB 传递。
5. 使用 `NPRRimContract::*MaterialName` 查找参数。目标材质随后在调用 `MF_RimLight` 前乘 π/180，函数继续接收弧度。

```cpp
#include "NPRRimParameters.h"

FNPRRimParameters Rim; // 8°、2°、强度 0、白色
FString Error;
if (!Rim.Validate(Error))
{
    // 交给调用方显示 Error，并停止本次更新。
    return;
}
const float WidthDegrees = Rim.GetMaterialWidthDegrees();
const float SoftnessDegrees = Rim.GetMaterialSoftnessDegrees();
const FName WidthName(NPRRimContract::WidthMaterialName);
// 第③步负责目标参数检查与四项 MID 写入；本结构不持有或修改材质。
```

注意方向：统一数据 → 材质参数是“弧度转度”；材质参数 → 函数输入是“度转弧度”。不能把 canonical 弧度值直接写入中文 Scalar 参数。直接在材质实例面板输入数值不会自动调用 C++ Validate，调用方需要自行遵守有效范围。GPU 使用 float，传入数据必须能表示为有限 float；Python 转换工具也检查这一边界。

## 第④步如何保存与继承

- `RimColor` 是新增的可选语义 ID，JSON 值必须是三个数，例如 `{"isSet":true,"value":[1,0.2,0.1]}`。旧风格缺少它时继承，最终 schema 默认是白色；不要求改写旧文件。
- 旧 `FNPRParameterPatch.Values` 仍为标量覆盖项表（`FNPRFloatOverride.Value` 是 double），不能保存三个 RGB 分量，也不能只存亮度。第④步需要增加有类型的颜色存储及往返转换。
- `isSet=false` 或缺字段表示继承；`0` 强度、`0` 角宽和黑色 `[0,0,0]` 都是有效覆写。Reset 删除当前层值。
- 保持已有覆盖顺序：`SchemaDefaults → MaterialDefaults → StyleDefaults → StyleMaterialPatch → InstancePatch → RuntimeTransientPatch`。当前没有完整 Resolver；工具中的缺省值填充只服务这四项，不替代六层解析。
- 原有三个标量 ID、数值含义和默认值保持不变。此增量为 contract v1 的可选字段扩展，不修改 `responseModelVersion`。不认识 `RimColor` 的旧消费端不能保存新颜色，需要在第④步明确迁移或报错。

## 材质计算与依赖

`亮边宽度、边缘柔和度 → 角度带权重 → × 边缘光强度 × 边缘光颜色 → Emissive`

形状继续使用现有角度模型与几何正面门控，没有改变算法。依赖非零世界法线、视线和像素导数；控制粒度为每个材质实例或函数调用，适用于 Generic / Face / Hair。不依赖 S58 或 ToonProfile；角宽不是固定像素厚度，颜色与强度存在乘法耦合，默认没有灯光方向或原生阴影可见度输入。

## 来源与验证

- 统一语义：`Documentation/Schemas/NPRParameterContract.v1.json`；可选颜色 JSON 形状：`Documentation/Schemas/NPRStyleProfile.schema.v1.json`。
- 材质映射：`Documentation/RimLight/MaterialParameters.v1.json`，版本 1。
- C++：`Plugins/PrincipledNPR/Source/NPRRuntime/Public/NPRRimParameters.h` 与对应 `.cpp`；不依赖 UnrealEd。
- 构造脚本：`Tools/create_rim_light_assets.py` 从映射表读取默认值；已有自有资产保留人工编辑，不强制重建。
- Python 转换：`Tools/rim_light_contract.py`；离线交叉检查：`Tools/verify_rim_light_contract.py`，27 项通过，见 `ContractValidation.json`。
- 实际二进制：`Tools/audit_rim_light_contract.py` 在 UE 5.8.3 / Windows 的独立 NullRHI 进程中重载并读取现有三个资产，34 项通过，包含四个命名函数输入的接线检查，未保存资产，见 `ParameterExposureValidation.json`。这是参数暴露检查，不是新画面验收。
- C++ 构建与执行范围见 `ImplementationValidation.json`。2026-10-22 在 UE 5.8.3 / Windows / DX12 SM6 完成三项自动化用例，其中 `NPR.Rim.MaterialContract` 覆盖默认值、单位转换、显式零值、HDR 和非法输入；双槽与真实图像范围见第③步记录。

完整 Rim 工作流仍处于开发中。第②步不会让 Profile 自动驱动画面，也没有新增角色绑定或调制窗口控件；角色、多视角、阴影、时域和 Cook 验收仍须单独完成。
