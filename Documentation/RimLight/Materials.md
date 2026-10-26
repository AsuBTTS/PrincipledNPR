# Rim Light 四项材质控制

我们按数学报告第 4.5 节创建可复用的角度边缘光材质函数。本页保留 2026-10-21/22 的材质构造与验证范围。2026-10-26 已补齐 RGB Profile、显式角色槽位绑定、实时窗口与隔离预览，当前操作见[统一效果工作区](../Editor/NPRWorkspace.md)；完整语义 Resolver 仍未完成。

## 资产与参数

- `/PrincipledNPR/RimLight/MF_RimLight`：纯节点材质函数，输出额外 RGB 贡献与 RimMask。
- `/PrincipledNPR/RimLight/M_RimLight`：普通 Opaque / Default Lit 表面材质，固定灰色基础表面，函数输出接 Emissive。没有引用 ToonProfile。
- `/PrincipledNPR/RimLight/MI_RimLight_Preview`：默认 8° / 2° / 强度 1 / 白色，方便直接查看。基础材质和函数的强度默认值仍为 0。

实例只有四项用户控制：亮边宽度、边缘柔和度、边缘光强度、边缘光颜色。完整单位、硬域、推荐范围和后端映射在 `MaterialParameters.v1.json`。前两项实例参数用度数，进入函数前乘 π/180，函数仍使用现有 canonical 弧度 ID。2026-10-22 将颜色正式定义为可选语义 ID `RimColor`，默认线性白色；旧标量 ID 保持不变，旧风格缺颜色时使用白色。后续 Profile 已增加独立 `Colors` 覆盖，原有标量 `Values` 结构继续保留。

## 计算与阶段

N、Ng、V 分别是世界空间 Rim 法线、几何法线和指向相机的方向，先归一化。默认 N 与 Ng 都来自顶点法线，可在函数调用处替换 N。

`gamma = asin(saturate(dot(N,V)))`

`w = max(艺术过渡半宽, 0.5 * (abs(ddx(gamma)) + abs(ddy(gamma))), 1e-5)`

`u = saturate((gamma - 亮边宽度 + w) / (2*w))`

`shape = 1 - u*u*(3-2*u)`

按同一 Band 算法对 `dot(Ng,V)` 在 0 处做抗锯齿的正面门控。最终贡献为 `RGB颜色 * 强度 * shape * frontGate`。亮边颜色 Alpha 不参与计算。由于使用屏幕导数，函数只能接到像素着色路径，不能接 WPO。

这是角度外观层，默认策略为独立自发光贡献，不取原生灯光可见度或屏幕深度。普通场景深度测试会挡住不可见表面，但投射到可见角色上的阴影不会自动抑制这份 Emissive；最终显示仍受曝光、色调映射、雾与 GI 路径影响。没有独立 Shader Pass，没有新增 HLSL 文件。报告中的受光 Mask 和可见度策略是后续范围。

## 兼容与操作

构造来源是 `Tools/create_rim_light_assets.py`，仅创建自有目录下的三个资产；已有同名但无归属标记的资产会报错，已有自有资产保留人工调整。它不替换角色材质、不保存用户地图。

运行时 `FNPRRimParameters` 提供四项强类型值、`Validate` 与弧度转材质度数的方法，交接规则见 [② 参数契约](ParameterHandoff.md)。第③步新增 `UNPRStyleBindingComponent::SetRimParameters`，检查全部绑定槽后整组写入三个 Scalar 和一个 Vector，支持颜色与实时更新，见 [③ 动态调参](RuntimeBinding.md)。旧 `SetScalarParameter` 继续直接按材质名写标量，不自动做 Rim 转换。Profile 存储和角色绑定已在[编辑工作流](EditorWorkflow.md)接通；完整 Principled 语义解析仍未完成。

## 验证边界

历史创建与当时二进制哈希见 `MaterialCreation.json`；2026-10-22 当前资产的参数与哈希以 `ParameterExposureValidation.json` 为准，34 项只读检查通过，没有重存人工编辑的资产。契约交叉检查见 `ContractValidation.json`，C++ 执行范围见 `ImplementationValidation.json`。材质编译、独立进程重载与真实图像检查分别记录，未运行的项目保持 NotRun。没有把参考工程的材质代码或图片复制进本实现。
