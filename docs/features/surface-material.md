# Surface 表面材质组合

我们用 Surface Master（表面母材质）把基础颜色、法线、自发光与多个 NPR 效果组织在同一个材质槽里。材质函数保存每个效果的计算，Recipe（预制组合说明）记录支持的效果、参数归属和组合规则，MID（运行中可调参数的材质实例）负责修改已经编译好的参数。

这个功能采用方案 B：保留现有 Runtime / Editor、Rim 和 Outline，逐步增加组合能力。**已实现并验证**：2026-10-22 在 UE 5.8.3 / Windows / DX12 SM6 的静态球体上，通过基础表面、同槽多贡献、原生 Toon + Rim 及两种 Outline 共存验证。角色、动画与生产性能仍待验收。

## 打开与使用

1. 在内容浏览器打开 `Principled NPR Content / Surface`。
2. `MI_NPRSurface_Preview` 展示基础纹理、切线法线和 Rim，继承 `M_NPRSurface_DefaultLit`。
3. 在“基础表面”组指定角色的 BaseColor、Normal、Emissive 纹理及倍率。默认颜色纹理是白色，法线是平坦法线，自发光为黑色。
4. “边缘光”组沿用 [Rim Light](./rim-light.md) 的原有四项中文参数、单位与默认值。“附加自发光”是独立的最小第二效果模块。
5. 运行时调用 `UNPRSurfaceComposerLibrary::ComposePackage`，提供 Family、候选 Recipe、请求效果与目标槽，再调用既有 `ApplyPackage`。继续用原有 `SetRimParameters` 更新 Rim；其他表面输入使用 UE 原生 MID 参数接口。

Composer 不自动复制角色原材质图。接入角色时必须明确提供原有表面纹理和数值；特殊混合、顶点位移或透明裁切不会自动迁移。请保留原材质，先在受控对象上配置和验证。

已经打开的旧编辑器进程需要重开工程，才能加载本轮新增的 Recipe 反射类型。主工程新的独立测试进程已加载这些类型并通过测试；不需要重新创建已保存的 Surface 资产。

下面依次是基础表面、开启 Rim 与第二份附加贡献、原生 Toon + Rim。图片来自本轮独立预览场景的真实渲染目标导出，分辨率 512 × 512；它们不是编辑器界面截图，也不代表角色或 Toon 质量验收。

![基础颜色纹理和切线法线的真实渲染](/generated/source/Documentation/Surface/Validation/SurfaceBase.png)

![同一个材质槽中的基础表面、Rim 和附加自发光](/generated/source/Documentation/Surface/Validation/SurfaceRim.png)

![原生 Toon 与 Rim 在同一个材质槽中显示](/generated/source/Documentation/Surface/Validation/ToonRim.png)

## 家族与资产

| 资产 | 用途 |
| --- | --- |
| `MF_NPRSurfaceInputs` | 共用基础纹理、颜色、切线法线和原有自发光计算 |
| `MF_RimLight` | 原有角域 Rim，直接引用，未修改算法 |
| `MF_NPRAdditiveColor` | 颜色乘强度的最小附加自发光模块 |
| `MF_NPRComposeEmissive` | 按固定规则相加三份贡献，不限幅 |
| `M_NPRSurface_DefaultLit` / `SR_DefaultLit_Rim` | 普通不透明表面家族与 Recipe |
| `M_NPRSurface_Toon` / `SR_Toon_Rim` | 引用已有未标定 Toon 探针 Profile 的原生 Toon 家族 |
| `MI_NPRSurface_Preview` | Default Lit 预览实例，不更改 Master 默认值 |
| `T_SurfaceValidationChecker` / `T_SurfaceValidationNormal` | 编写的测试输入纹理，不是渲染验收图 |

两个 Master 复用相同函数，只在最后的普通表面输出与原生 Toon BSDF 上不同。我们不为每一种开关组合复制一套材质。强度 0 可关闭贡献，但不保证省去全部计算；静态裁剪和性能预算需另行实测。

## 参数、算法与阶段

基础颜色为 `BaseColorTexture × BaseColor`。切线法线是平坦法线与解码 NormalTexture 按 NormalStrength 插值后归一化；切线空间表示相对于模型表面的局部方向。Rim 继续使用其原有顶点世界法线默认输入，基础法线不会偷偷改变它的语义。

```text
BaseEmissive = EmissiveTexture × EmissiveColor × EmissiveScale
AdditionalEmissiveContribution = AdditionalEmissiveColor × AdditionalEmissiveScale
FinalEmissive = BaseEmissive + RimColorContribution + AdditionalEmissiveContribution
```

Rim 不写 BaseColor 或 Normal，不覆盖 BaseEmissive。新参数的类型、默认值、硬域、推荐范围和用途列在[参数参考](../principled/parameters.md)。这些 `NPR_` 名称是 Backend 输入，**不是新增的全局 Principled 语义 ID**。原生 MID 接口不会自动执行本项目数值硬域校验，调用方需按契约检查；Rim 仍由原有 `FNPRRimParameters::Validate` 校验。

契约表里的 `[0,null]` 表示有限非负数，不设固定上界；仍不允许无穷大或 NaN。0 是有效值。

组合全部使用材质节点与 Material Functions，不新增 Custom HLSL 或独立 Shader Pass。原生 Toon 的灯光阶段仍由 UE 处理，不能把表面函数组合解释成所有效果都在同一个 GPU Pass 内完成。

## Recipe 与 Composer

`UNPRSurfaceRecipe` 保存家族、预制材质、EffectId、输出契约、SurfaceInputs / AdditiveEmissive / NativeLighting 规则、依赖及参数列表。一个实际参数只允许一个效果拥有；Rim 四项名字、类型及弧度转度映射必须符合原有契约。

Composer 按调用方提供的顺序，在指定家族中选择第一个满足请求的 Recipe；校验目标槽、类型、唯一归属、依赖顺序和必需效果。未知效果不会静默跳过，也不会偷偷换家族。选择只生成绑定包，不改网格；`ApplyPackage` 才创建 MID 并换材质，普通调参不应重复 Apply。

请求列表表示所需能力，不等于启用效果。实际强度、颜色需另写 MID。Recipe 是受控能力说明；不能仅凭同名参数推断任意图符合组合语义。实际图连接与 GPU 另行验收，生成的包保持 `ValidationStatus=NotRun`，不因选择成功就自称画面通过。

## 增加后续效果

当前 `AdditionalEmission` 通过独立函数和参数归属占用组合函数的第三个输入。加入 MatCap 等效果时，先定义输入、输出、阶段和混合规则，再在编辑期将新函数接到组合点，或经版本升级扩展组合函数，同时更新 Recipe。BaseEmissive 和 Rim 输入始终保留，各效果继续共用同一个槽 MID。

这个接口不声称完成 MatCap、角域高光或任意已光照颜色的 Multiply / Screen 混合。Toon 家族用真实 BSDF 接受同样的表面输入与自发光；一次组合验证不能替代逐灯、阴影与 GI 验收。

## Outline 与兼容范围

Screen Outline 继续使用 CustomDepth/Stencil 与后处理，Inverted Hull 继续使用辅助几何。Surface Composer 不接管 Stencil 或 PP MID。现有 Outline 的参数、UI、Preset、区域覆盖和绑定接口保留。

未来统一 Style Profile / Backend Resolver / Execution Plan 可以调用这个选择器，并把 Outline 设置交给原控制器。本次未实现完整语义 Resolver 或跨后端事务。默认只支持普通不透明 StaticMesh/SkeletalMesh；没有自动适配 Material Layers、Masked/Translucent、WPO 或任意原材质图。

## 验证与源码

本轮环境为 UE 5.8.3 / CL 58210709、Windows 11、Intel UHD Graphics、D3D12 SM6、Deferred / Substrate Blendable。主工程 Game 和 Editor 构建、两项新自动化、90 项真实二进制资产检查、17 张 GPU 捕获和 18 项汇总门禁均通过。原有三项 Rim 自动化与八项图像门禁在隔离目录重跑通过，原历史证据保留。

| 检查 | 本轮结果与范围 |
| --- | --- |
| 基础表面与 Rim | 真实纹理球体可见，现有 `SetRimParameters` 在 Composer 创建的同槽 MID 上生效 |
| BaseColor / Normal 保留 | 开关 Rim 前后两个缓冲差值均为 0；改为平坦法线后缓冲明显改变，证明测试法线贴图实际参与 |
| 原有 Emissive 与第二效果 | HDR 中分别可见；三份贡献相加的相对误差约 0.885%，小于 1% 门限；不是逐像素零误差声明 |
| Toon + Rim | 真实原生 Toon BSDF 使用现有未标定探针 Profile，与 Rim 共用同槽 MID；未验证完整 Toon 语义映射 |
| Screen / Hull Outline | 分别与 Surface 共存可见，均未替换 Surface MID；不是全套 Outline 交互、区域和时域回归 |
| 兼容性 | 本轮之前的插件源码、资产、地图与 Rim 历史证据按基线哈希保持；UI 实现未修改 |

原始[实施记录](/generated/source/Documentation/Surface/Implementation.md.txt)、[汇总门禁](/generated/source/Documentation/Surface/ImplementationValidation.json)、[真实资产检查](/generated/source/Documentation/Surface/AssetAudit.json)、[GPU 指标](/generated/source/Documentation/Surface/Validation/GpuMetrics.json)和[独立 Rim 回归](/generated/source/Documentation/Surface/RimRegression/ImageMetrics.json)可离线查看。早期默认材质捕获与缺失 Screen 的失败尝试保留在 `Documentation/Surface/Attempts`，未作为通过证据。

下面分别为 Screen 和 Inverted Hull 共存的真实渲染导出，绿色为本次测试描边。当前没有重做原有 Outline 的 UI、Preset、区域覆盖、动画或时间稳定性全流程验收。

![Surface 与 Screen Outline 共存](/generated/source/Documentation/Surface/Validation/SurfaceScreenOutline.png)

![Surface 与 Inverted Hull Outline 共存](/generated/source/Documentation/Surface/Validation/SurfaceHullOutline.png)

源码入口：`NPRSurfaceRecipe.h/.cpp`、`Tools/create_surface_master_assets.py`、`Tools/audit_surface_master_assets.py`、`Tools/check_surface_validation.py`。Runtime 用例为 `NPRSurfaceComposerTests.cpp`，GPU 用例为 `NPREditor/Private/Tests/NPRSurfaceRenderTests.cpp`。
