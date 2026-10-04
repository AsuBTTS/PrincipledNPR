# UE 5.8.3 Toon BSDF 本机源码审计

本次审计把专项报告中的部分 API 推测升级为**目标版本源码确认**。审计只读引擎文件，不修改引擎、不更改已有项目。它不是渲染验收：Editor 启动、材质编译、VSM/CSM、Lumen、Hair 方向响应及 Face 桥接画面仍为 **NotRun**。

目标安装目录：`UE_ENGINE_ROOT`。核查日期：2026-10-04。

| 项目 | 本机读到的值 | 证据 |
|---|---|---|
| 引擎版本 | 5.8.3 | `Engine/Build/Build.version` |
| Changelist | 58210709 | 同上 |
| CompatibleChangelist | 55116800 | 同上 |
| 分支 | `++UE5+Release-5.8` | 同上 |
| 本次验证层级 | SourceVerified | 已读取目标安装树的头文件、编译路径与 Shader 正文 |
| 渲染验证层级 | NotRun | 不由源码读取推定 GPU 画面通过 |

## 1. 创建项目需要显式选择 Blendable

真实枚举为 `ESubstrateStorageFormat::Type`，其 `BlendableGBuffer=0`、`AdaptiveBuffer=1`。EngineTypes.h (`Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h:781`)

新项目生成器会写入 `r.Substrate=True`、`r.Substrate.ProjectGBufferFormat=0`。GameProjectUtils.cpp (`Engine/Source/Editor/GameProjectGeneration/Private/GameProjectUtils.cpp:199`) 底层 CVar 默认则分别为0、1，用来保留旧项目行为；直接手建工程时不能依赖裸默认。RenderUtils.cpp (`Engine/Source/Runtime/RenderCore/Private/RenderUtils.cpp:2068`)

最小 Desktop Deferred、软件 Lumen、VSM 探针建议显式写入：

```ini
[/Script/Engine.RendererSettings]
r.Substrate=True
r.Substrate.ProjectGBufferFormat=0
r.ForwardShading=False
r.DynamicGlobalIlluminationMethod=1
r.ReflectionMethod=1
r.GenerateMeshDistanceFields=True
r.Shadow.Virtual.Enable=1
r.Substrate.Experimental.ToonUnifiedDiffuse=0
r.Substrate.Experimental.ToonReflectionQuantizationEnabled=0
```

GI=1、Reflection=1确为Lumen；ShadowMapMethod=1为VSM。枚举证据 (`Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h:452`) `GenerateMeshDistanceFields`为软件Lumen所需；Forward会回退普通ShadowMaps，因此基线选择Deferred。RendererSettings.h (`Engine/Source/Runtime/Engine/Classes/Engine/RendererSettings.h:664`) 反射量化与统一漫反射两个实验模式先保持0，再单独探针；不能从Profile的IncludeShadow开关推断必须打开统一漫反射。

## 2. 实际节点和资产

`UMaterialExpressionSubstrateToonBSDF`、`FToonProfileStruct`、`UToonProfile`、`UToonProfileFactory`均已落盘。节点提供BaseColor、Metallic、Specular、Roughness、Normal、EmissiveColor、PatternUVs、Anisotropy、Tangent；ToonProfile为资产属性，不是FaceMask、NoLOverride或VisibilityOverride输入。节点头文件 (`Engine/Source/Runtime/Engine/Public/Materials/MaterialExpressionSubstrate.h:860`)

Profile头文件明确写着per-material、不要在运行时修改。Profile ID上限256，默认条目占用其中一个槽位；资源去重与数量预算仍由适配器管理。ToonProfile.h (`Engine/Source/Runtime/Engine/Classes/Engine/ToonProfile.h:14`)

Factory存在不等于本次已成功通过Python创建资产。`unreal.ToonProfileFactory`、`unreal.ToonProfile`、`unreal.MaterialExpressionSubstrateToonBSDF`预期由反射导出，实际Editor模块可用性仍需启动后确认。Factory头文件 (`Engine/Source/Editor/UnrealEd/Classes/Factories/ToonProfileFactory.h:10`)

## 3. Signed Diffuse：坐标已经可以确定

非统一漫反射基线中的实际坐标是：

\[
q=Context.NoL,\qquad x_d=(q+1)/2.
\]

两种BxDF `Init`都令`Context.NoL=dot(N,L)`，没有把它saturate；截断的是NoH、VoH。BRDF.ush (`Engine/Shaders/Private/BRDF.ush:24`) Substrate上下文只另存`SatNoL`，未写回截断后的`Context.NoL`。SubstrateEvaluation.ush (`Engine/Shaders/Private/Substrate/SubstrateEvaluation.ush:441`) Blendable Toon调用也未在调用前截断NoL。ShadingModels.ush (`Engine/Shaders/Private/ShadingModels.ush:1067`) 最终common函数按上述式子remap。SubstrateToonBSDF.ush (`Engine/Shaders/Private/Substrate/SubstrateToonBSDF.ush:139`)

因此，对于零角半径方向光、无offset且不把shadow放入坐标的隔离测试，reference的分界`τ`和dot空间半宽`w`可直接映射为：

\[
\tau_{profile}=(\tau+1)/2,\qquad w_{profile}=w/2,
\quad R_d(x)=B(2x-1;\tau,w).
\]

这是**坐标层精确映射**，后端Curve/LUT仍有有限采样误差；光强、阴影、GI、配色与曝光也尚未与reference统一，不能将其升级成全外观Exact。非零SourceAngle/球面光的`SphereMaxNoH`还会改变Context.NoL，应另行标定。BRDF.ush的修改位置 (`Engine/Shaders/Private/BRDF.ush:112`)

原报告“若负q被折叠则不可恢复”作为理论反例保留，但**本机该调用路径没有在remap前折叠负q**。负阈值最终画面是否符合预期仍须渲染测试，不能由函数局部审计保证所有renderer/platform路径。

## 4. IncludeShadow：明确顺序，但不是独立最终可见度门

令`v_t`表示`ShadowTerms.TransmittanceOrOpticalThickness`提供的透射值，经可选hatching处理后的`TransmittanceShadow`；`δ_d`为offset；`A`为灯光falloff与color。实际代码给出：

\[
C_d^{off}=A\,v_t\,\frac{C_{diff}}{\pi}\,
R_d\!\left(\operatorname{saturate}(x_d+\delta_d)\right),
\]

\[
C_d^{on}=A\,\frac{C_{diff}}{\pi}\,
R_d\!\left(\operatorname{saturate}(x_dv_t+\delta_d)\right).
\]

源码先计算transmittance/hatching，再在IncludeShadow开启时乘入remap坐标，随后加diffuse offset、采样Ramp；开启时没有在Ramp后再乘`v_t`。SubstrateToonBSDF.ush (`Engine/Shaders/Private/Substrate/SubstrateToonBSDF.ush:79`) DiffuseRamp saturate位置 (`Engine/Shaders/Private/ToonProfileCommon.ush:28`)

这确认原生阴影相关值参与Toon Ramp，而不是表面材质中手算独立N·L；但不能把transmittance改称不带介质策略的binary visibility。`R_d(0)>0`时，IncludeShadow开启允许遮挡区保留阴影Ramp颜色；正offset也可能把完全遮挡样本从零点抬亮。**Face桥接若要求真实遮挡最终归零，应优先测试IncludeShadow关闭的组合，并单独验证透射/实际阴影门。**

`IncludeShadow`直接依据Profile分支，并不要求`r.Substrate.Experimental.ToonUnifiedDiffuse=1`。后者是另一个逐灯数据汇总、再执行Ramp的实验策略；默认0，ReadOnly。CVar (`Engine/Source/Runtime/RenderCore/Private/RenderUtils.cpp:2213`) 逐灯开关宏 (`Engine/Shaders/Private/DeferredLightPixelShaders.usf:31`) 汇总pass (`Engine/Source/Runtime/Renderer/Private/LightRendering.cpp:2506`)

VSM/CSM过滤质量、真实Cast/Receive/Self Shadow、此前修复是否覆盖本机路径，仍全部为NotRun。函数有ShadowTerms入口不能替代这些结果。

## 5. PatternUV：12bit、frac与Wrap改变Face桥接设计

`PackToonCustomData`把U/V各saturate后量化为12bit，并加8bit Profile ID。U/V步进为`1/4095`，当前float→uint行为还会带来量化误差。打包与解包 (`Engine/Shaders/Private/Substrate/SubstrateToonBSDF.ush:16`)

Diffuse offset的实际求值为：

\[
p=\operatorname{frac}(UV_{12}\,Size),\qquad
\delta_d=Strength\,[2T_{atlas}(p)_R-1].
\]

ToonProfileCommon.ush (`Engine/Shaders/Private/ToonProfileCommon.ush:89`) 原始offset纹理烘焙使用固定Wrap sampler；atlas tile带一圈边界，实际图案烘焙分辨率为`N−2`。CPU采样设置 (`Engine/Source/Runtime/Engine/Private/Rendering/ToonProfile.cpp:479`) Atlas烘焙 (`Engine/Shaders/Private/ToonProfile.usf:109`) 默认Profile分辨率为64，最低64，原报告常规自制LUT256不代表native Profile自动也是256。分辨率CVar (`Engine/Source/Runtime/Engine/Private/Rendering/ToonProfile.cpp:26`)

由此得到确定反例：`Size=1`、`u=1`时`frac(1)=0`。将原纹理设成Clamp不能取消shader的frac，也不能替换引擎固定的Wrap sampler。因此原报告“线性、Clamp的identity纹理即可生成全范围[-k,k]offset”的端点假设不成立。

替代桥接应定义一个可标定的内部单调区间`[a,b]⊂(0,1)`，让atlas采样远离接缝。若测得该区间输出单调，可离线求实际采样映射的逆函数：

\[
u=h_{atlas}^{-1}\!\left((\Delta/k+1)/2\right),
\quad h_{atlas}(u)=T_{atlas}(\operatorname{frac}(u))_R.
\]

只允许`Δ`落在实测覆盖域；超范围明确报限幅或Unsupported，不能假称完整[-k,k]。`a/b`要兼顾纹理texel中心、N−2 atlas边界及12bit UV量化，单纯写一个固定epsilon并不证明正确。关闭Specular offset与hatching可隔离共享PatternUV的干扰；运行时不修改Profile Settings。

完整逐灯Face模型仍不被这条桥接自动支持：PatternUV在表面阶段生成，所有灯共用同一值，主光计算的offset不能变成每盏灯独立的Critical Azimuth求值。桥接保持Approx候选。

## 6. Specular实际输入与Hair编译接口缺口

非Rect灯的SpecularRamp输入为：

\[
x_s=\operatorname{saturate}\!\left[D_{GGX/aniso}(H)\,
K(r,a,N\cdot V)+\delta_s\right].
\]

其中`K=GGXMaxEnergyLookup`依赖roughness、abs(anisotropy)与NoV；这条直接高光路径把Vis和Fresnel乘子注释掉。Ramp结果再乘SpecularColor，积分外乘SurfaceShadow、AreaLight.NoL、falloff与color。Rect采用LTC近似，应单独校准。Specular代码 (`Engine/Shaders/Private/Substrate/SubstrateToonBSDF.ush:98`) K查表 (`Engine/Shaders/Private/ShadingEnergyConservation.ush:99`)

所以“对完整GGX BRDF做一维Ramp”不是这条路径的准确描述；固定NoV下可以进一步反演角宽，但K的视角依赖、saturate、Rect分支与外乘，使全视角独立halfangle/softness/intensity仍需Approx校准。

Hair最直接的源码事实是：legacy Compile的共享basis只注册Normal，虽然稍后仍编译Tangent并作为单独参数传入。MaterialExpressionSubstrate.cpp (`Engine/Source/Runtime/Engine/Private/Materials/MaterialExpressionSubstrate.cpp:2489`) MIR Build明确用`nullptr`作为basis切线参数，并构造了**没有Tangent、Anisotropy输入**的ToonDesc。MaterialExpressionsToMIR.cpp (`Engine/Source/Runtime/Engine/Private/Materials/MaterialExpressionsToMIR.cpp:6847`)

这是该版本编译入口不完整的强证据，不能因为节点可接Tangent就认定Hair方向已支持。仍须记录实际使用的编译器，做0°/45°/90°Flow与anisotropy=0/±0.8对照，再作生产Gate。当前不把“未传入”夸大成已经完成画面失败复现，也不承诺通过仅改材质图解决独立shift。

## 7. 当前结论与后续探针

| 项目 | SourceVerified结论 | RenderTest状态 |
|---|---|---|
| Blendable设置 | Format=0，Substrate=True | NotRun |
| signed NoL坐标 | 已确认remap前NoL未截断；零角方向光可直接构造t/w坐标 | NotRun |
| IncludeShadow顺序 | transmittance先乘坐标，再offset/Ramp；关闭时外乘transmittance | NotRun |
| PatternUV端点 | u=1经frac回0；12bit量化；Wrap atlas采样 | NotRun |
| Face identity Clamp | 原端点假设被源码反例否定，应改内部区间逆映射 | NotRun |
| Hair切线/anisotropy | MIR输入缺失；legacy basis仅Normal | NotRun |
| Cast/Receive/Self、VSM/CSM | 本审计不证明阴影质量或缺陷修复 | NotRun |
| Lumen与indirect specular | 本审计不证明目标场景输出 | NotRun |

安装与工程创建完成后，先编译并保存最小Generic材质，随后按零角方向光signed-domain、IncludeShadow×VSM/CSM×extinction、PatternUV端点、Hair方向、Lumen和冷/热DDC顺序执行。主报告的数学reference不删除；仅升级已经由源码确认的后端映射事实。
