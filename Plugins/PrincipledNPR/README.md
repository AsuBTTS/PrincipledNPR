# Principled NPR plugin skeleton

The plugin now declares C++ `NPRRuntime` and `NPREditor` modules. Runtime provides semantic Data Asset types and an explicit prebuilt-material binding component. Editor provides a module extension point and uses built-in Data Asset/Details workflows. The semantic resolver and backend adapter remain unimplemented.

The first probe assets live under `/Game/NPR` so that project creation and native Toon validation can proceed independently of C++ toolchain validation.

See `Documentation/C++骨架.md` and `Documentation/CppScaffold.Validation.json` for the module layout and actual validation status. Runtime has no UnrealEd dependency and does not modify UToonProfile settings. Native S58 acceptance remains separate from C++ build acceptance.

Backend style packages are generated outputs; canonical StyleProfile assets must remain a separate semantic layer.
