# GitHub private project backup

This repository includes the project, both character fixtures, skeletal meshes,
skeletons, textures, materials, maps, semantic drafts, research records and scripts.
Wanderer's original PMX/material source files, the converted FBX and current Blender
file are preserved under `SourceAssets/Wanderer/`.

The repository is a personal **private backup**, not a redistributable character
asset package. The source Wanderer readme forbids secondary distribution and
commercial use. Other third-party assets remain subject to their original rights.
Do not publish this full repository or bundle character files with the plugin.

## Restore

Install UE 5.8.3 or a compatible UE 5.8 build, Git and Git LFS. Use:

```sh
git lfs install
git clone https://github.com/AsuBTTS/PrincipledNPR.git
cd PrincipledNPR
git lfs pull
git lfs fsck
```

This is now a C++ project. Install a compatible Visual Studio C++ toolchain and
Windows SDK, then build the local Editor modules before opening the project:

```powershell
./Tools/Build-PrincipledNPR.ps1 -EngineRoot '<your UE 5.8 installation>' -GenerateProjectFiles
./Tools/Build-PrincipledNPR.ps1 -EngineRoot '<your UE 5.8 installation>'
```

Source, Build.cs and Target.cs files are shared; compiled modules and generated
IDE files remain local. See [current build guide](项目开发说明与约束.md) and `CppScaffold.Validation.json` for
the architecture and verified build/load scope.

Open `PrincipledNPR.uproject`. The shared EngineAssociation is `5.8`. On another
machine, select the installed UE 5.8 engine using Switch Unreal Engine Version if
needed. The PowerShell launch helper accepts `-EngineRoot` or the `UE58_ENGINE_ROOT`
environment variable; it does not assume a drive or install location.

The editor startup map is `/Game/NPR/Maps/L_TwoStyle_CharacterProbe`. Both character
mesh packages, their skeleton packages and material dependencies are included.
LFS pointers alone are not the assets: complete `git lfs pull` before opening UE.
The independent lighting probe remains `/Game/NPR/Maps/L_S58_ToonProbe`.

## What is omitted

UE caches, build intermediates, editor logs, autosave files, Git internal files and
Blender's previous-save `.blend1/.blend2` backups are omitted. The current `.blend`,
FBX, original PMX and textures are included. These omissions do not remove the
authoritative model assets.

The machine-local Android File Server SecurityToken and local engine registration
GUID are removed from shared files by a repository-local clean filter. Local UE
config and engine registration values are unchanged. Research JSON records are
also filtered to keep project-relative paths. Original local evidence is retained
under ignored `Saved/LocalResearchRecords/`.
Before committing from a new clone, run the following with your Python 3 runtime:

```sh
python Tools/configure_git_backup.py
```

## Saved package provenance

`GitHubBackup.ProjectSave.json` records the final explicit UE save before upload.
It includes dirty migrated `/Game/EndfieldRender` packages now saved by UE 5.8.
Earlier copy/audit SHA values describe the original migration snapshot. They are
not claims that those subsequently saved UE packages still match the source bytes.
The external source folders remain unchanged.

Face/Hair specialization and full semantic adapters are unfinished. Uploading
the project does not change their validation status.
