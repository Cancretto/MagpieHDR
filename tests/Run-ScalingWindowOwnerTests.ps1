#Requires -Version 7.0
param([string]$OutputDirectory = (Join-Path $env:TEMP ('Magpie-WindowOwner-' + [guid]::NewGuid())))
$ErrorActionPreference = 'Stop'
$ownerRepo = Split-Path $PSScriptRoot -Parent
$ownerOutput = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $ownerOutput -Force | Out-Null
$ownerVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$ownerVs = & $ownerVswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if (!$ownerVs) { throw 'Visual Studio C++ tools not found.' }
Import-Module (Join-Path $ownerVs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $ownerVs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$ownerFmt = $null
foreach ($ownerCache in @((Join-Path $env:USERPROFILE '.conan2/p'), (Join-Path $env:USERPROFILE '.conan2/p/b'))) {
    if (!(Test-Path -LiteralPath $ownerCache)) { continue }
    foreach ($ownerPackage in (Get-ChildItem -LiteralPath $ownerCache -Directory -Filter 'fmt*')) {
        $ownerInclude = Join-Path $ownerPackage.FullName 'p/include'
        if (Test-Path -LiteralPath (Join-Path $ownerInclude 'fmt/format.h')) { $ownerFmt = $ownerInclude; break }
    }
    if ($ownerFmt) { break }
}
if (!$ownerFmt) { throw 'Existing fmt headers not found.' }
& python "$ownerRepo/scripts/tests/test_scaling_window_owner.py" $ownerOutput
if ($LASTEXITCODE) { throw 'Production startup extraction failed.' }
& python "$ownerRepo/scripts/tests/test_capture_shutdown.py" $ownerOutput
if ($LASTEXITCODE) { throw 'Capture shutdown extraction failed.' }
& python "$ownerRepo/scripts/tests/test_scaling_prerequisites.py" $ownerOutput
if ($LASTEXITCODE) { throw 'Scaling prerequisites extraction failed.' }
$ownerFlags = @('/nologo', '/std:c++20', '/EHsc', '/utf-8', '/MT', '/W4', '/DNOMINMAX', '/DUNICODE', '/D_UNICODE',
    '/DFMT_HEADER_ONLY', "/I$ownerFmt", "/I$ownerOutput", "/I$ownerRepo/src/Magpie.Core/include")
foreach ($ownerSource in @("$ownerRepo/scripts/tests/scaling_window_owner_policy.cpp",
    "$ownerRepo/scripts/tests/scaling_window_owner.cpp", "$ownerRepo/scripts/tests/ngx_runtime_guard.cpp",
    "$ownerOutput/capture_shutdown.cpp", "$ownerOutput/scaling_prerequisites.cpp")) {
    $ownerName = [IO.Path]::GetFileNameWithoutExtension($ownerSource)
    & cl.exe @ownerFlags $ownerSource "/Fe:$ownerOutput/$ownerName.exe" "/Fo:$ownerOutput/$ownerName.obj" /link User32.lib Advapi32.lib
    if ($LASTEXITCODE) { throw "Test compilation failed: $ownerName" }
    & "$ownerOutput/$ownerName.exe"
    if ($LASTEXITCODE) { throw "Test failed: $ownerName" }
    if ($ownerName -eq 'ngx_runtime_guard') {
        & "$ownerOutput/$ownerName.exe" shutdown-failure
        if ($LASTEXITCODE) { throw 'NGX final shutdown failure test failed.' }
    }
}
Write-Host "Owner/startup/NGX/capture regression checks passed. Output: $ownerOutput"
