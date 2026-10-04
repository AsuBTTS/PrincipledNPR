# S58 probe guide

Open `PrincipledNPR.uproject` using the installed UE 5.8 editor, or run `Tools/Open-PrincipledNPR.ps1`.

After bootstrap completes, the startup map is `/Game/NPR/Maps/L_S58_ToonProbe`.

The scene contains native Toon A/B and a physical Substrate Slab control. A/B are diagnostic profiles with `DiffuseRampIncludeShadow` off/on. They are not the two final semantic StyleProfiles. Their purpose is to isolate the native shadow order before fitting semantic parameters.

The movable directional light uses zero source angle for the signed-dot baseline. Preserve fixed exposure for comparisons. Change one feature at a time; save evidence with engine patch/CL, GPU/RHI, console settings and the actual Profile settings.

## Setup and acceptance are separate

- Creating and saving map/material/Profile assets verifies setup only.
- Immediate material compile errors and final shader completion are separate records.
- Scene visibility does not prove correct Cast/Receive/Self Shadow, VSM/CSM or Lumen response.
- Face offset bridging requires an interior calibrated atlas domain because native UVs use 12-bit quantization and frac/Wrap.
- Native Hair requires a direction response test of the actual material translator; exposed Tangent/Anisotropy pins alone are insufficient.

Use the formal `UE5.8_Toon_技术探针记录.json` matrix in this directory. Unexecuted tests stay `NotRun` with a null verdict.

## Next implementation gates

1. Save/reopen and shader compilation checks, then native lighting/shadow tests.
2. Parameter mapping and two semantic Style assets through a backend adapter.
3. Face and Hair gates using the unchanged reference models.
4. Runtime/Editor C++ modules after validating the actual UE 5.8 toolchain.
