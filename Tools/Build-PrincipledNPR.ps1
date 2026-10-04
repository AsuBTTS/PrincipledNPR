param(
    [string]$EngineRoot = $env:UE58_ENGINE_ROOT,
    [ValidateSet('Editor', 'Game')][string]$Target = 'Editor',
    [ValidateSet('Development', 'DebugGame', 'Shipping')][string]$Configuration = 'Development',
    [switch]$GenerateProjectFiles
)
$ErrorActionPreference = 'Stop'
$taskProjectRoot = Split-Path $PSScriptRoot -Parent
$taskProjectFile = Join-Path $taskProjectRoot 'PrincipledNPR.uproject'
if (-not $EngineRoot) { throw 'Specify -EngineRoot or set UE58_ENGINE_ROOT to your UE 5.8 installation.' }
$taskEngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$taskVersion = Get-Content -LiteralPath (Join-Path $taskEngineRoot 'Engine\Build\Build.version') -Raw | ConvertFrom-Json
if ($taskVersion.MajorVersion -ne 5 -or $taskVersion.MinorVersion -ne 8) { throw 'UE 5.8 is required.' }
$taskBuildFile = Join-Path $taskEngineRoot 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path -LiteralPath $taskBuildFile)) { throw 'Unreal Build.bat is missing.' }

if ($GenerateProjectFiles) {
    & $taskBuildFile -ProjectFiles "-Project=$taskProjectFile" -Game
} else {
    if ($Target -eq 'Editor' -and $Configuration -eq 'Shipping') { throw 'Shipping is only valid for the Game target.' }
    $taskTargetName = if ($Target -eq 'Editor') { 'PrincipledNPREditor' } else { 'PrincipledNPR' }
    & $taskBuildFile $taskTargetName Win64 $Configuration "-Project=$taskProjectFile" -WaitMutex
}
if ($LASTEXITCODE -ne 0) { throw "UnrealBuildTool failed with exit code $LASTEXITCODE." }
