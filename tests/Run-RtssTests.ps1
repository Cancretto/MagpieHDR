#Requires -Version 7.0
param(
    [switch]$Compile,
    [string]$OutputDirectory = (Join-Path $env:TEMP ('Magpie-RtssTests-' + [guid]::NewGuid()))
)
$ErrorActionPreference = 'Stop'
$rtssRepo = Split-Path $PSScriptRoot -Parent
# Default is source-only. Compilation is an explicit step for the follow-up session.
& python (Join-Path $PSScriptRoot 'check_rtss_contracts.py')
if ($LASTEXITCODE) { throw 'RTSS source contracts failed.' }
& python (Join-Path $PSScriptRoot 'check_frame_refresh_bindings.py')
if ($LASTEXITCODE) { throw 'Existing frame refresh contracts failed.' }
& python (Join-Path $PSScriptRoot 'check_rendering_gpu_selection_bindings.py')
if ($LASTEXITCODE) { throw 'Existing GPU placement contracts failed.' }
if (!$Compile) { return }

$rtssOutput = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $rtssOutput -Force | Out-Null
$rtssVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$rtssVs = & $rtssVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath | Select-Object -First 1
if (!$rtssVs) { throw 'Visual Studio with C++ tools is required.' }
Import-Module (Join-Path $rtssVs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $rtssVs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$rtssRapid = Get-ChildItem -LiteralPath (Join-Path $env:USERPROFILE '.conan2/p') -Directory -Filter 'rapid*' |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'p/include/rapidjson/document.h') } | Select-Object -First 1
if (!$rtssRapid) { throw 'Restore rapidjson before compiling the RTSS settings tests.' }
& cl.exe /nologo /std:c++20 /EHsc /utf-8 /MT /DNOMINMAX "/I$($rtssRapid.FullName)/p/include" `
    (Join-Path $PSScriptRoot 'RtssSettingsTests.cpp') "/Fe:$rtssOutput/RtssSettingsTests.exe" "/Fo:$rtssOutput/RtssSettingsTests.obj"
if ($LASTEXITCODE) { throw 'RTSS settings test compilation failed.' }
& (Join-Path $rtssOutput 'RtssSettingsTests.exe')
if ($LASTEXITCODE) { throw 'RTSS settings tests failed.' }
