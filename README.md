# Principled NPR Material System

This is the UE 5.8 project for the controlled Substrate Toon backend spike.

The canonical surface, material and style semantics remain independent of the rendering backend. S58 is the primary implementation candidate; A0 remains the mathematical reference. A native Toon material does not establish acceptance for Face, Hair, shadows or Lumen.

## Baseline

- UE 5.8.3, CL 58210709; Desktop Deferred, DX12/SM6.
- Substrate enabled, Blendable GBuffer; Software Lumen and Virtual Shadow Maps.
- No Starter Content. Chen Qianyu is preserved under /Game/EndfieldRender; Wanderer is converted/imported under /Game/NPR/Characters/Wanderer. Original sources are preserved.
- Editor-only Python scripting is enabled to create reproducible probe assets.
- `r.Substrate.Experimental.ToonUnifiedDiffuse=0` and indirect reflection quantization disabled for the first baseline.

## Status

UE 5.8.3 installation has completed. The real non-partitioned probe map, two native ToonProfiles, two Toon materials and two physical Slab materials have been generated and saved by UE 5.8. See `Documentation/ProjectCreation.json` for setup and reopen verification.

The native A/B profiles are shadow inclusion diagnostics. `Documentation/Styles/` contains two separate uncalibrated semantic style drafts. No C++ Runtime/Editor modules, Face or Hair model implementation are claimed.

Open the project with `Tools/Open-PrincipledNPR.ps1`. The editor startup map is `/Game/NPR/Maps/L_TwoStyle_CharacterProbe`; the game default and isolated lighting baseline remain `/Game/NPR/Maps/L_S58_ToonProbe`.

## Next gate

Run the native Toon probe against the acceptance matrix. In particular, test shadow inclusion separately from unified diffuse, validate PatternUV wrapping and UV quantization before the Face offset bridge, and test the actual translator's Tangent/Anisotropy response before native Hair acceptance.

See `Documentation/` for the full design and test record.

Developer scripts are optional asset creation, validation and Git collaboration tools. They do not run when opening the project or during rendering. See [tool roles and reconstruction workflow](Tools/README.md); use the editor's Save All for ordinary saves.

## Character fixture

Chen Qianyu has passed resource loading/dependency audit and is visible with generic native S58 component material overrides. The external source project remains preserved; dirty migrated packages were explicitly saved with UE 5.8 for this backup (see Documentation/GitHubBackup.ProjectSave.json). See `Documentation/Assets/` for the audit, slot mapping, uncalibrated Head/Face/Hair binding and actual creation record. Specialized Face/Hair response and the formal pipeline tests remain unfinished. Wanderer is imported with 26 material slots, 427 bones, 65 vertex morph targets and seven PNG textures; default material bindings are saved to the owned new mesh. The two-character map has passed save/reload and generic lit display checks. Third-party character assets are local research fixtures, not plugin distribution content.

## Two style directions

Chen Qianyu defaults to `SemiPBRStylized` (Endfield direction); Wanderer defaults to `AnimeFlat` (Genshin direction). These are independent default assignments, not exclusive material/style pairings. See `Documentation/Styles/CharacterStyleAssignments.v1.json`. Two initial native diffuse curves, ToonProfiles, parent materials and 34 material instances are saved. These diffuse-only seeds are uncalibrated; angular specular, Face, Hair, normal abstraction, rim and outline semantics remain unmapped. Runtime switching and the formal thirty-test acceptance matrix remain NotRun.

## Private GitHub backup

This full project backup includes both character models, skeletons, textures, maps, material assets and original Wanderer source files. Binary assets use Git LFS. See [backup and restore instructions](Documentation/GitHubBackup.md). Keep this repository private: third-party character assets are not redistribution content.

![Initial two-character fixture](Documentation/Images/TwoStyleCharacterProbe.png)
