# Reusable plugin content

`Outline/` contains three Unreal material assets and a default width mask generated and saved by
`Tools/create_outline_backend_assets.py`:

- `M_OutlineHull`: masked reverse hull, world-normal expansion in centimetres, asset vertex red and UV0 width-mask factors.
- `M_OutlineMask`: selected-slot visibility for helper meshes.
- `M_OutlineScreen`: CustomDepth/Stencil postprocess outline, pixel radius and RGB color.
- `T_OutlineWidthMaskWhite`: owned linear Masks texture; neutral default for Hull width.

These templates are referenced by `NPROutlineComponent` and `NPROutlineActorComponent`; transient MIDs and helper
components are reconstructed after loading. Generated character MICs and the
controlled fixture remain under `/Game/NPR/Outline`.

See `Documentation/Outline/多层宽度控制.md` for base width, supported edge-source scales,
region overrides and algorithm limits. Generic/Face/Hair surface acceptance and cooked visual acceptance remain separate.
