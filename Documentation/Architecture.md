# Principled NPR 初步语义骨架

2026-10-22 方案 B 增量：已增加 Surface Master Family / Recipe、共享表面函数、预制选择与绑定包链路，见 [Surface 实施记录](Surface/Implementation.md)。这不替代完整语义 Resolver；原有 Rim 与 Outline 保留。实际验收范围以新增执行记录为准。

2026-10-17 补充：独立的 Outline 模块已接入角色绑定、双视口、预设与关卡保存链路，见 [Outline 技术模块报告](Outline/技术模块报告.md)。下面的 Draft 状态指完整语义解析和表面模型后端，不代表 Outline 仍为空壳。

状态：**Draft / 未标定 / 未连接 Shader**。这些版本化 JSON 是语义契约草案；2026-10-04 已添加 C++ 宿主、NPRRuntime / NPREditor、基础 UDataAsset 类型和显式预制材质绑定组件，详见 `C++骨架.md` 与 `CppScaffold.Validation.json`。完整 Resolver、Backend Adapter 和语义驱动 Runtime Style Switch 尚未实现。JSON 不参与引擎着色，不等于 ToonProfile，也不证明渲染测试通过。

## 文件

- Schemas/NPRParameterContract.v1.json：单位、范围、语义、MaterialProfile 输入要求、S58 映射与能力边界。
- Schemas/NPRStyleProfile.schema.v1.json：风格草案 JSON Schema，拒绝未知参数 ID，角度存弧度。
- Styles/AnimeFlat.v1.draft.json：清晰两阶、窄角域高光、较强形体抽象与条带设计初值。
- Styles/SemiPBRStylized.v1.draft.json：宽明暗过渡、柔和高光、较多细节与间接贡献设计初值。

报告来源 SHA256：e2b89f5a297e0130db6cca61442e41ce040f556b3b0806f5795bdb6302b91ff3。它对应创建骨架时读取的报告字节；不是后端实现或标定结果 hash。

## 数据流与所有权

PBR Surface Data + Asset Facts + Material Semantics + Style Semantics → Resolve → Capability Check → Backend Mapping/Calibration → Immutable Backend Package → MIC/MID Bundle。

NPRStyleProfile → NPRBackendSubstrateToon → UToonProfile。Style 是引擎无关语义资产，ToonProfile 是后端产物。A0 仅作数学参考；S58 是须实测接受的 V1 生产候选。关键缺口被证明并冻结需求后，才评估局部 A+ 或自定义着色模型。

覆盖顺序：SchemaDefaults → MaterialDefaults → StyleDefaults → StyleMaterialPatch → InstancePatch → RuntimeTransientPatch。

Patch 使用稳定 ID 与 isSet/value；缺字段或 isSet=false 表示继承，0 为有效覆写。Reset 删除当前层字段。非法硬域报错，推荐范围只给诊断。HeadBone、Face 轴、UV 约定、临界角场编码与根梢流向是资产事实，不能由 Style 改写。当前还没有 MaterialDefaults 数值集合；Resolver 实现前以 schemaDefault 为缺字段基准，不猜测材质覆写。

## 后端边界

Generic diffuse 坐标映射可有条件精确，最终响应、LUT 与 AA 未标定。Specular 角宽/软度/峰值为 Approximate 候选，不能把 roughness 改名 size。Normal 预处理须按模型实现并验证引导法线。

Face 逐灯完整 terminator 替换在公开 Toon 接口下为 Unsupported。PatternUV offset bridge 为 Approximate 候选，须标定单调内部范围、12-bit 量化、frac/wrap 接缝与全遮挡表现。主光偏移不等于逐灯 Face 场。刘海、鼻子与场景遮挡仍须实测。

Hair 原生 Tangent/Anisotropy 为 Approximate 候选。独立 shifted lobe 与全视角参考角域瓣不能默认精确复现；旋转共享 Normal 会同时影响 diffuse/GI，不满足独立高光 shift 语义。

Rim、Outline、MatCap、颜色映射是独立模块。Outline 已通过强类型设置独立接入；2026-10-21 Rim 新增独立材质函数、表面材质与四项控制预览实例，见 `RimLight/Materials.md`，尚未接入完整语义 Resolver 与角色工作流。MatCap 和颜色映射尚未连接。Emissive Rim 不自动拥有 native shadow visibility。

2026-10-22 Rim 第②步补齐 `FNPRRimParameters` 强类型契约、校验及度数转换，四项材质绑定固定在 `RimLight/MaterialParameters.v1.json`，交接见 `RimLight/ParameterHandoff.md`。新增可选 `RimColor` 语义 ID，默认线性白色，原三个标量 ID 保持不变；旧 `FNPRParameterPatch.Values` 仍只存标量覆盖项，RGB 持久化由第④步扩展。第③步已通过 `UNPRStyleBindingComponent::SetRimParameters` 接入显式 MID 绑定，先检查全部槽再写四项值，验证范围见 `RimLight/RuntimeBinding.md`。完整 Resolver 和自动角色绑定仍待实现。独立 Rim 不需要 S58 或 ToonProfile，S58 只是可选后端候选。

## 下一轮开发

1. 固定 UE 5.8.3 patch/CL 与 GBuffer 配置，完成真实 Toon 测试地图并记录环境。
2. 实现最小语义结构和稀疏 Resolver，验证覆盖顺序、显式零值、Reset 与缺资源诊断。
3. 从 canonical diffuse 生成 native Curve/Profile；拟合 specular 并输出有效范围和误差。
4. 固定受控资产单独执行 FaceGate/HairGate，Generic 成功不替代角色特化验收。
5. 将两 Draft 风格标定为有证据的后端 bundle，预生成 ToonProfile/MIC；运行时预载与切换，禁止写 Profile Settings。
6. 独立 Rim/Outline 实现后再发布完整风格。保存源 hash、Profile/MI 引用、Engine patch/CL、测试状态和降级清单。

## 当前验收

只验收 JSON 可解析、参数 ID/单位/范围一致、两风格稀疏 patch 可校验、真实来源 hash 与文件存在。引擎安装、材质编译、实际着色、阴影/GI/运行时切换和受控角色资产由独立测试决定；此骨架均保持 NotRun。
