param(
    [Parameter(Mandatory=$true)][string]$EngineRoot,
    [string]$ResearchRoot,
    [int]$Adapter = 0
)
$ErrorActionPreference='Stop'
$taskRepoRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
if(-not $ResearchRoot){$ResearchRoot=Join-Path $taskRepoRoot 'work/ToonGPUResearch'}
$taskResearchPath=[IO.Path]::GetFullPath($ResearchRoot)
if($taskResearchPath -eq $taskRepoRoot){throw 'ResearchRoot must be an isolated directory.'}
if(Test-Path (Join-Path $taskResearchPath 'PrincipledNPR.uproject')){
    throw 'ResearchRoot already exists. Choose a fresh directory to preserve previous evidence.'
}
New-Item -ItemType Directory -Force $taskResearchPath | Out-Null
Copy-Item (Join-Path $taskRepoRoot 'Source') (Join-Path $taskResearchPath 'Source') -Recurse
Copy-Item (Join-Path $taskRepoRoot 'PrincipledNPR.uproject') $taskResearchPath
$taskPlugin=Join-Path $taskResearchPath 'Plugins/PrincipledNPR'
New-Item -ItemType Directory -Force $taskPlugin,(Join-Path $taskResearchPath 'Config') | Out-Null
Copy-Item (Join-Path $taskRepoRoot 'Plugins/PrincipledNPR/Source') (Join-Path $taskPlugin 'Source') -Recurse
Copy-Item (Join-Path $taskRepoRoot 'Plugins/PrincipledNPR/PrincipledNPR.uplugin') $taskPlugin
Copy-Item (Join-Path $PSScriptRoot 'Source/ToonGPUProbe.cpp') (Join-Path $taskPlugin 'Source/NPREditor/Private/Tests')
$taskBuildRules=Join-Path $taskPlugin 'Source/NPREditor/NPREditor.Build.cs'
$taskRules=Get-Content $taskBuildRules -Raw
$taskRules=$taskRules.Replace('"AdvancedPreviewScene",','"AdvancedPreviewScene", "MeshDescription", "StaticMeshDescription",')
Set-Content $taskBuildRules $taskRules -Encoding utf8
# Shared assets are loaded only. The fixture exclusively creates transient materials/components and never saves packages.
New-Item -ItemType Junction -Path (Join-Path $taskResearchPath 'Content') -Target (Join-Path $taskRepoRoot 'Content') | Out-Null
New-Item -ItemType Junction -Path (Join-Path $taskPlugin 'Content') -Target (Join-Path $taskRepoRoot 'Plugins/PrincipledNPR/Content') | Out-Null
$taskConfig=Get-Content (Join-Path $taskRepoRoot 'Config/DefaultEngine.ini') -Raw
$taskConfig=$taskConfig -replace 'EditorStartupMap=.*','EditorStartupMap=/Engine/Maps/Entry' -replace 'GameDefaultMap=.*','GameDefaultMap=/Engine/Maps/Entry'
# Keep only renderer settings, desktop RHI and shader CVar sections; device-service credentials are irrelevant to this fixture.
$taskSections=[regex]::Split($taskConfig,'(?m)(?=^\[)')
$taskConfig=($taskSections | Where-Object {$_ -match '^\[(?:/Script/EngineSettings.GameMapsSettings|/Script/Engine.RendererSettings|/Script/WindowsTargetPlatform.WindowsTargetSettings|ConsoleVariables)\]'}) -join "`n"
Set-Content (Join-Path $taskResearchPath 'Config/DefaultEngine.ini') $taskConfig -Encoding utf8
$taskProject=Join-Path $taskResearchPath 'PrincipledNPR.uproject'
& (Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat') PrincipledNPREditor Win64 Development "-Project=$taskProject" -WaitMutex -NoHotReloadFromIDE
if($LASTEXITCODE -ne 0){throw 'Isolated build failed.'}
& (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') $taskProject /Engine/Maps/Entry -unattended -nop4 -nosplash -nosound -RenderOffscreen "-graphicsadapter=$Adapter" -dx12 -NoLiveCoding '-ExecCmds=Automation RunTests NPR.Research.ToonGPU' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$taskResearchPath/Automation" "-abslog=$taskResearchPath/Run.log"
if($LASTEXITCODE -ne 0){throw "GPU automation failed; inspect $taskResearchPath/Run.log"}
Write-Output "Evidence: $taskResearchPath/Evidence. Analyze with a Python environment containing numpy and Pillow."
