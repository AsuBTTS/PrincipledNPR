# C++ 项目与插件骨架

Outline 在此骨架上增加了独立 Runtime 设置/预设/绑定组件，以及 Editor 服务和双视口工具窗口。新增接口与真实验证范围见 [Outline 技术模块报告](Outline/技术模块报告.md)；下文的 `CppScaffold.Validation.json` 保留为原骨架验收，不替代 Outline 图像和重开验收。

2026-10-04 起，项目从 Content-only 转为 C++ 宿主工程，插件增加独立 Runtime / Editor 模块。源资产、现有地图和材质保留原路径。引擎基线仍为 UE 5.8.3，CL 58210709。

## 模块与分工

```text
Source/
├── PrincipledNPR.Target.cs                 Game 构建目标
├── PrincipledNPREditor.Target.cs           Editor 构建目标
└── PrincipledNPR/                         最小宿主模块
    ├── PrincipledNPR.Build.cs
    └── Private/PrincipledNPR.cpp
Plugins/PrincipledNPR/Source/
├── NPRRuntime/                            不依赖 UnrealEd
│   ├── NPRRuntime.Build.cs
│   ├── Public/
│   │   ├── NPRSemanticTypes.h              稀疏覆写、材质种类、映射质量
│   │   ├── NPRStyleProfile.h               引擎后端无关的语义资产表示
│   │   ├── NPRMaterialProfile.h            模型、默认值、资产输入要求
│   │   ├── NPRBackendStylePackage.h        预制材质资源与映射质量
│   │   └── NPRStyleBindingComponent.h      显式应用预载包、MID 标量更新
│   └── Private/                           模块加载与绑定实现
└── NPREditor/                             仅 Editor 目标加载
    ├── NPREditor.Build.cs
    └── Private/NPREditorModule.cpp         扩展入口，当前不注册自定义 UI
```

阿苏负责参考模型、后端标定和渲染接口；A 扩展语义资产与 Resolver；B 扩展 Binding 和 Editor 工作流；C 维护角色绑定、场景与验证。新增模块不代表上述工作已经完成。

## 已实现的最小接口

- `UNPRStyleProfile`：StyleId、版本、默认 Patch、材质特化 Patch、响应曲线与命名策略。没有 UToonProfile 字段。
- `UNPRMaterialProfile`：Generic / Face / Hair、模型 ID、材质默认 Patch、所需资产输入 ID。
- `FNPRFloatOverride`：bIsSet / Value；显式 0 可存储，bIsSet=false 表示继承。完整多层 Resolver 和硬域校验仍待实现。
- `UNPRBackendStylePackage`：后端 ID、语义风格 ID、材质槽资源、映射质量与 NotRun 验收状态。材质/MIC 已包含自己的 ToonProfile 引用，包加载时硬引用材质一同加载；异步预载管理尚未实现。
- `UNPRStyleBindingComponent`：不逐帧 Tick。`ApplyPackage` 先校验所有槽位并创建 MID，再应用材质；非法槽、重复槽或缺材质不会改变网格。切换包会恢复上一包管理的槽，再绑定新包。`RestoreMaterials` 只恢复仍由该组件占用的材质，避免覆盖其他系统的修改。
- `SetScalarParameter`：有限数值、非空名称、所有已绑定 MID 都存在该 Scalar 参数后才更新。这是后端材质参数入口，不等于 canonical 参数映射；缺参数会给出诊断。

这里的 Style 语义独立于 S58：UE 的 UDataAsset 是存储载体，ToonProfile 是后端资源。运行时绑定不写 ToonProfile Settings，不改变静态开关；曲线生成、映射和标定需要单独实现。

## 使用与构建

协作者安装 UE 5.8、兼容 MSVC 工具链和 Windows SDK。使用自己的安装目录，完整构建前保存并关闭编辑器：

```powershell
./Tools/Build-PrincipledNPR.ps1 -EngineRoot '<本机 UE 5.8 安装目录>' -GenerateProjectFiles
./Tools/Build-PrincipledNPR.ps1 -EngineRoot '<本机 UE 5.8 安装目录>'
./Tools/Build-PrincipledNPR.ps1 -EngineRoot '<本机 UE 5.8 安装目录>' -Target Game
```

也可右键 `.uproject` 生成 IDE 工程，再构建 `PrincipledNPREditor / Win64 / Development`。生成的 IDE 工程、Binaries、Intermediate、Saved 和缓存不提交；源码、Build.cs、Target.cs 与模块声明提交。引擎路径通过参数或 UE58_ENGINE_ROOT 提供，不写入共享源码。

编辑器加载成功后，用内置“Data Asset”创建菜单选择 `NPRStyleProfile`、`NPRMaterialProfile` 或 `NPRBackendStylePackage`，通过 Details 编辑。Blueprint Actor 可添加 `NPRStyleBindingComponent`，显式传入网格和已加载的包，检查 bool 返回值与错误字符串。

当前没有自动把 JSON 草案转成生产语义资产，也没有自动生成后端包；已有两种原生漫反射种子保持原状。包必须使用与目标网格匹配的槽位，不能把陈千语的槽号直接套给流浪者。

## 验证边界与后续任务

实际构建、模块加载与绑定验证记录见 `CppScaffold.Validation.json`。这份记录只验收 C++ 骨架，不验收光照、阴影、Lumen、Face、Hair 或完整运行时风格切换。

下一步：A 实现契约驱动 Resolver / 硬域与版本校验；阿苏实现 S58 Adapter / Generic 标定并执行 Face / Hair Gate；B 添加资源预载、Vector / Texture 更新、编辑期生成和 Preset 工作流；C 执行两角色跨风格、Cold / Warm / PSO / Cook 验证。完整三十项管线验收仍保持 NotRun。
