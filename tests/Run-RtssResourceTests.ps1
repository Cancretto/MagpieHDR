#Requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$RuntimeDirectory,
    [Parameter(Mandatory)][string]$GeneratedDirectory,
    [string]$OutputDirectory = (Join-Path $env:TEMP ('Magpie-RtssResourceTests-' + [guid]::NewGuid()))
)
$ErrorActionPreference = 'Stop'
$rtssRepo = Split-Path $PSScriptRoot -Parent
$rtssOutput = [IO.Path]::GetFullPath($OutputDirectory)
$rtssRuntime = [IO.Path]::GetFullPath($RuntimeDirectory)
if (!(Test-Path -LiteralPath (Join-Path $rtssRuntime 'resources.pri'))) { throw 'Compiled product resources.pri is required.' }
New-Item -ItemType Directory -Path $rtssOutput -Force | Out-Null
& python (Join-Path $PSScriptRoot 'prepare_rtss_resource_tests.py') $rtssOutput
if ($LASTEXITCODE) { throw 'Production getter extraction failed.' }
Copy-Item -LiteralPath (Join-Path $rtssRuntime 'resources.pri') -Destination $rtssOutput -Force
$rtssVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$rtssVs = & $rtssVsWhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath | Select-Object -First 1
if (!$rtssVs) { throw 'Visual Studio with C++ tools is required.' }
Import-Module (Join-Path $rtssVs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $rtssVs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$rtssConanRoots = @((Join-Path $env:USERPROFILE '.conan2/p'), (Join-Path $env:USERPROFILE '.conan2/p/b'))
$rtssFmt = $rtssConanRoots | ForEach-Object { Get-ChildItem -LiteralPath $_ -Directory -Filter 'fmt*' } |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'p/include/fmt/xchar.h') } | Select-Object -First 1
if (!$rtssFmt) { throw 'Restore fmt before compiling the resource test.' }
$rtssExe = Join-Path $rtssOutput 'RtssResourceTextTests.exe'
& cl.exe /nologo /std:c++20 /EHsc /utf-8 /MT /O2 /W4 /WX /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DWINRT_NO_MODULE_LOCK /DFMT_HEADER_ONLY `
    "/I$GeneratedDirectory" "/I$rtssOutput" "/I$rtssRepo/src/Magpie" "/I$rtssRepo/src/Shared" "/I$($rtssFmt.FullName)/p/include" `
    (Join-Path $PSScriptRoot 'RtssResourceTextTests.cpp') "/Fe:$rtssExe" "/Fo:$rtssOutput/RtssResourceTextTests.obj" `
    /link WindowsApp.lib /MANIFEST:EMBED "/MANIFESTINPUT:$PSScriptRoot/EffectChoiceItemsTests.manifest"
if ($LASTEXITCODE) { throw 'Native resource test compilation failed.' }
Push-Location $rtssOutput
try { & $rtssExe; if ($LASTEXITCODE) { throw 'Native resource text test failed.' } }
finally { Pop-Location }
