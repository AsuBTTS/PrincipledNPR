# Principled NPR plugin skeleton

The plugin declares C++ `NPRRuntime` and `NPREditor` modules. Runtime provides semantic Data Asset types, an explicit prebuilt-material binding component, and the independent Outline preset/binding/rendering feature. Editor provides the Outline workflow window, shared conversion/preset services, and synchronized original/NPR preview viewports. The generic semantic resolver and S58 backend adapter remain unimplemented.

The Outline slice uses owned reusable materials in `/PrincipledNPR/Outline`, plus project-specific character fixtures in `/Game/NPR`. See `Documentation/Outline/操作流程.md`, `技术模块报告.md`, and `后续开发链路指导.md`. Its child MIC preserves the source surface; Outline success does not establish Generic/Face/Hair acceptance.

For the actor workflow, button behavior, preset reapplication, and operation feedback, see [Outline 按钮说明与修复](../../Documentation/Outline/按钮说明与修复.md). Actor mode uses an enable/disable control that preserves region rules; the material-slot conversion controls appear only in the single-slot workflow.

The first probe assets live under `/Game/NPR` so that project creation and native Toon validation can proceed independently of C++ toolchain validation.

See `Documentation/C++骨架.md` and `Documentation/CppScaffold.Validation.json` for the module layout and actual validation status. Runtime has no UnrealEd dependency and does not modify UToonProfile settings. Native S58 acceptance remains separate from C++ build acceptance.

Backend style packages are generated outputs; canonical StyleProfile assets must remain a separate semantic layer.
