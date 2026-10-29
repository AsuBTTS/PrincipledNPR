<!-- Generated from Documentation/Research/ToonLive/SourceMechanism.md. Edit the original, then run npm run docs:generate. -->

# Toon Profile 到 GPU 的更新与编译机制

日期：2026-10-28。以下依据本机 UE 5.8.3 / CL 58210709 的实际源码；路径相对于 `Engine/`。研究夹具与来源文件的哈希随实验记录保存。我们将代码路径确认标为 **SourceVerified**，只有对应运行结果才标 **GPUVerified**。

## 从 Profile 曲线到照明采样

1. `Source/Runtime/Engine/Classes/Engine/ToonProfile.h:24` 的 `FToonProfileStruct` 保存 CPU 曲线、偏移纹理和其他设置；`:159` 的 `UToonProfile` 保存 Settings 和 Guid。它是 UObject 资产，曲线不是直接嵌入材质 Shader 的一组常量。
2. `Source/Runtime/Engine/Private/Materials/MaterialInterface.cpp:2182` 附近在缓存材质统一表达式时收集 Profile、Guid 和 Settings，排队到渲染线程，调用 `ToonProfile::AddOrUpdateProfile`，并把 Profile 交给渲染代理。Profile 的注册身份与它的 RGB 曲线内容是不同信息。
3. `Source/Runtime/Engine/Private/Rendering/ToonProfile.cpp:215` 的注册/更新通过 Profile 指针和 Guid 找条目；`:289` 的 `UpdateProfile` 将设置复制到渲染线程条目，并对**全局图集**执行 `SafeRelease`。这里不是仅将某个 Profile 的一行标成 dirty。
4. 同文件 `:317` 的 `GetAtlasTexture` 在图集失效时创建/取得二维纹理数组。`:429` 起按 `(i+0.5)/R` 采样曲线，转为 FP16，建立 RDG 缓冲并排队上传；每层分别处理 Diffuse、Specular、SpecularIndirect、Shadow 四条曲线。
5. `:488` 起使用已编译的 `FToonProfileUpdateAtlasTileCS` 全局计算 Shader，将每个条目写入图集；源码循环遍历当前条目数组的所有层。分辨率 R 至少为 64，默认 64；宽为 R，高为 `5+2R`，格式 `PF_FloatRGBA`，每像素 8 字节。分辨率 64、23 层的名义纹理负载约 1.49 MiB，不含分配对齐、池驻留和上传缓冲。
6. `Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp:2089` 在渲染期间调用图集更新；`SceneRendering.cpp:2012` 将图集绑定进 View。`Shaders/Private/ToonProfileCommon.ush:15` 通过 Profile slice 和曲线行采样；支持独立 sampler 时使用双线性钳制 sampler。
7. `Shaders/Private/Substrate/SubstrateToonBSDF.ush:140` 起先将有符号 NoL 映射到曲线坐标，按 Profile 配置决定阴影是否进入坐标，叠加偏移，再采样曲线。渲染结果仍受外层光照调度和可见度影响。

“执行计算 Shader”不等于“重新编译 Shader”。上面的 atlas compute 是使用已有 Shader 对新数据执行计算；新 Shader 编译需要另走材质翻译、ShaderMap/任务系统。

## 编辑器通知路径

`ToonProfile.cpp:610` 的 `UToonProfile::PostEditChangeProperty`：

- 调用 UObject 基类编辑通知；
- 复制完整 Settings，通知 Renderer 使路径追踪输出失效；
- 排队一个渲染线程命令，调用图集管理器 `UpdateProfile`。

这个函数没有调用材质图 `PostEditChange`、材质重新翻译或提交 Shader 编译任务。**这只是源码事实，不足以单独证明一次实际编辑不会经其他系统间接触发编译。** 本轮因此在实际调用前后记录编译累计计数、材质翻译次数、材质资源与 ShaderMap 身份，另做自然渲染路径的首帧捕获，见[实验报告](toon-live.md)。

`FToonProfileStruct::Invalidate()` 的名字容易误解：它只是把结构重置成默认值，不是 GPU 刷新通知。保存资源、修改曲线、通知图集也是三件不同的事；运行期间可以刷新成功但尚未保存。

## 为什么同尺寸纹理上传可能没有效果

`ToonProfile.cpp:336` 的自动检测只比较偏移/排线纹理当前资源尺寸与缓存尺寸。相同尺寸的像素内容上传，不会通过这个条件自动使已烘焙图集失效。偏移纹理也不是逐灯照明时直接读取的任意用户纹理：它先烘焙进图集纹理块。

本轮 C_Stale 负对照保留相同尺寸，只上传新像素；随后 C_Refresh 通过 Profile 通知使图集重建。这两步有对应 GPU 读回，而非只根据源码推测。外部曲线资源发生变化时也应核对通知链；不能仅凭外部 Curve 对象可编辑便保证所有窗口会自动刷新 Toon 图集。

## 曲线内容、材质图和编译置换的区别

| 修改对象 | 代码路径与判断 | 本轮证据 |
| --- | --- | --- |
| 已注册 Profile 的曲线内容/inside-shadow 数据 | 复制 Settings、使图集失效、重新采样/上传/compute；通常不要求改变材质图 | Editor 通知路径的累计计数和 GPU 图像已测 |
| MID 的已编译标量/向量/纹理参数 | 更新实例统一参数；不自动给原生 BSDF 增加缺失 pin | B 的均匀偏移参数已测，完整宽度 remap 不支持 |
| Profile 引用或母材质/着色模型/静态开关 | 可能涉及资源注册、材质更新或新的编译置换；需要按实际入口测量 | 本轮 D 只选择已生成并编译的资源，未证明任意图修改免编译 |
| `ToonUnifiedDiffuse`、间接镜面量化开关 | `Private/ShaderCompiler/ShaderCompiler.cpp:4155` 起进入 Shader define；`LightRendering.cpp:2768` 还缓存 unified 设置 | SourceVerified；本轮固定为 0，不把它们当作普通滑块 |
| 图集分辨率/活跃层数 | 影响资源尺寸、循环与上传量；不等于一个实例参数 | 本轮固定 R=64、观察到 23 层，不外推多角色预算 |

## 编译计数的测量依据

夹具读取 `FShaderCompilingManager::GetLocalStats` 并保存完整 `FShaderCompilerStats::ToJson()` 快照。`Engine/Public/ShaderCompiler.h:493` 起的 Counters 包括累计 JobsAssigned/JobsCompleted、输入哈希和 DDC 计数；`:832` 起提供统计 API。`Private/ShaderCompiler/ShaderCompilerStats.cpp:1085` 记录实际材质翻译调用。

只看瞬时 pending=0 不足以排除刚刚完成的编译。本轮使用各更新前后的累计增量，并核对测试前预热与测试后总量。DDC hit 没增加表示本次更新根本没有走到新的材质缓存查询证据，不能反过来说“因为命中 DDC 所以无需编译”。

## Editor 与 Runtime 的边界

- `PostEditChangeProperty` 受 `WITH_EDITOR` 限定，是编辑器资源编辑路径。
- `ToonProfile::AddOrUpdateProfile` 是头文件中导出的非 `WITH_EDITOR` C++ 函数，需要在渲染线程调用；它接收设置快照，可以在研究夹具内驱动同一资源路径。本轮在 **Editor 进程**执行了该队列路径，不能将其称为游戏包验收。
- 同一头文件明确写着 Profile 不要在运行时更改，官方 [UToonProfile API](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UToonProfile) 也保留该边界。导出的底层函数不等于可对外承诺稳定的 Blueprint/游戏玩法热编辑契约。
- 现有项目契约要求 Runtime 使用不可变的预制后端资源，本轮保持这个规则。推荐先把可变 Profile 用于 Editor 的私有预览副本；Runtime 继续预制包切换。是否另立游戏运行时可变 Profile 能力，必须后续明确修改契约，并完成 Cook、生命周期、线程及平台验证。

共享一个 Profile 时，更新会影响所有引用它的 Material/MID。新建 MID 不会自动复制 Profile。隔离的预览资源需要以 Mesh/Slot/后端实例为所有权键；本轮用两个不同母材质/不同 Profile 和共享 Profile 两种情况验证，不宣称尚未测试的 ProfileOverride setter 能完成同样工作。
