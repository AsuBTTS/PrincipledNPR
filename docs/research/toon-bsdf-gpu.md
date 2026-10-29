<!-- Generated from Documentation/Research/ToonGPU/Report.md. Edit the original, then run npm run docs:generate. -->

# Toon BSDF 隔离 GPU 实验

日期：2026-10-28。我们在 UE 5.8.3 / CL 58210709 的独立预览场景完成了原生 Toon 后端的受控实验，保存逐用例 GPU 图像、线性像素、实际 Profile 设置和自动化记录。**这些结果支持继续开发后端适配器，但完整 Toon 功能和统一语义链路仍未交付。**

着色模型继续作为可配置参数的一部分。这里测量的是所选 `Substrate Toon BSDF` 后端；Default Lit 作为对照，也实际执行了两者之间的预编译材质切换。我们不把某个后端的接口直接定义成统一艺术参数，也不把本次切换探针描述为已经完成产品的模型选择器。

## 我们得到的结论

| 问题 | 实测结果 | 适配判断 |
| --- | --- | --- |
| 阈值与柔和度 | 单方向光、正受光区域的坐标换算和线性过渡得到测量支持；自身投影会截断背光亮区 | 需要适配；必须声明阴影条件，不能将整个 [-1,1] 外观标为精确 |
| 二阶、多阶、连续曲线与颜色 | 可见阶梯、连续过渡和 RGB 分区；关闭自投影另测背光色阶 | 原生曲线可承接，需生成 Profile 并处理过滤与阴影语义 |
| 投影与多个直接光 | 遮挡物产生真实投影；方向、点、聚光均有独立贡献；点光和聚光叠加近似线性 | 在本场景内验证原生路径；不代表所有阴影方法或光源形状已验收 |
| Sky 与 Lumen | Sky 的间接尺度 0/1 有显著差别；Lumen 开关和间接尺度有响应，红墙对球体产生可重复的间接色偏 | 在指定软件路径内验证贡献存在；不能外推至全部 Lumen 模式、动画和生产质量 |
| 高光大小、柔和度、强度与颜色 | 粗糙度改变覆盖；色阶软化边缘；Specular 改变峰值；RGB SpecularRamp 改变高光颜色 | 有原生控制能力，统一角域语义仍需拟合；没有证明它们跨视角完全独立 |
| 表面数据和组合 | BaseColor、切线空间 Normal 向量、Roughness 有响应；现有 Surface Toon 的副本与 Rim、两种 Outline 共存 | 保留既有功能，当前证据限定在静态简单几何 |
| 动态更新与槽位 | 独立 MID 的颜色更新、同一网格两槽隔离、预制 Lit/Toon 切换得到图像支持 | 采用参数写入与预制后端资源切换；没有运行时编辑 Profile 曲线 |

## 环境、来源与隔离方式

- 引擎：5.8.3、CL 58210709，Windows 11、DX12 SM6、Deferred、Substrate Blendable GBuffer；VSM 开启、硬件光追关闭。运行值见 [environment.txt](/generated/source/Documentation/Research/ToonGPU/Evidence/environment.txt.txt)。
- 实际渲染适配器：RTX 4060 Laptop GPU，指定 `-graphicsadapter=0`，以运行时 `GRHIAdapterName` 和 D3D12 日志所选设备为准。自动化报告的系统设备摘要显示 Intel UHD，这是系统枚举字段，与实际 RHI 选择不同；两份原始记录均保留。
- 分辨率 256 × 256；相机位于 (-230,0,0) cm，朝 +X，透视 FOV 40°。球半径 50 cm；光照方向、强度、遮挡物位置、基础颜色、粗糙度、间接光、捕获来源和等待帧数逐项保存在 [cases.csv](/generated/source/Documentation/Research/ToonGPU/Evidence/cases.csv)。
- GPU 目标为 RGBA16F，读回保存为 RGBA32F；[读回下载包](/generated/source/Documentation/Research/ToonGPU/Evidence/readbacks.zip)中的 `readbacks.npz` 无损压缩原始四通道数据。测量在**线性 HDR RGB**上进行。PNG 只是对读回作 sRGB 显示转换并钳制到 0–1；明亮多灯区会截白，不能用 PNG 测量 HDR 能量。`SCS_FinalColorLDR` 的两项另在 CSV 中注明，不与 HDR 直接比较。
- 关闭 TAA、Bloom、运动模糊和反射。普通用例等待 20 帧；GI 用例等待 96 帧，并以 192 帧重复对照。手动曝光，禁用物理相机曝光。测试世界启用预览世界的 Lumen primitive tracking，SceneCapture 保留 view state。
- 所有测试材质和 Profile 在内存中独立生成；仅对现有 Surface Toon 作临时副本并换成研究 Profile，不保存 .uasset 或地图。双槽网格也是临时生成的两段几何。项目代码、资产与配置的文件保留检查见 [Preservation.json](/generated/source/Documentation/Research/ToonGPU/Evidence/Preservation.json)。

这是**真实 GPU 渲染导出**，不是编辑器界面截图，也没有使用生图。拼图仅排列原图并加用例名，原始 PNG 和浮点数据均保留。

## 明暗、色阶与阴影的关系

操作顺序：先关闭点光、聚光、Sky 和 GI，固定白色方向灯；分别替换预制曲线，读取球体的世界法线和 HDR 颜色，再旋转方向灯。`Mask` 与 `Normals` 是同一相机导出的缓冲；测量时去掉球边缘两圈像素，避免轮廓覆盖率影响。基准颜色读回为 0.80078125，属于 FP16/GBuffer 表示后的值。

令 `n=N·L`，也就是表面法线和指向灯光的方向点积；`tau` 是语义阈值，`w` 是点积域的过渡半宽。候选换算为 `u=(tau+1)/2`、曲线半宽 `w/2`。

| 用例 | 目标 | GPU 测得 | 解释 |
| --- | --- | --- | --- |
| Step0 | tau=0、half-width=0.01 | 中间过渡点约 0.00681 | 与目标差小于 0.02；过滤、法线编码和自影边界仍影响像素 |
| StepHigh | tau=0.5、half-width=0.01 | 约 0.49815 | 与目标差小于 0.02；排除距目标 0.04 的边界带后，亮暗分类误差为 0 |
| SoftPositive | tau=0.5、half-width=0.15 | 中心约 0.49995，半宽约 0.15009 | 在过渡区以 HDR 与世界法线拟合；这只验证一个线性曲线条件 |
| StepLow | tau=-0.5，自投影开启 | 可见边界仍接近 0，而不是 -0.5 | 反例：不透明自影抑制了背光面，不能把曲线坐标准确当成最终边界准确 |
| StepLowNoSelfShadow | 同一阈值，关闭球体 CastShadow | 球体平均 RGB 从约 0.695 升至 0.801 | 证明背光扩展受自投影影响；此对照不作为建议关闭角色阴影的产品方案 |

![明暗与曲线 GPU 对照](/generated/source/Documentation/Research/ToonGPU/Evidence/01_diffuse.jpg)

图中 `StepLow / Step0 / StepHigh` 改变阈值，`Soft` 增加过渡，`RGB` 使用彩色色阶，`Continuous` 使用连续灰度；`Angle0/3/6` 对应灯方向旋转 0°/90°/180°。背光色阶对照见下面的反例拼图和独立 `RGBNoSelfShadow.png`。

### 投影实验

保持相机和灯方向不动，将一个真实遮挡立方体放到光路中，并以 Default Lit 同场景确认装置有效。分别测试 Profile 的 `bDiffuseRampIncludeShadow` 开关与消光系数。

原生源码中，include-shadow 关闭时近似为 `Ramp(u) × t`，开启时为 `Ramp(u×t)`，其中 `t` 是引擎送来的阴影透射值。但是这个局部公式还受外层光照调度限制：`SubstrateDeferredLighting.ush` 的 `bNeedLighting` 会在表面/透射阴影项均为零时跳过光照。**因此开启 include-shadow 并不保证完全遮挡区一定显示 Ramp(0) 的暗色。** 本次消光 100 的两组在深阴影里均接近黑；消光 10 时两种模式出现可测差别；消光 0 时两种模式一致，遮挡区域明显变亮。0 表示无消光，不表示关闭透射。

![阴影与直接光 GPU 对照](/generated/source/Documentation/Research/ToonGPU/Evidence/02_shadow_lights.jpg)

点光与聚光分别捕获后再同时开启，在球体固定区域内测量 `Both - Point - Spot + Dark` 的相对 L1 残差，约为 **0.1%**。遮挡局部灯时，其他灯仍有贡献。遮挡物本身也会反射光、部分遮住相机视线，因此遮挡用例的全图差不能直接作为纯可见度函数或精确阴影 IoU。

![自影与消光对照](/generated/source/Documentation/Research/ToonGPU/Evidence/05_counterexamples.jpg)

我们确认了本场景中的投影、受影与自影影响；没有完成动画投影 IoU、完整遮挡阈值、CSM 和全部阴影过滤质量门禁。

## Sky 与 Lumen

Sky 实验只启用指定的均匀灰色 cubemap，不启用直接光。Toon Profile 的 DiffuseIndirectScale 从 0 到 1 时，球体平均 RGB 从 0 升到约 **0.61227**；Default Lit 控制约为同值。这验证了该条件下间接漫反射尺度的作用，不代表 Toon 阶梯会以相同方式作用于 Sky。

Lumen 使用有地面和侧墙的固定装置，保持直接光不变。`GI0` 为间接尺度 0，`GI1` 为尺度 1，`GI2` 为 GI 关闭；后两项关闭贡献的结果相同。`GI1` 比 GI0 的平均 RGB 高约 **0.047**，证明存在额外间接贡献。

再把侧墙从灰白改为红色，直接灯和球体数据保持不变。由于平均亮度的变化与阴影抖动噪声相近，我们没有用亮度差宣布色彩通过，而是测固定球体区域的平均 `R-G`：灰墙接近 0，红墙约 **0.0008**，并比较两种墙各自的重复捕获。这是一个较弱但可重复的红色间接信号；具体噪声比在 [measurements.json](/generated/source/Documentation/Research/ToonGPU/Evidence/measurements.json) 的 `GI_chroma`，不把这个比值当作多样本统计置信区间。Default Lit 红墙控制也产生同向色偏。

![运行时与 GI GPU 对照](/generated/source/Documentation/Research/ToonGPU/Evidence/04_runtime_gi.jpg)

结论限定为：本机关闭硬件光追的 Lumen 预览路径下，Toon 能接收可隔离的间接贡献及颜色变化。硬件路径、离屏稳定性、Surface Cache 细节、反射、动画、性能与生产噪声质量仍需独立验收。本轮没有启用间接镜面量化。

## 高光与原有表面数据

将漫反射曲线设为黑色、关闭环境贡献，固定灯与视角，扫描 Roughness，再单独改变 SpecularRamp、Specular 和 BaseColor。

| 粗糙度 | 半峰值以上像素数 | 意义 |
| --- | --- | --- |
| 0.15 | 4 | 小于适合角宽标定的分辨率，本次只确认小高光存在 |
| 0.35 | 133 | 可用于后续固定视角拟合 |
| 0.55 | 868 | 覆盖扩大 |
| 0.75 | 3988 | 覆盖继续扩大 |

这些面积是屏幕上的受控测量，不是角域语义。Specular 0.25、0.5、1 对应峰值约 0.062、0.124、0.247，半峰值面积基本不变；这说明固定非金属条件下可控制亮度，不能外推为所有材质的独立 `SpecPeakScale`。将 SpecularRamp 的边缘展开会软化高光。将其改为蓝色后，亮点读回为蓝通道；仅改变非金属 BaseColor 为蓝色，高光仍近白色，符合 F0 与底色分工。

![高光与表面输入 GPU 对照](/generated/source/Documentation/Research/ToonGPU/Evidence/03_specular_surface.jpg)

BaseColor 换为彩色、切线空间 Normal 向量倾斜 30°、Roughness 扫描均产生预期可见变化。本次 Normal 用向量控制，**没有新增法线贴图质量、复杂原图迁移、金属度、切线各向异性或任意资产保真验收**。原表面数据与 Toon 的视觉结果并不要求和 Default Lit 相同。

## 动态配置、模型切换、双槽与组合

普通标量/向量通过已编译材质的 MID 更新。`PairBefore / PairAfter` 用两个不同 MID 验证对象隔离；`SlotBefore / SlotAfter` 在同一临时网格的两个真实 Section、两个材质槽上重复验证，第二槽使用另一份预制 Profile。只更新左槽时左侧平均通道差约 0.467，右侧约 0.000025，低于 0.005 的稳定性容差。容差用于覆盖该捕获路径的抖动噪声，不宣称像素绝对不变。

`SwitchLit / SwitchToon` 在同一组件上选择预先编译的两种材质后端，两次捕获有明确差别，切回 Toon 与原基线接近。它支持“模型参数解析为后端资源选择”的工程路线；没有证明在游戏发行包中任意模型组合均可无卡顿切换，也未执行 100 次切换的 PSO、预加载和 cook 门禁。

现有 Surface Toon 的临时副本继续使用原材质函数，新增 Rim 后有真实 HDR 差异；Screen 与 Hull 两种 Outline 均出现绿色轮廓。Screen 用 FinalColorLDR 与相同来源的 `SurfaceRimFinal` 对照，Hull 用 HDR 与 `SurfaceRim` 对照，避免混比不同捕获源。

![双槽、高光与 Outline GPU 对照](/generated/source/Documentation/Research/ToonGPU/Evidence/06_slots_specular.jpg)

最初双槽夹具误用引擎 cube helper 的非零中心，导致两块几何重叠并遮住左槽；该尝试没有作为槽隔离通过证据。最终夹具先构建居中的立方体再平移对应顶点，并检查两段 Section 的材质索引分别为 0 和 1，随后重新捕获。较早尝试仍保留在隔离工作目录；交付目录只把明确标为最终的记录作为依据。

## 对后端适配器的建议

继续保持 **Principled Parameters → Parameter Mapping → Shader / Rendering Implementation**。模型选择是离散配置，艺术参数中的阈值、宽度、颜色与高光角宽仍保留原语义；每个后端报告支持级别、输入依赖和实测范围。

1. 先做原生 Toon Adapter 的 Generic 子集：为阈值、曲线、颜色和间接尺度生成不可变 Profile，记录版本、曲线与材质资源；将阴影模式列为明确前提，负阈值被自影遮蔽时返回限制说明。
2. 高光角宽与边缘柔度继续作为需要拟合的语义，建立粗糙度、色阶、视角与灯型的标定表。当前数据不足以冻结全域公式。
3. MID 标量/向量用于可连续变化的图参数；模型、Profile 曲线与资源组合优先解析为预制材质包切换。对不支持的语义明确拒绝或报告近似，不静默忽略。
4. 保留现有 Rim、Outline 和表面输入。只有冻结需求存在已测能力缺口时，才设计局部材质函数、渲染扩展或自定义后端；普通材质图不能凭空取得逐灯阴影或任意 GI 数据。

本次未执行：Face 严格逐灯替换、Hair 完整角域瓣、各向异性切线、矩形灯、CSM、硬件 Lumen、反射、冷 DDC、Cook、角色动画、多 LOD、透明、生产性能和时间稳定性。它们保持未验收；本次结果不把既有全局生产门禁改为通过。

## 复查与复现

- [逐用例配置](/generated/source/Documentation/Research/ToonGPU/Evidence/cases.csv)、[Profile 实际设置](/generated/source/Documentation/Research/ToonGPU/Evidence/profiles.txt.txt)、[运行环境](/generated/source/Documentation/Research/ToonGPU/Evidence/environment.txt.txt)、[测量与 SHA256](/generated/source/Documentation/Research/ToonGPU/Evidence/measurements.json)、[验证汇总](/generated/source/Documentation/Research/ToonGPU/Evidence/Validation.json)。
- [原始线性读回压缩包](/generated/source/Documentation/Research/ToonGPU/Evidence/readbacks.zip)、[自动化报告](/generated/source/Documentation/Research/ToonGPU/Evidence/Automation.json)、[UE 日志节选](/generated/source/Documentation/Research/ToonGPU/Evidence/Run.txt.txt)、[引擎及夹具来源指纹](/generated/source/Documentation/Research/ToonGPU/Evidence/Sources.json)。源文件保留各 PNG 的独立文件；拼图不替代它们。
- 隔离夹具：[ToonGPUProbe.cpp](/generated/source/Documentation/Research/ToonGPU/Source/ToonGPUProbe.cpp.txt)；构建运行：[run.ps1](/generated/source/Documentation/Research/ToonGPU/run.ps1.txt)；测量：[analyze.py](/generated/source/Documentation/Research/ToonGPU/analyze.py.txt)。脚本要求提供 UE 安装路径和全新的隔离目录，拒绝覆盖上一次目录。分析器需要 NumPy 和 Pillow，可直接读取交付的 NPZ，无须重跑 UE。

在 PowerShell 中运行 `./run.ps1 -EngineRoot <UE安装目录> -ResearchRoot <新的隔离目录>`，再用带 NumPy/Pillow 的 Python 执行 `analyze.py <隔离目录>/Evidence`。不得运行会覆盖原有 Surface/Rim 历史图像的旧测试集合来代替本次探针。

源码接口背景与三条实现路线比较见[源码调研记录](/generated/source/Documentation/Research/ToonBSDF调研-2026-10-28.md.txt)。
