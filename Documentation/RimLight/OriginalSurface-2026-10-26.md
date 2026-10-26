# 原材质 Rim：检查记录

本次修改让 Rim 聚焦边缘光：在每个 Mesh 的具名材质槽启用，默认保留原图和贴图，不要求选择 Recipe 或重新填写 Normal、自发光等数据。Surface 及 Recipe 保留为底层能力。

## 腿部凹凸问题

此前按董提供的宽度 15.28°、柔和度 4.021317°、强度 1、白色及 `SR_DefaultLit_Rim` 复现。旧替换路径会让空 Normal Texture 继承母材质引用的 `/Engine/EngineMaterials/DefaultNormal`。这张引擎纹理有凹凸细节，不是平坦法线；强度归零仍有凹凸，把法线强度设为 0 或显式换成平坦法线后消失。因此凹凸来自替换后的基础表面，不是 Rim 角度算法。

本次新路径默认保留原图，原材质没有法线贴图时也不补入 DefaultNormal。旧 `ApplyRimPreview` 接口在 Normal Texture 为空时将法线强度写为 0，避免兼容路径继续引入该细节；显式提供贴图时保留原强度。原始本地排查在 `Saved/Investigations/LegSurface-20261026/`，不把引擎纹理导出文件纳入发布资产。

## 实施与验证范围

2026-10-26，Windows / UE 5.8.3 CL58210709 / DX12 SM6。第一阶段使用独立 DebugGame 构建与隔离测试工程；随后在用户保存并关闭主编辑器后，完成常规 Development Editor 构建与复测。

第一轮 13 项自动化通过 12 项：[原始结果摘要](OriginalSurface-2026-10-26/Run1.json)。传统材质的原图与 Rim 归零图像最大通道差为 0；Toon 首次应用在编译完成前捕获到棋盘格占位材质，导致 1 项失败。随后增加首次应用的编译完成检查，在材质通过编译前不替换槽位；DebugGame 复测 [13 项均通过，0 警告](OriginalSurface-2026-10-26/Automation.json)。更早一次复测出现过引擎联网探测超时警告，最终结果已无该警告。

验证分别检查源图不变、原纹理和 ToonProfile 保留、无 Normal 时不注入凹凸、实际 Subsurface BSDF、各槽草稿互不影响、Undo、外部材质保护及真实 GPU 改色/强度/归零。构建和静态检查不代替渲染与界面验收；动画、多 LOD、Cook、性能、复杂 Substrate 层和透明材质不在本轮通过范围内。


## 原表面真实 GPU 对比

图像来源：2026-10-26，独立预览场景，512×512，使用共享角色视口返回的同一份实际 MID，在球体上渲染。传统材质使用 WorldGrid，Toon 使用 Qianyu 的原 SemiPBRStylized 实例与贴图；这些是渲染目标导出，不是编辑器截图。

| 原 Toon 表面 | 加入 Rim 后强度 0 | 绿色 Rim |
| --- | --- | --- |
| ![原 Toon 表面](OriginalSurface-2026-10-26/Toon/Original.png) | ![保留原表面的 Rim 归零](OriginalSurface-2026-10-26/Toon/Off.png) | ![原表面上的绿色 Rim](OriginalSurface-2026-10-26/Toon/Green1.png) |

传统与 Toon 的原图和 Rim 归零图，最大 RGB 通道差均为 **0**；再次归零也为 0。Toon 红色变化像素 19717 个，红色与绿色变化分别集中在对应通道，强度 1→2 的平均绿色通道增量约 2.888。原始指标见 [传统材质](OriginalSurface-2026-10-26/Legacy/Metrics.json)、[Toon](OriginalSurface-2026-10-26/Toon/Metrics.json)及[原图差值](OriginalSurface-2026-10-26/Toon/SourcePreservation.json)。

DebugGame Editor、Development Editor 和 Development Game 构建通过。Subsurface 检查覆盖实际 BSDF/模型选择和编译，尚不宣称皮肤外观或散射质量通过验收。


## 实际窗口与保存重开

2026-10-26，隔离工程 Qianyu 骨骼网格，11 个材质槽。实际点击身体槽的启用框，使用原生取色器改为绿色；再启用脸部槽、选择 Subsurface，并点击保存。退出测试进程后用新进程重开同一关卡，两个槽的局部值、原贴图、颜色、着色选择和生成材质引用全部相同。两槽的 Profile 和 Recipe 均为空，证明新流程没有这些必填前置条件。读回记录见[保存前](OriginalSurface-2026-10-26/BeforeRestart.json)与[新进程重开](OriginalSurface-2026-10-26/AfterRestart.json)。

![实际 UE Rim 窗口：精简入口、具名材质槽与 Subsurface 选择](OriginalSurface-2026-10-26/RimSlotsSaved.png)

来源：2026-10-26，真实 UE 原生窗口截图；展示两个槽启用、脸部槽 Subsurface 选择和保存完成状态。图像不作为 Subsurface 皮肤散射质量验收。较早手动填写 Source Surface 的截图继续保留为历史证据。

界面试验第一次用资产复制接口复制 World 后立即加载，触发 UE 地图清理断言；断言发生在测试脚本的 LoadLevel，尚未执行本次材质应用。随后改用现有隔离地图并用新进程重开，上述保存与读回检查通过。该失败日志保留在本地 `Saved/RimOriginalValidation/Final/Editor.log`，自动化的 13 项结果在此界面试验前已完成。

常规 Development Editor 首次构建被主编辑器的 Live Coding 阻止。用户保存并关闭主编辑器后，Development Editor 构建通过；最终常规 DLL 的 [13 项自动化全部通过，0 警告](OriginalSurface-2026-10-26/DevelopmentAutomation.json)。随后重开主工程。另补旧 Recipe 绑定首次编辑回归，确认首次自动预览真正绑定新生成的原表面父材质，而不是只向旧 Recipe MID 写四项参数。


主工程使用更新后的常规 DLL 重新打开，当前角色为 `PNPR_ChenQianyu_EndfieldLike`。该步骤只读取角色并打开 Rim 窗口，没有保存或改写主工程角色材质。主工程窗口的最后人工复核因用户按 Esc 停止而中止，不把脚本打开窗口等同于该次界面复核完成；隔离工程的实际窗口与保存重开检查范围见上文。用户原先修改的 `MI_RimLight_Preview.uasset` SHA-256 仍为 `8c9d199de2a75a7d40596ea72944d4b1174a8113b2470f9e518d15184654df14`。


证据文件与源码的摘要见 [SHA-256 清单](OriginalSurface-2026-10-26/Hashes.json)。原始 JSON 和 PNG 保留字节；源码条目按 Git 仓库的 LF 换行快照计算。Windows 编译工作副本的原始字节摘要单独保留在[编译副本摘要](OriginalSurface-2026-10-26/WorkingCopySourceHashes.json)，避免把换行规范化造成的字节变化当成算法修改。
