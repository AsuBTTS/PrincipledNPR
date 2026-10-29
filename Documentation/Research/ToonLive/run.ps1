param([Parameter(Mandatory=$true)][string]$EngineRoot,[Parameter(Mandatory=$true)][string]$ResearchRoot,[int]$Adapter=0)
$ErrorActionPreference='Stop'
$taskRepo=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$taskRoot=[IO.Path]::GetFullPath($ResearchRoot)
if(Test-Path -LiteralPath $taskRoot){throw 'Choose a fresh isolated directory. Existing results are never overwritten.'}
New-Item -ItemType Directory -Path $taskRoot | Out-Null
Copy-Item (Join-Path $taskRepo 'Source') (Join-Path $taskRoot 'Source') -Recurse
Copy-Item (Join-Path $taskRepo 'PrincipledNPR.uproject') $taskRoot
$taskPlugin=Join-Path $taskRoot 'Plugins/PrincipledNPR'
New-Item -ItemType Directory -Force $taskPlugin,(Join-Path $taskRoot 'Config') | Out-Null
Copy-Item (Join-Path $taskRepo 'Plugins/PrincipledNPR/Source') (Join-Path $taskPlugin 'Source') -Recurse
Copy-Item (Join-Path $taskRepo 'Plugins/PrincipledNPR/PrincipledNPR.uplugin') $taskPlugin
New-Item -ItemType Junction -Path (Join-Path $taskRoot 'Content') -Target (Join-Path $taskRepo 'Content') | Out-Null
New-Item -ItemType Junction -Path (Join-Path $taskPlugin 'Content') -Target (Join-Path $taskRepo 'Plugins/PrincipledNPR/Content') | Out-Null
$taskTest=Join-Path $taskPlugin 'Source/NPREditor/Private/Tests/ToonGPUProbe.cpp'
Copy-Item (Join-Path $PSScriptRoot '../ToonGPU/Source/ToonGPUProbe.cpp') $taskTest
Add-Content $taskTest '#include "ToonLiveProbe.inl"'
Copy-Item (Join-Path $PSScriptRoot 'Source/ToonLiveProbe.inl') (Split-Path $taskTest)
$taskRules=Join-Path $taskPlugin 'Source/NPREditor/NPREditor.Build.cs'
$taskBody=Get-Content $taskRules -Raw
$taskBody=$taskBody.Replace('"AdvancedPreviewScene",','"AdvancedPreviewScene", "MeshDescription", "StaticMeshDescription", "Json",')
Set-Content $taskRules $taskBody -Encoding utf8
$taskConfig=Get-Content (Join-Path $taskRepo 'Config/DefaultEngine.ini') -Raw
$taskConfig=$taskConfig -replace 'EditorStartupMap=.*','EditorStartupMap=/Engine/Maps/Entry' -replace 'GameDefaultMap=.*','GameDefaultMap=/Engine/Maps/Entry'
$taskSections=[regex]::Split($taskConfig,'(?m)(?=^\[)')
$taskConfig=($taskSections | Where-Object {$_ -match '^\[(?:/Script/EngineSettings.GameMapsSettings|/Script/Engine.RendererSettings|/Script/WindowsTargetPlatform.WindowsTargetSettings|ConsoleVariables)\]'}) -join "`n"
Set-Content (Join-Path $taskRoot 'Config/DefaultEngine.ini') $taskConfig -Encoding utf8
$taskProject=Join-Path $taskRoot 'PrincipledNPR.uproject'
& (Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat') PrincipledNPREditor Win64 Development "-Project=$taskProject" -WaitMutex -NoHotReloadFromIDE
if($LASTEXITCODE -ne 0){throw 'Isolated build failed.'}
foreach($taskPhase in @('Evidence','Reopen')){
    $taskFlags=@($taskProject,'/Engine/Maps/Entry','-unattended','-nop4','-nosplash','-nosound','-RenderOffscreen',"-graphicsadapter=$Adapter",'-dx12','-NoLiveCoding',
        "-ToonLiveOut=$taskRoot/$taskPhase","-ToonLiveAssets=$taskRoot/Assets",'-ExecCmds=Automation RunTests NPR.Research.ToonLive','-TestExit=Automation Test Queue Empty',
        "-ReportExportPath=$taskRoot/Automation$taskPhase","-abslog=$taskRoot/$taskPhase.log")
    if($taskPhase -eq 'Reopen'){$taskFlags+='-ToonLiveReload'}
    & (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') @taskFlags
    if($LASTEXITCODE -ne 0){throw "GPU phase $taskPhase failed. Preserve the log and inspect it before rerunning."}
}
Write-Output "Analyze with: python analyze.py $taskRoot/Evidence --reopen $taskRoot/Reopen (NumPy and Pillow required)."
