# PrincipledNPR 角色资产接入记录

2026-10-04；实机引擎 UE 5.8.3 / CL 58210709。

陈千语已接入 `.\PrincipledNPR.uproject`，并在 UE 编辑器中创建、保存和观察了独立角色关卡。流浪者已完成 PMX 文件结构与贴图引用检查，尚未转换或导入 UE。

## 实际完成

| 项目 | 结果 |
|---|---|
| 陈千语源项目 | UE 5.6 项目；源目录未转换或写入 |
| Content 迁入 | 150 个文件，97,391,373 字节；逐文件 SHA-256 与源文件一致 |
| 资源路径 | 保留 `/Game/EndfieldRender`，避免物理重命名破坏引用 |
| 依赖检查 | UE Asset Registry 的硬、软包依赖未发现缺失；不等于所有原蓝图逻辑已验证 |
| 加载检查 | 3 个骨骼网格、80 张 Texture2D、25 个材质实例完成元数据读取，无审计异常 |
| 首选主网格 | `/Game/EndfieldRender/Character/Qianyu/SKM_Chen` |
| 主网格规模 | LOD0：56,635 个渲染顶点、11 个 Section；325 根骨骼；只有一个 LOD |
| 骨架 | `/Game/EndfieldRender/Character/Qianyu/SKM_Qianyu_Skeleton` |
| 头骨候选 | `Bip001_Head` 存在；头骨局部 Forward/Right/Up 尚待校准 |
| 新关卡 | `/Game/NPR/Maps/L_ChenQianyu_AssetProbe`，7 个 Actor |
| 新材质 | 两个通用 S58 父材质、一个零覆盖率代理排除材质、16 个贴图材质实例 |
| 材质编译 | 三个父材质的即时错误数组为空；最终角色实际显示，当前日志未发现 Material/Shader 编译失败 |
| 源资源保留 | 不编辑迁入网格、原材质、原纹理；使用 Actor Component 的材质覆写 |
| 原球体探针 | `/Game/NPR/Maps/L_S58_ToonProbe` 保留，用于独立管线测试 |

角色关卡左右两份角色分别采用既有诊断 Profile A/B：A 把阴影可见性放在 Ramp 外，B 开启 `DiffuseRampIncludeShadow`。两者不是已完成的 AnimeFlat / SemiPBRStylized 风格资产。

八个可见材质槽使用通用 Toon 响应。多数 BaseColor 绑定来自原材质实例的实际 `T_Base_Color` 参数；槽 4 原为 WorldGrid，暂明确使用服装 D 贴图，需后续逐槽检查。槽 3 的原内嵌描边、槽 5/10 的原阴影代理暂以零 OpacityMask 排除。排除像素不等于消除了这些 Section 的提交或顶点成本。没有生成第二份 Outline 网格 Actor，也没有运行源项目的角色蓝图。

![UE 5.8 实际角色检查截图](Documentation/角色资产_陈千语S58测试场景.png)

## 仍需完成的资产契约

1. **Face 场编码。** 候选 `T_actor_common_female_face_01_SDF` 为 1024×1024，当前 sRGB=True、默认压缩、Wrap 寻址。存在该贴图不代表它已经是临界方位角场，也不证明它接入了现有 Face 材质。必须检查原图、源材质图和数值约定，再制作独立的线性数据版本并校准 threshold、mirror 和 UV；不要直接改变原纹理设置。
2. **Head Frame。** 网格正面在当前 Component 坐标中沿 +Y，可由实机正面观察确认；这不能替代头骨局部轴校准。需要验证头骨动画、左右基向量和非均匀缩放处理。
3. **Hair 法线与流向。** `hair_01_HN` 是 2048×2048、sRGB=False、BC7；不能未经解码验证就按普通 Normal Map 接入。原头发材质的 `ShiftTex` 实际绑定公共 512×256 纹理，而角色 `hair_01_ST` 在源描边实例中作为 `T_Mask` 使用。文件名 `_ST` 不能证明 Hair Flow 语义。
4. **UV 与遮罩。** 当前检查没有读取每个 LOD 的 UV 通道布局、切线手性、贴图 RGBA 的完整含义、脸部镜像缝或 Morph Target。绑定草案中的未知项保留为 Unknown/null。
5. **性能与 LOD。** 主网格有 11 个 Section，Outline 网格有 109,019 个渲染顶点、14 个 Section。V1 可以研究简化、移除不使用的源代理和生成 LOD，不能把当前整套源材质的规模作为插件性能结论。

当前 Face、Hair Angular Model、Style Adapter、运行时风格切换和原 30 项正式管线测试仍未完成。截图只证明本次资产加载、通用材质绑定和场景显示；地面阴影可见不等于 Cast/Receive/Self Shadow、VSM/CSM、Lumen 或间接反射已通过验收。

## 流浪者

源文件 `SourceAssets/Wanderer/Original\流浪者.pmx` 是 PMX 2.0：27,650 个顶点、32,747 个三角形、382 根骨骼、26 个材质。全部 10 个声明贴图引用可在源目录找到，材质三角形数量与总索引数一致。检查读取到材质表和骨骼总数，没有检查各骨骼、表情、物理约束或完成格式转换。

本机工程没有启用 PMX 导入器，因此本次没有尝试把 `.pmx` 当作 UE 骨骼网格。后续需要受控转换流程，验证坐标单位、法线、骨骼、权重、UV、材质拆分和表情后，再评估为第二个泛化测试角色。陈千语优先承担首个角色探针。

源说明注明模型提供 miHoYo、模型改造观海，并禁止商用与二次配布。本项目按本地非商业研究素材登记，不将该模型或原贴图打包进插件分发物。陈千语目录本次未发现同类说明文件，分发用途仍需另行确认素材来源。

## 开发入口

- `Tools/audit_character_assets.py`：重新扫描并只读审计迁入资源。
- `Tools/create_character_asset_probe.py`：创建或复用自有诊断材质、实例及关卡；不覆盖已完成 Actor 的后续编辑。
- `Tools/set_character_front_view.py`：明确的正面观察设置工具，只操作自有关卡的相机和方向光。
- `Documentation/Assets/NPRCharacterBinding.ChenQianyu.v1.draft.json`：Face/Hair/Head Frame 的未校准绑定草案。
- `Documentation/Assets/ChenQianyu.UE58.audit.json`：完整骨骼名称、材质槽、纹理设置及有效材质参数。
- `Documentation/Assets/ChenQianyu.ProbeCreation.json`：本次新建资产、Actor、材质槽策略和编译请求记录。

所有脚本必须在指定 PrincipledNPR 项目内运行。UE Python 创建 `Rotator` 使用具名 `pitch/yaw/roll` 参数。

本机 5.8.3 的 `UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue` 实现没有把 `bResult` 设为 True，所以返回 False 不能证明赋值失败。创建脚本改为调用 Setter 后读取实际纹理参数并比较资源路径。依据：本机 MaterialEditingLibrary.cpp (`Engine/Source/Editor/MaterialEditor/Private/MaterialEditingLibrary.cpp:1507`)。标量 Setter 有相同实现模式，后续自动化同样应做读回校验。

建议下一轮依次执行：原生 S58 光照能力测试 → Head Frame/UV/纹理通道标定 → Face 场转换与后端桥接探针 → Hair Tangent/Anisotropy 探针。通过这些门槛后，才生成两个语义 StyleProfile 的可用后端资源。
