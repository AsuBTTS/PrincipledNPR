# PrincipledNPR 当前项目结构

工程入口：PrincipledNPR.uproject。2026-10-04 已通过 UE 5.8.3 Editor / Game Development 构建、IDE 工程生成与 15 项 C++ 加载/绑定检查。当前是 C++ 基础骨架；完整 Resolver、S58 标定与 Face/Hair 仍待实现。

```text
PrincipledNPR/
├── PrincipledNPR.uproject
├── Source/                                  C++ 宿主工程
│   ├── PrincipledNPR.Target.cs               Game 目标
│   ├── PrincipledNPREditor.Target.cs         Editor 目标
│   └── PrincipledNPR/
│       ├── PrincipledNPR.Build.cs
│       └── Private/PrincipledNPR.cpp
├── Plugins/PrincipledNPR/
│   ├── PrincipledNPR.uplugin
│   ├── Content/README.md
│   └── Source/
│       ├── NPRRuntime/                      数据类型与最小绑定
│       │   ├── NPRRuntime.Build.cs
│       │   ├── Public/
│       │   │   ├── NPRSemanticTypes.h
│       │   │   ├── NPRStyleProfile.h
│       │   │   ├── NPRMaterialProfile.h
│       │   │   ├── NPRBackendStylePackage.h
│       │   │   └── NPRStyleBindingComponent.h
│       │   └── Private/
│       │       ├── NPRRuntimeModule.cpp
│       │       └── NPRStyleBindingComponent.cpp
│       └── NPREditor/                       编辑器扩展入口
│           ├── NPREditor.Build.cs
│           └── Private/NPREditorModule.cpp
├── Content/
│   ├── EndfieldRender/                      陈千语迁入资产
│   └── NPR/
│       ├── Characters/ChenQianyu/
│       ├── Characters/Wanderer/
│       ├── Maps/                            双角色场景与光照探针
│       ├── Materials/
│       ├── Profiles/
│       ├── Styles/AnimeFlat/
│       ├── Styles/SemiPBRStylized/
│       ├── Presets/                         预留目录
│       └── TestAssets/
├── Config/                                  UE 项目配置
├── SourceAssets/
│   ├── Wanderer/                            PMX / Blender / FBX 源文件
│   └── BackendSeeds/                        原生曲线初值
├── Documentation/
│   ├── Architecture.md
│   ├── C++骨架.md
│   ├── CppScaffold.Validation.json
│   ├── 项目结构Tree.md
│   ├── Schemas/
│   ├── Styles/
│   └── Assets/                              资产绑定与导入证据
└── Tools/
    ├── Open-PrincipledNPR.ps1
    ├── Build-PrincipledNPR.ps1
    └── 资产重建、验证与 Git 协作工具
```

构建产物 Binaries、Intermediate、Saved、.vs、生成的 .sln/.slnx、缓存与临时 work 文件保留在本机，不属于共享源码架构。Runtime 不依赖 UnrealEd；NPREditor 只参与 Editor 目标。ToonProfile 保持为预制后端资源，语义 StyleProfile 独立存在。
