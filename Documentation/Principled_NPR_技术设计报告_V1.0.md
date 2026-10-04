# Principled NPR Material System
## 理论模型、UE 5.8 Toon 后端专项修订与 V1.0 实施设计

**版本：研究设计稿 1.0-S58专项修订｜日期：2026-10-04｜初版检索：2026-10-03，专项复核：2026-10-04｜目标：UE 5.8.x Toon Production Candidate，固定具体 patch/CL；5.7/A0仅保留参考与回归｜桌面Deferred、Substrate Blendable GBuffer、单个受控角色**

**阅读约定。** 本文优先完成理论推导，再约束工程实现。标记「事实」的内容有外部依据；「设计」表示本项目建议；「待验证」表示需要目标版本实验。本文没有运行 UE 项目，没有把文档调查当作 GPU 实测。所有性能数字、质量阈值和开发工时均为初始验收预算，不是已取得的结果。文中 HLSL 是参考伪代码，尚未通过 UE 材质编译器。独立ProjectCreation后续状态见[项目创建记录](项目创建记录.md)。

## 1. 执行摘要

本项目是一套**保留表面数据、统一艺术参数语义、按材质特性解释风格，并明确声明渲染能力边界**的角色材质框架。

核心理论成立，但不宜把整个系统严格称为数学上的 reparameterization。严格重新参数化通常不增加原模型的自由度；Face 控光、描边、MatCap 和自由配色显然扩大了模型空间。更准确的表述是：**PBR 数据语义复用 + 部分响应重新参数化 + 艺术自由度扩展 + 外观合成**。它不保证物理可实现，也不需要为了使用「Principled」而假装所有参数都来自 BRDF。

推荐 V1：**S58 Substrate Toon BSDF 为首选生产候选，A0 Unlit 保留为数学校验后端**。语义系统通过 `NPRBackendSubstrateToon` 生成后端Profile/MI与映射质量报告。优先复用原生直接/多灯/阴影/Sky/Lumen及间接镜面路径，不默认为全角色重造受控灯光。官方5.8发布说明明确支持这些主要灯光类型与GI；具体场景、shader坐标、参数等价性仍须验收。[Epic 5.8 发布说明](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes?lang=pt-BR)

Generic先由S58落地；Hair采用标定后的native各向异性候选，但需检查Tangent修复及独立shift边界；Face保留第5节完整模型，单独验证PatternUV/offset桥接或专用后端。**Face严格逐灯分界替换和Hair完整角域瓣尚无已确认的原生等价入口**，不能由Generic成功推断全V1成功。A+/C只处理冻结合同中确实缺失的局部能力。

**生产候选并非已通过生产验收。** 本机已读取UE5.8.3、CL58210709安装树的节点/Profile、编译入口与Toon Shader正文；signed diffuse坐标、IncludeShadow顺序、PatternUV编码与Hair编译缺口已有SourceVerified证据。安装完整性、Editor启动及工程创建以独立ProjectCreation记录为准；截至本次源码审计Editor未运行，渲染实测仍为NotRun。第10.8节与[本机源码审计](UE5.8.3_本机源码审计.md)区分源代码事实和画面验收。第4–6节数学reference与参数契约完整保留。

描边仍用独立反向外壳作为V1基线。最大风险转为后端语义映射、Face专用入口、目标CL的Tangent/阴影/DDC问题，以及未经实验的感知契约；风险优先级不再由A0缺失整个原生光照决定。

## 2. 问题定义与研究边界

PBR 参数描述反射率、微表面统计和几何方向，TA 则经常希望直接描述「脸上哪块亮」「高光多宽」「边缘留几像素」。这两类问题并不一一对应。例如粗糙度同时改变高光峰值、角宽、尾部和环境反射模糊；把它改名为高光尺寸不能消除这些耦合。

大量 Toon 系统已经支持色阶、MatCap、边缘光、描边和预设。本项目的价值不能建立在「别人没有这些功能」上，而应建立在参数契约、资产契约、材质与风格分离、可复用初值生成以及可测量的工作流改进上。

V1 的角色是一个受控测试对象，不是任意角色自动适配器；目标平台是桌面 SM6、透视相机、Opaque/Masked 角色表面。移动、VR、多视图、透明发片排序、Nanite 骨骼路径和 Path Tracer 均不在默认支持声明中。版本升级应逐项重新运行能力矩阵。

## 3. 数学基础：从单条链路改成有上下文的有向图

### 3.1 必须补充的输入

仅有 \(F_M(X_{PBR},\Theta,S)\) 无法确定颜色：没有视角、灯光、可见性和曝光，同一材质也没有唯一最终颜色。定义：

\[
X(p)=\{c_b,r,m,a_o,e,n_t,n_v,t,h,\text{masks}\},\quad
\Gamma=\{p,N_g,V,UV,TBN,\text{head frame}\}
\]

\[
\mathcal L=\{(L_i,E_i,v_i)\}_{i=1}^{n}\cup\{L_{ind},L_{env}\},\qquad
\mathcal K=\text{renderer capability contract}.
\]

\(c_b\) 是线性颜色；\(r,m,a_o\in[0,1]\)；\(L_i,V\) 始终从表面指向光源/相机；\(v_i\in[0,1]\) 为可见度，1 表示无遮挡。灯 Actor 的前向射线通常相反，适配器必须完成符号转换。\(E_i\) 表示到达表面的直射光尺度，已含距离/聚光衰减；普通材质驱动模式中只是归一化艺术强度，不能直接把 lux 数字当 RGB 相乘。

最终系统写成：

\[
\theta^0=G_{S,M}(\mathcal D(X),A),\quad
\theta=\Pi_{\mathcal C_{S,M,\mathcal K}}
\bigl(\operatorname{Resolve}(\theta^0,O)\bigr),
\]
\[
B=F_M(X,\Gamma,\mathcal L;\theta,\mathcal F_S,\mathcal K),
\]
\[
C_{scene}=\mathcal A_S(B,\text{rim},\text{matcap},e),\quad
C_{display}=\mathcal T\bigl(\mathcal O_S(\mathcal V(C_{scene}))\bigr).
\]

\(\mathcal D\) 解码纹理/统计特征，\(A\) 是资产元数据，\(O\) 是显式覆写，\(\Pi\) 验证约束，\(\mathcal V\) 为雾等场景合成，\(\mathcal O\) 为选定位置的描边/后处理，\(\mathcal T\) 为曝光及显示映射。实际 \(\mathcal O\) 的前后顺序属于渲染策略，不存在普遍正确的固定排序。

**原始表面数据必须旁路进入 \(F_M\)**。如果只保留压缩后的 \(\theta\)，就会丢失颜色纹理、金属度和方向场。\(G\) 也要区分两类：编辑器根据资产元数据生成均匀初值；像素着色器按 roughness/mask 计算空间变化。两者不能混成「Data Asset 每像素求值」。

PBR 的基准是渲染方程
\[
L_o=L_e+\int_\Omega f_r(L,V)L_i(L)\,v(L)\,(N_g\cdot L)_+\,d\omega.
\]
若模型依赖 UV 艺术遮罩、屏幕深度或固定相机 MatCap，它可以是有效的外观模型，但未必是满足互易性、能量约束的 BRDF。本文所有新增 NPR 公式均为本项目设计推导；经典来源用于解释物理基线，不把新组合冒充已有论文结论。Disney 的实用原则启发了这种可控性目标，但并不替本项目证明可用性。[Disney 2012](https://disneyanimation.com/publications/physically-based-shading-at-disney/)

### 3.2 参数分类与原十维重构

| 原参数 | 决策与准确语义 | 所属层 | 来源/耦合 |
|---|---|---|---|
| shadowThreshold | 保留 UI 别名，内部为 terminatorPosition，规定输入域 | 局部响应 | 新艺术自由度；与 wrap、normal abstraction 耦合 |
| shadowSoftness | 保留，内部为响应坐标过渡半宽 | 局部响应 | 不等同于 shadow map 半影宽度 |
| specularSize | 保留，内部为半向量空间 50% 角半径 | 局部响应 | roughness 可初始化，不能强绑定 |
| specularSoftness | 保留，角边界过渡宽度 | 局部响应 | 与积分能量和像素过滤仍有耦合 |
| rimWidth | 保留，掠射角带宽 | 外观合成 | 不是屏幕像素宽度 |
| rimIntensity | 保留，线性场景颜色倍率 | 外观合成 | 会受曝光、Fresnel、光向遮罩影响 |
| normalFlatten | 保留兼容别名，重定义为 macroNormalBlend | 几何响应 | 需要明确的资产抽象法线场 |
| outlineWidth | 移出 Core，单位为输出分辨率像素 | 几何/屏幕 | 相机、深度、壳体方向耦合 |
| matcapWeight | 移出 Core，为指定混合算子的权重 | 外观合成 | 加法与 lerp 完全不同，必须记录 |
| saturation | 移出 Core，为指定颜色空间的色度缩放 | 颜色合成 | 亮度保持依赖空间，不能保证感知线性 |

补充独立的 normalDetailStrength、specularIntensity、shadowTint、rimSoftness、faceAngularBias、hairShift 与 hairAnisotropy；不为凑足「十维」删除必要变量。Principled 是**有定义、有范围、有边界、有测量方法的局部参数契约**，不是每个滑条都绝对正交。

**S58修订的阅读边界：**下面第4–6节原样保留数学reference及其受控单主光验证语境；其中“V1/单主光推荐”不再表示整个生产后端的灯光限制。产品后端优先级和原生多灯/GI策略以第10.8、14、19节为准。参考模型可分别验证raw瓣/遮罩；最终native响应由Adapter声明Exact/Approximate/Unsupported。

## 4. NPR Core 模型

### 4.1 公共阶跃与抗锯齿

定义 \(H(z)=u^2(3-2u),u=\operatorname{clamp}(z,0,1)\)，及
\[
B(x;t,w)=H\!\left(\frac{x-t+w}{2w}\right),\quad w>0.
\]
因此 \(B(t)=1/2\)，完整过渡区为 \([t-w,t+w]\)。所有 softness 都要声明是半宽还是全宽，本文使用半宽。像素实现取
\[
w_{eff}=\max(w_{art},\tfrac12\kappa\operatorname{fwidth}(x),\epsilon),\quad \kappa=1\text{ 为初值}.
\]
零 softness 表示零艺术模糊，而不是关闭抗锯齿。有限像素滤波只能近似带限，不能保证 TSR 中无闪烁；AA 会使屏幕显示宽度超过资产值。导数只在像素阶段并在一致控制流中求值，不放在逐像素分歧分支后。[GPU Gems：程序纹理过滤](https://developer.nvidia.com/gpugems/gpugems/part-iv-image-processing/chapter-25-fast-filter-width-estimates-texture-maps)

```hlsl
float Band(float x, float center, float artHalfWidth) {
    float w = max(max(artHalfWidth, 0.5 * fwidth(x)), 1e-5);
    float u = saturate((x - center + w) / (2.0 * w));
    return u*u*(3.0 - 2.0*u);
}
```

### 4.2 Diffuse：保留方向语义，分离明暗分界与遮挡

令 \(q=N_d\cdot L\in[-1,1]\)，则
\[
D=B(q;\tau_d,w_d).
\]
UI 的 shadowThreshold \(t_d\in[0,1]\) 对应 \(\tau_d=2t_d-1\)。shadowSoftness \(s_d\in[0,1]\) 初始映射为 \(w_d=0.5s_d\)；这只是域映射，不是已验证的感知均匀标尺。也可直接显示 \(\tau_d\) 和半宽，避免隐藏转换。

把 \(q\) 仿射变成 \(u=(q+1)/2\) 完全合法，但必须同步变换阈值和宽度：\(t=(\tau+1)/2,w_u=w_d/2\)。否则两种实现不是同一个模型。不要先 saturate(q) 再想用负阈值控制背光侧，因为负域信息已丢失。

Half-Lambert 的仿射版本只是换坐标；带平方的版本 \(((q+1)/2)^2\) 还改变梯度和能量。Wrap \(u=\operatorname{saturate}((q+w)/(1+w))\) 与阈值在未截断区域满足
\[
u>t\iff q>(1+w)t-w.
\]
**同一边界可以由无穷多组 wrap/threshold 产生**。这就是不把 wrap 再作为 V1 独立核心滑条的原因。它可作为风格内部固定坐标变换，但要在参数提示中显示有效分界位置。Wrap 不是 SSS，也不能穿透真实可见度遮挡。

多阶统一为
\[
Q(q)=c_0+\sum_{j=1}^{K-1}(c_j-c_{j-1})B(q;\tau_j,w_j).
\]
\(K=2\) 即 two-band；\(\tau_1<\cdots<\tau_{K-1}\)。若希望无颜色超调、各色有平台，要求相邻过渡区不重叠：\(\tau_j+w_j\le\tau_{j+1}-w_{j+1}\)。任意 RGB 色序无需逐通道单调，但「越来越亮」应额外要求亮度单调。交叠情况下不能假定存在完整中间色带。

解析曲线和 Ramp 都实现同一个接口 \(Q:[-1,1]\to\mathbb R^3\)，另带输入域、色彩空间、边界模式、采样过滤、版本和单调性声明。曲线在编辑器烘焙为 LUT；建议 256×1 起步、Clamp、线性数据、禁止 sRGB 解码。硬阶跃用普通 mip 平均会改变色带，故需要距离/LOD 验证，不能只凭近景检查。

**颜色组合推荐采用单主光调色板模型**：
\[
C_d^{palette}=A_{ind}\odot[(1-d)P_0+dP_1],\qquad d=D\,v_{key},
\]
其中 \(P_0,P_1\) 是材质派生暗/亮色；\(A_{ind}\) 是有明确命名的艺术曝光/填充尺度，不冒充真实间接光。若希望保留光强响应，使用
\[
C_d^{add}=C_{fill}\odot A_{AO}+E_{key}\odot v_{key}\,D\,C_{diff}.
\]
第二式将阴影下颜色交给填充项，直射光被遮挡后归零，更适合 Semi-PBR；第一式便于动画平涂但明确是艺术模型。不能同时把 \(P_0\) 当环境填充又在每盏灯中重复累加。

物理基线为 \(C_d^{phys}=\sum_i E_i\odot v_i\,(1-m)c_b\,(N_g\cdot L_i)_+/\pi+C_{ind}^{phys}\)。若混合艺术与物理支路，使用**完整、单位匹配的响应插值**，不要在 native Lit 上再叠加完整手算 diffuse。V1 Unlit 的 Semi-PBR 是受控光源下的手算响应，名称不代表自动得到 UE 的 GI。

多光源有三种不等价策略：逐灯量化再相加、先聚合照度再量化、主光决定色阶而辅光连续补偿。一般 \(Q(a+b)\ne Q(a)+Q(b)\)。V1 选第三种的最小子集——一个明确指定的主光，常量/半球填充；不从「最亮灯」每帧硬切主光。将来自动选择必须有权重过渡与身份滞后，否则脸会跳边。

直接光可见度只乘直接贡献；AO 通常影响间接项或艺术接触暗部，不应默认把全部直接光、高光、Rim 一起乘黑。原式中的 shadow 实际表示 terminator，不是 shadow map 的 occlusion；两个概念应在 UI Tooltip 中明说。

阈值位于域端点且宽度非零时，过渡支持会超出 [-1,1]，因此端点未必出现完整亮/暗平台。这是可定义的边界行为，不应把两端 smoothstep 边界各自 clamp 后制造不对称软化。V1 保留中心与宽度原义，显示有效覆盖区；需要「全部亮/全部暗」用显式响应策略，不将其暗藏在 threshold=0/1。

```hlsl
float3 DiffuseAdditive(float3 Nd, float3 L, float center, float width,
                      float3 Ekey, float visibility, float3 diffuseColor,
                      float3 fillColor, float assetAO) {
    float D=Band(dot(Nd,L),center,width);
    return fillColor*saturate(assetAO)
         + Ekey*saturate(visibility)*D*diffuseColor;
}
// Palette 策略独立调用：lerp(P0,P1,D*visibility)*artScale。
// artScale 对主光强度采用 Ignore/ScaleAll 的显式策略；测试按该策略判定。
```

### 4.3 Normal Flatten：拆分微观细节与宏观抽象

明确四种法线：几何面法线 \(N_g\)、插值顶点法线 \(N_v\)、切线空间贴图法线 \(n_t\)、供模型使用的最终着色法线 \(N_d,N_s\)。它们不可以不变换空间就直接 lerp。

贴图细节强度采用斜率缩放：
\[
n_t(k)=\operatorname{normalize}(k n_{t,x},k n_{t,y},\max(n_{t,z},\epsilon)),\quad k\in[0,1].
\]
\(k=0\) 回到 \((0,0,1)\)，\(k=1\) 还原合法正半球贴图。它不等同于线性减小法线角度；超强细节 \(k>1\) 留到专家模式。两张细节法线组合时用 RNM 或经过验证的斜率方法，RNM 的任务是重定向细节，不是自动「扁平化」。[RNM 原始说明](https://blog.selfshadow.com/publications/blending-in-detail/index.html)

宏观抽象依赖资产提供的 \(N_a\)：可以是低模代理烘焙、椭球/面部引导场，不能默认为世界上方向、相机方向或全脸一个常量。推荐
\[
N_{macro}=\operatorname{slerp}(N_v,N_a,f),\quad f=normalFlatten\in[0,1].
\]
如果需要稳定近似且两向量夹角小于 90°，normalized lerp 可用，但它不是角度匀速。\(N_a=-N_v,f=0.5\) 会使原公式产生零向量，这是直接反例。V1 资产验证要求 \(N_v\cdot N_a>0\)；不支持的资产禁止强行启用，并给出诊断。slerp 在近同向时退化为 nlerp；反向情况不由任意 epsilon「修好」。

将原 TBN 用从 \(N_v\) 到 \(N_{macro}\) 的最短弧旋转整体重定向，再用 \(n_t(k)\) 生成着色法线。这能保持细节方向，不应只替换 N 而保留已不正交的 T/B。Diffuse 和 Specular 可使用不同细节保留率，但宏观坐标必须一致；V1 高级设置固定一个策略，避免额外滑条淹没用户。

因此 normalFlatten 的正式含义是「向资产指定的宏观引导法线旋转的比例」，它不改变网格轮廓，也不保证像屏幕平涂滤镜。没有引导法线时显示 Unsupported，并令 \(f=0\)；绝不偷偷改成 normalDetailStrength。

```hlsl
// 所有方向均为 world space，Na 由资产解码并正确蒙皮/变换。
float c = clamp(dot(Nv, Na), -1.0, 1.0);
// 导入验证保证 c > 0；非法资产在 CPU 侧禁用该功能。
float angle = acos(c);
float3 Nm = (angle < 1e-4) ? Nv :
    normalize((sin((1-f)*angle)*Nv + sin(f*angle)*Na) / sin(angle));
float3 nt = normalize(float3(detailStrength * normalTS.xy,
                             max(normalTS.z, 1e-4)));
// RotateShortestArc 必须同时旋转 T/B/N，并处理接近同向。
float3 Tm = RotateShortestArc(T, Nv, Nm);
float3 Bm = RotateShortestArc(B, Nv, Nm);
float3 Nshade = normalize(nt.x*Tm + nt.y*Bm + nt.z*Nm);
```

### 4.4 Specular：以角半径取代任意 threshold

Blinn–Phong 的形状为 \((N\cdot H)^p\)，半峰角满足 \(\cos^p\beta=1/2\)，即
\[
p=\frac{\ln(1/2)}{\ln(\cos\beta)}.
\]
这已经说明 exponent 与可见尺寸高度非线性。GGX 的物理基线为
\[
f_s=\frac{D_\alpha(H)F(V\cdot H)G(N,L,V)}{4(N\cdot L)_+(N\cdot V)_+},
\]
\[
D_\alpha=\frac{\alpha^2}{\pi[1+(\alpha^2-1)(N\cdot H)^2]^2},\qquad \alpha=r^2\text{（本项目采用 UE 习惯）}.
\]
GGX 有较长尾部；roughness 到 \(\alpha\) 的映射并非跨渲染器统一标准。UE 的平方映射有公开技术来源；不要把别的库同名 roughness 直接当作相同含义。[Epic：Real Shading in UE4](https://cdn2.unrealengine.com/Resources/files/2013SiggraphPresentationsNotes-26915738.pdf)、[PBRT：微表面模型](https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory)

**候选一，V1 推荐：角度控制的艺术高光瓣。** 令
\[
H=\frac{L+V}{\|L+V\|},\quad\theta_h=\arccos(\operatorname{clamp}(N_s\cdot H,0,1)),
\]
\[
\beta=\beta_{min}+(\beta_{max}-\beta_{min})s,\quad
\delta=softness\,\min(\beta,\pi/2-\beta)\,0.95,
\]
\[
S_a=1-B(\theta_h;\beta,\delta),\quad
C_s=E_{key}\odot v_{key}\,I_s\,F_{style}\,S_a\,g_Lg_V.
\]
建议 \(\beta_{min}=1^\circ,\beta_{max}=60^\circ\)；UI 同时显示实际度数。size=0 对应最小可用角宽，关闭高光使用强度 0/显式开关；不要让 size=0 一面表示零宽一面又有 AA 高光。\(g_L,g_V\) 是几何法线的半球门控，接近切线方向做约一像素过滤。\(L+V\) 长度不足时返回零高光，不能 normalize(0)。

角域模型保证 \(\theta_h=\beta\) 时形状权重正好为 0.5，softness 改变过渡坡度、intensity 只乘响应。**它保证的是参考瓣的几何语义，而不是任意视角截图中高光面积与 size 线性。** Fresnel、投影、几何与遮罩仍会改变实际图像。

直接取 \(t=1-size\) 时，\(\beta=\arccos(1-size)\)，导数在 size 接近 0 时发散。即便使用角度映射，如果改成 `smoothstep(cos(beta+delta), cos(beta-delta), NH)`，其 50% 角位置也变为 \(\arccos(\cos\beta\cos\delta)\)，会随 softness 漂移。因此基线保留角域参考实现；以后用 LUT/近似优化时，以此作误差基准。

**粗糙度只提供初值。** 由 \(D(\beta)/D(0)=1/2\) 推导
\[
\cos^2\beta=\frac{1-\sqrt2\alpha^2}{1-\alpha^2}.
\]
仅当 \(0<\alpha<2^{-1/4}\) 有半球内部解；等号时恰在 \(\beta=90^\circ\) 达到半峰。更粗糙时 NDF 在整个半球都未降到半峰，不能伪造一个精确半峰宽度。实际 \(G,F\) 还会改变 BRDF 形状。V1 使用经标定的单调 curve \(\beta_0(r)\) 初始化，或在有效域使用此公式并记录截断；用户手调后不被 roughness 自动覆盖。**roughness 越大通常瓣越宽，不应设 size=1-roughness。**

**候选二：GGX 形状重映射。** 定义 \(z=D_\alpha(H)/D_\alpha(N)\)，对 z 使用有版本的曲线 \(Q_s(z)\)，再乘 Fresnel 与几何因子。它保留粗糙度尾部趋势，但阈值、峰值归一化、G 项和掠射角会重新耦合；对整个 \(f_s\) 直接阈值还会让灯强改变高光面积。适合 V1.1 的 Semi-PBR 扩展，不作为所有材质的统一主模型。若要求严格 PBR，保留完整 GGX 支路，不对其任意重映射后继续声称能量守恒。

Fresnel 默认保留可控权重：
\[
F_0=(1-m)F_{0,diel}+mc_b,\quad F=F_0+(1-F_0)(1-V\cdot H)^5,
\]
\[
F_{style}=(1-\rho_F)C_{spec}+\rho_F F.
\]
默认电介质 \(F_{0,diel}=0.04\) 是常用初值，不是所有电介质常数。金属减去漫反射并使用有色 \(F_0\)，而非给整个最终颜色乘 metallic。艺术瓣默认**峰值模式**，宽度增加会增加积分能量；若采用积分归一化模式，则峰值必然随宽度变动。两者不可同时完全解耦。V1 明确不承诺全局能量守恒，避免混入巨大的归一化因子造成 HDR 爆点。

```hlsl
float StylizedSpec(float3 N, float3 Ng, float3 L, float3 V,
                   float beta, float softness) {
    float3 h0 = L + V;
    float len2 = dot(h0,h0);
    float3 H = h0 * rsqrt(max(len2, 1e-8));
    float theta = acos(saturate(dot(N,H)));
    float delta = 0.95 * softness * min(beta, 0.5*PI-beta);
    float shape = 1.0 - Band(theta,beta,delta);
    float gate = Band(dot(Ng,L),0,0) * Band(dot(Ng,V),0,0);
    return (len2 > 1e-8) ? shape*gate : 0.0;
}
// 颜色乘法在调用方统一执行 E * visibility * intensity * Fresnel。
```

### 4.5 Rim：从核心 BRDF 移至可选外观层

\(1-N\cdot V\) 是便利的掠射量，不自动等于反射 Fresnel。推荐以离轮廓的角距离
\[
\gamma=\arcsin(\operatorname{clamp}(N_r\cdot V,0,1))
\]
定义 \(R=1-B(\gamma;\omega_r,\delta_r)\)。\(\omega_r\) 是掠射角带宽，建议 1°–35°，rimSoftness 定义边缘半宽；rimIntensity 为线性颜色幅度。视向垂直轮廓处 \(\gamma=0\)，正面为 90°。背面处理需先按材质约定翻法线或门控，不能依赖 saturate 让所有背面发亮。

\[
C_r=I_rC_{rim}R\,M_r,\quad
M_r=(1-\lambda_L)+\lambda_L B(N_r\cdot L;\tau_r,w_r).
\]
若要专门背光 Rim，可用 \((-L\cdot V)_+\) 作为另一种有名称的策略；它与表面受光门控不是同一效果。选择是否乘主光可见度属于风格策略。基于深度/法线邻域的轮廓响应依赖屏幕采样，独立归入 Outline/ScreenRim，不能伪装成同一个函数。

在单位球正交投影中，角带宽 \(\omega\) 对应归一化径向厚度 \(1-\cos\omega\)，已足以证明「角宽」不等于「像素宽」。需要固定像素宽度时走屏幕空间层。真正基于 Fresnel 的反射应留在 specular 分支；艺术 Rim 默认归 Appearance Layer，避免物理边缘反射和额外 Rim 被无意识重复计入。

```hlsl
float3 RimAppearance(float3 Nr,float3 Ng,float3 V,float3 L,
                     float omega,float edgeHalfWidth,float peak,
                     float3 rimColor,float lightDependence,
                     float lightCenter,float lightHalfWidth) {
    float gamma=asin(saturate(dot(Nr,V)));
    float shape=1-Band(gamma,omega,edgeHalfWidth);
    float lightMask=lerp(1.0,Band(dot(Nr,L),lightCenter,lightHalfWidth),
                         saturate(lightDependence));
    float frontGate=Band(dot(Ng,V),0,0);
    return peak*rimColor*shape*lightMask*frontGate;
}
// 是否乘 visibility、是否参与雾前合成由 Appearance Policy 明确指定。
```

因此 \(C_d+C_s+C_r\) 可以保留为最终局部外观的方便记法，但严格的 Core 为 \(C_d+C_s\)，\(C_r\) 在独立组合阶段加入；整体再加自发光和有声明的 MatCap 混合。

## 5. Face SDF：正式语义、全周连续性与实现

### 5.1 先纠正名称：单张阈值图不是任意灯光下的真实 SDF

真正二维有向距离场 \(d(u,v)\) 表示到一个边界的带符号距离；把阈值平移只产生该边界的平行偏移族。常见角色 Face SDF 工作流更适合解释为**临界照明角场**：
\[
A(u,v)\in[0,1],\qquad\text{该点在 }q<A(u,v)\text{ 时为亮面。}
\]
这里 q 是主光离开面部前向的水平角占半周的比例。纹理值不是米、厘米或 UV 距离。工程可保留 FaceSDF 名称以兼容工作流，但 Schema 必须存 `FieldSemantic=CriticalAzimuth`。

这个模型有严格表达边界：每个 texel 随单侧角度只能从亮转暗一次，全部亮区必须嵌套。若美术参考要求某点「亮→暗→再亮」，一张标量图不能表达；应拒绝静默拟合，转用多角度遮罩图集、双阈值或分段场。真正距离场纹理若未经过重新标定，不能直接套入下面的角度公式。

### 5.2 资产坐标契约

定义角色语义局部轴 \(F_0=(1,0,0),R_0=(0,1,0),U_0=(0,0,1)\)：前、角色自身右、上。这里「右」不是屏幕右；正面相机看过去时角色右通常位于屏幕左。导入时记录 DCC 到语义轴的固定旋转，不能假设所有头骨 X 轴朝前。

用最终动画姿态的 HeadBone 旋转 \(Q_h\) 与固定校准 \(Q_c\) 得到 \(Q=Q_hQ_c\)，令 \(F=QF_0,R=QR_0,U=QU_0\)。要求三轴单位正交且有一致手性。用旋转而非含非均匀缩放的完整逆矩阵变换方向；负缩放/镜像实例在 V1 拒绝，未来用显式手性修正规则。骨骼更新后再发送该帧 basis，避免慢一帧。

\[
L_f=(\ell_f,\ell_r,\ell_u)^T=(F\cdot L_w,R\cdot L_w,U\cdot L_w)^T.
\]
这等价于正交旋转逆变换。直接用世界 L 的 x/y 或 Actor yaw 会在头部单独转动时出错。

FaceUV 是独立的规范图：原点 \((0,0)\) 在图像左上，u 向右、v 向下；中线为 \(u=0.5\)；完整脸图左右对应点满足 \(u\leftrightarrow1-u\)。它可放 UV1，UV0 保持原彩色贴图。导入配置保存 `FlipU/FlipV` 和 UV 通道，翻转必须在进入规范坐标时做一次，不能采样和光方向各翻一次。

纹理 A 表示**光从角色自身右侧绕行时的完整脸阈值图**。左侧来光采样 \(A(1-u,v)\)。若资产明显不对称，使用独立左右通道 \(A_+,A_-\)，不可强行镜像。所谓「只保存半脸」若真指 UV 域只有一半，单纯 fold 并不能恢复任意全脸数据；需存受光同侧/异侧双通道，或明确施加对称约束。本项目 V1 采用完整 FaceUV 域的一侧来光场，避免这一歧义。

### 5.3 水平角与退化情况

\[
h=\sqrt{\ell_f^2+\ell_r^2},\qquad
q=\frac{\operatorname{atan2}(|\ell_r|,\ell_f)}{\pi}\in[0,1]\quad(h>0).
\]
正前光 q=0，侧光 q=0.5，正后光 q=1。`atan2` 保留正确象限，对全周旋转不需要把带符号角跨 \(-\pi,+\pi\) 插值。若换成 \(q_c=(1-\ell_f/h)/2\)，它仍单调，但不是线性角度；整张阈值图和 softness 都必须重新编码。两次 dot 不能凭空得到线性角度。V1 一角色一方向光可以在 CPU 每帧计算 q，无需每像素 atan2；点光每点方向不同，暂不支持同样的优化。

垂直光 h=0 时水平角数学上无定义。定义确定性回退：
\[
\eta=\operatorname{smoothstep}(h_0,h_1,h),\quad h_0=0.05,h_1=0.15,
\]
\[
D_{pole}=B(\ell_u;0.15,0.25).
\]
它表示顶光偏亮、底光偏暗的简化策略；不是精确面部顶光。若风格不接受，改为单独 top/bottom 遮罩，或把该灯角范围判为资产覆盖不足。不能沿用上一帧角度作为唯一回退，否则停放姿态、回放和批量截图有历史依赖。

### 5.4 左右切换与边界闭合

先分别求两侧响应，再混合遮罩：
\[
D_+=B(A_+(UV);q-b,w_f),\quad D_-=B(A_-(UV);q-b,w_f),
\]
\[
\lambda=\operatorname{smoothstep}(-\epsilon_s,+\epsilon_s,\ell_r/\max(h,10^{-6})),\quad
D_{mid}=(1-\lambda)D_-+\lambda D_+.
\]
默认 \(\epsilon_s=\sin2^\circ\)，对水平投影归一化以保持约 2° 的水平切换半宽；若直接使用 \(\ell_r\)，混合带会随光照俯仰变宽。在垂直光区域，任意方位已由 \(\eta\) 淡出。**不要先 lerp 两张阈值图再阈值化**，那会生成不对应任何遮罩的过渡边界。双采样的代价换来正前/正后附近无硬镜像跳变；资产验证还应要求两个端点接近一致。

\(b\) 是角度偏移占半周的比例，正值推迟转暗；建议 \([-5^\circ,+5^\circ]/180^\circ\)。\(w_f\) 是临界角的软化半宽，初始 0°–10°，转换到 q 域除以 180°。它不是面部屏幕空间羽化像素；\(|\nabla A|\) 不均匀会让图像边界宽度变化。需要固定像素边界时可用残差 \(r=A-q+b\) 与其 fwidth 过滤，但这仍不是世界空间软阴影。

为了保证前/后完整闭合，定义两个窄端点包络：
\[
e_F=1-\operatorname{smoothstep}(0,q_F,q),\quad
e_B=\operatorname{smoothstep}(1-q_B,1,q),
\]
\[
D_h=[(1-e_F)D_{mid}+e_F]\,(1-e_B),
\]
\[
D_{face}=\eta D_h+(1-\eta)D_{pole},\qquad q_F=5^\circ/180^\circ,\ q_B=10^\circ/180^\circ.
\]
端点包络是明确的艺术设计：正前全亮、正后全暗。它会覆盖端点附近的 b/softness 效果，属于可记录的耦合；不接受该约束的风格应使用独立前后遮罩。建议 A 落在 [0.03,0.97]，避免纹理端值与压缩把大量像素卡在阈值上。

只要贴图连续过滤、\(w_{eff}>0\)、\(\epsilon_s>0\)，上述模型随水平角连续。正后角的绝对值产生导数折点，但包络把该处响应压平；垂直退化通过 \(\eta\to0\) 消除任意方位依赖。**连续不等于没有美术上可见的过渡形变**，全周动画仍需验收。

### 5.5 与真实动态阴影组合

设 \(v_{dyn}\) 是后端确实提供的主光可见度，\(k_{dyn}\in[0,1]\) 控制保留程度，则完整公式为
\[
\boxed{
M_{faceShadow}=1-D_{face}(A,L_w,Q_hQ_c,UV;b,w_f,\epsilon_s,h_0,h_1,q_F,q_B)
\,[1-k_{dyn}+k_{dyn}v_{dyn}]
}
\]
其中 M=1 为暗、M=0 为亮；这个方向必须写入参数名与调试视图。可见度乘积是一个平滑默认；`min(Dface, vdyn)` 是另一种合成策略。软遮罩在 [0,1] 内满足乘积≤最小值，因此乘积更暗、阴影更强；二值遮罩时两者相同。min 在两者相等处引入折点，不能混称相同模型。

Face 场决定面部明暗设计；鼻子、手、刘海、外物的投影依赖真实遮挡。两者乘积能共存，却可能把已经画在 A 中的鼻影和真实鼻影重复强调。V1 制作 A 时不要烘入动态刘海投影；必要时把 Face 的自阴影风格单独列策略。**普通合并 shadow factor 无法告诉你遮挡者是鼻子、头发还是外物**，选择性排除需要独立渲染数据/遮挡分类，不是一个材质滑条能实现。

Plan A Unlit 没有该可见度输入时取 \(v_{dyn}=1\)，同时 `ReceiveShadow=Unavailable`；这只得到 Face 明暗模型，不得叫完整动态自阴影。接收阴影是决定引擎路线的 Gate，不是这个数学公式自然提供的能力。

```hlsl
// 参考伪码。实际方向光版本在 CPU 计算 q/eta/side/pole。
float FaceShadow(float2 uv, float3 Lw, float3 F, float3 R, float3 U,
                 float bias, float halfWidth, float vis, float keepShadow) {
    float lf=dot(Lw,F), lr=dot(Lw,R), lu=dot(Lw,U);
    float horizontal=sqrt(lf*lf+lr*lr);
    float q = horizontal > 1e-6 ? atan2(abs(lr),lf)/PI : 0.0;
    float eta=smoothstep(0.05,0.15,horizontal);
    float side=smoothstep(-sin(radians(2.0)),sin(radians(2.0)),
                          lr/max(horizontal,1e-6));
    float ap=FaceField.Sample(FaceSampler,uv).r;
    float am=FaceField.Sample(FaceSampler,float2(1-uv.x,uv.y)).r;
    // 若非对称资产，am 改为第二通道/第二图的规范 UV 采样。
    float dp=Band(ap-q+bias,0,halfWidth);
    float dm=Band(am-q+bias,0,halfWidth);
    float lit=lerp(dm,dp,side);
    float front=1-smoothstep(0,5.0/180.0,q);
    float back=smoothstep(1-10.0/180.0,1,q);
    lit=lerp(lit,1.0,front)*(1-back);
    float pole=Band(lu,0.15,0.25);
    lit=lerp(pole,lit,eta);
    return 1-lit*lerp(1.0,saturate(vis),keepShadow);
}
```

## 6. Hair：可解释的各向异性角域模型

### 6.1 候选模型与取舍

| 思想 | 提供什么 | V1 不选择完整实现的原因 |
|---|---|---|
| Kajiya–Kay | 切线主导的纤维反射形状，成本低 | 缺少真实多次散射/吸收；原始模型与常见半向量近似不能混同 |
| Marschner | R/TT/TRT 等圆柱发丝散射路径、纵向与方位分布 | 光学参数、角度分布和资产条件超出受控卡片/发块需求 |
| Shifted Tangent | 用倾斜方向场偏移条带 | 本身不是完整 BRDF，需结合明确的形状函数 |
| Anisotropic GGX | 切线平面两个粗糙度的物理反射瓣 | 更适合表面微槽；强纤维条带与纵向偏移仍需艺术扩展 |

Kajiya–Kay 原论文建立了毛发体素/纹理表达下的着色思路；Marschner 则针对纤维散射结构。V1 不完整实现后者，而采用**KK 启发的半向量条带 + 正交移位坐标系 + 可退化为各向同性的角域度量**。[Kajiya–Kay 1989](https://doi.org/10.1145/74333.74361)、[Marschner 等 2003](https://www.cs.cornell.edu/~srm/publications/SG03-hair.pdf)

选择依据还包括响应空间的差异：KK 启发的切线模型可用 \(\sqrt{1-(T\cdot L)^2}\) 表示纤维横向入射，以 H 离开 T 垂直平面的程度组织高光；半向量版本是实时近似，并非原始模型的逐项复制。Marschner 的散射可按路径 p 组织为纵向分布、方位分布与衰减的组合 \(S=\sum_p A_p M_p N_p\)，包含表面反射 R、透射 TT、内反射 TRT；想保留它的物理意义需要吸收、折射率与发丝结构，而不只是把 exponent 换个名字。

各向异性 GGX 在正交表面 frame 中有
\[
D_{aniso}(H)=\frac{1}{\pi\alpha_T\alpha_B
[(h_T/\alpha_T)^2+(h_B/\alpha_B)^2+h_N^2]^2}.
\]
还必须结合相应 Smith masking 与 Fresnel，不能只有 D 就叫完整 BRDF。两个粗糙度控制椭圆微表面统计，很适合带纹理的表面高光；但它与圆柱纤维的散射路径不同。V1 目标是发块/卡片上的可控条带，所以优先采用下述可解释艺术度量，保留 GGX 为未来物理后端候选。[PBRT 微表面理论](https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory)

### 6.2 必须有真实的发丝方向场

\(T\) 定义为沿发根到发梢的切向单位向量。它不一定等于 UV 的 U 切线；资产需提供 flow map 或已校准的顶点方向。先正交化
\[
T=\operatorname{normalize}(T_0-N(N\cdot T_0)),\quad B=\operatorname{normalize}(N\times T).
\]
若投影长度接近零则资产无效，不能让随机 fallback 在发片接缝翻转。N 是发块/发片的光滑参考法线，不应默认让高频法线贴图旋转发丝方向。背面发片的 TBN 手性、法线翻转规则须独立验证。

shift 采用角度 \(\phi\)，初始范围 −20°–20°：
\[
T_\phi=\cos\phi\,T+\sin\phi\,N,\qquad
N_\phi=\cos\phi\,N-\sin\phi\,T.
\]
与只写 `normalize(T + shift*N)` 相比，它使 shift 直接对应旋转角，且 \(T_\phi,N_\phi,B\) 仍正交；前者对应的真实角度为 atan(shift)。T 的根梢方向反转会反转偏移含义，所以方向场必须带方向，而不仅是无向轴。

### 6.3 完整公式及端点证明

设 \(H=\operatorname{normalize}(L+V)\)，\(h_T=H\cdot T_\phi,h_B=H\cdot B,h_N=H\cdot N_\phi\)。定义
\[
\theta_a=\arcsin\!\left(\sqrt{\operatorname{clamp}
\bigl(h_T^2+(1-a)^2h_B^2,0,1\bigr)}\right),\quad a\in[0,1].
\]
\[
\boxed{S_{hair}=\bigl[1-B(\theta_a;\beta,\delta)\bigr]\,g(h_N)g(N_g\cdot L)g(N_g\cdot V)}
\]
\(\beta,\delta\) 复用 Generic 高光的角域定义；\(g\) 为过滤的正半球门控。强度、颜色、Fresnel 与 visibility 在外层统一相乘。必须保留 \(h_N\) 门控，否则因平方失去符号会出现背面假瓣。\(L+V\) 退化时返回零。

在正半球内：a=0 时 \(\theta_a=\arccos(h_N)\)，严格退化成相对于 \(N_\phi\) 的各向同性角瓣；a=1 时 \(\theta_a=\arcsin|h_T|\)，仅测量 H 偏离切线垂直平面的程度，形成纤维条带。这个连续插值是本项目的艺术响应设计，不能声称等价于某个已验证的微表面分布或 Marschner 散射。

size 的严格含义是**沿 T 截面的 50% 角半宽**，与 a 无关。沿 B 截面的 50% 半宽为
\[
\beta_B=\arcsin\frac{\sin\beta}{1-a},\qquad1-a\ge\sin\beta.
\]
当 \(1-a<\sin\beta\) 时该截面不再在半球内降到半峰，成为贯通条带。上述宽度关系只描述未乘门控的形状项，最终 \(S_{hair}\) 会被半球门控、可见度及几何投影截断。这不是数值错误，但说明 anisotropy 不能命名为固定长宽比。UI 使用 `strandAnisotropy`，解释为「向纤维条带展宽的程度」。扩大 a 会增大原始瓣覆盖面积和总能量；强度仍定义峰值。

建议 Hair 的 \(\beta\) 工作范围 1°–25°，超出时允许专家模式但发出宽条带诊断；这是软推荐范围，不改变公共 size 到角度的映射。持久化使用角度值，Generic/Hair 的归一化 UI 必须共用映射，不能分别隐藏不同最大值。V1 单瓣，无双层色散、第二高光、随机噪声；未来加入第二瓣应记录独立方向/角宽，不能让共享 size 的行为悄悄改变。

```hlsl
float HairShape(float3 T0,float3 N,float3 Ng,float3 L,float3 V,
                float beta,float soft,float shiftRad,float anisotropy) {
    float3 tp=T0-N*dot(T0,N);
    float validT=step(1e-8,dot(tp,tp));
    float3 T=tp*rsqrt(max(dot(tp,tp),1e-8));
    float3 B=cross(N,T);
    float sn=sin(shiftRad), cs=cos(shiftRad);
    float3 Ts=cs*T+sn*N, Ns=cs*N-sn*T;
    float3 h0=L+V;
    float h2=dot(h0,h0);
    float3 H=h0*rsqrt(max(h2,1e-8));
    float ht=dot(H,Ts), hb=dot(H,B), hn=dot(H,Ns);
    float c=1-saturate(anisotropy);
    float theta=asin(sqrt(saturate(ht*ht+c*c*hb*hb)));
    float delta=0.95*soft*min(beta,0.5*PI-beta);
    float shape=1-Band(theta,beta,delta);
    float gate=Band(hn,0,0)*Band(dot(Ng,L),0,0)*Band(dot(Ng,V),0,0);
    return shape*gate*validT*(h2>1e-8 ? 1.0:0.0);
}
```

HLSL 调用方需确保 N/Ng/L/V 单位化；无效方向场由资产验证报错。GPU 防 NaN 只是最后防线，不代替制作规范。两侧发片的门控应使用已朝向观看侧的几何法线，并保留统一根梢方向；发块表面优先使用单面模式。

## 7. Material Model：共享契约与特化接口

Material Profile 不应只是枚举决定某几个默认值；它声明需要哪些资产、如何解释方向场、哪个局部响应可替换，以及哪些功能禁止使用。

| 材质 | 共用部分 | 特化部分 | V1/V2 |
|---|---|---|---|
| Generic | 色阶、角域高光、宏观法线、外观组合 | 无特殊输入 | V1 |
| Face | 颜色组合、可选受限高光/Rim | Face 临界角场替代 diffuse terminator，Head frame | V1 |
| Hair | 颜色组合、强度/Fresnel/可见度契约 | 根梢方向、shift、条带角度模型 | V1 |
| Skin | 配色和风格映射 | 厚度、传输、SSS profile/Slab MFP | V2 |
| Eye | 风格与合成 | 角膜层、虹膜几何/视差、瞳孔、巩膜 | V2 |
| Cloth | 核心色调策略 | Sheen、Fuzz、纤维方向 | V2 |
| Metal | \(F_0\)、roughness、风格映射 | 金属有色反射，环境项 | V2 专用 profile |

Skin 的 SSS 是非局部传输近似：需要厚度、散射尺度和光照后端，不能在 Core 加个 wrap 就声称完成。Eye 要把透明/清漆角膜与虹膜凹陷、瞳孔遮罩和巩膜分开；虹膜视差依赖深度与折射假设，不能只用 MatCap 代表眼球。Cloth 的 Sheen 是掠射散射瓣，Fuzz 还涉及覆盖和纤维几何；两者共享控制语义而非等于 Rim。Metal 保留金属度/F0/roughness 信息，其 Toon 高光可由相同角域映射解释，但缺少环境反射时仅能称为风格化金属外观。

Profile 可以声明 `Unsupported` 参数；不能让 Hair 把 shadowSoftness 暗改成高光柔和度，也不能让 Face 忽略 normalFlatten 却仍显示可调。通用性指同名参数保持可测语义，而非强迫所有材质都有同一套有效参数。

## 8. Style Profile 与高层风格空间

正式定义
\[
S=(\Theta_S,\mathcal F_S,\mathcal P_S,\mathcal R_S,\mathcal O_{S,M},\mathcal A_S,\mathcal V_S).
\]
分别是默认值、响应函数标识/曲线、渲染策略、允许范围与约束、材质特化覆写、引用资源与资产要求、版本/验证元数据。\(\mathcal F_S\) 在 V1 是受控枚举加参数或 Curve/LUT 引用，不是在 Data Asset 里任意执行 HLSL 字符串。

Material×Style 合理，但不是无条件笛卡尔积：\(\mathcal C(M,S,Backend,Asset)\) 决定该组合是否可用、降级或拒绝。Hair×两种风格共享 Hair 模型，改变色阶、角宽、shift 范围、Rim 和描边策略；如果换了完全不同数学模型，必须记录 ResponseModelVersion，不能只改 Preset 名称。

V1 两个端点作为设计初值，需美术标定：

| 项目 | Anime Flat | Semi-PBR Stylized |
|---|---|---|
| 漫反射 | 两阶调色板，\(w_d\approx0.02\) | 宽过渡，\(w_d\approx0.20\)，加性直射+填充 |
| Generic 高光 | \(\beta\approx8^\circ\)，窄过渡，峰值可控 | \(\beta\approx18^\circ\)，宽过渡，增强 F0/Fresnel 保留 |
| Hair | \(a\approx0.9\)，清晰条带 | \(a\approx0.65\)，软条带；仍为同一 Hair 函数 |
| Face | 清晰 A 场，较小角软化 | 相同 A，较大软化与柔和暗色 |
| 法线 | 高宏观抽象、低细节保留 | 低宏观抽象、较高细节保留 |
| Rim/描边 | 明确边缘强调，约 2 px 壳线 | 较弱 Rim，约 1 px 或关闭 |
| S58光照策略 | 原生灯光，低间接贡献；Face独立声明key策略 | 原生灯光，较高间接/反射保留；不因风格名称改变能力等级 |

上述角宽、各向异性与softness数字是canonical/reference初值；S58适配器须标定后转换，不直接复制为native roughness/anisotropy。这里数字不是预先证明的最优组合，也不声称复刻任何商业作品。「类原神/绝区零/终末地/巴别塔圣歌」将来只作为参考标签与视觉目标，不将截图逆向出的猜测写成其真实管线。

高层向量 \(S_{high}\) 可作为研究界面：toneQuantization、softness、specularity、formAbstraction、colorStylization、edgeEmphasis、physicality。但它不是天然正交坐标系。softness 同时改变 diffuse/face/spec/rim；physicality 同时涉及能量、Fresnel、间接光和后端能力，不能由一个 lerp 统一实现。V1 不公开全套高层滑条，仅存储命名端点和少量单调 Curve。

推荐 \(\Theta_{low}=G_M(S_{high};\text{curve set, constraints})\) 采用可解释解析映射与单调曲线组合，不用 ML。增加 toneQuantization 时若跨越整数色阶数，存在拓扑变化；可连续移动或消失色带，但不能假设 band count 线性可微。若需要感知单调性，应固定其他维度、固定观看条件，对实际视觉指标标定。

安全插值条件：同后端、同响应模型、同色阶拓扑、同资产坐标语义、兼容 LUT 输入域、均有效的参数范围。角宽在角域插值；方向在球面/四元数上插值；正尺度可在 log 域插值；颜色在线性场景空间混合或采用已指定的感知空间并明确输出映射。离散渲染策略、整数 ID、贴图索引、是否接收阴影不能随意 lerp。完整画面 crossfade 只证明两张图可以叠加，不证明中间参数有统一语义。

## 9. Principled 的验证定义

令 \(y=\Phi(I(\theta,c))\) 为固定测试条件 c 下的测量向量：色阶边界位置/宽度、高光半峰角宽/峰值/面积、Rim 角宽、颜色亮度与色度等。对参数和输出各自用有效范围归一化，中心差分近似
\[
J_{ij}=\frac{\Delta y_i/\operatorname{range}(y_i)}{\Delta\theta_j/\operatorname{range}(\theta_j)}.
\]
每个参数声明 PrimaryTargets、AllowedCouplings 和 ProtectedAttributes。例如 size 的主要目标为半峰角宽，允许联动包含面积/总能量，受保护属性包含阴影分界和独立强度倍率。正交性不是「所有导数为零」，而是目标灵敏度显著、受保护属性泄漏小：\(Leakage_j=\sum_{i\in Protected(j)}|J_{ij}|/(|J_{target,j}|+\epsilon)\)。允许联动也必须完整报告，不能为了低 leakage 把所有有问题属性事后移出 Protected。V1 基线域目标 leakage≤0.25；契约和阈值可在预实验后冻结，不能事后挑最好场景。

连续性测试扫 101 个值和 360° 光轨迹，检查 NaN/Inf、孤立突变和小步长极限；硬量化的空间不连续不等于参数动画可任意无跳变。可预测性用不提供截图的参数名任务，记录参与者预期影响对象，再给实际结果。泛化性比较 Generic/Hair 的参考截面宽度意义，Face 的 shadowSoftness 则声明不同输入域但相同「分界过渡宽」语义，不能强行比较未经换算的数值。

小规模实验建议 8 人，允许 5–10 人，记录 TA/美术/程序经验；每人 30–45 分钟。先 5 分钟训练，随后完成：

1. 随机展示六档 softness/size，各任务排序；每人约 12 组，包含两个风格、两种几何，重复 2 组检验稳定性。
2. 给出目标图，分别用基线材质实例与本系统匹配，约 4 个任务；AB/BA 顺序平衡，记录时间、操作数和误差。
3. 参数影响判断约 10 题，记录正确率及信心；最后访谈歧义词与不符合预期的组合。

单调性报告每人 Spearman \(\rho\)、中位数和个体分布，初始目标中位数≥0.8；ties 使用平均秩。5–10 人不足以声称一般人群结论，报告参与者层面的 bootstrap 区间和原始匿名数据，不能把同一个人 100 次操作当作 100 个独立参与者。匹配任务目标为中位时间下降≥20%、错误率不增加；这些是项目验收目标，不是研究结果。

测量先关闭 bloom、锁曝光、固定 tonemapper/分辨率；再在实际显示管线重复关键实验。若参数在 tone mapping 后失去单调性，UI 契约需要写明条件或重新标定。小样本可支持「在受控条件下可用」而不能证明普适感知正交性。

## 10. Unreal Engine 渲染架构调查

### 10.1 证据边界与版本锁定

UE 5.7 官方公告确认 Substrate 生产就绪；这不意味着所有未来 Toon 功能都在 5.7。在线 API 当前默认可指向 5.8，本文区分版本明确的公告、跨版本结构性文档，以及必须在实际安装版本复核的接口。[Epic 5.7 公告](https://www.unrealengine.com/news/unreal-engine-5-7-is-now-available)

本次未读取用户授权的 Epic 源码 checkout，所以后文引擎修改清单是设计审查范围，不是对具体 CL 的补丁说明。开发第一步必须记录引擎完整版本/CL、RHI、Feature Level、Deferred/Forward、Substrate 开关、GBuffer 格式、Lumen/VSM 设置。不能只写「UE5 支持」。

### 10.2 「纯插件」与后端能力必须分开

**A0：内容/普通材质插件。** Material Function、MI、MPC、Data Asset、普通 Custom HLSL。它能生成表面数据，或在 Unlit Emissive 中计算自己的颜色。

**A+：渲染扩展插件。** 可使用插件 shader、Render Target、SceneCapture、Global Shader、经核验的渲染扩展点，自己生产额外数据；这仍可能不改引擎，但成本、同步和版本耦合明显提高。官方支持插件 shader，同时明确私有 shader 接口不能当作稳定公共 API。[Shaders in Plugins](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-shaders-in-plugins-unreal-engine)

**S58：原生Toon后端适配插件。** 使用UE5.8已有Toon closure与Profile，不修改引擎也能复用其逐灯阶段；它和A0一样可按插件分发，但不具有同一能力边界。插件方式不等于只允许Unlit自算。

**C：引擎着色模型/BSDF 修改。** 修改 Engine/Shaders 或 renderer 内部调用即属于引擎分支；在外面包一个插件安装器不会把它变成纯插件。

### 10.3 材质图能获得什么

Deferred 的 BasePass 先计算表面属性，后续灯光阶段才结合灯和 shadow attenuation。普通 Surface 材质表达式不是「每盏灯调用一次，且已有可见度」的上下文。Custom HLSL 只扩展该上下文内的计算，不自动绑定 shadow atlas、VSM page table 或 Lumen irradiance，也不能反向读取尚未完成的当帧最终光照。此判断是由官方描述的 pass 分工及节点域限制推导，**不是声称不存在任何私有引擎技巧**。[后处理与 GBuffer](https://dev.epicgames.com/documentation/unreal-engine/post-process-materials-in-unreal-engine?lang=en-US)、[Custom Expressions 5.7](https://dev.epicgames.com/documentation/en-us/unreal-engine/custom-material-expressions-in-unreal-engine?application_version=5.7)

LightVector 的官方适用域为 LightFunction/DeferredDecal，不是通用 Surface 主光入口。SkyAtmosphere 的光方向节点针对所配置的大气太阳光；它也不附赠该像素可见度。A0 reference与Face主光艺术场显式绑定DirectionalLight Actor，方向经MPC/实例参数传递以避免歧义；S58的Generic/Hair原生照明由UE逐灯计算，不由MPC接管整个lighting。[Vector Expressions](https://dev.epicgames.com/documentation/unreal-engine/vector-material-expressions-in-unreal-engine?lang=en-US)、[Sky Atmosphere 属性](https://dev.epicgames.com/documentation/unreal-engine/sky-atmosphere-component-properties-in-unreal-engine?lang=en-US)

ShadowPassSwitch/ShadowReplace 是为投影 pass 选择输入，不是读取当前表面阴影。网上依赖私有 LightAttenuationTexture 或私有 USH 的方案，若未核验资源绑定、渲染路径和版本，就不能作为发布契约。[ShadowReplace API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UMaterialExpressionShadowReplace?lang=en-US)

### 10.4 Lit 的双重光照问题

Default Lit 保留原生直接光、间接光和反射。把手算 \(C_d\) 接到 BaseColor，只会使 UE 把它当作反照率再照一次；接 Emissive 并保留完整 Lit 则变成 \(C_{UE}+C_{manual}\)。修改 Metallic/Roughness 不是通用的「关闭原生光照但保留全部阴影」开关。[Shading Models](https://dev.epicgames.com/documentation/en-us/unreal-engine/shading-models-in-unreal-engine)

Lit 可作为另一种合法产品模式：只控制基础色、法线、roughness 与少量艺术 Emissive/Rim，名为 `NativeLitModulation`。但其 shadowThreshold、独立高光角宽和 Face 分界无法自动等价于第 4–6 节。因此需要不同能力标签，不能让用户选择同一 Style 后静默丢失语义。

### 10.5 Unlit 的能力边界

Unlit 的表面颜色来自 Emissive；它不因为场景灯光自动改变。角色可能仍参加深度/阴影投射 pass，也可能在镜子里出现，但这不代表角色自己的颜色接收到阴影或反射。四件事必须分测：角色向地面投影、角色接收外物阴影、镜子看见角色、角色表面反射镜外场景。

| 能力 | A0 Unlit 普通材质路径的文档预判 | 实际含义 |
|---|---|---|
| 指定主方向光 | ✓ 参数注入可实现 | 方向/颜色/归一化强度，非自动逐灯服务 |
| Cast Shadow | △ 预计可用，必须实测 | Opaque/Masked 几何与 primitive 投影设置 |
| Receive 动态阴影 | ✗ 不自动提供 | 原生 visibility 不会乘到自算 Emissive |
| Self Shadow | ✗ 不自动提供 | Face 场不是几何自遮挡 |
| 多光源 | △ 手工固定数量可算 | 衰减、选择和阴影需另做 |
| Sky Light | △ 可用手动填充/预采样环境 | 不等于原生遮蔽后的天空入射 |
| 接收 Lumen GI | ✗ 不自动提供 | 原生 GI 不成为自算 Core 的输入 |
| 表面反射 | ✗ 原生自动不可用 | 静态 cubemap 是另一个自算近似 |
| AO | △ 资产 AO 可人工组合 | 不是自动读到完整屏幕/GI AO |
| Fog | △ 有后续合成依据，待目标设置验证 | 能被雾覆盖不等于读到体积光照 |
| Volumetric Lighting 输入 | ✗ Core 不自动提供 | V1 放弃此输入，保留场景雾兼容测试 |

上表为能力预判，**不是已经执行的 ✓/△/✗ 实验记录**。PrimitiveComponent 的投影开关和具体 Shadow Path 仍需场景确认。[PrimitiveComponent API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UPrimitiveComponent)

Unlit 自算颜色写入 Emissive 还有副作用：Lumen 可能把它作为发光源，或通过 screen traces 看见它。**不接收 GI ≠ 不影响 GI。** 一项 primitive 开关不足以保证消除所有屏幕追踪污染，必须在红墙/暗室中测试。官方的 Lumen Material AO 路径要求关闭 `Allow Static Lighting` 以腾出 GBuffer 空间，并让受光材质输出 AO，可用于骨骼角色的自遮蔽；这不能作为 Unlit 自动 AO 的依据。[Lumen 技术细节](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine)、[Lumen GI/Reflections](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine)

Deferred opaque 的雾可在后续 fog pass 施加，因此「Unlit 一定不受雾影响」也是错误概括。透明、壳线及后处理的排序需单独测。[渲染宏中的 Fog 路径说明](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/RenderCore/FShaderMaterialDerivedDefines)

### 10.6 SceneTexture 与后处理的不可逆性

PostProcessInput0 是选定阶段的合成颜色；GBuffer 存表面属性。`DiffuseColor/SpecularColor` 是由材质属性导出的颜色，**不是已分离的 diffuse lighting / specular lighting 结果**。[ESceneTextureId](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/ESceneTextureId)

从一个 RGB 最终值不能唯一恢复直接漫反射、镜面、GI、雾和 emission 多个 RGB 分量。`SceneColor/BaseColor` 只在无高光、无雾、无发光、非零反照率、已知曝光的限制条件下近似有效；黑色 albedo 是简单反例。后处理可以做全局量化/颜色映射/边线，却难以成为独立 Face/Hair 语义系统。

Substrate 的多 closure GBuffer 还会增加数据解释复杂度。当前官方编程文档说明普通 SceneTexture 查询可能只代表第一 closure；具体格式及 shader 接口须按安装版本核对，不硬编码纹理通道。[Substrate GBuffer Formats](https://dev.epicgames.com/documentation/en-us/unreal-engine/programming-with-substrate-gbuffer-formats)

### 10.7 Substrate：S57与S58的不同角色

S57 Slab帮助组织物理表面、SSS/MFP、fuzz、各向异性和层间关系，适合未来Skin/Cloth/Metal后端。分层框架本身不等于可以替换逐光Toon函数；专用Hair/Eye closure能否参与某个operator仍按版本核验。[Substrate Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-substrate-materials-in-unreal-engine)

S58 Toon是具体原生NPR入口，应成为当前V1首选生产候选。其Profile承接ramp、间接贡献与阴影策略，StyleProfile保留上层语义。完整API、语义映射与实验限制见下面专项评估；Experimental决定验收深度，不直接决定否定或采用。


### 10.8 UE 5.8 Toon BSDF Backend Evaluation

#### 10.8.1 专项修订的决策与证据状态

**修订决策：S58 提升为 V1 首选 Production Candidate；A0 降为 Mathematical Reference Backend。** 项目的产品核心是表面/参数/材质/风格语义及后端适配，不是再造灯光积分器。已有原生后端覆盖的灯光遍历、遮挡、天空和 GI 不应默认重做。Production Candidate 表示第一优先实现与验收对象，尚不等于已通过生产验收。

官方 5.8 发布说明明确：Toon 基于 **Substrate Blendable GBuffer（legacy）模式**，支持局部光、Sky Light 与 Lumen GI，提供漫反射/高光 ramp、各向异性与 GI 缩放。这是选择优先级的直接依据，强于「可能将来有用」的猜测。V0 首先锁定这个 GBuffer 模式，不把 Adaptive 作为默认目标。[Epic 5.8 发布说明](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes?lang=pt-BR)

公开 C++/Python API、编译器接口与 Epic 一手说明已核验。后续本机只读审计又取得 **UE5.8.3、CL58210709、CompatibleCL55116800** 安装树源代码，读取Toon节点/Profile、legacy/MIR入口、Profile atlas与common lighting正文。关键结论已升级SourceVerified，详见[本机源码审计](UE5.8.3_本机源码审计.md)。截至这次源码审计Launcher仍在校验、Editor未运行；后续安装/工程状态以独立ProjectCreation记录为准，不把瞬时安装进度写入渲染结果。阴影/GI/PSO/参数映射画面实测仍为 **NotRun**；存在源文件不等于安装完整或渲染通过。

等级使用两轴：`Exact / Approximate / Unsupported` 表示语义映射；`APIConfirmed / SourceVerified / Calibrated / RenderTested / NotRun` 表示证据。SourceVerified只确认目标源代码，渲染测试仍可为NotRun。条件精确公式在条件未核验前不能记录为当前 Exact。UI 与资产均须保存映射质量及限制，不将所有后端滑条冒称参考模型的精确实现。

#### 10.8.2 实际节点输入与 Profile 字段

正式类名为 `UMaterialExpressionSubstrateToonBSDF`，不是假设存在的任意 Toon 自定义节点。其头文件为 `Materials/MaterialExpressionSubstrate.h`；`UToonProfile/FToonProfileStruct` 位于 `Engine/ToonProfile.h`。[节点 API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UMaterialExpressionSubstrateToon-?lang=en-US)

| 层级 | 实际输入/类型 | 本项目用途及边界 |
|---|---|---|
| 节点表面值 | BaseColor(float3)、Metallic、Specular、Roughness、EmissiveColor | PBR 表面语义直接输入；Specular 是表面反射属性，不能直接改名为艺术峰值强度 |
| 节点方向值 | Normal(float3)、Tangent、Anisotropy | 法线与方向场接口；是否被目标 CL 正确使用必须测试 |
| 节点模式坐标 | PatternUVs(float2 expression) | 每轴saturate并量化12bit；offset使用frac/Wrap；不是逐灯Face数据接口 |
| 节点 Profile | `TObjectPtr<UToonProfile>`，编辑器属性 | 不是普通 `FExpressionInput` 或通用 TextureParameter |
| 漫反射 Profile | `DiffuseRamp: FRuntimeCurveLinearColor` | 已确认common输入为signed NoL remap，shadow/offset顺序见10.8.3；画面仍需标定 |
| 漫反射 offset | Texture、Strength(float)、Size(float) | 固定 Profile 资源与配置，Size 为采样 UV 倍率 |
| 镜面 Profile | SpecularRamp、SpecularRampOffsetTexture/Strength/Size | 表面镜面响应映射，不自动满足角域瓣契约 |
| 阴影 Profile | bDiffuseRampIncludeShadow、ShadowExtinctionCoefficient | 原生可见度/传输参与后端响应的控制；不是可写的 VisibilityOverride |
| 阴影图案 | ShadowHatchingPatternTexture/Size/Strength/DistributionRamp | 独立艺术功能，V1 默认关闭以隔离测试 |
| 间接光 Profile | DiffuseIndirectScale、SpecularIndirectScale | 控制原生间接贡献；不需要 A0 手动模拟 Lumen |
| 间接镜面量化 | SpecularIndirectRamp、SpecularIndirectRampRepetition | 另有实验 cvar 条件；不能认为填 Curve 就一定启用 |

上述字段来自 [FToonProfileStruct](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FToonProfileStruct)。offset灰度[0,1]映射到[-1,1]，建议距离场；间接镜面量化需 `r.Substrate.Experimental.ToonReflectionQuantizationEnabled=1`，Repetition=0不应用该ramp。[Python ToonProfileStruct](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/ToonProfileStruct) 本机头文件已确认Shadow extinction单位1/m，并在接口乘1/100转为1/cm；VSM/CSM透射与过滤效果仍待实测。native Profile atlas默认分辨率64、最低64，offset图案内部烘焙分辨率为N−2；第4节自制reference LUT的256/Clamp建议不能自动套给它。ToonProfile.cpp (`Engine/Source/Runtime/Engine/Private/Rendering/ToonProfile.cpp:26`)

**「建议 distance field」不是「支持主光方向相关的 Face Critical Azimuth Field」的证明。** 一张固定空间 offset 图与根据 Head frame/每盏光生成的遮罩是不同输入。

#### 10.8.3 编译与 Shader 路径：已经看到什么，还缺什么

| 路径层 | 本次证据 | 可以得出的结论 |
|---|---|---|
| Material expression | Build/Compile、表面/方向/PatternUV/Profile 属性 | 有原生 Toon closure 入口，不是 Emissive 后处理 |
| 传统编译器 | `FMaterialCompiler::SubstrateToonBSDF` 签名 | ProfileID、表面值、Normal/PatternUV/Anisotropy/Tangent/basis/operator 进入编译接口；无公开 FaceMask/NoLOverride/VisibilityOverride 参数 |
| MIR 编译器 | 本机`MaterialExpressionsToMIR.cpp:6847`的Build正文 | ToonDesc没有Tangent/Anisotropy输入，共享basis切线参数为nullptr；不是任意回调函数 |
| descriptor | BaseColor、Emissive、Metallic、Normal、PatternUVs、Roughness、Specular、ToonProfileId、LocalBasisIndex、IsAtBottom | 无逐灯Face输入；实际Build缺失方向输入见10.8.6 |
| 后端 lighting/lookup | 本机`SubstrateToonBSDF.ush/ToonProfileCommon.ush`正文 | signed NoL remap、shadow/offset顺序、GGX D查表归一化与外乘已确认；画面NotRun |

[编译器入口与完整签名](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FMaterialCompiler)、[MIR Emitter](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FEmitter)、[FSubstrateToonDesc](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FSubstrateToonDesc?lang=en-US)

本机CL58210709已读到`SubstrateToonBSDF.ush`、`ToonProfileCommon.ush`、`ToonProfile.usf`、`BRDF.ush`、`ShadingModels.ush`、`SubstrateEvaluation.ush`及legacy/MIR节点编译正文。历史阴影讨论还定位`ShadowProjectionPixelShader.usf`和`VirtualShadowMapProjection.usf`，但此次common函数审计不等于这些阴影路径的质量回归已完成。[源码审计与链接](UE5.8.3_本机源码审计.md)、[Epic阴影源码片段](https://forums.unrealengine.com/t/5-8-preview-toon-bsdf-shadows-cast-on-surface-are-blurry/2736077)

先固定非统一漫反射基线`r.Substrate.Experimental.ToonUnifiedDiffuse=0`。令\(q=Context.NoL\)、\(x_d=(q+1)/2\)，\(v_t\)为原生`TransmittanceOrOpticalThickness`提供的透射值经可选hatching处理后的`TransmittanceShadow`，\(\delta_d\)为offset，\(A\)为灯光falloff×color。common函数实际执行：
\[
C_d^{off}=A v_t\frac{C_{diff}}{\pi}R_d(\operatorname{saturate}(x_d+\delta_d)),
\qquad
C_d^{on}=A\frac{C_{diff}}{\pi}R_d(\operatorname{saturate}(x_dv_t+\delta_d)).
\]
off/on指Profile IncludeShadow；on先将透射乘入坐标，再加offset采样Ramp，Ramp后不再乘\(v_t\)。这确认阴影相关值参与Toon响应，但不能把带介质策略的transmittance改称binary visibility。on且\(R_d(0)>0\)允许遮挡区保留Ramp阴影色，正offset还可能抬亮全遮挡样本。common公式 (`Engine/Shaders/Private/Substrate/SubstrateToonBSDF.ush:139`)

`BRDF.ush`两种Init均令Context.NoL=dot(N,L)，没有saturate；Substrate更新另存SatNoL而不截断Context.NoL。Blendable Toon调用前同样未截断。非零SourceAngle/球面光的SphereMaxNoH可改变NoL，所以零角半径方向光用于初始signed-dot标定，其他灯形另测。Init (`Engine/Shaders/Private/BRDF.ush:24`)、上下文更新 (`Engine/Shaders/Private/Substrate/SubstrateEvaluation.ush:441`)

IncludeShadow直接依据Profile分支，不要求UnifiedDiffuse=1。后者默认0且ReadOnly，另决定是否输出逐灯汇总数据并在后续pass应用Ramp，不应和IncludeShadow混淆。`ProjectGBufferFormat=0`才是Blendable；真实枚举为`ESubstrateStorageFormat`，新项目默认显式Substrate=True/Format=0。格式枚举 (`Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h:781`)、逐灯模式宏 (`Engine/Shaders/Private/DeferredLightPixelShaders.usf:31`)

非Rect直接镜面输入已确认：\(x_s=\operatorname{saturate}[D_{GGX/aniso}(H)K(r,a,N\cdot V)+\delta_s]\)，\(K=GGXMaxEnergyLookup\)。Vis与Fresnel乘子在此分支被注释；Ramp结果再乘SpecularColor，积分外乘SurfaceShadow、AreaLight.NoL与\(A\)。Rect使用LTC近似，不能套同一角域精确映射。镜面分支 (`Engine/Shaders/Private/Substrate/SubstrateToonBSDF.ush:98`)、视角查表K (`Engine/Shaders/Private/ShadingEnergyConservation.ush:99`) 完整平台调用图、VSM/CSM回归、GI/运行时资源与缓存仍须继续验证；SourceVerified不等于RenderPass。

#### 10.8.4 NPR 参数映射：不能仅凭名称宣称 Exact

设后端漫反射坐标为 \(x_d=\phi_d(q,\zeta)\)，q=N·L，\(\zeta\) 代表可能的 shadow/衰减等其他因素。若已证明 \(\phi_d(q)\) 单变量且可逆，则可烘焙
\[
R_d(x)=B(\phi_d^{-1}(x);\tau_d,w_d).
\]
本机CL58210709的上述common路径在remap前保留signed NoL。对于零角半径方向光、无offset、IncludeShadow关闭的隔离坐标，已得到\(\phi_d(q)=(q+1)/2\)，因此\(\tau_{profile}=(\tau_d+1)/2\)、\(w_{profile}=w_d/2\)，\(R_d(x)=B(2x-1;\tau_d,w_d)\)为**坐标层精确映射**。有限Curve/LUT采样、外部乘子与GI仍须标定，全外观没有提前升级Exact。其他后端若\(\phi_d(q)=\max(q,0)\)，负q域被折叠仍是不可恢复的理论反例；本机该调用路径没有这种折叠。混入shadow等变量的一维曲线也不能自动解耦它们。

高光只有在\(x_s=\phi_s(\theta_h;r)\)可逆且条件固定时，才能烘焙\(R_s(x;r)=1-B(\phi_s^{-1}(x;r);\beta,\delta)\)。本机非Rect坐标实际为D×视角查表K，再saturate；直接Vis/Fresnel被注释，不能继续称“完整GGX BRDF输入”。K随NoV、roughness与anisotropy变化，外部还有NoL/SurfaceShadow乘子；一条固定全视角Ramp仍不能一般保证独立角宽和峰值。第4节HWHM只提供NDF初值，不能替代native响应标定。

| 参数/操作 | 当前映射判断 | 实施办法与精确边界 |
|---|---|---|
| 表面 BaseColor/Metallic/Roughness 字段传递 | Exact，数据层 | 同单位/色彩空间输入；不代表最终响应等于reference |
| normalDetailStrength / macroNormalBlend | Exact，预处理层 | 保留第4节方向算法后送Normal；共同影响native diffuse/spec/GI |
| terminatorPosition | Exact坐标层；全外观未标定 | 零角方向光、off/无offset基线按(τ+1)/2生成Ramp；LUT/GI/外乘误差仍需测 |
| terminatorSoftness | Exact坐标层；采样/外观为Approx待测 | native坐标半宽w/2；atlas默认64及过滤/AA限制需标定 |
| specularHalfAngle | Approximate，角宽标定必需 | roughness初值+SpecularRamp拟合；不采用size=1-r |
| specularSoftness | Approximate | 拟合D×K瓣边缘，验证NoV归一化、NoL外乘、AA与Rect分支 |
| specularIntensity | Approximate | 有证据时缩放SpecularRamp输出/可用乘子；Specular不是独立峰值旋钮，影响F0与介质关系 |
| 任意角域瓣全视角精确复现 | 当前不承诺Exact | 坐标不可逆/额外变量造成冲突时对应契约Unsupported；单变量可逆情形保留条件精确可能 |
| Rim / Outline / ColorMapping | ToonProfile字段层Unsupported；系统可独立实现 | 外观/几何模块按自身契约判断Exact/Approx；Emissive Rim不自动取得native shadow |
| bDiffuseRampIncludeShadow设置传递 | Exact，配置层 | 实际可见度组合质量为RenderTested后才能确认 |

Exact 也要分数据/配置与外观响应；「值写进去了」不等于「观感契约已满足」。拟合建议冻结光向、视角、几何、F0/曝光，扫描 roughness/ramp，生成角半宽/边缘宽/峰值的响应表；再在保留测试角度与灯强上检验。拟合目标为全条件误差，而非只匹配一张漂亮截图。若没有稳定单调解，适配器报告 Unsupported，不制造假滑条。

#### 10.8.5 Face：保留 native visibility 的近似桥接与严格边界

Epic 工程师 2026-08-24 明确回复：SDF shading maps 尚非开箱支持，延迟逐灯求值需要额外 GBuffer 数据而当前空间不足。这支持把 **完整方向相关 Face terminator 替换**判为公开接口下 Unsupported，而不是默认只缺几行连接。[Epic Face SDF 一手回复](https://forums.unrealengine.com/t/substrate-feedback-thread/691314?page=41)

**候选一：PatternUV→offset atlas，源码已确认，外观仍为Approx候选。** 本机PatternUV先saturate并以每轴12bit打包；offset求值先\(p=\operatorname{frac}(UV_{12}Size)\)，再从atlas读\(\Delta=k[2T_{atlas}(p)_R-1]\)。原始图案以固定Wrap sampler烘焙，内部区域N−2。原方案“Clamp identity表即可生成全范围[-k,k]”有确定端点反例：Size=1、u=1时frac回到0；纹理Clamp不能改变该shader流程，端点双线性还会跨接缝。编码 (`Engine/Shaders/Private/Substrate/SubstrateToonBSDF.ush:16`)、frac/offset (`Engine/Shaders/Private/ToonProfileCommon.ush:89`)、Wrap烘焙 (`Engine/Source/Runtime/Engine/Private/Rendering/ToonProfile.cpp:479`)

应确定远离接缝的内部单调区间\([a,b]\subset(0,1)\)，标定实际采样函数\(h_{atlas}(u)\)，在其可达域内离线生成\(u=h_{atlas}^{-1}((\Delta/k+1)/2)\)。有效Δ范围、atlas分辨率、12bit量化、texel中心与过滤误差都进入QualityReport；仅加固定epsilon不证明逆映射成立，超范围明确限幅/Unsupported，k=0禁用。Profile预生成不可变，线性数据并确定采样通道，关闭specular offset/hatching隔离共享PatternUV副作用。不再承诺完整[-k,k]端点Exact。

IncludeShadow关闭时，已确认坐标\(x_d=(q+1)/2+\Delta\)再saturate，且Ramp后外乘transmittance。对一个已指定主光、可达且单调的响应可尝试
\[
\Delta(p)=x_{target}(D_{face}(p))-\frac{N\cdot L_{key}+1}{2},
\]
其中\(x_{target}\)仅在所选响应可达域覆盖Dface时定义：先用单调两阶标量响应，再选有限右逆/预像；任意RGB、多阶或非单调ramp没有通用逆函数。hard-step不能精确表示0<Dface<1的软边，需软ramp或记录误差。off分支可在艺术Ramp之外保留原生transmittance，但其shadow/介质质量仍须实测。on分支先将未知\(v_t\)乘入坐标，再加Δ且不外乘\(v_t\)；普通材质图无法一般精确补偿，正Δ还可能抬亮全遮挡样本。因此Face桥接优先测试off并增加全遮挡回归，当前仍为 **Approximate候选/RenderNotRun**，不得写成已实现完整Face。

更关键的是偏移由主光生成，但会作用于所有后端灯：\(\phi_d(q_i)+\Delta_{key}\) 不等于每盏灯所需的 \(x_{target}(D_{face}(L_i))\)。选择主光控制脸+辅光限制的策略可以是可接受风格策略，却不是逐灯精确映射。镜像/顶光/背光依旧由第5节处理；共享 PatternUV 的副作用、mip和范围均须专项测试。

**候选二：S58 native-lit Face + 艺术mask调制。** FaceMask 进入BaseColor/配色可保留原生receive/self/GI，但普通N·L仍参与着色，可能形成双重分界；这是 Approximate FaceAppearance 模式。把差值塞Emissive无法获得该差值的原生visibility，会漏光，不能作为默认生产桥接。

**候选三：Face专用后端。** 其他slot保留S58，Face用A0 reference做受控研究对照；若发布要真实receive，则只为Face增加有界A+ visibility，或C类小范围逐灯/扩展GBuffer入口。A0 Face仍缺原生GI/反射输入，必须声明差异。这比为全角色重造灯光更聚焦，但严格Face合同不达标时，不能仅因身体S58好看就宣告完整V1完成。

刘海、鼻子与场景投影继续由原生visibility负责；FaceField只定义艺术分区。当前native shadow factor不自动带遮挡者身份，选择性排除鼻影/保留刘海影仍需额外分类数据。第5节可见度乘积是reference，不假设 `IncludeShadow` 开关内部就是完全相同的乘积。

#### 10.8.6 Hair：方向接口不等于参考瓣等价

Toon各向异性沿用GGX类行为，与参考\(\theta_a=\arcsin\sqrt{h_T^2+(1-a)^2h_B^2}\)的展宽机制不同，不能直接照搬参数。Epic曾确认Tangent实现不完整。[Epic说明](https://forums.unrealengine.com/t/substrate-feedback-thread/691314?page=41) 本机CL58210709又读到具体缺口：legacy Compile共享basis只注册Normal，虽后面另传Tangent；MIR Build的basis切线参数为nullptr，ToonDesc没有Tangent/Anisotropy输入。不能仅凭pin存在或patch号码认为方向已修好；该入口缺口为SourceVerified，实际使用的translator、方向响应与修复效果仍RenderNotRun。legacy入口 (`Engine/Source/Runtime/Engine/Private/Materials/MaterialExpressionSubstrate.cpp:2489`)、MIR入口 (`Engine/Source/Runtime/Engine/Private/Materials/MaterialExpressionsToMIR.cpp:6847`)

| Hair语义 | 分类 | 原因/处理 |
|---|---|---|
| HairFlow→Tangent数据 | 节点输入可表达；编译缺口已确认、方向未验 | MIR不传Tangent/Anisotropy；记录translator并做0°/45°/90°测试，失败即方向能力Unsupported |
| 主截面高光角宽 | Approximate | roughness+anisotropy+ramp共同标定，不复制参考a值 |
| softness/peak | Approximate | D×NoV查表K、NoL外乘、saturate与ramp采样耦合；该直接分支不乘Vis/Fresnel |
| strandAnisotropy全域等价 | Approximate候选；Exact未确认 | roughness/anisotropy/ramp联合变换可能匹配部分域，需参数可达域与shader坐标证明 |
| 独立specular shift | Unsupported精确映射 | 固定N仅送倾斜T若被正交化，shift消失；同时旋转N/T会影响diffuse/GI/reflection |
| 无shift的raw angular lobe | Approximate候选，可研究条件Exact | 固定条件NDF等值线匹配后可能逆映射；NoV查表K、NoL外乘或不可达域导致失败 |
| 含独立shift的完整Hair契约 | Unsupported精确原生映射 | 缺独立specular frame/shift接口，须局部扩展或明确降级 |

不能仅因GGX名字不同就证明形状不可映射。理想各向异性GGX NDF在正交frame下可整理为
\[
D(H)=\frac{1}{\pi\alpha_T\alpha_B[1+A_T h_T^2+A_B h_B^2]^2},\qquad
A_T=\alpha_T^{-2}-1,\ A_B=\alpha_B^{-2}-1.
\]
若参数域可实现\(A_B/A_T=(1-a)^2\)、\(A_T>0\)，其等值线可匹配reference度量，再经单变量ramp重塑raw瓣；但\(A_T=0\)等退化、native参数域、NoV归一化查表K、saturate与NoL外乘均需标定。源码已确认直接x_s使用D×K，仍未证明全视角/完整参数域等价；不能武断宣称“必能Exact”或“各向异性全域绝不可能”，也不能跳过目标编译入口缺口。

V1 Hair生产候选为 **经标定的NativeAnisotropicToon近似响应**，仍保存reference参数与QualityReport；方向流向、角宽单调、软硬与强度独立性均须验收。若用户冻结独立shift和完整角域瓣为硬契约，S58原生Hair单独不合格，需专用扩展/小补丁或受控reference模式。不能用临时MatCap替代后仍命名“完整各向异性”。

#### 10.8.7 StyleProfile→Backend Adapter→ToonProfile

\[
\boxed{NPRStyleProfile+MaterialProfile+InstancePatch
\xrightarrow{NPRBackendSubstrateToon}
\{UToonProfile,MI/MID,\text{resource manifest},\text{MappingQualityReport}\}}
\]

这里“引擎无关”指语义/序列化契约可跨后端；UDataAsset本身仍是UE载体，不宣称该UObject类可直接跨引擎。StyleProfile保留引擎无关的canonical参数、响应ID、Curve、材质特化、外观策略和能力需求；后端包记录 `Backend=S58`、patch/CL、Blendable配置、shader语义版本、标定集/误差、Profile与MI引用、固定cvar、降级项、资源hash。引擎对象引用放在后端资产，不反向污染理论Style定义。

每个映射条目包含 `SemanticId/TargetProperty/TransformId/Quality/ValidDomain/Couplings/ErrorMetric/EvidenceId`。

映射设计示例（伪结构，不是已编译UE代码）：

```yaml
Source: {StyleId: AnimeFlat, SchemaVersion: 1, MaterialModel: Generic}
Backend: {Id: S58, Patch: required, CL: required, GBuffer: Blendable}
Mappings:
  - SemanticId: specularHalfAngleRad
    Targets: [ToonProfile.SpecularRamp, Material.Roughness]
    TransformId: CalibratedAngularFit_v1
    Quality: Approximate
    ValidDomain: calibration_dataset_reference
    Couplings: [view_dependent_GGX_normalization, NoL_outer_factor, peak_response, F0_color]
    EvidenceId: NotRun
GeneratedResources: [ToonProfile, MIC, ResourceManifest]
Runtime: {ProfilePolicy: ImmutablePrebuilt, Selection: MaterialBundle}
```

生成顺序为Resolve→CapabilityCheck→Map/Calibrate→BuildCurve/Profile→生成MIC override→保存资源hash/质量→CookManifest。`required`缺失时拒绝发布包；实例要求改曲线时生成profile variant而非普通MID写入。

允许Style×Material生成不同ToonProfile：例如Generic与Hair的SpecularRamp不同，Face有不同offset policy；实例的普通表面参数走MID，要求改Profile曲线的覆写在编辑器产生可缓存profile variant。相同解析结果用hash去重，不为每角色复制Profile。

分离两类范围：canonical硬域不变；后端有效域限制通过验证报错/降级。物理性保留、GI强度和多灯策略属于后端策略，不删除Core参考模型。若后端拟合不能满足specularHalfAngle的契约，UI显示 Approximate及实际标定范围；不把roughness重命名为size后隐藏差异。

#### 10.8.8 Runtime：切换资源，而不是每帧改Profile Settings

`UToonProfile` 官方仍标Experimental，数据per-material，提示不要runtime修改Settings；这限制资产变更，并不等于禁止运行时换Style。[UToonProfile](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UToonProfile?lang=en-US)

官方5.8 `UMaterialInstance` 已有 `bOverrideToonProfile` 与 `ToonProfileOverride`，render proxy也有Get/SetToonProfileOverrideRT；因此可以预生成不同Style的MI，**无需因Profile不同就复制Master**。本次未确认通用public MID `SetToonProfile` setter，不杜撰该接口。[UMaterialInstance](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UMaterialInstance)、[FMaterialRenderProxy](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FMaterialRenderProxy)

V1稳妥流程：编辑器生成Profile_A/B与MI_A/B→cook并预载→按各自MI创建MID_A/B→应用同一实例表面patch→运行时用SetMaterial切换整个slot/角色bundle。预载期间准备全部资源和PSO，批量交换角色slot，防止一帧出现身体A/脸B。保持强引用，避免切换后资源GC；Profile占用的GPU表/缓存需查实际实现。

MID可更新已经参数化的scalar/vector/texture：BaseColor倍率、roughness、metallic、Specular、法线强度/宏观blend、Tangent/Anisotropy、PatternUV桥接参数、Emissive等；不能因此任意改节点拓扑、静态开关或Profile Settings。动态数值修改是否有视觉意义仍受native映射契约约束。

Style A↔B若只改普通参数，可保留同Profile；若改ramp/offset/间接策略等Profile字段，使用预生成Profile及相应MI bundle。连续曲线变形/任意风格插值不在V1。运行时选择**已cooked材质**不需要现场构造shader，但可能首次创建/绑定PSO并产生hitch；同图同静态配置可能共享PSO，不能未经capture保证零切换成本。修改静态开关、graph、GBuffer模式或生成新材质属编译/配置问题，与PSO首次使用严格区分。[PSO Precaching](https://dev.epicgames.com/documentation/en-us/unreal-engine/pso-precaching-for-unreal-engine)

#### 10.8.9 阴影、GI与当前Patch：必须测的事实问题

官方发布说明与Profile字段支持「后端已负责接入原生光照」，但是否符合本项目图像/参数契约要实测。Cast、Receive、Self、VSM、CSM分别建用例；`IncludeShadow` on/off不能只观察颜色，要隔离可见度导致的ramp边界变化，固定已标定灭绝基线、关闭hatching/GI和多光。

已知历史问题：Epic 2026-06-16说明Toon CSM/VSM修复不回移当时5.8 patch，讨论指向 Git commit `eef3f0de5c050521fd19362c6089abddcbc59445`（它不是数字CL）。当前官方汇总已列5.8.3，但未取得其对应源码证明该修复与Tangent修复纳入；**当前target CL状态Unknown**，既不宣布已经修好，也不宣布全部5.8.3仍坏。[Epic阴影问题与修复说明](https://forums.unrealengine.com/t/5-8-preview-toon-bsdf-shadows-cast-on-surface-are-blurry/2736077)、[5.8发布/Hotfix汇总](https://forums.unrealengine.com/t/unreal-engine-5-8-released/2729274)

另有官方 **UE-388460**：5.8 Toon在DDC命中时可能未传播 `MATERIAL_SHADINGMODEL_TOON` 编译标志，专项复核时页面为Unresolved、Target Fix 6.0。问题公开了 `FHLSLMaterialTranslator::Translate` 缓存恢复片段；本机common Shader审计不解除该缓存风险。必须增加独立测试缓存的冷/热DDC、重启与Cook对照，不擅自删除用户全局DDC，也不把每次强制重编译当正式方案。[Epic UE-388460](https://issues.unrealengine.com/issue/UE-388460)

Lumen测试分diffuse bounce与indirect specular：分别改变只经反弹可见的红墙、移动屏内/屏外反射物；扫描IndirectScale并开关ReflectionQuantization cvar/Repetition。镜中能看到角色不等于角色表面具有reflection；直接SpecularRamp生效不等于间接SpecularRamp启用。SW/HW Lumen、screen traces、hit lighting、Sky与Fallback路径分别记录，不从“supports Lumen”推断所有模式已通过。

#### 10.8.10 四个最终回答

**Epic替我们承担什么：** 原生Toon闭包、灯光遍历/衰减/累加、阴影接入、Sky/Lumen间接贡献、漫反射/镜面ramp与相关资源，以及native各向异性基础。V1应复用这些，默认不自行创建全角色shadow/GI/反射积分器。

**仍由我们完成什么：** 语义Schema与参数标定、Material×Style解析、资产/方向场/Face协议、后端映射质量与范围、Curve/资源生成、实例覆写、风格bundle与运行时切换、Outline/Rim/颜色合成、Face/Hair专用适配和真实验收。原生Profile不能取代这层系统。

**V1是否值得使用A0：** 值得保留为数学校验、消融和严格Face/Hair研究参考；不值得继续默认将全角色生产光照建立在A0。它是验证依据与有限fallback，不是先造整套受控光照再随便测S58。

**S58能否成为实际主后端：** 能成为当前首选候选，Generic最有理由先落地；Hair取决于Tangent修复及近似契约验收，Face须独立选择桥接/混合/扩展。现有证据不足以把“生产候选”升级为“已通过”。Generic成功不自动等于完整Generic+Face+Hair V1成功；严格Face或独立Hair shift需要的缺口只在局部补，不先重造全角色灯光。

## 11. 四路线比较与描边选择

### 11.1 决策矩阵与优先级

1为不利、5为有利，表示设计判断，非benchmark。`*`表示需目标构建实测。硬条件优先，不用加权总分掩盖Face/Hair入口缺口。S58与S57必须分列；同属Substrate并不意味着控制能力相同。

| 评价 | A0 reference | B Lit后处理 | C引擎扩展 | S57 Slab | S58主候选 |
|---|---:|---:|---:|---:|---:|
| 参考Core/Face/Hair数学控制 | 5 | 1 | 5 | 2 | 3*，按参数分级 |
| 原生直接/多灯接入 | 1 | 5 | 4 | 5 | 5*，官方支持 |
| 原生receive/self shadow | 1 | 5 | 5 | 5 | 5*，目标CL质量未测 |
| 精确Face与native visibility组合 | 1 | 1 | 5 | 1 | 2*，须桥接/扩展 |
| Lumen/间接镜面接入 | 1 | 5 | 3 | 5 | 5*，模式/质量须测 |
| 总成本可预测性 | 4 | 3 | 2 | 3 | 3*，含native passes |
| 低开发成本（满足产品目标） | 2，需重造灯光 | 3，语义不足 | 1 | 3 | 4*，适配取代重造 |
| 插件分发便利 | 5 | 5 | 1 | 5 | 5*，锁版本资源 |
| UE升级维护便利 | 5 | 4 | 1 | 4 | 2*，实验接口/缺陷 |
| 当前角色NPR产品匹配 | 2，参考价值高 | 2，补充价值高 | 3，高成本 | 2，物理层用途 | 4*，首选验收对象 |

**决策顺序：S58原生Generic→Hair映射/目标Tangent→Face入口门槛→原生光照与Runtime/Cook共同验收。** A0并行承担数学对照，不作为全角色生产默认。B仅辅助颜色/边线。S57留作未来物理层。若Face主光近似桥接符合明确冻结的合同，使用S58三slot；若Face需要独立visibility或逐灯分界，只补Face局部后端，不先为全角色建A+灯光。

Hybrid允许身体/Generic与合格Hair走S58，Face走专用路径；它不自动取得完整V1资格。Face A0是研究模式，真实receive/self为硬条件时不能靠它通过。原生Hair不支持独立shift等价时，必须明确采用近似响应、专用Hair扩展或阻断该合同。参数UI显示质量等级与有效域，不让“主后端”掩盖局部降级。

只有在目标版本S58有可重现阻断缺陷、native语义无法标定、Face/Hair硬需求无法桥接且不可接受降级时，才评估A+局部数据提供或C扩展；若改Engine/Shaders即计引擎分支维护。选择C也优先复用原生Toon接入，补缺少的输入/瓣，而不是默认重新设计整个renderer。

### 11.2 Plan C 的实际工作量

Legacy 自定义模型至少审查：ShadingModel 注册/枚举与序列化、Material 编译与输入、BasePass 输出、GBuffer CustomData 编码/解码、逐灯 BxDF 分派、shadow attenuation 接入、反射与间接路径、Debug View、Forward/Mobile permutations。常见相关区域包括 `ShadingModels.ush`、`DeferredLightingCommon.ush`，但具体文件与分支以锁定源码为准。[官方 ShadingModel 枚举](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/EMaterialShadingModel?lang=en-US)

进入逐灯阶段可以利用引擎已解析的灯方向、衰减与阴影因子，而不是每个材质自己查 VSM；然而 GBuffer 空间有限，新增参数需要压缩、索引表或额外存储。既不能任意覆盖其他材质的通道，也不能默认把复杂 Face 资产和每像素 T 都免费搬进去。

Substrate 自定义 BSDF 则需要 closure 类型、编译翻译、pack/unpack、直接/环境求值与 deferred/forward 分派。Lumen 还需决定：间接采用 PBR surrogate，还是扩展 surface cache/hit lighting/反射以匹配 NPR。仅新增直射函数不保证所有路径一致。V1 若被迫走 C，先限定一个平台、一个 renderer、一个 CL，单独立项；插件分发将依赖定制引擎和版本矩阵，升级需要回归、合并冲突和重编译。

### 11.3 A+：无引擎分支的受控主光阴影候选

纯插件还可以自己生成阴影，而不是偷读 UE VSM。用正交 SceneCapture2D 捕捉一个主光方向下的受控区域深度；以相同 light-space 矩阵把世界位置映射到 RT。官方有正交投影与 SceneDepth/DeviceDepth capture source，具体输出单位和捕获时序必须探针。[SceneCapture2D](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USceneCaptureComponent2D)、[CaptureSource](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/ESceneCaptureSource)

若保存沿捕获相机前向的线性深度 \(z_s\)，接收点深度为 \(z_r\)，则
\[
v_{custom}(p)=\frac1K\sum_j \mathbf1[z_r(p)-b(p)\le z_s(u(p)+o_j)].
\]
初始 1024²、4 taps PCF、固定包围区；阴影相机 origin 做 texel snapping。reversed-Z device depth 与线性厘米不得混比。frustum 外记录 Unknown 并采用显式 lit 回退，不能标为正确无阴影。捕获必须覆盖角色、刘海和受控遮挡板，保持动画姿态/WPO/Masked alpha 一致。

这可能解决局部动态 receive/self，但新增 acne、peter-panning、精度、时序、捕获开销与范围限制。它是一个额外渲染子系统，**不直接等价原生多光/VSM/Lumen**。只有探针通过才进入 V1；不为坚持 plugin-only 无限扩展自制渲染器。

### 11.4 Outline 技术选择

| 项目 | Inverted Hull | 后处理深度/法线边线 |
|---|---|---|
| 宽度 | 世界位移会随距离变；可投影补偿 | 天然用像素邻域控制，受内部渲染分辨率/TSR 影响 |
| 骨骼成本 | 额外网格绘制、蒙皮/变换及 overdraw | 额外 CustomDepth/Stencil 可有几何成本，后处理按像素付费 |
| 外轮廓 | 适合 | 适合 |
| 内轮廓 | 仅靠壳体难处理，需要专门几何/材质边界 | 深度、法线、材质 ID 边界可检测，但可能过度描出皮肤细节 |
| 遮挡 | 依正常深度测试，壳体膨胀可穿插 | 必须比较 SceneDepth 与 CustomDepth，避免透墙和隐藏边 |
| 时间稳定 | 网格/法线质量主导 | jitter、低分辨率、邻域采样和历史重投影主导 |
| 风格 | 漫画外边线，适合 V1 | 更适合内结构、对象边界差异化 |

V1 采用单独的简化描边网格与材质，不把描边塞进 Core。使用专门平滑的 outline normal，避免 UV/硬法线缝撑开；复用骨骼姿态并检查 LOD。外壳只绘制合适背面并保留深度测试，关闭其额外投影/接收需求；两面材质加背面过滤只是实现选项，仍需测 masked overdraw，不能假定 UE 有任意反剔除开关。

透视近似：在相机空间 \(p=(x,y,z),z>0\)，屏幕像素映射为 \(s_x=f_xx/z,s_y=f_yy/z\)。沿视空间方向 n 外推 d 时
\[
J_pn=\left(f_x\frac{n_xz-xn_z}{z^2},f_y\frac{n_yz-yn_z}{z^2}\right),\quad
d\approx\frac{w_{px}}{\max(\|J_pn\|,\epsilon)}.
\]
这比单纯乘 camera distance 更有定义，但在投影方向退化、凹形和近裁剪处仍须 clamp；一阶近似不保证所有位置精确像素宽。\(f_x,f_y\) 必须使用实际输出/内部目标分辨率并约定换算；正交相机另算，不沿用透视式。V1 验收以中景 1–3 px、多个距离误差≤1 px 为目标，极近镜头列为限制。用户的 outlineWidth 是目标宽度，不是绝对保证。

V1.1 再增加 stencil 选角、深度/法线联合检测与时间稳定策略。两种描边可以由 Style Policy 选择，但混用时要避免同一轮廓双重加粗。

## 12. 对标成熟系统与差异化

### 12.1 lilToon

lilToon 已有分组界面、最多三层艺术阴影、阴影范围/模糊、独立阴影法线强度与平面化遮罩；这些与本项目初始参数有大量重合，不能作为新功能声称。[官方 Shadow 文档](https://lilxyzw.github.io/lilToon/ja_JP/color/shadow.html)

其各向异性具有两轴宽度、offset、noise 等控制；Rim 支持范围/模糊、法线响应和灯光侧区分；MatCap 还考虑 UV、相机滚转与头发表达。说明角色风格材质已经有成熟艺术自由度。[Anisotropy](https://lilxyzw.github.io/lilToon/ja_JP/reflections/anisotropy.html)、[Rim](https://lilxyzw.github.io/lilToon/ja_JP/reflections/rimlight.html)、[MatCap](https://lilxyzw.github.io/lilToon/ja_JP/reflections/matcap.html)

优化路径包括去除未用功能、烘焙图层、Lite/变体及共享源码组织，预设是其现有工作流。值得学习的是按场景裁剪；不能仅因功能多就推断其性能差或参数不可用。[优化](https://lilxyzw.github.io/lilToon/ja_JP/other/optimization.html)、[Shader Structure](https://lilxyzw.github.io/lilToon/ja_JP/dev/shader_structure.html)

### 12.2 Unity Toon Shader

UTS 以 Base/1st Shade/2nd Shade 颜色与 Step/Feather 为核心，分开系统阴影与艺术分界；高光和 Rim 有自己的遮罩/光向控制。法线还可分别作用于不同效果。这是清楚的动画制作语义，本项目应进行受控任务对比，而不是仅比较滑条数量。[UTS 官方仓库](https://github.com/Unity-Technologies/com.unity.toonshader)、[Step/Feather](https://github.com/Unity-Technologies/com.unity.toonshader/blob/master/com.unity.toonshader/Documentation~/ShadingStepAndFeather.md)、[Normal](https://github.com/Unity-Technologies/com.unity.toonshader/blob/master/com.unity.toonshader/Documentation~/NormalMap.md)

Angel Ring 是摄像机相关的头发投影表达，需要第二 UV，不等于受 T/L/V 驱动的完整各向异性散射。UTS 还有 MatCap、高光与多管线样例；管线支持并不意味着各自功能完全一致。[Angel Ring](https://github.com/Unity-Technologies/com.unity.toonshader/blob/master/com.unity.toonshader/Documentation~/AngelRing.md)、[Highlight](https://github.com/Unity-Technologies/com.unity.toonshader/blob/master/com.unity.toonshader/Documentation~/Highlight.md)

其属性模板与 shader generator 支持多个变体共享参数来源，值得借鉴为本项目 Schema 生成器。当前取证不足以声称它与 lilToon 拥有相同的任意个人预设数据库。[UTS Generator](https://github.com/Unity-Technologies/com.unity.toonshader/blob/master/com.unity.toon-graphics-test/Editor/README_UnityToonShaderGenerator.md)

### 12.3 MToon 与 UE 原生 Toon Profile

MToon 1.0 已把 Toon 材质写成版本化资产规范：颜色、明暗偏移、GI equalization、Rim/MatCap、世界/屏幕描边、单位与通道均有约定。最值得学习的是**参数作为资产交换契约**。本项目的「保留 PBR 字段 + NPR 语义」已有实践先例，不是单凭命名就有新颖性。[MToon 1.0 规范](https://github.com/vrm-c/vrm-specification/blob/master/specification/VRMC_materials_mtoon-1.0/README.md)

UE 5.8 ToonProfile 也削弱了「Ramp + 预设资产化」的独立差异化。项目应在这些后端之上提供材质语义、可测角度参数、Face/Hair 资产协议与验证，而不是与原生功能重复造轮子。

| 维度 | lilToon | UTS | MToon | 本项目应证明的改进 |
|---|---|---|---|---|
| 参数组织 | 丰富功能组、层、预设 | 动画色组与局部效果 | 版本化统一规范 | 数学单位、测量方法、来源追踪 |
| 阴影 | 多层与接收策略 | 系统/艺术阴影分开 | 标准化颜色与分界 | terminator/visibility 严格分离 |
| 高光/头发 | 各向异性、MatCap | 高光、Angel Ring | 基础表达 | 角半宽与移位方向场契约 |
| Rim/MatCap | 多种灯光/相机策略 | 艺术控制丰富 | 可交换参数 | 明确外观合成层和混合算子 |
| 性能/变体 | 裁剪、Lite、共享 pass | 模板与管线变体 | 依实现 | 有限变体清单与实测预算 |
| 扩展性 | 成熟生态与源码扩展 | 多管线集成 | 规范互操作 | Material×Style×Backend 兼容诊断 |

合理差异化是用户提出的几项的**联合可验证结果**：统一语义 + 二维组织 + 角色特化 + 风格资产 + UE 工作流 + 实验评价。它足以成为优秀技术美术/图形系统作品，但不能在没有对照实验、消融和新理论结果前宣称学术首创或全面优于现有系统。

## 13. 资产管线、正式数据模型与 V1 模块

### 13.1 Controlled Character Asset Schema

资产规范属于模型输入契约，必须在材质美化前建立。建议 `AssetSchemaVersion=1`，记录 DCC/导入设置、单位、授权来源、贴图版本和验证状态。

| 字段 | V1 规定 | 检查与失败处理 |
|---|---|---|
| FaceForward/Right/Up | 第 5 节语义轴与校准旋转 | 正交、单位、固定手性；六方向标记图人工确认 |
| HeadBone | 名称与最终动画姿态来源 | 不存在则拒绝 Face；不可用 Actor 旋转隐式替代 |
| Transform | 旋转中去缩放；V1 不支持负缩放 | 非均匀缩放须修复或明确验证变换方案 |
| FaceUV | 专用规范图、中线 u=0.5，原点与翻转元数据 | 有 atlas 时先转回 face-local UV，不能对全 atlas 做 1-u |
| FaceField | CriticalAzimuth，数值大代表较晚转暗 | 真正距离场拒绝套用；图集/多场使用另外 semantic/version |
| Mirroring | 一侧来光完整脸图，或明确双侧图 | 检查非对称脸；每侧端点和接缝验证 |
| HairDirection | 根到梢、切向投影后非零 | 检出 T≈N；UV 镜像后保留根梢，不随机翻向 |
| NormalMap | Tangent Space，Y/绿通道约定记录 | UE 导入转换只执行一次；检查 TBN 手性 |
| MacroNormal | 代理烘焙/引导方向场及蒙皮方式 | 与顶点法线正半球一致；缺失则禁止宏观抽象 |
| MaterialSlots | 显式 Generic/Face/Hair 映射 | 不凭文件名猜测；每个 slot 有绑定记录 |
| LOD | FaceUV/方向场/材质身份保持语义一致 | LOD 动画回放；禁止旧 SDF 跨 UV 版本复用 |
| OutlineData | 独立平滑方向及宽度遮罩 | 不占用已给 Hair/MacroNormal 的通道而无声明 |

纹理约定为项目选择，并不是 UE 强制格式：BaseColor/颜色自发光纹理按 sRGB 解码；ORM 为线性 R=AO、G=Roughness、B=Metallic；NPRMask 为线性 R=specMask、G=rimMask、B=outlineMask、A=macroBlendMask。FaceField 独立线性灰度图，建议先用高精度参考格式检验，再比较可压缩格式；不能在未测试前承诺 BC4/8-bit 足够。HairFlow 为线性二分量切线平面方向，解码后单位化并变换；通道清单若变化必须迁移 Schema。FaceField、Flow、Normal 与 ORM 不应共用 sRGB 颜色压缩设置。

MacroNormal 可以用额外 UV/顶点属性/纹理编码，但其**所在坐标空间、蒙皮方式和资源成本**必须明确。静态物体空间图不能只乘 Actor 旋转来追随任意骨骼变形；V1 的受控资产可将脸部引导场绑定 Head frame，其他区域使用经过蒙皮的顶点方向或限制为无宏观抽象。Normal、HairDirection 和 OutlineNormal 是三种用途，不能因「都是向量」就复用后不说明。

可自动化：按已给映射绑定 profile、检查数据字段/纹理设置、生成 MI、解析风格初值、展示诊断与保存 preset。不可可靠自动化：理想面部色块设计、不同拓扑的 Face 场重建、作者的梳理方向、哪些几何细节应抽象、非对称脸的镜像及最终美术判断。因此未来「导入→快速套风格」成立的前提是资产遵循协议，不能宣传成任意角色零制作成本。

### 13.2 参数定义与数据所有权

推荐结构如下，USTRUCT/UCLASS 名称为设计接口；具体属性宏和 API 签名在目标引擎实现时核验。

| 类型 | 必须存储内容 | 责任 |
|---|---|---|
| `FNPRParameterDefinition` | 稳定 ID、单位、数学域、UI 软范围、默认值、影响对象、适用 profile、别名与迁移版本 | 唯一语义来源 |
| `FNPRCoreParameters` | Diffuse、Specular、Normal 子组；Rim 独立 Appearance 子组但提供旧 UI 别名 | 存 canonical 数值而非含糊滑条 |
| `UNPRMaterialProfile` | ModelID、输入槽、方向语义、默认值、支持参数与后端、硬约束 | 定义材质解释器 |
| `UNPRStyleProfile : UPrimaryDataAsset` | Schema/Model 版本、默认 patch、响应族、Curve/LUT 引用、颜色/光照/Outline/MatCap policy、材质 patch、所需 capabilities | 风格 authoring |
| `UNPRCharacterBinding` | SkeletalMesh、slot→profile、HeadBone/校准、FaceUV/field、方向场、实例 patch | 资产事实与绑定 |
| `UNPRPreset` | Style/Material/Binding 引用、稀疏 patch、链接/快照模式、解析器版本 | 可复现方案保存 |
| `FNPRResolvedMaterial` | 验证后参数块、variant ID、资源引用、provenance、hash、降级清单 | 唯一 MI/MID 写入源 |
| `FNPRBackendCapabilities` | 主光、visibility、GI、反射、雾、模型入口、实例切换等支持等级 | 阻止静默失真 |
| `FNPRBackendMappingEntry` | SemanticID、TargetProperty、TransformID、Exact/Approx/Unsupported、有效域、联动、误差与证据 | 按参数审核映射 |
| `UNPRBackendStylePackage` | canonical源hash、Backend/patch/CL/GBuffer、Profile与MI bundle、cvar、资源manifest、标定与质量报告 | 后端编译产物，不替代Style |

Data Asset 是 CPU/编辑器配置源；Primary Data Asset 还可参与 Asset Manager 管理，**不是自动 GPU 解释器**。[Epic Data Assets](https://dev.epicgames.com/documentation/unreal-engine/data-assets-in-unreal-engine)

canonical 数值使用弧度/明确的线性倍率，编辑器可显示角度。示例字段：`TerminatorDotCenter[-1,1]`、`TerminatorDotHalfWidth[0,0.5]`、`SpecHalfAngleRad[1°,60°]`、`SpecEdgeSoftness[0,1]`、`SpecPeakScale≥0`、`NormalDetailStrength[0,1]`、`MacroNormalBlend[0,1]`；Appearance 内 `RimGrazingAngleRad`、`RimPeakScale`；Face 内 `AngularBiasRad`、`AngularHalfWidthRad`；Hair 内 `ShiftAngleRad[-20°,20°]`、`StrandAnisotropy[0,1]`。外部保存角度时必须带版本和单位转换，不能混入 q 域比例。

### 13.3 覆写顺序与解析算法

原顺序 `Global→Style→Material→StyleMaterial→Instance` 可能让 Material 默认值盖掉 Style。推荐：

\[
\boxed{SchemaDefaults\to MaterialDefaults\to StyleDefaults
\to StyleMaterialPatch\to InstancePatch\to RuntimeTransientPatch}
\]

随后进行域验证、支持检查、资源解析、变体选择和 provenance 生成。每个 patch 字段都有 `isSet`，0 是合法显式值，不能用它表示「没有覆写」。硬域由 Schema/Material/Backend 共同约束；Style 推荐范围只控制 UI，超出时诊断，不默默夹紧。非法数值、缺资源和不支持的渲染策略应产生可定位错误；有意可降级功能按预先声明映射解析为 △。

资产事实独立于上述参数链：HeadBone、UV 原点、SDF semantic、根梢方向不能被 Style 覆写成另一种编码。UI 的 Reset 删除当前 patch 字段，恢复继承；保存链接 preset 保留引用，保存快照 preset 冻结最终值并记录源 hash。切换 Style 应保留明确 instance patch，同时列出其造成的差异，避免「新风格看起来没变化却不知道为何」。

不同角色用独立 MID/绑定参数，不将头骨 F/R/U 放进共享 MPC。MPC只含Face艺术key/reference需要的共享方向及诊断参数；S58的原生多灯/Sky/Lumen不经MPC重建；按 World 管理，不用进程全局静态变量，以免多 PIE world 串扰。动画姿态更新后计算 Face 派生量并一次提交，每帧只更新变化值。CPD 可列未来优化，但在 SkeletalMesh 路径测过前不承诺自动合批。[MPC 5.7](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-material-parameter-collections-in-unreal-engine?application_version=5.7)、[CPD 5.7](https://dev.epicgames.com/documentation/en-us/unreal-engine/storing-custom-data-in-unreal-engine-materials-per-primitive?application_version=5.7)

### 13.4 模块、后端适配与数据流

`NPRRuntime`：版本化语义结构、Material/Style/Binding/Preset资产、Resolver、BackendCapabilities、CharacterBindingComponent、预生成bundle加载与MID提交。只为Face/key/reference提供轻量WorldLightBinding；不依赖UnrealEd，也不在运行时编辑ToonProfile Settings。

`NPREditor`：资产校验、canonical Curve整理、`NPRBackendSubstrateToon`编译器、Profile/MIC生成与hash去重、标定/映射质量报告、有限变体清单、Preset/Details与证据导出。Curve转LUT只用于确有纹理求值需求的模块；原生Profile曲线由目标实现负责GPU资源生成，不另造重复LUT系统。

`Content/Backends/S58`：`M_NPR_S58_Generic`、`M_NPR_S58_Hair`、经FaceGate确定的`M_NPR_S58_FaceBridge`或专用Face材质。直接将PBR字段、预处理Normal/Tangent及可选PatternUV桥接送Toon BSDF；禁止把完整reference光照接BaseColor/Emissive再叠加。MI通过已公开的ToonProfile override分别引用风格资源；不同Profile默认共用Master。

`Content/Backends/Reference`：最小`M_NPR_Ref_Generic/Face/Hair`与第4–6节MF/HLSL，用于参数golden/消融/受控演示；可按需单独cook，不让研究对照自动成为全部生产资源的依赖。共享法线与资产解码函数；shader不得读未绑定的私有shadow纹理。独立`M_NPR_OutlineHull`与Appearance策略跨后端复用。

主数据流：Asset/Schema验证→Resolve canonical值→Adapter检查有效域→Editor生成BackendStylePackage与ToonProfile/MIC→Cook/预载/PSO准备→创建每角色MID bundle→运行时选择Style bundle并恢复instance patch→UE native直接/阴影/间接照明→Appearance/Outline/显示。Face每帧仅更新Head派生量/key桥接参数；切Style需要改Profile的值走预生成资源，不每帧重新烘焙。

Backend编译若遇Unsupported必需字段产生错误；可降级字段须在package中记录Approximate及理由。普通运行时数值覆写只有在目标字段/有效域允许时提交；要求改变Profile曲线的自由手调在编辑器生成新variant，V1不承诺在游戏中任意编辑ramp。参考模型与后端拟合结果分别输出，避免将“写入成功”当作语义正确。

## 14. Technical Spike：S58优先、实际证据与后端门槛

### 14.1 环境与未执行状态

本机已读取UE5.8.3（CL58210709）安装树源码；核心坐标/顺序已有SourceVerified证据，但截至该审计Launcher仍在校验、Editor未运行。安装、Editor与工程创建的后续状态以独立ProjectCreation记录为准。以下方案仍**不是渲染结果**，同目录`UE5.8_Toon_技术探针记录.json`的API01等测试初始ExecutionStatus=NotRun、Verdict=null；运行后才填✓/△/✗。源代码审计独立记录，不擅自替测试JSON填通过。

固定UE5.8具体patch/CL/branch、Win64/DX12/SM6、桌面Deferred、Substrate **Blendable GBuffer**、GPU/驱动、分辨率与Cook配置；先确认目标cvar/enum再设值。关闭自动曝光/Bloom/MotionBlur/MegaLights；锁白平衡。Lumen、offset、hatching、rim先关闭；extinction先标定为不透射基线，再单独扫描。灭绝系数0按文档对应零吸收，不能当作“关闭透射”。VSM为首选路径，CSM单独作为兼容测项；不在一条结果中混写。

场景含球/法线条、平面、流向0/45/90°发条、一个角色、原生Lit复制体与A0reference复制体。道具包括灰地面、移动遮挡板、独立刘海/鼻子、方向光+第二方向/点/聚光、均匀Sky、只通过反弹可见的红墙、镜面表面与屏内/屏外反射参照物、雾对照球。所有后端共享几何/相机；先建最小Generic，禁止先完成整套A0再开S58。

### 14.2 执行顺序与产物

1. **源码/API审计**：复用第10.8.3与本机审计中已确认的坐标、offset/透射顺序、直接镜面D×K与编译缺口；继续追踪完整调用图、Profile绑定与版本缺陷。补充来源才能升级对应项SourceVerified，黑箱画面不冒充源码证据；源码同样不冒充RenderPass。
2. **Generic坐标标定**：使用输出颜色可识别的诊断ramp扫描signed NoL、光强/视角、roughness；分离漫反射与镜面测试。记录负q是否折叠、每灯累加、峰值/角宽联动，生成QualityReport而非只存一组preset。
3. **原生阴影**：固定FaceField/法线，依次cast、遮挡板receive、鼻子/刘海self。每项VSM/CSM独立记录。IncludeShadow开/关配三阶假色ramp观察边界；hatching为零，extinction保持已标定不透射值，source size/过滤/偏置固定。验证它是否改变ramp坐标而非只在ramp后乘暗，再分别扫extinction和距离。
4. **Sky/Lumen/间接镜面**：只经反弹的红墙隔离GI；移动屏内/屏外物体验证surface reflection。分别开关间接scale、量化cvar、Repetition=0/非零；SW/HW Lumen与screen traces记录所用路径。
5. **Hair门槛**：0/45/90°方向测试；扫描角宽/softness/anisotropy，比较双截面；检查shift送T后是否消失，旋转N/T是否污染diffuse。冻结native近似域或判该合同不合格。
6. **Face门槛**：reference旋转/镜像/顶背光测试；单灯PatternUV identity-offset桥接；再加第二灯与遮挡板，测试double terminator、clamp、共享PatternUV副作用、真实shadow组合。桥接不合格仅为Face评估A+/C或明确research模式。
7. **Runtime与Cache**：Profile_A/B、共享Master的MIC_A/B及预载MID bundle切换；独立测试冷/热DDC、重启、编辑器/cook、首次/预热PSO。不得修改全局用户DDC；使用隔离测试缓存配置。

产物包含环境manifest、源码定位/表达式、材质和Profile快照、线性HDR诊断/最终截图、GPU/CPU capture、缺陷重现步骤与逐项状态。不可公开改写的Epic源码只保留授权范围内引用与定位。测试装置无法隔离因素时记录NotRun/需要补装置，不用“基本可用”。

### 14.3 能力与参数判定标准

下列为建议验收初值，预实验后冻结。native路径无raw debug时，可用GI/其他光全关闭的差分图与诊断ramp；不得假装普通材质图能直接读取native visibility。

| 能力/优先级 | ✓标准 | △允许的明确限制 | ✗条件 |
|---|---|---|---|
| 主方向光/必须 | 六轴/key方向误差<1e-3；native旋灯按冻结ramp响应，光强0则direct为0 | 无 | 方向反相/响应不稳定 |
| Cast/必须 | 动画投影正确；与Lit几何阴影轮廓IoU>0.95，忽略过滤边缘 | 仅冻结Opaque/Masked与shadow配置 | 无投影/姿态错误 |
| Receive/必须 | 移动板使隔离direct降至无遮挡的5%内，移开恢复差<2% | 仅通过的VSM或CSM路径 | 不随遮挡变化/漏光 |
| Self/必须 | 固定Face场时刘海/鼻运动改变真实遮挡；持续acne/脱离区<面部1% | 受控资产/明确bias范围 | 艺术mask冒充真实遮挡 |
| VSM、CSM/分别实测 | 上述cast/receive/self分别通过；过滤宽度/接触误差≤冻结预算 | 一种路径通过另一种明确不支持 | 主要发布路径失败 |
| IncludeShadow/必须测试 | on/off的假色ramp变化与审计表达式一致；遮挡后边界可预测 | 限定SoftShadow/距离/路径 | 无效果、错误范围或与承诺不符 |
| 多灯/可降级 | 灯可独立开关，directional/point/spot累加符合记录的native策略 | Face只艺术key、其他灯限制单独声明 | 宣称逐灯Face却使用同一个key mask |
| Sky/希望 | 黑/均匀Sky与IndirectScale变化产生可隔离原生贡献 | 固定Sky更新模式/路径 | 无变化且无声明 |
| Lumen GI/希望或明确降级 | 只经反弹红墙改变角色间接项；scale=0消除该项；Lit对照确认装置有效 | 仅验收的SW/HW模式或明确关闭 | 未测却称Lumen全兼容 |
| IndirectSpec/必须测试，可降级 | 移动物体改变角色反射；scale/量化cvar/repetition行为可独立确认 | 反射可用但quantization关闭或指定路径 | 只以镜中看到角色作证据 |
| AO/希望 | 指定native/material AO路径确有环境遮蔽响应，来源明确 | 仅资产AO；节点无AO入口则不得声称已传递 | 用全彩图乘暗替代所宣称AO路径 |
| Fog/希望 | 同深度NPR/Lit透射率差<0.02，壳线无明显排序错误 | 指定opaque deferred，透明未支持 | 主体或壳线穿雾 |
| 体积光/V1可放弃 | 若宣称Core读取体积光需隔离实测 | 场景雾合成可用、Core不额外读取 | 未测而声称全部支持 |
| Generic参数/必须 | 角宽误差≤max(1°,目标10%)；dot分界误差≤0.02；softness排序单调；峰值漂移≤10%（受保护测试域） | 有明确Approx域与Couplings，可收窄合同 | 不单调/无稳定拟合且仍显示可调 |
| Face/必须 | 第5节资产/旋转测试+冻结生产合同：主光桥接mask RMS≤0.05、边界≤2px、阴影分别通过 | 标为key-only或专用Face后端；不宣称逐灯Exact | 双分界/多灯破坏合同或缺必须visibility |
| Hair/必须 | flow0/45/90°响应正确；双截面宽度单调、size/softness误差符合冻结域 | NativeAnisotropic近似，不支持独立shift时明确显示 | flow无效；承诺独立shift却同时旋转diffuse |
| Runtime/必须 | 两Style各slot与override一致；预热后100次切换无shader编译，资源/PSO开销符合预算 | 首次加载受预载策略约束 | Profile运行时写入/风格混帧/明显hitch |
| DDC/Cook/必须 | 冷/热缓存+重启+打包结果误差≤1/255（固定非随机诊断图）且无缺失Toon宏 | 已确认修复构建/维护方案写入支持表 | UE-388460可重现且无可靠正式解决 |

软阴影、透射/灭绝等艺术策略不一定使黑区完全为0：验证receive装置先使用不透射Profile，再单独验证非零策略，不能用“这是风格”掩盖未接收阴影。RMS/像素阈值均固定相机/分辨率，动态场景另存时间序列。

### 14.4 发布门槛与回退顺序

**S58 Production Candidate Gate**为当前默认：目标CL基础灯光/动态cast-receive-self、Generic标定、合格Hair/Face路径、Runtime/Cook/DDC必须通过；Sky/Lumen/indirectSpec/AO/多灯可按冻结合同逐项降级，不能被未测试默认覆盖。只支持VSM可标△兼容，必须实测并声明CSM结果。

**Strict Reference Semantics Gate**独立判断：若要求Face逐灯精确替换、Hair独立shift和完整角域瓣，当前公开S58原生接口不足，必须局部扩展或如实阻断该严格版本。接受Approx需要在实现前冻结范围与误差，不能验收后改名以掩盖失败。

**A0 Research Gate**保留数学/感知消融用途；receive/self/GI等✗要实测声明，不作为完整生产V1的替代发布标签。回退顺序为收窄已明确允许的native映射域→Face/Hair局部Hybrid→A+有界数据或C最小扩展评审；不恢复“先做全角色A0再可选测S58”的优先级。

## 15. 性能分析与预算

### 15.1 资源预算先于毫秒承诺

S58默认使用UE原生灯光与单native Hair瓣，Profile负责色阶，MatCap关闭。下表只计本项目表面/资产函数的读取预算，**不含**原生Profile GPU查询、逐灯ramp、shadow/Lumen/reflection passes；其真实次数与ALU须目标shader审计/GPU capture，不把A0计数当作S58总成本。

| 路径 | 基础读取 | 特化增量 | 常见总数/限制 |
|---|---|---|---|
| Generic | BaseColor 1、Normal 1、ORM 1、NPRMask 1 | MacroNormal 若纹理+1；Ramp+1；MatCap+1 | 基线 4，目标正常配置≤6 |
| Face | Generic 中实际使用的 3–4 次 | 左右 Face 场双采样+2；MacroNormal 若纹理+1 | 目标 5–7，不使用的 Normal/ORM 应裁去 |
| Hair | 3–4 次基础图 | HairFlow+1；共用 packed mask 不额外计 | 目标 4–6；无默认第二瓣/noise |
| S58 Profile | 不预填 | native ramp/offset/indirect查询待审计 | per-light与间接成本分别统计 |
| 局部A+ shadow | 不变 | PCF 4 taps +独立 capture | 仅选中局部路径后单列预算 |
| Hull Outline | 可无纹理或宽度 mask 1 | 几何/蒙皮/overdraw | 不以 texture count 代表总成本 |

若 Face 双侧字段打包 RG，它们仍可能因 UV 不同需要两次采样；同 UV 的独立通道可一次读取。不能为了标「Face SDF 只+1」而删掉连续切换所需采样。宏观引导法线的纹理成本要如实计入。

A0 reference/Face计算含dot、normalize、sqrt、acos/asin与shift sincos；S58 native lighting由引擎shader负责，不能认为这些reference ALU全部仍在生产Core。反三角是否优化取决于实际编译与瓶颈。方向光 Face 的 q/side/pole、均匀 shift 的 sincos 可由 CPU 预算。像素曲线通过 LUT 或解析表达，不逐像素执行 UObject Curve。必要的近似优化要与数学 reference 比较，初始最大角误差≤0.25°、响应误差≤0.01；这不替代最终视觉测试。

### 15.2 变体与编译控制

Generic/Face/Hair 分成三个 Master，避免巨型 Master 用动态分支运行七类材质。Material Function 主要提供组织/复用，编译后通常可内联；不保证像 CPU 函数那样只付一次调用成本，重复调用可能重复计算。应检查编译结果。[Custom Expressions 5.7](https://dev.epicgames.com/documentation/en-us/unreal-engine/custom-material-expressions-in-unreal-engine?application_version=5.7)

Static Switch 可裁掉分支，但 n 个独立开关理论上产生 \(2^n\) 组合，再叠加平台/质量/pass。运行时普通数值修改不改变静态参数。两种Style若仅普通数值/Profile资源不同，优先共Master/静态配置；是否共shader/PSO以目标构建证据为准。原生Profile曲线不默认生成一个static switch。选择响应族使用受控有限枚举。[Material Instances 5.7](https://dev.epicgames.com/documentation/en-us/unreal-engine/instanced-materials-in-unreal-engine?application_version=5.7)

初始每 Model 最多 4 种获准配置，总产品配置≤12；这不是 shader binary 数量上限。Cook 清单列出实际使用组合，禁止动态加载未 cooked 的任意组合。动态分支不保证省采样，尤其屏幕内发散或包含导数时；优先离线配置，而不是每像素读取 ProfileID 再运行所有模型。

MPC 更新适合共享广播；只更新数值，修改字段布局会触发重编译。官方限制单材质最多引用两个 MPC，每个集合最多 1024 scalar 与 1024 vector；项目只应使用少量共享字段，不把此上限当作推荐规模。每角色 MID 更新缓存参数标识并批量提交，避免每帧遍历全部资产。Data Asset 本身不增加 GPU 成本，其选择的纹理、变体和 pass 才决定 GPU 成本。[MPC 5.7](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-material-parameter-collections-in-unreal-engine?application_version=5.7)

### 15.3 测量协议与初始时间目标

在 V0 指定一台真实 GPU/驱动/RHI 后冻结预算：1080p、单角色屏高约 70%、三个材质 slot、相同几何/灯/相机对照；缓存预热后至少 300 帧，报告 GPU 中位数与 p95，CPU/Game/Render thread 分开，避免把编辑器编译噪声算进 shader。初始建议新增 NPR 表面开销≤0.5 ms、描边≤0.5 ms、总增量≤1.0 ms；**未指定硬件前这些只是候选门槛**。若 A+ 被选中，额外 capture 与 PCF 需单列预算，不挤进原数字后假装免费。

基准至少含同几何原生Substrate Lit/Slab、最小S58 Toon、完整S58+语义函数，以及A0reference。对比最小S58能隔离适配/资产函数增量，对比Lit能估计native Toon整合差异；Profile GPU表、逐灯ramp、阴影和Lumen均纳入总量。不同渲染路径不可仅比较一项总帧时。记录 Platform Stats、采样/寄存器、draw/pass、overdraw、渲染分辨率；Shader Complexity 只是诊断图，不是性能报告。

Runtime切换初始建议：预载后100次切换无HLSL编译、无PSO Miss/TooLate，额外CPU/GPU切换增量p99≤2ms；最终阈值在指定GPU后冻结，首次加载成本另记。

额外验证 50%/100%/150% 渲染分辨率与 3 个重叠角色，定位 fill-rate、壳体和 masked hair 成本。Outline 带额外几何 pass，CustomDepth、Shadow 和 SceneCapture 也必须纳入总量。远景亚像素高光与发片边缘可能受 TSR 影响，比减少几个 ALU 更重要。

## 16. 验证计划与本次已有证据

### 16.1 数学 reference 的可复现验证

本次运行了 Python 标准库、固定随机种子的双精度标量抽查，16/16 项通过。结果见同目录 `数学验证结果.json`；交付脚本为 `verify_reference_math.py`。包含：GGX 半峰推导与临界边界、角域高光 50% 位置、移位 frame 正交性、Hair 的 a=0/a=1 恒等与主截面宽度、各向异性展宽单调性、Face 前后端点/全周闭合、同旋不变以及垂直光回退。

验证使用合成连续 Face 场，不是真实角色纹理；未模拟 fwidth、float32/half、压缩、mip、骨骼帧序、UE 材质编译、VSM、TSR、GPU 性能或用户感知。它支持公式的一致性与反例定位，不能支持「插件运行成功」。正式 shader 实现必须再对照该 reference。

独立复核已经修正三类容易误判的问题：软可见度乘积比 min 更暗；GGX 临界等号是 90° 边界解；Hair 横向半宽关系只适用于 raw lobe，不能直接套到门控后的最终截图。Face 左右切换则使用归一化水平投影，避免混合角带随俯仰漂移。

### 16.2 工程与视觉验证

核心验收分三层：raw masks/analytic lobe、组合后的线性 HDR、最终 tone-mapped 图像。遮罩错误先在第一层查；灯强/配色在第二层查；曝光/雾/边线/TSR 在第三层查，避免用调色遮盖方向场错误。

必须保存以下固定用例：球上参数六档、正/侧/背光 Face、头灯同旋、垂直光、T 根梢翻转、T≈N、L≈−V、anisotropy 两端、shift 两端、双面发片、UV 镜像、LOD 切换、两角色独立转头、低分辨率远景高光，以及移动刘海/遮挡板。合法输入不产生 NaN/Inf；非法资产产生可定位诊断。

编辑器/打包一致性包括：保存重开资产、链接/快照 preset、切 Style 后 override 保留、Reset 继承、无 runtime shader 编译、干净项目安装插件、cook 后引用完整。Resolver 的幂等性与稀疏覆写属于必要逻辑验证，不为每个简单 MF 节点写镜像实现的单元测试。

两个风格应在同一曝光与资产下明显不同，而不是靠相机后处理大幅调色制造差异；盲测至少 80% 正确区分两个端点，并记录误判。该数字是小规模工程标准，不代表商业游戏风格相似度。

### 16.3 研究消融

对比四组：直接 PBR 字段驱动、传统 threshold 参数、角域/分层语义、再加入 Style 与资产校验工作流。隔离「公式改进」与「工具省操作」的贡献，不能把更好的默认配色全归功于 Principled。第 9 节用户实验在稳定实现后执行，公开失败例和适用条件。

## 17. Implementation Roadmap

依赖优先。人日是假设已有UE5.8环境/受控资产、一名熟悉UE开发者与TA配合的排期预算，未包含引擎安装、资产重制与必要fork；不是工时承诺。

| 阶段 | 交付 | 退出条件 | 初始预算 |
|---|---|---|---|
| V0a理论reference | 第4–6节数学/资产契约与16项标量检查 | 已完成理论部分，GPU未验证 | 已完成研究 |
| V0b S58基础探针 | 固定5.8CL；节点/Profile/Shader审计；Generic、VSM/CSM、Sky/GI/IndirectSpec与DDC | 各测试✓/△/✗或明确NotRun，阻断项定位 | 4–7人日 |
| V0c语义/角色门槛 | Generic标定、Hair流向/瓣、Face桥接、Profile bundle切换 | 明确native有效域、Hybrid需求；生产合同冻结 | 4–7人日，Face扩展另估 |
| V1.0 | S58适配器优先；3Model×2Style、单角色、Schema/Package/Preset/Details、Hull与证据 | 光照/语义/Runtime/Cook/性能通过，近似项透明 | Gate后15–25人日 |
| V1.1 | 已选后端修复/适配范围扩展、远景AA/内边线、第二受控角色 | 既有contract回归不破坏 | 分项估算 |
| V2.0 | Skin/SSS、Eye、Cloth、Metal、物理Slab/更丰富StyleSpace | 新模型先有资产/传输RFC和后端验证 | 分模块立项 |

V1不包含完整Style插值、AI、任意角色自动适配、大型Slate、完整Marschner或默认fork。若Strict Face/Hair合同迫使C扩展，先提交单独成本/分发/版本维护设计，修改阶段预算；不能把它藏在15–25人日工具层估计里。A0只完成必要reference闭环，不先投入完整生产灯光系统。

V1.1处理实测最大失败项与最新CL修复，并重新审计缓存/Tangent/shadow；**StyleProfile→ToonProfile适配属于V1核心，不再推迟到V1.1**。V2的physicality按模型/传输分项控制，不提前承诺单滑条解决全部物理保留程度。

## 18. Risk Register

“已确认”指已有公开结构限制/缺陷证据，目标CL是否触发独立记录；概率为项目判断。每项有触发信号和局部回退，防止用Experimental标签代替分析。

| 风险 | 概率/影响 | 触发条件 | 缓解与Plan B |
|---|---|---|---|
| 5.8安装完整性/首次Editor验收 | 当前未完成/高 | 已有5.8.3源文件但Launcher校验/启动或项目验证未完成 | 源码按CL58210709记录；安装/工程以ProjectCreation为准，RenderNotRun不伪造 |
| Ramp坐标不满足canonical契约 | 中高/高 | 负NoL折叠、外乘子耦合、非单调拟合 | 固定有效域/质量等级；角域reference保留；硬域缺口局部扩展 |
| Face逐灯入口缺失 | 公开接口限制/高 | 多灯共享key offset、双分界、shadow顺序不可补偿 | 单灯桥接探针；冻结key-only近似域或Face专用A+/C；不退全角色A0 |
| Hair独立shift/各向异性等价缺失 | 已有理论反例/高 | 正交化抹去shift、旋N污染diffuse | 明示native近似；严格Hair局部扩展；禁止假参数 |
| Toon Tangent/Anisotropy编译缺口 | 本机入口缺口已确认，画面未验/高 | CL58210709 MIR不传方向输入，或0/45/90°flow无响应 | 记录translator并实测Hair Gate；选确证修复构建或局部维护，不凭pin称支持 |
| Toon VSM/CSM历史阴影问题 | 历史确认，目标CL未知/高 | 模糊/错误transmittance、shadow ramp异常 | 两路径独立测试；对照修复commit与targetCL；主路径失败阻断生产 |
| UE-388460 DDC命中缺宏 | 官方Unresolved，目标复现待测/高 | 冷缓存正常、热缓存/重启/cook错误 | 隔离DDC重现与源码审计；确定修复构建/补丁维护；不把强制重编当生产策略 |
| Profile不可runtime改/PSO首次hitch | API限制已确认/中高 | 动改Settings无效；换资源突卡/风格混帧 | 预生成MI/Profile bundle、预载/PSO、保留override；任意ramp编辑留Editor |
| Native Lumen/AO路径差异 | 中/中高 | 某SW/HW/screen模式无对应贡献 | 固定配置和测试；独立标GI/AO/Reflection等级，别把支持公告当全模式验收 |
| Hybrid外观/照明不一致 | 中高/高 | Face与body GI/影色不一致 | 先S58桥接；专用Face限制写package并验收；A0仅研究模式 |
| A0 Emissive影响Lumen | 中高/中高，仅ref/hybrid | 暗室reference异常溢光 | 参考隔离场景/配置；不用于解释S58正常native GI |
| A+局部阴影范围/时序/性能 | 条件启用高/高 | capture延迟、frustum漏遮挡者 | 有界单key与全成本测试；失败进入C评审或阻断合同 |
| Face非嵌套/镜像资产 | 高/高 | 分界多次亮暗、左右端点不闭合 | 重制受控场；双侧数据/图集V2，不用参数掩盖资产错误 |
| Hair方向/LOD/根梢错误 | 高/高 | 接缝翻转/条带断裂 | 显式flow与人工梳理；LOD方向验证 |
| MacroNormal蒙皮/坐标错 | 中高/高 | 动画引导场脱离 | 记录空间/蒙皮；无引导数据禁用 |
| Principled只停留命名 | 中/高 | 排序失败、保护属性泄漏、耗时无改善 | 标定/消融/5–10人实验；收窄有效声明 |
| Profile/变体资产膨胀 | 中/中高 | 每角色复制ramp，cook/编译超预算 | hash去重、共享Master、有限manifest；区分Profile数与shader变体数 |
| Outline/MaskedHair/TSR问题 | 中高/中高 | 壳体裂缝、距离宽差、亚像素闪烁 | 专用outline方向/LOD、完整pass预算、远景测试 |
| 数据覆写/版本证据漂移 | 中/高 | 切Style无效、API与target不一致 | isSet/provenance/hash、CL/source semantic version、重跑证据 |
| 范围扩大/单资产过拟合 | 高/高 | Gate未决开始Eye/AI；换球/发条语义崩 | Gate限制，合成对照；V1.1第二资产 |

最大资产风险仍是FaceField/HairDirection承载无法从BaseColor恢复的艺术语义；最大技术风险已从“Unlit缺全部visibility”转为“S58是否保住语义合同、目标CL是否可靠”。平台复用不消除参数和资产研究。

## 19. Recommended V1.0 Architecture

\[
\boxed{\text{Semantic Framework}+\text{S58 Primary Candidate Adapter}
+\text{Face/Hair Capability Gates}+\text{A0 Mathematical Reference}}
\]

**推荐主后端：UE5.8 Substrate Toon，固定patch/CL与Blendable GBuffer。** Generic先复用native直接/多灯/阴影/Sky/Lumen/镜面，再标定canonical参数；Hair以流向正确的native各向异性候选拟合，明确Approx范围；Face优先探测合法offset桥接，严格需求不足时只选局部Hybrid/扩展。默认不建全角色自有shadow/GI/reflection，也不在Lit上叠加整套reference lighting。

**理论层：**第4–6节signed diffuse、angular spec、CriticalAzimuth Face与Hair angular模型保留，作为Parameter Contract、golden、感知验证和反例。Native近似响应须有ModelID/MappingQuality与有效域，不能篡改reference公式制造“等价”。Rim/Color/Outline保持独立策略；native阴影组合由审计与探针描述，不凭名字写成reference乘积。

**数据与Runtime：**canonical Schema→MaterialDefaults→Style→StyleMaterialPatch→Instance→Transient→Validate；Editor Adapter输出版本化BackendStylePackage、不可变ToonProfile与共享Master的MIC_A/B。Cook/预载后创建MID bundle，普通参数可动态更新，Profile差异通过切bundle；Head参数逐角色，MPC只供Face/key/reference，原生灯光由UE负责。

**V1范围：**Core语义、Generic/Face/Hair、两个抽象Style、一个受控角色、DataAsset/Curve/Preset/MI/Details、独立Hull与能力验证。它是目标范围，当前尚未通过生产验收；Face/Hair硬条件失败不能降格后仍宣称完整原范围。A0仅研究/reference资源，A+/C仅在明确缺口下立项。

### 19.1 最终二十项结论索引（修订）

| 问题 | 明确回答 |
|---|---|
| 1 PBR→NPR是否成立 | 语义复用/响应重映射成立；整体含新自由度，非严格双射 |
| 2 Core最佳模型 | 第4节作为reference；S58使用native响应经适配，不宣称数学相同 |
| 3 十维参数 | 保留定义/别名，几何/外观分层；新增Backend质量与有效域 |
| 4 Face公式 | 第5节临界角场+连续封套/镜像+真实visibility若后端提供；S58逐灯入口不足 |
| 5 Hair公式 | 第6节正交shift frame+角域度量+gate；native GGX近似须标定 |
| 6 Material×Style | 合理，增加Backend/Asset能力条件 |
| 7 Style存什么 | canonical默认/曲线/策略/覆写/能力/版本；ToonProfile引用属于编译包 |
| 8 plugin-only能什么 | 5.7 A0缺native visibility/GI；5.8 S58可复用原生Toon管线，但无任意Face/Hair回调 |
| 9 动态阴影 | S58原生接入优先，目标VSM/CSM逐项实测；局部A+/C处理严格缺口 |
| 10 A/B/C/S选择 | S58首选生产候选；A0数学reference；B补充；A+/C条件局部扩展 |
| 11 Substrate角色 | S58为V1lighting backend，S57 Slab为未来物理层用途 |
| 12 Outline | V1独立Hull，屏幕宽度限制声明；内部边线V1.1 |
| 13 V1范围 | 3Model×2Style+受控角色+语义数据/工具；生产资格须模型/lighting/runtime各门槛 |
| 14 Pass/Fail | 第14节与探针JSON；未运行NotRun不改为✓或✗；Strict语义单独判断 |
| 15 V1.1/V2 | 先native缺陷/标定/AA/第二资产，后Skin/Eye/Cloth/Metal和StyleSpace |
| 16 技术最大风险 | Face逐灯入口、Hair shift/目标Tangent、shadow/UE-388460与映射联动 |
| 17 资产最大风险 | Face场/镜像、Hair方向、MacroNormal蒙皮 |
| 18 研究价值 | 语义契约与native映射、参数可测性、Face/Hair抽象、感知/工作流消融 |
| 19 工程内容 | Adapter/Profile/MIC生成、Resolver、bundle、cook/PSO、资产校验与Details |
| 20 差异化/作品集 | 引擎已有Toon之上可复现语义工具，3×2对照、质量报告、失败例与TA实验 |

## 20. Implementation Backlog：可直接分派的任务

P0阻断生产候选定型，P1发布必需，P2条件扩展/后续。任务编号保留，原T10从可选探针提升为核心Adapter；T25–T28补充源码、标定、Runtime与缓存。排列按ID展示，不代表执行顺序。CL58210709核心源码已审计，完整调用图/渲染验收仍待继续；安装与工程创建状态以独立ProjectCreation记录为准。

| ID/优先级 | 输入 | 输出 | 依赖 | 验收标准 | 风险 |
|---|---|---|---|---|---|
| T01/P0 目标环境与合同 | 5.8安装/授权源码、工程、GPU、第14节 | patch/CL/RHI/Blendable配置、生产/严格语义合同 | 无 | 环境可运行；必须/降级项冻结；只5.7时保持NotRun | 配置/版本漂移 |
| T02/P0 语义Schema | 第3–6/13节 | canonical ID/单位/域/别名、QualityEntry/Package定义 | T01 | 同名语义不换；isSet保证0合法；后端域独立 | 隐藏假映射 |
| T03/P0 资产绑定 | 受控角色/纹理/骨骼 | Head/UV/FaceField/HairFlow/Normal协议及诊断 | T02 | 数据空间/镜像/LOD明确，非法资产定位 | 艺术语义不能全自动查 |
| T04/P0 reference samples | 数学脚本/JSON与第4–6节 | CPU/float32及最小A0对照材质 | T02 | golden无NaN，角误差≤0.25°，不扩A0成生产lighting | GPU精度/AA差异 |
| T05/P0 Face KeyBinding | DirectionalLight、World/Head | key方向与每角色Head提交器 | T01 | 六轴误差<1e-3，无跨World污染/一帧延迟；不重建native多灯 | MPC串扰 |
| T06/P0 S58 Generic最小探针 | 节点/Profile API、球/平面 | 原生Toon材质、假色ramp、隔离诊断场景 | T01 | 编译/保存重开正常；native灯光响应可测 | CL缺陷/无法隔离 |
| T07/P0 Face Gate | 第5节资产/key、Adapter/offset方案 | reference mask+S58桥接/混合对照与质量报告 | T03/T04/T05/T10 | 全周/同旋/顶背光；单灯/多灯/shadow测完；必须项不隐藏 | 无逐灯输入/双分界 |
| T08/P0 原生Lighting Spike | Generic场景、第14节 | Cast/Receive/Self/VSM/CSM/Sky/GI/IndirectSpec/AO/Fog记录 | T06 | 逐项实测✓/△/✗且有配置/图像；IncludeShadow因果隔离 | 漂亮图掩盖失败 |
| T09/P0 Backend发布Gate | 各探针与冻结合同 | S58主路线/局部Hybrid/阻断决策 | T07/T08/T12/T27/T28 | 缺硬能力则阻断生产；Approx域与局部扩展明确 | 偷换范围 |
| T10/P0 S58 Adapter | Schema、目标shader语义与标定 | Style→ToonProfile/MIC/Package生成器 | T02/T25/T26 | 版本/hash去重；mapping质量/范围；缺字段报错；共Master | experimental API变更 |
| T11/P2 局部A+/C可行性 | Face/Hair失败证据与硬需求 | 有界visibility或最小输入/瓣扩展RFC/原型 | T09，条件任务 | 只补已证缺口；完整成本/分发/维护；patch不冒称纯插件 | 范围/时序/fork成本 |
| T12/P0 Hair Gate | Flow、reference、目标native行为 | 0/45/90°/双截面/shift对照、Approx域或拒绝 | T03/T04/T26 | Tangent有效；宽度/softness单调；shift不能假实现 | native瓣不等价 |
| T13/P1 Resolver | Schema/Style/Material/patch | 不可变Resolved/provenance/诊断 | T02 | 幂等/0值/Reset/优先级正确；Unsupported拒绝 | 循环引用/静默clamp |
| T14/P1 Curve与资源生成 | 域声明、Adapter与Curve | Profile生成更新、必要LUT、resource manifest | T10/T13 | 线性/域/寻址/缓存正确；不用重复LUT取代native管理 | mip改变边界 |
| T15/P1 生产材质与变体 | 合格后端、局部Gate与资源 | backend分目录三Model、≤12配置manifest | T09/T12/T14 | 无七profile巨Master；Profile与shader variant区别明确 | 变体膨胀 |
| T16/P1 两Style标定 | 同角色/同曝光/canonical域 | AnimeFlat/SemiPBR、3×2图与quality | T07/T12/T15 | 盲辨≥80%；Approx不冒称Exact；不靠换曝光 | 配色掩盖模型 |
| T17/P1 实例/动画提交 | Binding/Resolved与bundle | MID参数、动画后Head、两角色测试 | T05/T13/T27 | 独立转头；patch跨Style保留；无全资产逐帧解析 | slot混帧/延迟 |
| T18/P1 Preset/Details | Style/patch/MI/质量报告 | 链接/快照、来源/近似标记、Reset | T13/T16/T17 | 重开/切Style/Reset可复现，后端包版本兼容 | 把继承值全冻结 |
| T19/P1 Hull Outline | 平滑方向/StylePolicy | 壳材质/同步组件与宽度声明 | T03/T15 | 多距离宽差≤1px目标域；LOD同步/无明显缝 | 蒙皮/overdraw |
| T20/P1 性能/时间稳定 | 固定GPU、最小/完整S58/Lit/ref | 全pass中位/p95/CPU/PSO/远景报告 | T15/T16/T17/T18/T19 | Profile/逐灯/shadow/GI/Hull均计；预算失败明确 | 将表面采样当总成本 |
| T21/P1 Cook/插件交付 | Runtime/Editor与manifest | 干净工程/打包样例/版本支持表 | T18/T19/T20/T28 | runtime无Editor依赖；全部Profile/bundle引用cook；冷热一致 | 缺引用/DDC宏缺失 |
| T22/P1 Principled实验 | 稳定实现、5–10参与者 | 排序/匹配/预测/消融与匿名数据 | T16/T18/T20 | ρ/耗时/失败例和适用域完整，不夸大小样本 | 学习/偏差 |
| T23/P1 最终证据文档 | 所有图/性能/实验/合同 | 实测矩阵/限制/指南/维护说明 | T08/T21/T22 | 生产与reference清楚；未测项不写可用 | 目标冒充成绩 |
| T24/P2 V2材质RFC | 稳定V1与后端 | Skin/Eye/Cloth/Metal模型/资产/传输设计 | T23 | 不破坏canonical语义，先验证后扩展 | 提前扩范围 |
| T25/P0 Shader审计 | 目标5.8安装/授权源码/API | 编译→Profile→GBuffer→lighting路径、关键表达式/CL | T01 | xd/xs/offset/visibility/F/G/Tangent/资源绑定有定位；缺正文明确标记 | API冒充源码结论 |
| T26/P0 后端坐标/参数标定 | Generic装置、shader表达式、reference | Ramp域/roughness/ramp拟合与误差/留出验证 | T04/T06/T25 | Exact条件证明；Approx域/联动透明；不稳定解拒绝 | 单截图过拟合 |
| T27/P0 Runtime Style bundle | Adapter/MIC Profile_A/B、实例patch | 预载MID bundle切换与PSO/资源报告 | T10 | 100次预热切换无shader编译；无Profile Settings写入；override/slot一致 | setter猜测/首次hitch |
| T28/P0 DDC/缺陷复现 | 目标版本/最小Adapter资产、独立缓存 | UE-388460冷/热重启/cook结果与Tangent/shadow修复表 | T10 | 固定图误差≤1/255；target修复有源码/结果；不删全局DDC | 修复commit与CL混淆 |

**下一轮第一迭代：T01→T06/T25，T02–T05可并行准备；接T26→T10，再T07/T12/T08/T27/T28→T09。** Generic与native lighting先做最小闭环，Face/Hair只做用于决策的探针；Gate后进入完整Resolver/UI/Style/Preset产品化。环境未齐时先取得版本/工程路径，不用5.7或空模板冒充5.8实测，不大量生成未核验API的插件类。

## 附录 A：来源与证据管理

正文链接优先使用 Epic 官方文档/API、原作者论文与成熟系统官方仓库。初版检索为2026-10-03，UE5.8专项API/缺陷复核为2026-10-04。在线 UE 文档默认版本可能更新；只有明确版本参数/发布公告可以支撑版本归属，目标实现仍需核对安装版本。本文没有声称读取Epic私有源码或商业游戏内部实现。Epic发布说明的英文索引曾出现5.7 URL却返回5.8标题/内容的异常，本文使用明确5.8标题与NPR段落的官方语言页面交叉核验，不以URL数字独立认定版本。

| 证据类别 | 使用方式 | 能证明什么 / 不能证明什么 |
|---|---|---|
| Epic版本公告 | 5.7 Substrate、5.8 Toon变化 | 功能发布时间/状态；不证明自定义Face/Hair已可注入 |
| Epic材质/管线/API | Lit/Unlit、域、SceneTexture、Lumen、MPC、SceneCapture | 公开能力与架构推论；不证明目标场景实测质量/成本 |
| Epic工程师/官方Issue | Toon Tangent、Face入口、shadow修复、UE-388460 | 历史缺陷与当前tracker；targetCL修复需源码/实测，不证明所有patch状态 |
| Disney/GGX/RNM/毛发经典资料 | 物理基线、方向组合与候选比较 | 理论背景；不替本项目新艺术模型证明感知可用性 |
| lilToon/UTS/MToon官方资料 | 功能/组织/规范对标 | 真实先例；不支持未经测量的性能优劣声明 |
| 本次数值JSON | 标量reference抽查 | 列明输入域内的数学行为；不等于GPU或用户实验 |

重要原始来源还包括 [Walter等：Microfacet Models for Refraction](https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.html)，用于 GGX 背景；其余关键 URL 已在对应技术结论旁标注。后续证据包应额外记录文档版本、精确源码 CL（若授权可读）、重现配置和截图，而不是仅存搜索摘要。

## 附录 B：研究结果的最终边界

当前已经交付的是理论推导、参数与资产契约、技术路线调查、可复现标量验证以及完整实施设计。**UE 技术探针、实际角色制作、材质编译、GPU 性能与小规模参与者实验尚未执行。** 这些工作对应T03–T28的明确输入、依赖和验收；报告不能将它们提前写成完成。

本项目的下一步是用最小引擎实验校验数据入口，再以受控模型和可追踪风格构建工具。真正的 Principled 应体现为使用者能够预测、测量和复现参数行为，以及系统能够准确说明其能力边界。
