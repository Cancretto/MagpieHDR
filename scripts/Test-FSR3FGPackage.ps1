#Requires -Version 7.0
param([Parameter(Mandatory)][string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
# Read-only payload validation. File hashes pin the matched SDK 2.3.0 pair;
# DLL product version does not describe the selected 3.1.6 algorithm provider.
$required = [ordered]@{
    'amd_fidelityfx_loader_dx12.dll' = 'E2D85AA05A9BD9ED8B38935FDF5199372CCA6F74C12015143BB6F945EE1608AA'
    'amd_fidelityfx_framegeneration_dx12.dll' = '02297BEEDD285E822D3A64F314CF00FAF378DCEC0EDC47FF0C4DD71B3A8C2F18'
    'effects/FSR3FG/FSR3_FrameGeneration.hlsl' = $null
    'AMD-FSR-SDK-LICENSE.md' = $null
    'AMD-FSR-SDK-THIRD-PARTY.md' = $null
    'FSR3FG-README.md' = $null
}
foreach ($entry in $required.GetEnumerator()) {
    $path = Join-Path $PackageDirectory $entry.Key
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "FSR3FG package is missing $($entry.Key)" }
    if ($entry.Value -and (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.Value) {
        throw "FSR3FG runtime differs from SDK 2.3.0: $($entry.Key). Validate provider/API compatibility before updating the pin."
    }
}
'FSR3FG payload matches the pinned SDK pair; this does not validate runtime or GPU behavior.'
