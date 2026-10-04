# UE 5.8.3 bootstrap 脚本接口依据

目标：`UE_ENGINE_ROOT`，Build.version 5.8.3 / CL 58210709。准备阶段完成源码核查与 AST 检查，随后已实际执行资产生成、保存与 Editor 重开。最终结果见 `ProjectCreation.json` 与 `AssetBootstrap.json`；这仍不等于完成光照专项验收。

重开检查发现 Python Rotator 的位置参数按 Roll/Pitch/Yaw 解释。脚本已统一改为 `unreal.Rotator(pitch=..., yaw=..., roll=...)`；已有测试灯光与相机已修正并保存，见 `ProbeTransformCorrection.json`。新工程使用本机真实登记到 UE 5.8 路径的 EngineAssociation ID，规避新 Launcher v4 已安装但旧 LauncherInstalled.dat 未同步的识别差异。

## 已确认的真实接口

| 脚本调用 | 目标安装源码 |
|---|---|
| `unreal.ToonProfileFactory` | Source/Editor/UnrealEd/Classes/Factories/ToonProfileFactory.h:10–18，真实 UToonProfileFactory；其 Python 模块导出最终由运行时 preflight 核查 |
| `unreal.ToonProfile` / `settings` | Source/Runtime/Engine/Classes/Engine/ToonProfile.h:158–164 |
| `unreal.MaterialExpressionSubstrateToonBSDF` / `toon_profile` | Source/Runtime/Engine/Public/Materials/MaterialExpressionSubstrate.h:860–935；921 为 ToonProfile EditAnywhere 属性，不能作为输入线连接 |
| `MaterialProperty.MP_FRONT_MATERIAL` | Source/Runtime/Engine/Public/SceneTypes.h:179 为 MP_FrontMaterial |
| `connect_material_property(node, "", MP_FRONT_MATERIAL)` | Source/Editor/MaterialEditor/Private/MaterialEditingLibrary.cpp:907–929，取得目标 Root FExpressionInput 并连接，返回 bool |
| `create_material_expression` / `get_material_expression_input_names` | Source/Editor/MaterialEditor/Public/MaterialEditingLibrary.h:171、325；脚本动态查询 pin 名称，避免 BaseColor/空格拼写误猜 |
| `recompile_material` 返回 errors 数组 | MaterialEditingLibrary.h:267，cpp:1034–1062 返回 FMaterialResource::GetCompileErrors；空数组可能仍未完成异步编译，因此 manifest 不声称渲染通过 |
| `LevelEditorSubsystem.new_level(path, False)` | Source/Editor/LevelEditor/Public/LevelEditorSubsystem.h:142–147；第二参数 bIsPartitionedWorld，默认为 false |
| `load_level` / `save_current_level` | 同上167、174；创建后真实 .umap 存盘检查 |
| `EditorAssetSubsystem` 保存/元数据 | Source/Editor/UnrealEd/Public/Subsystems/EditorAssetSubsystem.h:277（SaveLoadedAsset）、323（DoesDirectoryExist）、383/392（Get/SetMetadataTag） |
| `EditorActorSubsystem.spawn_actor_from_class` | Source/Editor/UnrealEd/Public/Subsystems/EditorActorSubsystem.h:228；native Actor 标签保证复跑不删除角色 |
| `DirectionalLightComponent.set_light_source_angle(0)` | Source/Runtime/Engine/Classes/Components/DirectionalLightComponent.h:297–299，BlueprintCallable SetLightSourceAngle |
| `.set_cast_shadows(True)` | Source/Runtime/Engine/Classes/Components/LightComponentBase.h:143–146，BlueprintCallable SetCastShadows |
| `Paths.project_dir / convert_relative_path_to_full` | Source/Runtime/Engine/Classes/Kismet/BlueprintPathsLibrary.h:141、519 |

`FToonProfileStruct` native 默认 Diffuse 是四阶渐变、Specular 是0.5附近阶跃；Source/Runtime/Engine/Private/Rendering/ToonProfile.cpp:534–583。脚本保留这些 native 默认，Profile A 的 IncludeShadow=false、B=true，仅构成诊断差异。两者不是完整 Anime Flat / Semi-PBR StyleProfile，不能据此声称完成两个风格。

## 所需工程插件

`PythonScriptPlugin` 开启即可使用 Python 反射与主入口。当前工程也已开启 `EditorScriptingUtilities`；脚本主要使用 UnrealEd 原生 EditorAsset/Actor/Level 子系统，并不要求第三方插件。Blueprint 工程无需先编译 C++ 或安装另一套工具链。

## 主代理复制并运行的命令示例

安装验证完成后，把脚本复制到 `.\Tools\create_principled_npr_project_assets.py`，使用真正 Editor 执行入口，而不是 commandlet：

```powershell
& 'UE_ENGINE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  '.\PrincipledNPR.uproject' `
  '-ExecutePythonScript="./Tools/create_principled_npr_project_assets.py"' `
  -unattended -nop4 -nosplash -nosound -stdout -FullStdOutLogOutput
```

`PythonScriptPlugin/Private/EditorUtilities/EditorPythonExecuter.cpp:159–163` 明确支持带空格脚本路径，188指出 `ExecutePythonScript` 不用于 commandlet。不要为这一步加 `-run=PythonScript`，不要用 `-nullrhi` 后声称 GPU 材质或 Toon 光照有效。首次 Shader/DDC 需要时间。

## 结果与失败

- 清晰状态记录：`Project/Saved/PrincipledNPR/bootstrap_manifest.json`。核心 Profile/Material/Map 保存失败即 Failed；额外 Sky/曝光/相机失败记录 warnings，仍保存核心地图。
- 设置或版本错误时拒绝运行：5.8.3-58210709、目标工程目录、Substrate Blendable、Deferred、Lumen、VSM 都有门禁。
- 复跑只复用属于 bootstrap 的资产；现有非 bootstrap 同名资产拒绝覆盖。地图整体不会删除，专属 actor tags 避免重复生成；部分配置失败的 bootstrap actor 可以重试修复，完成后用户手调会保留。
- manifest 的成功仅表示资产创建并保存；RenderingValidation 仍 NotRun。材质异步编译完成、画面截图、cold/warm DDC、Shadow/Lumen等留作真实探针。
- Directional 初始3.14159是配合手动单位曝光的诊断照度；不是作品最终的物理日照配置。

本脚本不创建 Face/Hair、不创建 C++ NPR 运行时语义资产、不替用户完成 S58 后端验收。它只建立可打开、可继续研发的最小真实 UE 研究骨架。
