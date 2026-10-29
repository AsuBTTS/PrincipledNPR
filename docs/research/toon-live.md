<!-- Generated from Documentation/Research/ToonLive/Report.md. Edit the original, then run npm run docs:generate. -->

# Toon Lighting 数学映射与实时 Profile 调研

日期：2026-10-28。我们完成了完整数学模型审阅、UE 5.8.3 源码追踪，以及新一轮 **109 次动态更新、221 张主进程 GPU 捕获和新进程重开对照**。已有 GPU 数据另行独立复算，没有当成新渲染实验。结果支持先采用 **Editor 私有 Profile 实时刷新 + Runtime 不可变资源切换** 的实施路线。

本轮没有开发产品级 Adapter 或修改生产模块。Shading Model 仍是统一体系中的离散配置；艺术参数由 Backend Adapter 翻译到所选后端。现有功能、UI、参数契约和旧 GPU 报告保持保留。

## 结论和证据等级

| 结论 | 状态 | 具体边界 |
| --- | --- | --- |
| 阈值/半宽的仿射坐标换算成立 | 数学推导 | 只保证坐标；实际 LUT、阴影和照明另测 |
| Editor 中改变私有 Profile 曲线，无需每次重编译材质 Shader | SourceVerified + GPUVerified | 本批累计新增 JobsAssigned、材质翻译均为 0，所选资源和 ShaderMap 身份不变；不是仅看 pending=0 |
| Profile 通知会使当前全局图集失效，并重烘焙所有层 | SourceVerified + GPUVerified | 默认 R=64，本批 23 层；不是每个 Profile 独立局部行更新 |
| 第一次自然 SceneCapture 可显示新的阈值 | GPUVerified | 12 个自然渲染更新对照，没有调用夹具的显式图集 probe；不是键鼠到显示器的端到端延时测试 |
| B/C 可以经现有偏移路径近似移动阈值 | GPUVerified / Approximate | 固定半宽 0.08；不提供通用曲线输入缩放，不能独立调半宽 |
| 同尺寸偏移纹理上传本身不足以刷新已烘焙图集 | SourceVerified + GPUVerified | C_Stale 保持旧响应，C_Refresh 通知后改变；不是纹理上传失败 |
| 不同 MID 共享一个 Profile 时仍会联动 | GPUVerified | 双槽共享/独立 Profile 对照分别验证 |
| 保存原生研究资产并在新 Editor 进程加载，可恢复图像 | GPUVerified | 原生 Profile/Material 资产范围；没有证明 Style Profile 产品保存、Cook 或游戏包 |
| 非 Editor 的 C++ 更新函数存在 | SourceVerified；Editor 中队列试验 GPUVerified | 头文件仍明确限制 Runtime 修改；游戏发行包、生命周期及正式契约均 NotRun |

数学审阅见[完整记录](toon-math-review.md)，源码和具体入口见[机制报告](toon-profile-mechanism.md)，逐参数能力与下一阶段步骤见[Adapter 草案](toon-adapter-plan.md)。

## 数学审阅和旧证据复算

本次通读用户提供的[完整 TeX](/generated/source/Documentation/Research/ToonLive/Inputs/MathematicalModel.tex.txt)及 [PDF](/generated/source/Documentation/Research/ToonLive/Inputs/MathematicalModel.pdf)，按原式编号审查。主坐标变换 (4.5) 成立；建议补充 (7.3) 的 s=0 分支、(3.12) 重叠过渡的非负性条件，以及 (4.6) 越界控制点的生成规则。

一个反例是 c=(0,1,0)、tau=(-0.1,0)、w=(0.5,0.01)，在 q=0.02 时多阶和式得到 -0.38。另一个例子 tau=0.95、w=0.2、暗亮系数 0.1/1，在 q=1 的正确响应是 0.6625，不能简单把越界亮色键钳到 1。Hermite 的 10–90 宽度系数约为 1.21679958，不能沿用线性版本的 1.6。

从旧浮点读回独立复算得到 Step0 中心 0.0068100183、StepHigh 0.4981492996、SoftPositive 中心 0.4999491955、半宽 0.1500888632，与旧报告一致。[MathChecks.json](/generated/source/Documentation/Research/ToonLive/MathChecks.json)保留数值验证与原文哈希。

## 新 GPU 实验条件

- 引擎 5.8.3 / CL 58210709；实际 RHI 为 NVIDIA RTX 4060 Laptop GPU / DX12 SM6，Deferred、Substrate Blendable、VSM；同一配置下比较各路线。
- 固定 256×256 透视相机、FOV 40°、位置 (-230,0,0) cm，球半径 50 cm；白色方向光强度 PI，yaw=55°。关闭 GI、Sky、反射、TAA、Bloom 和运动模糊。手动曝光，禁用物理相机曝光。
- 普通映射实验显式关闭球体自身投影以隔离参考曲线；另设 NegativeSelf、NegativeClear、InsideSelf 保留自影反例。这个实验设置不会变成正式角色默认。
- 线性 HDR 来自 RGBA16F GPU 目标，保存为 RGBA32F，再无损打包为 NPZ。PNG 只用于显示，不用于能量误差拟合。
- A 以 257 个函数样本生成线性键；D 的预制线性曲线使用少量转折键。两者目标函数相同，但键生成器 CPU 成本不同。A 的原型生成时间不能直接当作原生通知 API 的最低成本。
- 两次捕获 f0/f1 分别表示更新后首次及下一次 SceneCapture。同步 readback 和 Flush 是实验观测方式，不是建议照搬的交互实现。
- 双槽使用同一临时网格的两个真实 Section；Profile 和所有测试材质/纹理独立创建。保存只写 `/ToonLiveResearch/` 对应的专用资产目录，不写角色或生产 Content。

## 路线与负对照

| 代号 | 本次实际执行 | 能证明什么 |
| --- | --- | --- |
| A | 写入私有 Profile 的曲线，构造 Interactive 编辑事件，调用 PostEditChangeProperty；测图集和 HDR | Editor 曲线热更新、阈值/半宽及其他已测形状 |
| Natural | 同 A，但不调用显式 atlas probe，由正常 SceneCapture 触发 native 图集更新 | 排除“只有测试手动烘焙才及时显示”的混淆 |
| RuntimeAPI | Editor 进程中，在渲染线程排队调用导出的 AddOrUpdateProfile | 底层快照更新路径确实可驱动 GPU；不等于游戏 Runtime 可用声明 |
| B | 固定 DiffuseRamp，MID 改变 PatternUV；通过预烘焙灰度梯度图产生均匀 offset | 固定宽度的阈值平移近似；有 12-bit UV 和 LUT 误差 |
| C | 相同尺寸的偏移纹理上传，加 Profile 通知；另测不通知的 C_Stale | 纹理更新和图集通知的关系；不是任意 DiffuseRamp LUT 输入 |
| D | 选择预先创建、编译的 12 个阈值材质及 6 个宽度材质 | 已准备资源的切换；不代表任意连续组合已缓存或任意模型图修改免编译 |
| Hold | 不更新参数，执行相同测量与捕获 | 计时底噪和资源有效状态对照 |

阈值同条件扫描为 tau=0.15、0.20……0.70，w=0.08；宽度扫描为 tau=0.5，w=0.02、0.05……0.17。另测共享/独立双槽、Hermite 函数采样、w=0、域边界和 10 次通知合并到一次图集重建。

![四种路线的真实 GPU 阈值对照](/generated/source/Documentation/Research/ToonLive/Evidence/01_live_threshold.jpg)

这些是 GPU 导出拼图，不是编辑器截图。A/B/C/D 的每列目标阈值相同；显示图经过 sRGB 转换，数值结果使用原始 HDR。

## 编译、误差与延迟

初次创建/编译、纹理与材质预热完成后开始计数，持久化时创建新研究材质的工作另列。每步记录完整累计 Counters 和 MaterialCounters，以及材质资源/ShaderMap 是否变化。没有把 DDC 命中冒称成零编译的原因。

<!-- LIVE_METRICS_START -->
| 路线 | 同条件阈值样本 | 增量 JobsAssigned | 最大中心误差（dot） | 最大 HDR RMSE |
| --- | --- | --- | --- | --- |
| A 私有 Profile | 12 | 0 | 0.00015658 | 0.00485149 |
| D 预制材质 | 12 | 0 | 0.00015658 | 0.00485149 |
| B MID 偏移 | 12 | 0 | 0.00292992 | 0.00535734 |
| C 偏移纹理+通知 | 12 | 0 | 0.00357644 | 0.00607050 |
| A 自然渲染 | 12 | 0 | 0.00015658 | 0.00485149 |
| 底层 C++ 队列（Editor） | 12 | 0 | 0.00015658 | 0.00485149 |

最终进程预热后/测量后累计 JobsAssigned 均为 0；累计材质翻译次数均为 168。较早首轮预热产生 6 个编译任务，更新区间仍未增加，详见 Attempts.json。

新进程重开与保存前 HDR RMSE 为 0.00000000。
<!-- LIVE_METRICS_END -->

上表的 RMSE 单位是线性场景 RGB，针对固定球体区域，不是角度误差或百分比。中心拟合使用世界法线和归一化响应的中间段；窄宽度、Hermite、阴影、边界等特殊情况另看逐用例数据，不能共用线性反算方法。

Natural 的 f0 已符合目标误差，表示本实验没有要求额外等待另一次捕获；不声称零帧延时，也没有测量 UI 输入队列、帧调度或显示器延迟。

显式 probe 观察到 A 的 29 次、C 的 13 次及底层队列的 12 次图集“失效→有效”；B/D 均没有发生该状态转换。Natural 的 12 次由正常渲染触发，没有插入 probe，因此不填直接测得的重建次数。Coalesced10 重复提交同一目标的 10 次通知，只观察到一次重建；这验证通知合并行为，没有测任意输入事件序列的交互预算。

## 更新成本与计时范围

<!-- LIVE_PERF_START -->
同一 12 点阈值扫描，单位 ms；每格为中位数 / P95。

| 路线 | 绑定入队 CPU | 编辑/生成 CPU | Atlas 准备/提交 CPU | GPU dispatch 区间 |
| --- | --- | --- | --- | --- |
| A 私有 Profile | 0.0090 / 0.0103 | 0.8101 / 0.8247 | 1.1747 / 1.2990 | 0.0160 / 0.0170 |
| D 预制材质 | 0.0266 / 0.0340 | 0.0010 / 0.0050 | 0.0835 / 0.1104 | 0.0010 / 0.0010 |
| B MID 偏移 | 0.0088 / 0.0222 | 0.0235 / 0.0316 | 0.0783 / 0.0932 | 0.0010 / 0.0010 |
| C 偏移纹理+通知 | 0.0091 / 0.0182 | 0.0758 / 0.0837 | 1.1579 / 1.2004 | 0.0160 / 0.0170 |

B/D 的 Atlas/GPU 项是已缓存情况下的 probe 底噪，不代表存在同等更新工作。完整较宽计时区间、带 Flush 的绑定时间及逐次数据保存在 updates.json。
<!-- LIVE_PERF_END -->

编辑 CPU 只包括曲线生成/通知或 MID/纹理写入；绑定列记录组件材质/几何设置的入队 CPU，另有包含渲染 Flush 的字段。Atlas CPU 包括本机渲染线程上的 native 图集准备和 RDG 执行提交；**不能把这些来自不同线程、带同步的时间简单相加当成游戏帧耗时**。

GPU dispatch 区间是在构建完 RDG 后插入的 GPU 时间戳，包围图集计算与相关命令；缓存对照约有微秒级底噪。这个区间可能不覆盖提前处理的缓冲上传，也不是只统计某一个 Shader 内核的硬件周期。另存的 `atlas_gpu_ms` 是更宽的端到端 probe 区间，包含队列空泡，不用来冒称纯 GPU 计算耗时。

采集与同步读回约为数十毫秒，其中包含实验性等待，不能用其倒数推断产品最大拖动频率。A 的样本生成器不是优化后的少键实现；C 的 CPU 写入较少，但仍支付整图集重建。固定 23 层的结果也不能外推至任意角色/材质数量。

## 独立性、保存与失败记录

不同 Profile 时，只改左槽，左侧平均通道差约 0.699，右侧为 0；共享同一个 Profile 时，两槽均改变。这验证的是资源所有权，不是“只要 MID 不同就独立”。

C_Stale 上传新纹理但不通知，仍保留旧阈值；C_Refresh 通过 Profile 通知后命中新的目标。负阈值在自身投影开启时仍受限，新的实时更新并没有消除旧反例。

![生命周期、共享和边界对照](/generated/source/Documentation/Research/ToonLive/Evidence/02_lifecycle.jpg)

我们把单独的原生 Profile/Material 保存到受控资产挂载点，退出 Editor，再由一个新进程加载并捕获。保存前后 HDR 结果和保存成功标志见证据；这不是完整 Style Profile 保存/撤销或 Cook 验收。

计时夹具有一次明确失败：无参数 RDG 时间戳 Pass 使用了 Copy 标志，被引擎校验拒绝，在正式统计前停止。修正为无参数 Pass 的 None 标志后重跑。失败日志和对应源码哈希保留在 [Attempts.json](/generated/source/Documentation/Research/ToonLive/Evidence/Attempts.json)，没有把失败运行的 GPU 数字写入最终性能表。初版较宽的时间戳区间也作为范围较宽的参考保留。

## 决策与交付边界

推荐先实施 **A 的 Editor 私有预览**，配合合并更新和活动资源规模管理；Runtime 继续 **D 的不可变后端包切换**。B 是受限阈值预览候选；C 没有证明通用 Ramp 资源可直接接入原生逐灯响应。正式参数能力表、兼容、缓存键和阶段门槛见 [AdapterPlan](toon-adapter-plan.md)。

游戏包/Cook、正式 Runtime 修改 Profile、PSO/预加载预算、真实角色、动画、多 LOD、性能扩展曲线和完整 Specular 角域标定仍为 NotRun 或 NotVerified。本轮没有新增着色实现的 HLSL 或生产 Shader Pass；夹具的两段 RDG Pass 只记录时间戳。未修改引擎，也未改变既有 Runtime 不可变 Profile 契约。

## 可复查文件

- [Analysis.json](/generated/source/Documentation/Research/ToonLive/Evidence/Analysis.json)：逐次误差、计数、资源变化、性能与检查结果；[summary.csv](/generated/source/Documentation/Research/ToonLive/Evidence/summary.csv)用于逐行比较。
- [updates.json](/generated/source/Documentation/Research/ToonLive/Evidence/updates.json)：每次修改前后的完整累计统计和实际目标；[Validation.json](/generated/source/Documentation/Research/ToonLive/Evidence/Validation.json)记录来源与保留检查。
- [HDR 下载包](/generated/source/Documentation/Research/ToonLive/Evidence/readbacks.zip)：无损主进程与重开数据，包含浮点原始读回哈希；[重开记录](/generated/source/Documentation/Research/ToonLive/Evidence/Reopen.json)、[自动化记录](/generated/source/Documentation/Research/ToonLive/Evidence/Automation.json)、[仅研究用的保存资产](/generated/source/Documentation/Research/ToonLive/Evidence/research_assets.zip)。
- [日志节选](/generated/source/Documentation/Research/ToonLive/Evidence/Run.txt.txt)、[来源指纹](/generated/source/Documentation/Research/ToonLive/Evidence/Sources.json)、[保护检查](/generated/source/Documentation/Research/ToonLive/Evidence/Preservation.json)；旧批完整报告保持原位。
- [复现脚本](/generated/source/Documentation/Research/ToonLive/run.ps1.txt)、[新夹具](/generated/source/Documentation/Research/ToonLive/Source/ToonLiveProbe.inl.txt)、[复用的既有夹具](/generated/source/Documentation/Research/ToonGPU/Source/ToonGPUProbe.cpp.txt)、[分析器](/generated/source/Documentation/Research/ToonLive/analyze.py.txt)。分析器需要 NumPy/Pillow，可以读取交付 NPZ；不必重跑 UE 才能复算。

运行 `run.ps1 -EngineRoot <UE目录> -ResearchRoot <新的专用目录>`，再运行 `analyze.py <专用目录>/Evidence --reopen <专用目录>/Reopen`。脚本拒绝覆盖已有目录，测试创建的保存资产使用专用挂载点；原工程资源仅加载。
