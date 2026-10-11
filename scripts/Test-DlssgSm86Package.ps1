param([Parameter(Mandatory)][string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'DlssgSm86Package.ps1')
$null = Assert-DlssgSm86Files (Join-Path $PackageDirectory 'runtimes/dlssg_sm86')
# Root utility proxies can execute before our LUID-based preparation.
foreach ($name in @('nvngx_dlssg.dll', 'version.dll', 'winmm.dll', 'dbghelp.dll', 'dinput8.dll', 'dlssg_sm86.ini')) {
    if (Test-Path -LiteralPath (Join-Path $PackageDirectory $name)) {
        throw "Unexpected root DLSSG/proxy file: $name. Package may bypass the managed loader."
    }
}
Write-Output 'DLSSG community package identities verified; GPU routes and redistribution are separate acceptance items.'
