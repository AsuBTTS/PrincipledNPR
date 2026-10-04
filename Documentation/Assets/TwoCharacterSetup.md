# 流浪者导入与双风格对应

2026-10-04；实际引擎 UE 5.8.3 / CL 58210709。

流浪者已从 PMX 转换并导入现有 PrincipledNPR 项目，双角色关卡已保存、重载并实际观察。

| 角色 | 默认语义风格 | 参考方向 |
|---|---|---|
| 陈千语 | SemiPBRStylized | 终末地方向 |
| 流浪者 | AnimeFlat | 原神方向 |

这只是默认对应。角色资产、Generic / Face / Hair 材质语义与 StyleProfile 继续独立，允许日后用同一角色交叉验证两种风格。

## 项目与关卡

- 项目：`.\PrincipledNPR.uproject`。
- 双角色关卡：`/Game/NPR/Maps/L_TwoStyle_CharacterProbe`，已设为 EditorStartupMap。
- 独立光照探针和 GameDefaultMap 保持 `/Game/NPR/Maps/L_S58_ToonProbe`。
- 流浪者网格：`/Game/NPR/Characters/Wanderer/Meshes/SKM_Wanderer`。
- 流浪者材质实例：`/Game/NPR/Characters/Wanderer/Materials/AnimeFlat`。
- 原始 PMX 与全部源文件的项目内存档：`SourceAssets/Wanderer/Original`；可复查 `.blend` 和 FBX：`SourceAssets/Wanderer/Converted`。

## 导入与验证

使用本机 Blender 5.2 与已有 MMD Tools 4.5.14 转换，没有安装新工具。源网格 27,650 顶点、32,747 三角形；UE 导入后 28,988 渲染顶点，UV/法线/材质边界会拆分顶点。UE 实际读取到 427 根骨骼、26 个材质槽、65 个顶点 Morph Target 和七张 PNG 贴图；高度约 166.30 厘米。头部骨骼 `頭` 保留映射为 `B_132`，新增单根 `PNPR_Root`，骨骼原名与导出名对照保存在转换记录中。

已核对网格、骨骼、材质槽与 Morph 数量，检查贴图参数读回，保存 26 个默认材质槽绑定；双角色关卡保存后重新加载，七个 Actor 与全部组件材质绑定读回通过。实际 Lit 视口中两位角色正面朝向、贴图布局与中性姿态可见。源目录 14 个文件再次 SHA-256 检查，原始文件未变，项目存档与原始一致。

## 风格资产当前状态

为两个语义风格各生成一条颜色曲线、一份原生 ToonProfile 和一个 Substrate Toon 父材质，另有陈千语八个、流浪者 26 个材质实例。语义 StyleProfile 草案、角色默认对应 JSON 与 UE ToonProfile 分别保存。

初始漫反射曲线采用有符号 N·L 过渡：AnimeFlat 半宽 0.02、SemiPBRStylized 半宽 0.20；暗部乘数分别 0.28 / 0.18，是本次手工选择的未校准起点。颜色曲线通过 UE 原生 CSVImportFactory 导入 257 个线性采样点并读回检查，原生 ToonProfile LUT 再采样会产生近似误差。DiffuseRampIncludeShadow=false：原生动态阴影可见性仍由后端正常应用，尚未验证纳入 Ramp 的方案。高光采用后端默认响应，Roughness=0.65、Specular=0.35 仅用于显示。

**这不是两款游戏画面的完成复刻。** Face Critical Azimuth Field、Hair 角域各向异性高光、宏观法线抽象、Rim、Outline 与角域高光参数映射仍未接入；运行时风格切换和正式光照能力验收尚未执行。图中帽檐及其他几何自阴影也尚未进行面部艺术控制。

## 格式迁移边界

65 个顶点表情已导入；MMD 的骨骼/材质/UV/组 Morph 逻辑、物理刚体约束、IK 控制器和动画没有移植。UE 线性蒙皮不等价于 PMX SDEF/QDEF；尚未进行动画变形验收。MMD Toon / Sphere Map 响应由原生 S58 材质替代，源 `髮+` 加法高光覆盖层槽 10 在此通用探针中隐藏，槽 25 保留源 alpha=0；源几何仍在网格内。没有伪造 Face 场或把彩色头发贴图认作 Flow 数据。

陈千语源 `/Game/EndfieldRender` 资源保留，显示使用组件材质覆写。流浪者默认材质只写入本项目新导入且具有所有权标记的网格。第三方角色文件只用于本地研究，不放入插件分发内容。

## 下一阶段明确输入

1. 为两位角色分别验证动画头部坐标系、UV 镜像规则与 Face 场数值；流浪者需单独制作 Critical Azimuth Field。
2. 验证 Hair 切线与 Flow，比较原生各向异性响应和参考角域模型。
3. 用固定曝光、同一光照和同一角色的跨风格 A/B 校准两个语义端点，避免角色贴图差异冒充风格差异。
4. 将语义 Resolver / S58 Adapter / UDataAsset 接入后再做运行时风格切换；正式三十项探针仍以独立测试关卡记录，不能用这张截图代替。

## 可复现脚本

项目 `Tools/` 内保存 `convert_wanderer_to_fbx.py`、`import_wanderer_into_ue.py`、`create_two_style_character_probe.py` 和 `finalize_two_character_import.py`。后三者通过 UE 编辑器 Python 执行。导入与创建脚本核对当前项目和资产所有权；不覆写未知同名资源。最终保存脚本用于初次设置显示镜头与天空光，手动重跑会恢复该初始展示参数。

工具参考：[MMD Tools 官方仓库](https://github.com/MMD-Blender/blender_mmd_tools)。实际 UE 曲线接口依据本机 `CSVImportFactory.h/.cpp` 与 `CurveBase.cpp`，Toon 后端资产依据本机 `ToonProfile.h`。

![双角色实际 UE 视口](../Images/TwoStyleCharacterProbe.png)


最终 GitHub 备份前已显式保存所有项目脏资源；源外部目录未修改。迁入项目内的 UE 包可能由 UE 5.8 重存，最初复制校验值属于迁移时点证据。详见 `../GitHubBackup.ProjectSave.json`。
