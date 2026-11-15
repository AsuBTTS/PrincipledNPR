<!-- Generated from Documentation/MatCap/使用说明与实现.md. Edit the original, then run npm run docs:generate. -->

# MatCap 材质光泽

我们给模型指定一张球形光泽贴图，用它添加金属反光、柔和亮面或手绘高光。正对镜头的表面取贴图中央，向侧面倾斜的表面取靠近边缘的位置。在同一材质槽中，MatCap 将这一层光泽加到原有自发光，模型原有的底色、贴图和着色仍可参与渲染。

## 1. 算法原理

### 从表面朝向得到贴图坐标

法线表示表面朝向。我们先把几何法线与材质表面法线按「表面法线影响」插值并归一化，再转到相机空间。相机空间的 X 向右、Y 向上、Z 向前。U 是贴图横坐标，V 是贴图纵坐标，均以左上角为原点。MatCap 用投影出的 X、Y 获得 UV，遮罩则用网格原有的第 0 套 UV。

传统材质读取 `PixelNormalWS`。直接连接的 Substrate Toon、Slab 和 Shading Models 从各自着色节点的 `Normal` 输入取法线。切线空间的输入先转到世界空间，未连线时使用几何顶点法线。法线贴图是否改变 MatCap 的落点，可以用「表面法线影响」在 `0` 与 `1` 之间对比。

### 透视修正与材质混合

透视视口中，画面边缘的实际观察方向与镜头正前方有夹角。开启透视修正后，我们按每个像素的观察方向旋转取样基底，再计算 UV。没有开启时，所有像素使用相机朝向作为基底。

```text
N = WorldToCamera(Normalize(Lerp(VertexNormalWS, SurfaceNormalWS, NormalInfluence)))
R = -WorldToCamera(CameraVectorWS)
P = N.xy - R.xy × (Dot(N, R) + N.z) / Max(1 + R.z, 0.001)
Q = Lerp(N.xy, P, Perspective)
UV = (0.5 + 0.5 × Q.x, 0.5 - 0.5 × Q.y)

EffectiveWeight = Weight（启用时）或 0（关闭时）
MaskMultiplier = Mask(UV0).r（指定遮罩时）或 1（空遮罩时）
Contribution = MatCapTexture(UV).rgb × Tint.rgb × MaskMultiplier × EffectiveWeight
Emissive = ExistingEmissive + Contribution
```

`R` 是从相机指向像素的单位方向，与 UE 材质节点 `CameraVectorWS` 相反。`P` 是修正后的二维投影，`Q` 按透视修正系数混合两个投影。当 `R = (0, 0, 1)` 时，`P = N.xy`，画面中心的取样不变。

例如，关闭透视修正时，相机空间法线的 `xy = (0.5, 0)` 会取样于 `UV = (0.75, 0.5)`。假设该点解码后的线性 RGB 是 `(0.8, 0.4, 0.2)`，染色为白色，线性遮罩和权重均为 `0.5`，加到原有自发光的 RGB 就是 `(0.2, 0.1, 0.05)`。当前混合方式为 **线性加法**。曝光和色调映射会继续影响最后看到的亮度。第三方算法背景见 [lilToon 源码对照](../reference/liltoon-comparison.md)。

线性加法让 MatCap 在权重为 `0` 时回到原材质输出，并能独立于 Toon 和 Rim 调整。将来如果增加乘法混合，需要指定它调制底色还是其他材质输出，再对相应着色家族逐项验证。双目视差需要用左右眼画面对照检查采样稳定性。

## 2. 当前实现与展示

**已实现并验证。** 2026-11-14 至 15，我们在 UE 5.8.4 / CL 58902052、Windows 11、RTX 4070 Laptop、DX12 SM6 验证了逐槽预览和应用、关卡保存重开及 Win64 Development 打包运行。演示截图纳入测试后，NPR 全套 25 项编辑器用例通过。着色专项覆盖观察方向、法线贴图、透视修正、手动曝光和 Toon、Rim 同槽叠加。各项使用的材质、图像与测量值见 [MatCap 验证记录](../reference/matcap-validation.md)。

### 原材质与 MatCap

![MatCap 原生窗口中同一球体的原材质和冷色光泽预览](/generated/source/Documentation/MatCap/Evidence/GuideSphereComparison.png)

图中使用 `MatCapRuntimeActor` 的 0 号材质槽。左预览是原材质。右预览使用 `T_MatCapCool` 和 `0.65` 权重，球体保留红蓝格纹，画面左上和左下多出冷色光泽。两侧共享相机和 Studio 预览灯光。

### 同一模型更换光泽贴图

| 冷色贴图 `T_MatCapCool` | 暖色贴图 `T_MatCapWarm` |
| --- | --- |
| ![倒角模型使用冷色 MatCap 的 UE 窗口截图](/generated/source/Documentation/MatCap/Evidence/GuideBeveledCool.png) | ![同一倒角模型使用暖色 MatCap 的 UE 窗口截图](/generated/source/Documentation/MatCap/Evidence/GuideBeveledWarm.png) |

两图使用 `WarmBeveledModel` 的 0 号材质槽，保持模型姿势、相机、Studio 灯光、白色染色、空遮罩和 `0.65` 权重一致。贴图变化可在倒角亮带和相邻表面观察。[冷色原图](/generated/source/Documentation/MatCap/Evidence/GuideBeveledCool.png) · [暖色原图](/generated/source/Documentation/MatCap/Evidence/GuideBeveledWarm.png)

三张图均由 UE 原生 MatCap 窗口在 `L_MatCapDemo` 上截取，见 [截图复现和 GPU 验证](../reference/matcap-validation.md)。示例关卡位于 `/Game/NPR/MatCapDemo/L_MatCapDemo`，冷色球和暖色倒角模型的已保存权重均为 `0.65`。新槽默认权重见第 4 节。

## 3. 使用方式

1. 要体验示例，在内容浏览器搜索并双击关卡 `L_MatCapDemo`。在关卡大纲选中要编辑的角色或静态网格对象，再打开 **窗口 → NPR 调制**，点击顶部 **MatCap · 材质光泽**。
2. 点左上方 **读取选中角色**。在左侧 **Mesh / 材质槽** 里选中目标，或点击预览里对应的模型区域。可先点 **隔离当前槽** 确认影响范围，再点 **显示全部材质** 检查全貌。隔离只影响预览。
3. 任选一张 **光泽贴图**，如 `T_MatCapCool`。勾选 **启用 MatCap**，把 **混合权重** 设为 `0.65`。先保持白色染色和空遮罩，这样更容易观察贴图本身的亮面形状。
4. 在预览上方勾选 **原材质对比**。左侧显示原材质，右侧显示当前效果，两侧共用相机和所选预览灯光。如果原来已应用 Toon 或 Rim，要单独比较 MatCap 的贡献，可在右侧保持同一个预览，把权重设为 `0`，再恢复到 `0.65`。
5. 确认效果后点 **应用到所选槽**，然后保存关卡。要关闭效果并保留参数，取消 **启用 MatCap**，再次应用并保存。若之前已保存的是旧版 Substrate MatCap 材质，再应用一次并保存会更新该槽的法线接线。

材质槽是 Mesh 绑定材质的位置。多个绘制区域共用一个槽时会一起变动。窗口的预览修改先写入预览材质实例，点击应用才影响关卡中的目标槽。工作区的其他操作见 [NPR 调制插件使用说明](../guides/NPR调制插件使用说明.md)。

角色材质已经内置 MatCap 时，沿用该材质原有的入口调整那一层。只有需要额外一层光泽时，才在 NPR 调制中为该槽启用 MatCap。

### 在示例关卡验收

| 观察项 | 操作 | 预期结果 |
| --- | --- | --- |
| 原材质与当前效果 | 选 `MatCapRuntimeActor` 的 0 号槽，使用 `T_MatCapCool`、权重 `0.65`，打开 **原材质对比** | 原有红蓝格纹仍可见，当前效果多出贴图中的冷色柔光。 |
| 只比较贴图差异 | 选 `WarmBeveledModel` 的 0 号槽，预览灯光选 **标准 / Studio**，保持视角、染色和 `0.65` 权重一致，依次选择 `T_MatCapCool` 与 `T_MatCapWarm` | 同一模型的亮面颜色与分布随贴图变化。倒角与平面朝向不同，取样也不同。 |
| 权重和关闭状态 | 在右侧的同一预览里将权重设为 `0`，再设回 `0.65`。也可以取消 **启用 MatCap** | `0` 或关闭状态下 MatCap 贡献消失。恢复权重和启用状态后，光泽重新显示。 |
| 保存和重开 | 需要验证持久化时，先把关卡另存为测试副本，再应用、保存并重新打开 | 目标槽恢复各自保存的光泽贴图、启用状态和权重。 |

## 4. 参数、单位与适用条件

| 面板名称 | 默认值 | 硬范围与建议 | 生效方式 |
| --- | --- | --- | --- |
| 启用 MatCap | 关闭 | 开或关 | 按槽保存。关闭时实际权重写为 `0`，保留贴图与参数。 |
| 光泽贴图 | 空 | 普通 sRGB `Texture2D`。启用前必填 | 读取投影 UV 对应的 RGB，按边缘 Clamp 采样。可先试用冷、暖示例贴图。 |
| 混合权重 | `0` | 无单位，硬范围和推荐范围均为 `[0, 1]`。预览可从 `0.65` 试起 | 统一语义 ID 是 `MatCapWeight`，映射到材质标量 `NPR_MatCapWeight`。RGB 贡献按权重线性缩放。 |
| 染色 | 线性白色 `(1, 1, 1)` | 每个 RGB 通道为有限值 `[0, 1]`，忽略 Alpha | 逐通道乘到贴图颜色。白色保留原贴图的色相和亮度。 |
| 遮罩 | 空 | 可选，使用普通 sRGB `Texture2D` | 由网格 UV0 查红通道，解码为线性值后参与计算。黑色移除光泽，白色保留光泽，清空遮罩恢复全量贡献。 |
| 表面法线影响 | `1` | 无单位，`[0, 1]` | `0` 使用几何顶点法线，`1` 使用材质表面法线。只调整 MatCap 采样。 |
| 透视修正 | `0` | 无单位，`[0, 1]` | `0` 用固定相机基底，`1` 用像素视线修正。可在透视视口中把模型移到画面边缘观察变化。 |

两个贴图槽都要求开启 sRGB，并使用非虚拟纹理、非 Normal Map 类型。面板与运行时接口使用同一套校验。除 `MatCapWeight` 外，其余选项属于当前逐槽材质后端的控制项。效果强度的语义、映射和渲染实现见 [参数参考](../principled/parameters.md)、[语义映射](../principled/mapping.md)和 [渲染管线](../developer/shader-pipeline.md)。

## 5. 材质保存与运行时

编辑器为目标槽生成派生材质，保留原图及参数。传统表面把贡献加到材质的 `Emissive Color`，直接连接的 Substrate 表面加到相应根节点的自发光输入。常规贴图和数值调整复用预览的 **MID**，即动态材质实例。点击应用后，关卡绑定保存派生材质和每槽设置，运行时加载这些资产并恢复该槽的 MID。

在关卡已绑定 MatCap 的前提下，蓝图可调用 **Set Mat Cap Parameters**。C++ 调用同一个接口，传入目标 Mesh、材质槽和完整的 `FNPRMatCapParameters`。运行时修改作用于当前实例。字段非法、启用时贴图为空或槽材质已被替换时，接口返回失败及说明。

```cpp
FNPRMatCapParameters MatCap;
MatCap.bEnabled = true;
MatCap.Texture = MatCapTexture; // 已加载的 sRGB Texture2D
MatCap.Weight = 0.65f;

FString Error;
const bool bUpdated = Binding->SetMatCapParameters(Mesh, SlotIndex, MatCap, Error);
```

## 6. 支持范围与限制

- 接入范围为 Opaque 与 Masked，包含传统材质属性、直接连接的 Substrate Toon、Slab 和 Shading Models。本页图像证据覆盖示例材质和直接连接的 Substrate Toon。复杂 Substrate 分层和半透明材质需要独立接入与验证。
- 当前每槽使用一层加法光泽，按相机方向取样。真实环境反射、灯光方向和投影阴影不参与这层计算，原材质的照明与阴影仍由原着色路径处理。
- 乘法或屏幕混合、双目视差和专用头发坐标尚未接入。角色动画、多 LOD、自动曝光过渡、生产性能及其他平台仍需专项验证。

## 7. 源码与验证入口

| 入口 | 职责 |
| --- | --- |
| `NPRMatCapGraph` | 构造原生 UE 法线、相机变换、纹理采样与混合节点。 |
| `NPRMatCapMaterialBuilder` | 复用原表面派生逻辑，追加自发光或升级旧版法线接线。 |
| `NPRMatCapEditorUI` | 处理每槽参数、预览、隔离和应用。 |
| `FNPRMatCapParameters`、`UNPRStyleBindingComponent` | 校验参数并恢复、更新已绑定槽的材质。 |
| `NPR.MatCap.ProjectionGPU`、`NPR.MatCap.EditorWorkflow`、`NPR.MatCap.Window`、`NPR.MatCap.GuideCapture`、`NPR.MatCap.RestoredMap` | 验证 GPU 读回、编辑行为、窗口、示例截图和打包后恢复。 |

详细验收条件、原始图像及复现命令见 [MatCap 验证记录](../reference/matcap-validation.md)，项目总体进度见 [实现状态与证据](../reference/status.md)。
