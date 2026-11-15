<!-- Generated from Documentation/MatCap/Validation.md. Edit the original, then run npm run docs:generate. -->

# MatCap 验证记录

2026-11-14 至 15（北京时间），UE 5.8.4 / CL 58902052，Windows 11，RTX 4070 Laptop，DX12 SM6。验证使用独立 GPU 夹具、陈千语骨骼网格、MatCap 示例关卡和 Win64 Development 打包程序。[MatCap 使用说明](../features/matcap.md) · [运行记录](/generated/source/Documentation/MatCap/Evidence/Verification.json)

## 首轮验证

| 验收项 | 方法和结果 |
| --- | --- |
| 投影、透视、权重、遮罩 | `NPR.MatCap.ProjectionGPU` 读取浮点渲染目标。中心为 `(0.50293, 0.50293, 0.25)`，半权重中心增量误差为 `0`。左右 `U` 为 `0.253418 / 0.751465`，上下 `V` 为 `0.258057 / 0.756348`。黑色遮罩返回基线。 [指标](/generated/source/Documentation/MatCap/Evidence/Metrics.json) |
| 离轴比较 | 透视相机将球放到屏幕偏右位置，对应样本 `U` 从 `0.35376` 变为 `0.510742`。颜色编码来自测试渐变输入，图像来自 UE GPU 读回。 [固定相机基底](/generated/source/Documentation/MatCap/Evidence/OffAxisFixed.png) · [透视修正](/generated/source/Documentation/MatCap/Evidence/OffAxisCorrected.png) |
| 同材质组合 | 项目 Surface Toon 母材质的 Rim 和 MatCap 共用一个槽位。MatCap 开关的线性 HDR 增量与无光照参考的增量在所测样本的 `0.025` 误差范围内。 [原 Toon 与 Rim](/generated/source/Documentation/MatCap/Evidence/ToonRim.png) · [叠加后](/generated/source/Documentation/MatCap/Evidence/ToonRimMatCap.png) |
| 编辑流水线 | `NPR.MatCap.EditorWorkflow` 使用陈千语骨骼网格的两个真实材质槽。覆盖私有预览、原材质对比、独立权重、复用预览 MID、真实纹理参数写入、组合保留、临时保存状态恢复、非法值和外部材质保护。 |
| Windows 独立运行 | Editor/Game Development 构建通过。`/Game/NPR/MatCapDemo/L_MatCapDemo` Cook 与 Stage/Archive 通过。独立可执行文件中的 `NPR.MatCap.RestoredMap` 验证关卡组件、已 Cook 材质、贴图和 `0.65` 权重恢复，随后在同一 MID 写入 `0`。 |
| 编辑器保存与重开 | 新 UE Editor 进程从磁盘读取示例场景，将带实时 MID 的场景另存为测试关卡，切换场景再重新打开。保存前、保存后和重开后，两个实际槽实例均读取到正确的冷暖贴图与 `0.65` 权重。 |
| 窗口布局与回归 | `NPR.MatCap.Window` 在两个窗口宽度捕获原生编辑器页面。十项 Editor 自动化通过，涵盖四项 MatCap 用例和原有 Rim、Surface 及 Toon 契约回归。 [窄窗口](/generated/source/Documentation/MatCap/Evidence/Narrow.png) · [宽窗口](/generated/source/Documentation/MatCap/Evidence/Wide.png) |

## 2026-11-15 GPU 回归

沿用同一 UE 版本与 GPU。`NPR.MatCap.ProjectionGPU` 在同一浮点渲染目标中完成以下验收。测试贴图用红绿通道编码 UV，截图用于观察采样位置和曝光响应。[线性 GPU 读数](/generated/source/Documentation/MatCap/Evidence/Metrics.json)

| 验收项 | 方法和结果 |
| --- | --- |
| 镜头旋转 | 平面固定，正交相机绕平面中心切换 `−30° / 0° / +30°`，中心 `U` 为 `0.25 / 0.5 / 0.753906`。 [−30°](/generated/source/Documentation/MatCap/Evidence/CameraYawMinus30.png) · [0°](/generated/source/Documentation/MatCap/Evidence/CameraYaw0.png) · [+30°](/generated/source/Documentation/MatCap/Evidence/CameraYawPlus30.png) |
| 法线贴图 | 在工程实际 Surface Toon 材质接测试法线，扣除同样有法线贴图的零权重基线后，中心 `U` 从 `0.50293` 移到 `0.806152`。将「表面法线影响」设为 `0` 后回到 `0.50293`。 [零权重基线](/generated/source/Documentation/MatCap/Evidence/NormalMapBaseline.png) · [读取法线贴图](/generated/source/Documentation/MatCap/Evidence/NormalMapOn.png) · [使用几何法线](/generated/source/Documentation/MatCap/Evidence/NormalMapExcluded.png) |
| 已生成材质 | 首轮 Toon 法线贴图接入后坐标没有移动。修正后，编辑器识别旧版法线连接，在派生副本上接入 BSDF 法线。旧图重建后的中心像素与新图相符。 [重建后的 UE 捕获](/generated/source/Documentation/MatCap/Evidence/NormalMapUpgraded.png) |
| 手动曝光 | 用 `Final Color HDR` 捕获两档曝光。`−1 EV` 时中心蓝通道为 `0.136597`，`+1 EV` 时为 `0.534668`。两个 EV 档相差两级，线性亮度约为四倍。 [−1 EV](/generated/source/Documentation/MatCap/Evidence/ExposureMinus1EV.png) · [+1 EV](/generated/source/Documentation/MatCap/Evidence/ExposurePlus1EV.png) |

| Toon 与 Rim | 同槽追加 MatCap |
| --- | --- |
| ![零 MatCap 权重时的 UE GPU 读回](/generated/source/Documentation/MatCap/Evidence/ToonRim.png) | ![完整 MatCap 权重时的 UE GPU 读回](/generated/source/Documentation/MatCap/Evidence/ToonRimMatCap.png) |

| MatCap 取几何法线 | MatCap 取材质表面法线 |
| --- | --- |
| ![表面法线影响为 0 时的 UE GPU 读回](/generated/source/Documentation/MatCap/Evidence/NormalMapExcluded.png) | ![表面法线影响为 1 时的 UE GPU 读回](/generated/source/Documentation/MatCap/Evidence/NormalMapOn.png) |

上面两组均为 `NPR.MatCap.ProjectionGPU` 的 256 × 256 UE 渲染读回。测试贴图使用红绿通道编码取样坐标。第二组的 Toon 材质均启用同一张法线贴图，变化量来自 MatCap 读取的法线。法线专项使用直接连接的 Substrate Toon，曝光专项使用固定手动曝光。

### GPU 判定条件

下表的容差均以线性读回数值计。具体采样坐标和断言由 `NPRMatCapRenderTests.cpp` 定义，对应实测值写入 [Metrics.json](/generated/source/Documentation/MatCap/Evidence/Metrics.json)。

| 对照项 | 测试通过条件 |
| --- | --- |
| 半权重与黑遮罩 | 半权重结果距零权重和全权重的中点小于 `0.003`。黑遮罩结果距零权重基线小于 `0.002`。 |
| 镜头旋转 | `−30° / 0° / +30°` 时的中心 `U` 分别落在 `0.25 / 0.5 / 0.75` 的 `0.025` 容差内。 |
| 离轴透视 | 使用同一离轴像素，开启修正后距中心 `U=0.5` 的误差至少改善 `0.08`。 |
| 法线与同槽叠加 | 法线贴图使至少一轴的 MatCap 取样变化大于 `0.1`。将法线影响设为 `0` 后，增量回到几何法线基线的 `0.025` 容差内。Toon 与 Rim 同槽时，MatCap RGB 增量与独立参考的差小于 `0.025`。 |
| 手动曝光 | 两次曝光相差 `2 EV`。高曝光的中心 RGB 各通道与低曝光的四倍值之差不超过 `0.04`。 |

扩展专项后，10 项编辑器用例及独立程序中的 `NPR.MatCap.RestoredMap` 通过。下节记录随后执行的全量回归。

## 完整回归复查

同一台机器上运行 `Automation RunTests NPR.`，24 项编辑器测试全部通过。覆盖 MatCap 的材质槽应用、窗口、GPU 图像与数值，以及 Rim、Toon 和 Surface 的原材质保留、窗口操作、着色和运行时绑定。

- 在已应用 MatCap 的陈千语槽位上，用 Toon 后端生成并应用新的组合材质。回到 MatCap 后调整权重并清空被保存进 Toon 资产的遮罩。重新挂载保存的效果后，确认仍在使用新的 Toon 材质、MatCap 权重为 `0.45`，遮罩回到白色。
- 从磁盘加载示例 MatCap 材质并转成 Substrate Toon。此前直接复制参数会崩溃。改为读取材质参数的编辑器数据后，转换与正常预览更新均通过，Substrate 法线接线同步修复。
- 重新完成 Win64 Development 构建与打包。`NPR.MatCap.RestoredMap` 验证冷色球和暖色倒角模型各自恢复贴图与 `0.65` 权重。启用状态下提交空贴图会被拒绝，设置再清空遮罩恢复白色，权重可在同一材质实例上写为零。
- 在 UE Editor 将示例关卡另存为测试关卡，加载空关卡后重新打开。两个模型的实际材质都恢复了各自的贴图与 `0.65` 权重。

## 演示截图复现

2026-11-15，在 `L_MatCapDemo` 打开后运行 `Automation RunTests NPR.MatCap.GuideCapture`。用例通过 UE Slate 原生窗口截取实际预览，输出为 `Saved/NPR/MatCap/Guide` 下的三张 `1506 × 880` PNG。窗口中的实拍图用于说明操作与视觉结果，数值门限取自独立的 GPU 读回。

| 原始截图 | 捕获条件 |
| --- | --- |
| [球体原材质对比](/generated/source/Documentation/MatCap/Evidence/GuideSphereComparison.png) | `MatCapRuntimeActor` 的 0 号槽。开启原材质对比，右侧使用 `T_MatCapCool`。 |
| [倒角模型冷色](/generated/source/Documentation/MatCap/Evidence/GuideBeveledCool.png) | `WarmBeveledModel` 的 0 号槽，贴图为 `T_MatCapCool`。 |
| [倒角模型暖色](/generated/source/Documentation/MatCap/Evidence/GuideBeveledWarm.png) | 保持上一张图的模型和相机，换成 `T_MatCapWarm`。 |

三张图使用同样的 Studio 预览灯光，权重为 `0.65`，染色为白色，遮罩为空。源码中的截图用例校验大纲名称、对比开关和实际预览材质的贴图。以 `L_MatCapDemo` 为启动关卡执行全量 NPR 回归，25 项编辑器用例通过。[自动化结果和原图校验值](/generated/source/Documentation/MatCap/Evidence/Verification.json)

## 复现自动化和打包验证

1. 首次建立演示资产时，从 UE 执行 `Tools/create_matcap_demo_inputs.py`，然后执行 `Automation RunTests NPR.MatCap.CreateDemoMap`。测试发现关卡已存在时会保留它。运行 `Automation RunTests NPR.MatCap` 会运行当前编辑器适用的用例，GPU 捕获写到 `Saved/NPR/MatCap`。
2. 用 Win64 Development 构建并打包 `/Game/NPR/MatCapDemo/L_MatCapDemo`。本机首次 Cook 通过后，UAT 卡在 Zen 的 Stage；使用 `-AdditionalCookerOptions=-SkipZenStore` 完成文件式 Cook、Stage 与 Archive。
3. 以该关卡启动打包程序，使用 `-d3d12 -ExecCmds="Automation RunTests NPR.MatCap.RestoredMap" -TestExit="Automation Test Queue Empty"`。检查实际用例通过。首次 Game 运行发现恢复路径将 Cook 中的材质槽误判成外部替换。修正后检查范围包括重新创建 MID、参数读回和运行时更新。

角色测试最初引用旧的生成 Toon 资产，加载时引擎发生访问异常。随后改用工程已存在的 Surface Toon 母材质和陈千语网格。本次验证范围包括相应的受控材质与组合。任意分层、透明、多 LOD、动画质量、完整 GI、Shipping 与生产性能需独立验证。
