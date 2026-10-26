# HUA 合并版本联调基线

日期：2026-10-26；版本：`b551a3da09ff0a7c4da423a841ec28caebc4b635`。本页保留修复前的结果，新工作区结果另见 [工作区说明](../Editor/NPRWorkspace.md)。

我们先 fetch 并快进本地 dev。用户原有 `MI_RimLight_Preview.uasset` 的改动已备份，SHA-256 为 `8c9d199de2a75a7d40596ea72944d4b1174a8113b2470f9e518d15184654df14`；本次没有覆盖该资产。

## 已通过的范围

- UE 5.8.3 / CL 58210709 的 Game 与 Editor 构建，7 项 C++ 自动化零错误、零警告。
- Windows 11 / Intel UHD Graphics / D3D12 SM6：8 张 Rim 和 17 张 Surface 真实 GPU 导出。BaseColor、Normal 差值为 0；HDR 合成相对误差约 0.885%，小于现有 1% 门限；Rim 的 7 项像素指标通过。
- Surface 实际二进制资产 90 项检查通过。保存后重开、Profile 切换、非法输入拒绝、0/黑色和 11 槽骨骼网格的单槽应用等 27 项补充检查通过；这些读回使用 NullRHI，不作为画面验收。
- 四个 Reset 按钮及保存通过：覆盖项确实被删除，恢复继承的 12°、2°、强度 0.4 和蓝色，而非写入 0。

## 复现的问题

1. **角色切换覆盖外部材质。** 读取有保存绑定的 SphereA；外部将槽 0 替换为 `M_RimLight`；选中 SphereB 后点击 Read Selection。SphereA 被改回新建的 Surface MID。`FImpl::Release` 在恢复后无条件重应用保存配置，绕过了运行时的所有权保护。前后读回见 `Integration-2026-10-26/NativeExternalBefore.txt` 和 `NativeReadback.json`。
2. **Undo 丢失上一步未保存值。** 保存强度 0.4，连续修改为 0.8、1.6，退出输入框后撤销一次，界面与 MID 都回到 0.4，期望为 0.8。`PostUndoRedo` 重新读取 Profile，且覆盖意图保存在不可撤销的普通布尔数组中。见 `Undo.png` 及 `Readback/R4-Intensity08.json`、`R4-Intensity16.json`、`R4-Undo.json`。
3. **图像门禁沿用旧用例数量。** `check_rim_binding_images.py` 要求恰好 3 项 Rim 测试，新增 StyleProfile 和 ProfileBinding 后实际为 5 项，导致所有测试与像素指标通过时仍报失败。失败原件保存在 `Rim/ImageMetrics.json`。

两次辅助 Python/Slate 采样会话发生访问异常，相关日志保留，不能据此断言 HUA 渲染代码崩溃。角色切换问题随后在没有后台采样的原生会话中重现。完整原始日志、截图、捕获哈希和源码/DLL 哈希保存在 [汇总](Integration-2026-10-26/Summary.json)。旧的 2026-10-22 验收证据保持原样。
