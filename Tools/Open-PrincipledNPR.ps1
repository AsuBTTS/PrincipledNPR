param([string]$EngineRoot = $env:UE58_ENGINE_ROOT)
$ErrorActionPreference = 'Stop'
$taskProjectFile = Join-Path (Split-Path $PSScriptRoot -Parent) 'PrincipledNPR.uproject'
if (-not $EngineRoot) {
    throw 'Specify -EngineRoot <UE 5.8 install directory> or set UE58_ENGINE_ROOT. You can also open the .uproject with your installed UE 5.8 editor.'
}
$taskEngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$taskVersion = Get-Content -LiteralPath (Join-Path $taskEngineRoot 'Engine\Build\Build.version') -Raw | ConvertFrom-Json
if ($taskVersion.MajorVersion -ne 5 -or $taskVersion.MinorVersion -ne 8) {
    throw 'This project requires the installed UE 5.8 engine.'
}
$taskEditorFile = Join-Path $taskEngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path -LiteralPath $taskEditorFile) -or -not (Test-Path -LiteralPath $taskProjectFile)) {
    throw 'Editor or project descriptor is missing.'
}
Start-Process -FilePath $taskEditorFile -ArgumentList ('"' + $taskProjectFile + '"') -WorkingDirectory (Split-Path $taskProjectFile -Parent) -WindowStyle Hidden
