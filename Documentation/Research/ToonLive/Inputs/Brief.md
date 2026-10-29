# Codex 任务书：PrincipledNPR Toon Lighting 数学参数与 UE5.8.3 Toon BSDF 后端适配研究

> **任务类型**：在既有 GPU 调研之后开展数学模型核对、源码机制研究和隔离验证；**本轮不直接实施产品级 Toon Adapter、不改现有生产代码/UI**。  
> **环境**：以仓库实际版本为准，目前已有实验环境为 UE 5.8.3 / CL 58210709、DX12 SM6、Substrate。  
> **首要参考**：本任务随附 `PrincipledNPR_ToonLighting_MathematicalModel.tex`（完整 LaTeX 推导）及其 PDF；`Report.md`（2026-10-28 GPU 试验报告）；`ToonGPUProbe.cpp`（隔离试验源代码）。先核查这些内容与仓库当前状态，不重做已经完成的验证来冒充新成果。

## 1. 项目方向：我们在做“艺术参数 → 渲染后端”的翻译官

PrincipledNPR 的核心架构约束：

1. **Shading Model 本身也是一个可配置参数**：如 Default Lit、Substrate Toon、未来的 Custom。模型切换是离散的资源/后端选择，未必能够由普通 MID float 完成。
2. **艺术参数语义独立于具体后端**：一套清晰的参数（如明暗分界、过渡半宽、高光角半径）经 Resolver / Backend Adapter 翻译为对应材质、Profile、材质实例和渲染配置。不得将 UE Toon Profile 内部参数名直接替换为全局艺术语义。
3. **后端能力必须可声明、可验证**：输出 `Exact / Approximate / Unsupported / NotVerified` 及其条件和误差。不能因为某个接口存在，就宣称艺术控制符合语义。
4. **允许多类着色模型并存**：连续 PBR/NPR 融合着色与阶层式 Toon 均是可选路线，不预设 Toon BSDF 是唯一渲染后端。
5. **按需 Shader**：优先 UE 原生节点、材质函数和已有渲染路径；只有明确证实能力缺口后才讨论自定义 HLSL、Shader 扩展或底层 Pass。

目标架构（概念结构，不表示现已全部实现）：

```text
Style Profile / Material Profile
    └─ Shading Model（离散） + Principled 艺术参数（连续/离散）
                 ↓
         Semantic Resolver
                 ↓
         Backend Adapter
        ┌────────┴─────────┐
  Default Lit Adapter    UE5.8 Toon Adapter      [后续 Custom]
        │                     │
     Material/MID      Toon Profile + Material/MID
        └──────────┬──────────┘
                   ↓
            UE Renderer
        Rim/Surface 组合 + 独立 Outline
```

已有 `NPRStyleProfile`、`NPRMaterialProfile`、`NPRBackendStylePackage`、`NPRSurfaceRecipe` 等类型应视为现有架构基础。**不要假设完整 Resolver 和 Adapter 已开发完成**。

## 2. 后续 NPR 效果开发方向（保留既定优先级）

| 优先级 | 效果 | 主要功能 | 难度 |
|---|---|---|---|
| ✅ | Outline | 模型轮廓、区域边界、线宽控制 | ⭐⭐⭐⭐ |
| ✅ | Rim Light | 轮廓边缘光、宽度、颜色、强度 | ⭐⭐ |
| 🔴 P0 | **Toon Shading** | 二值/多阶明暗、阴影分界、渐变控制 | ⭐⭐⭐⭐ |
| 🔴 P0 | **Stylized Specular** | 卡通高光形状、尺寸、硬度与亮度 | ⭐⭐⭐ |
| 🟠 P1 | **MatCap** | 模拟环境反射、金属质感、风格化反光 | ⭐⭐ |
| 🟠 P1 | **Color Grading** | 色调映射、明暗色彩、饱和度控制 | ⭐⭐ |
| 🟠 P1 | **Face Shading** | SDF 脸部阴影、面部明暗控制 | ⭐⭐⭐⭐ |
| 🟡 P2 | **Hair Shading** | 各向异性高光、发束高光、头发阴影 | ⭐⭐⭐⭐ |
| 🟡 P2 | **Eye Shading** | 虹膜深度、视差、眼球高光 | ⭐⭐⭐ |
| 🟡 P2 | **Stylized SSS** | 皮肤柔和透光、背光散射 | ⭐⭐⭐⭐ |
| 🟡 P2 | **Cloth Shading** | 布料明暗、织物反射特化 | ⭐⭐⭐ |
| 🟡 P2 | **Metal Shading** | 分层金属反射、硬质高光 | ⭐⭐⭐ |

目前聚焦 **P0 的 Generic Toon Lighting 参数映射与实时编辑可行性**；后续再开展 Stylized Specular 角域拟合与角色特化。该优先级是未来路线，并不授权本轮实现所有项目。

## 3. 数学模型：请以附带完整 LaTeX 推导为准

### 3.1 Principled Toon 的基础变量

取世界空间单位着色法线 $\mathbf N_d$、指向第 $i$ 盏灯的单位光向 $\mathbf L_i$，保留有符号点积：

$$
q_i=\mathbf N_d\cdot\mathbf L_i\in[-1,1].
$$

**注意**：$q$ 是朝向参数，不包含真实遮挡可见性 $v_i$。如果先 `saturate(N·L)`，则无法在参考模型中表达负阈值的背光侧分界。

原有全局语义 ID：

| 语义 ID | 数学参数 | 含义 | 当前契约范围/默认 |
|---|---|---|---|
| `TerminatorDotCenter` | $\tau$ | 明暗分界的 50% 响应点（有符号点积域） | $[-1,1]$ / $0$ |
| `TerminatorDotHalfWidth` | $w$ | **半宽**（完整过渡宽为 $2w$） | $[0,0.5]$ / $0.05$ |
| `SpecHalfAngleRad` | $\beta$ | 参考高光瓣半峰角（弧度） | $[1^\circ,60^\circ]$ / $8^\circ$ |
| `SpecEdgeSoftness` | $s$ | 高光边界角域柔度 | $[0,1]$ / $0.15$ |
| `SpecPeakScale` | $I_s$ | 高光峰值倍率 | $I_s\ge0$ / $1$ |

**不要擅自新建或改名这些语义 ID**。Ramp 色阶资源、阴影策略、间接尺度等需核对现有契约，当前未确认均是通用 Principled ID。

### 3.2 明暗中心和过渡半宽

隔离 GPU 实验使用分段线性 Ramp，定义

$$
B_{\rm lin}(q;\tau,w)=\operatorname{clamp}\left(\frac{q-\tau+w}{2w},0,1\right),\qquad w>0.
$$

可推得 $B(\tau)=1/2$，$B(\tau-w)=0$，$B(\tau+w)=1$。$w\to0^+$ 对应理想硬分界，但产品仍须考虑采样过滤与 AA。

既有设计中的平滑备选为

$$
B_{\rm smooth}(q;\tau,w)=S\!\left(B_{\rm lin}(q;\tau,w)\right),\qquad S(x)=3x^2-2x^3.
$$

二者中心和过渡支持区间相同、形状不同；**当前报告中的线性 GPU 测量不能当作 Hermite 版本通过验证**。

### 3.3 二阶及多阶色阶

用线性 RGB 响应系数 $\mathbf c_j$：

$$
\mathbf Q_2(q)=\mathbf c_0+(\mathbf c_1-\mathbf c_0)B_{\rm lin}(q;\tau,w).
$$

推广为 $K$ 阶：

$$
\boxed{\mathbf Q_K(q)=\mathbf c_0+\sum_{j=1}^{K-1}(\mathbf c_j-\mathbf c_{j-1}) B_{\rm lin}(q;\tau_j,w_j).}
$$

若要求相邻色阶各有完整平台，则要求 $\tau_j+w_j\le \tau_{j+1}-w_{j+1}$。多阶色阶、RGB 曲线、连续 Ramp 的插值/过滤和色彩空间必须明确。

### 3.4 到 UE5.8 Toon Profile 曲线坐标的严格变换

基于目前 S58 候选映射：

$$
u=\frac{q+1}{2},\qquad t=\frac{\tau+1}{2},\qquad h=\frac{w}{2}.
$$

代入可得：

$$
\frac{q-\tau+w}{2w}=\frac{u-t+h}{2h}
\quad\Longrightarrow\quad
\boxed{B_{\rm lin}(q;\tau,w)=B_{\rm lin}(u;t,h).}
$$

由此可生成 $[(0,\mathbf c_0),(t-h,\mathbf c_0),(t+h,\mathbf c_1),(1,\mathbf c_1)]$ 等 Ramp 控制点。**这仅证明在规定参数域内的坐标恒等式，绝不自动证明最终 GPU 画面完全匹配。** 当 $t-h<0$ 或 $t+h>1$、$w=0$ 时要单独处理边界与极限。

### 3.5 投射阴影与分界是独立量

第 $i$ 盏灯的可见性/透射因子为 $v_i\in[0,1]$。现有报告调查到 Toon 后端的两类**局部近似**：

$$
\Phi_{\rm outside}(u,v)\simeq v\,\mathbf Q(u),\qquad
\Phi_{\rm inside}(u,v)\simeq\mathbf Q(uv).
$$

两式通常不等价，且 UE 外层光照调度可能在完全遮挡时跳过计算。这些式子**不能被误写成真实 UE 最终照明的完整方程**。

重要 GPU 反例：$\tau=-0.5$ 且角色/球体自阴影开启时，最终可见亮暗交界仍接近 $q=0$；无自阴影对照才显现背光延伸。**不能为了“通过”这个验证，就在正式材质默认关闭自阴影。** Adapter 应声明该条件下的限制。

### 3.6 多光源与 GI 的参考式

为区分“逐灯量化再求和”和“先聚合再量化”：

$$
\mathbf Q\!\left(\sum_i a_iq_i\right)\ne\sum_i a_i\mathbf Q(q_i)\quad\text{（一般情况下）}.
$$

可将受控参考模型写作

$$
\mathbf C_{\rm lit}^{\rm ref}=\mathbf C_{\rm dir}^{\rm ref}+\gamma_{\rm ind}\mathbf C_{\rm ind}^{\rm UE}+\mathbf C_{\rm spec}^{\rm ref}.
$$

$\gamma_{\rm ind}$ 是候选后端的 `DiffuseIndirectScale`，目前没有证据证明它是已经接通的全局语义参数。该式表示模型分解，**不意味着普通 Surface Material 可以直接读写完整 Lumen GI**。

### 3.7 Stylized Specular 暂作为参考模型

$$
\mathbf H=\frac{\mathbf L+\mathbf V}{\|\mathbf L+\mathbf V\|},\quad
\theta_h=\arccos(\operatorname{clamp}(\mathbf N_s\cdot\mathbf H,0,1)),
$$

$$
\delta=0.95s\min\left(\beta,\frac\pi2-\beta\right),\qquad
S_{\rm ref}=1-B_{\rm smooth}(\theta_h;\beta,\delta).
$$

$\|\mathbf L+\mathbf V\|\to0$ 时要显式避免零向量归一化。UE 实验已证明 Roughness、Specular、SpecularRamp 影响高光覆盖/峰值/颜色，但**尚未完成高光半峰角 $\beta$ 与 Roughness/SpecularRamp 的可逆标定**。禁止使用 `size = 1 - roughness` 冒充统一语义。

### 3.8 如何标定（参考）

线性 Ramp、受控单光条件下，用 HDR 响应的 10%/50%/90% 交叉位置 $q_a$：

$$
\widehat\tau=q_{0.5},\qquad \widehat w=\frac{q_{0.9}-q_{0.1}}{1.6}.
$$

这里的 1.6 系数**只对分段线性过渡成立**，Hermite 要用对应反函数。已有报告的数据为 Step0 中心约 $0.00681$（目标 0）、StepHigh 中心约 $0.49815$（目标 0.5）、SoftPositive 中心约 $0.49995$（目标 0.5）、半宽约 $0.15009$（目标 0.15）；这些是**报告给出的实测**，不代表我们已经独立复算全部原始浮点数据。

**完整严谨推导、完整变量说明及边界证明都在随附的 `.tex` 中；后续报告应引用原式编号，不用简短 prompt 取代该文件。**

## 4. 这轮要重点解决的技术问题：Toon Profile 实时曲线编辑

**核心问题：如何在不反复触发 Shader 编译的情况下，让艺术家拖动 `TerminatorDotCenter`、`TerminatorDotHalfWidth` 等参数时，原生 Toon BSDF 的结果连续、及时、独立地更新？**

### 4.1 源码追踪

在实际 UE 5.8.3 / 当前 CL 的引擎源码中调查（路径以实际安装树为准）：

- `UToonProfile` / `FToonProfileStruct` 的资源生存期、资产序列化、编辑事件和渲染线程更新路径；
- `DiffuseRamp`、`SpecularRamp` 转换为 GPU LUT/纹理/缓冲的流程；Shader 是否读取独立资源，还是某些设置需参与编译或管线置换；
- Profile 发生改变时的 dirty、通知、重建、RenderThread/RHI 上传流程；热更新是否有公开、可靠的 Editor API；
- 一个 Profile 被多个 Material/MID 引用时，更新如何传播；单槽独立 Profile 如何保证不串改其他对象；
- 编辑器下和 Cook/打包 Runtime 下的接口能力差异，区分“设计时资源重建”和“运行时可调”。

**不要仅凭 `UObject` 可写、曲线有字段或调用 `PostEditChange` 就判断 Shader 不重编译。需要真实的调用链和计数/实验。**

### 4.2 至少比较四种候选路线

| 路线 | 要确认什么 | 主要取舍 |
|---|---|---|
| A. 修改 Toon Profile 并刷新 GPU 资源 | 是否有明确资源更新 API；触发 Shader 编译与否 | 最符合原生模型，但可能受运行时接口/共享资源限制 |
| B. 通过 MID 参数重映射曲线采样 | 是否能在现有 Material Graph/BSDF 接口控制所需坐标；语义能否保持 | 实时调参方便，但原生 Ramp 输入可能不开放 |
| C. 生成 Ramp/LUT 资源并实时更新 | 纹理读入路径、上传、过滤、颜色空间、资源所有权 | 灵活，但可能需要受控材质扩展或资源成本 |
| D. 预制 Profile/材质包切换与缓存 | 预生成数量、PSO/Shader/内存与切换延迟 | 可靠兜底，但对连续滑动编辑可能不理想 |

可以提出其他方案，但必须展示源码证据和隔离 GPU 结果；不得预设 A/B/C 任何一种必然可行。

### 4.3 最小 GPU 实验和可比较的指标

1. 固定光源/相机/曝光，连续修改 Threshold、Softness，逐帧记录线性 HDR 读回与目标参考曲线误差。
2. **对每种更新方式记录 Shader 编译任务数量**、材质资源重新创建/缓存命中、Profile GPU 资源更新次数、帧数延迟与 CPU/GPU 开销；记录测量方法，区分冷启动的初次编译和滑块调整后的增量编译。
3. 以两材质槽、两不同 MID/Profile 检查独立更新；验证同 Profile 共享时的联动行为。
4. 验证保存、重开编辑器和加载后的资源恢复；研究 Runtime/打包可行性，若未测试 Cook 须标注。
5. 保留负阈值 + 自阴影反例，测试 `bDiffuseRampIncludeShadow` 等阴影条件与动态曲线更新之间的耦合。
6. 对至少两种路线做同条件实验；无法实现时记录明确阻碍，并列出有证据支持的替代路线。

实验继续采用**隔离目录、临时材质/测试资产、真实 GPU 捕获、线性 HDR 数据和可复现命令**；保留原始代码、Profile 设置、日志、失败过程与结论边界。

## 5. 未来的 Backend Adapter 路线（本轮产出设计，不进入生产实现）

根据研究结果，提出一个 Generic Toon Adapter 的**实施方案**，包含：

- 统一参数 $\theta\mapsto$ Toon Profile/MID/材质包的映射与缓存机制；是否需要可变 Profile、Profile 复制或预制组合；
- `ShadingModel` 离散切换的资源选择与预加载建议；
- 参数更新频率分类：**实时 MID 值**、**实时 GPU Profile/LUT（如证实可行）**、**需要资源切换/重编译的设置**；
- 异常/边界状态：未支持、跨域、负阈值自影、共享 Profile 误操作、GPU 颜色采样不稳定；
- 保持 Rim、Outline、Surface Composer、现有 Editor 工作区和原材质数据的组合语义；
- 实现顺序：Generic Toon Diffuse → Profile/MID 的实时编辑 → Style Profile 持久化/切换 → Stylized Specular 的角域标定 → Face/Hair 专用模型。

对每一个参数输出一行能力表：**Principled ID/单位/默认值 → 数学参考 → UE 接口/资源 → 更新时间类别 → Exact/Approximate/Unsupported/NotVerified → 误差和适用域 → 实验依据**。

## 6. 本次交付形式与执行边界

### 交付物

1. **数学模型审阅记录**：对随附 `.tex` 各主要公式逐项检查量纲、边界、定义的一致性；发现问题提出勘误，不自行更改统一参数语义。
2. **源码机制报告**：Toon Profile 到 GPU 实际数据路径、是否发生 Shader 编译、编辑器/Runtime 能力边界，标注具体文件、函数、源码行或提交版本。
3. **实时编辑隔离实验**：测试代码、真实 GPU 图像/原始 HDR、曲线配置、编译计数、性能记录、复现方法与失败用例。
4. **方案比较与技术决策**：A/B/C/D 的可行性与开销；推荐路线、已知限制及下一阶段 Generic Toon Adapter 设计草案。
5. **状态严格标注**：`SourceVerified`、`GPUVerified`、`Approximate`、`NotRun`、`Unsupported` 必须按实际证据区分，不得将数学推导直接标成产品功能已实现。

### 保护现有项目

- **不删除、不精简、不重构** 任何已有功能、设置、数据契约、典籍断词、逻辑细节或 UI 样式；不改动既有 Outline、Rim Light、Surface Composer、Editor 工作区和运行时绑定行为。
- 不覆盖或修改用户原有角色、场景、材质资产、以前 GPU 验收截图与报告。隔离实验仅在专用目录开展。
- **此轮仅研究与实验**；未经明确的新指令，禁止直接实施正式 Toon Adapter、扩展 Shader Pass、修改引擎或迁移原始材质。
- 不声称可以自动复现《终末地》《原神》等项目真实渲染实现；样式目标由 Principled 参数表达，具体技术证据需分开说明。

## 7. 工作顺序

1. 阅读既有架构及参数契约、这份任务书、`.tex`/PDF、GPU 报告和夹具源码。
2. 复核数学模型与已完成 GPU 证据，列出可依赖的事实和待验证的缺口。
3. 优先源码追踪 Toon Profile 曲线与 GPU 更新机制。
4. 实施最小隔离实验，测真实效果和 Shader 编译频率。
5. 交付报告、证据与 **下一阶段的开发设计**。当前止于研究结论，不直接开发产品功能。
