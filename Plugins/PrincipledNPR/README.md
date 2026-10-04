# Principled NPR plugin skeleton

The initial descriptor is a content-only development skeleton. It contains no compiled C++ modules and does not implement the semantic resolver or backend adapter.

The first probe assets live under `/Game/NPR` so that project creation and native Toon validation can proceed independently of C++ toolchain validation.

After the S58 gates are measured, add `NPRRuntime` and `NPREditor` modules according to `Documentation/Architecture.md`. Validate the actual UE 5.8 toolchain before declaring those modules in this descriptor. Runtime must not depend on UnrealEd or modify UToonProfile settings.

Backend style packages are generated outputs; canonical StyleProfile assets must remain a separate semantic layer.
