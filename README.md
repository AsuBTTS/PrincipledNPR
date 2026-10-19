# Principled NPR Material System(in dev)

**[中文文档首页](docs/index.md)** · [快速开始](docs/guide/quick-start.md) · [Outline 描边](docs/features/outline.md) · [开发者指南](docs/developer/architecture.md)

我们用这套文档介绍功能、操作流程和开发方法。文档站点支持左侧分级目录和搜索；在本地执行 `npm ci`、`npm run docs:dev` 即可打开。上面的链接可以直接在 GitHub 中阅读，完整站点的预览与构建方法见[文档站点开发](docs/reference/documentation.md)。

This is the UE 5.8 project for the controlled Substrate Toon backend spike.

The canonical surface, material and style semantics remain independent of the rendering backend. S58 is the primary implementation candidate; A0 remains the mathematical reference. A native Toon material does not establish acceptance for Face, Hair, shadows or Lumen.

## Baseline

- UE 5.8.3, CL 58210709; Desktop Deferred, DX12/SM6.
- Substrate enabled, Blendable GBuffer; Software Lumen and Virtual Shadow Maps.
- No Starter Content. Chen Qianyu is preserved under /Game/EndfieldRender; Wanderer is converted/imported under /Game/NPR/Characters/Wanderer. Original sources are preserved.
- Editor-only Python scripting is enabled to create reproducible probe assets.
- `r.Substrate.Experimental.ToonUnifiedDiffuse=0` and indirect reflection quantization disabled for the first baseline.

## Status

Outline 多粒度扩展新增 Actor 分组、组件/材质覆盖、受限 Section 选择和 Hull 顶点宽度控制。详见 [使用说明](Documentation/Outline/MultiScope/使用说明.md) 与 [审查与设计](Documentation/Outline/多粒度审查与设计.md)。统一外轮廓要求 Screen Space；共享槽的独立 Section 覆盖、多 LOD Section 身份映射与透明覆盖适配仍有明确限制。

UE 5.8.3 installation has completed. The real non-partitioned probe map, two native ToonProfiles, two Toon materials and two physical Slab materials have been generated and saved by UE 5.8. See `Documentation/ProjectCreation.json` for setup and reopen verification.

The native A/B profiles are shadow inclusion diagnostics. `Documentation/Styles/` contains two separate uncalibrated semantic style drafts. The project now has a C++ host plus `NPRRuntime` / `NPREditor` plugin modules, semantic Data Asset types and a minimal prebuilt-material binding component. Resolver, backend calibration and Face/Hair models remain unfinished. See [C++ structure and build workflow](Documentation/C++骨架.md) and `Documentation/CppScaffold.Validation.json` for actual build/load verification.

Open the project with `Tools/Open-PrincipledNPR.ps1`. The editor startup map is `/Game/NPR/Maps/L_TwoStyle_CharacterProbe`; the game default and isolated lighting baseline remain `/Game/NPR/Maps/L_S58_ToonProbe`.

## Next gate

Run the native Toon probe against the acceptance matrix. In particular, test shadow inclusion separately from unified diffuse, validate PatternUV wrapping and UV quantization before the Face offset bridge, and test the actual translator's Tangent/Anisotropy response before native Hair acceptance.

See `Documentation/` for the full design and test record.

## Outline workflow slice

`Window > Principled NPR · Outline` opens the single-slot editor workflow: select a controlled skeletal character, create a child material instance, edit outline color/width/method, compare synchronized viewports, and save a preset plus the bound level. The fixture map is `/Game/NPR/Maps/L_Outline_Workflow`.

Outline offers Inverted Hull (width in cm), Screen Space (width in output pixels, requires `r.CustomDepth=3`), and None. The surface retains its original material graph and texture references; this is an Outline conversion, not a completed PBR-to-Toon surface converter. It currently accepts saved Opaque source slots. See [operation guide](Documentation/Outline/操作流程.md), [technical module report](Documentation/Outline/技术模块报告.md), and [extension workflow](Documentation/Outline/后续开发链路指导.md) for scope and actual validation evidence. This feature does not change the separate Generic/Face/Hair or S58 acceptance status.

Developer scripts are optional asset creation, validation and Git collaboration tools. They do not run when opening the project or during rendering. See [tool roles and reconstruction workflow](Tools/README.md); use the editor's Save All for ordinary saves.

Screen outlines now run after DOF and before TSR/TAA. Continuous-frame measurements, the jitter fix and its tested limits are recorded in the [temporal stability report](Documentation/Outline/Temporal/抖动修复报告.md).

Outline width now combines base width, an edge-source scale and region overrides. Hull also accepts asset vertex red and a UV0 width mask. Defaults are restrained (1.25 px / 0.1 cm). Crease and occlusion classification remain unsupported; see the [width control guide](Documentation/Outline/多层宽度控制.md) for the capability table, authored feature boundaries and inheritance rules.

## Character fixture

Chen Qianyu has passed resource loading/dependency audit and is visible with generic native S58 component material overrides. The external source project remains preserved; dirty migrated packages were explicitly saved with UE 5.8 for this backup (see Documentation/GitHubBackup.ProjectSave.json). See `Documentation/Assets/` for the audit, slot mapping, uncalibrated Head/Face/Hair binding and actual creation record. Specialized Face/Hair response and the formal pipeline tests remain unfinished. Wanderer is imported with 26 material slots, 427 bones, 65 vertex morph targets and seven PNG textures; default material bindings are saved to the owned new mesh. The two-character map has passed save/reload and generic lit display checks. Third-party character assets are local research fixtures, not plugin distribution content.

## Two style directions

Chen Qianyu defaults to `SemiPBRStylized` (Endfield direction); Wanderer defaults to `AnimeFlat` (Genshin direction). These are independent default assignments, not exclusive material/style pairings. See `Documentation/Styles/CharacterStyleAssignments.v1.json`. Two initial native diffuse curves, ToonProfiles, parent materials and 34 material instances are saved. These diffuse-only seeds are uncalibrated; angular specular, Face, Hair, normal abstraction, rim and outline semantics remain unmapped. Runtime switching and the formal thirty-test acceptance matrix remain NotRun.

## Private GitHub backup

This full project backup includes both character models, skeletons, textures, maps, material assets and original Wanderer source files. Binary assets use Git LFS. See [backup and restore instructions](Documentation/GitHubBackup.md). Keep this repository private: third-party character assets are not redistribution content.

![Initial two-character fixture](Documentation/Images/TwoStyleCharacterProbe.png)

## 中文使用手册与开发文档

正式文档入口：[docs/index.md](docs/index.md)。在工程根目录执行 `npm ci`、`npm run docs:dev` 可本地阅读；`npm run docs:build` 构建静态站点。参数或实现变化后先运行 `npm run docs:generate`，再人工检查说明与状态。原始技术资料继续保存在 `Documentation/`。详见 [文档开发指南](docs/reference/documentation.md)。
