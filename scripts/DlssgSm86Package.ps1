# The C++ package table is authoritative for restore, packaging and both hosts.
# MSBuild may launch Windows PowerShell with an inherited PowerShell 7 module path.
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -ErrorAction Stop
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Security\Microsoft.PowerShell.Security.psd1') -ErrorAction Stop
function Get-DlssgSm86Contract {
    $header = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../src/Shared/DlssgSm86Package.h') -Raw
    $revision = [regex]::Match($header, 'Revision\[\] = "([a-f0-9]{40})"').Groups[1].Value
    $table = [regex]::Match($header, '(?s)File Files\[\]\{(.*?)\r?\n\};').Groups[1].Value
    $files = @([regex]::Matches($table, '\{ L"([\w.-]+)", (\d+), "([a-f0-9]{64})" \}') | ForEach-Object {
        [pscustomobject]@{ Name = $_.Groups[1].Value; Bytes = [long]$_.Groups[2].Value; SHA256 = $_.Groups[3].Value }
    })
    if (!$revision -or $files.Count -ne 4) { throw 'Invalid DLSSG dependency contract.' }
    [pscustomobject]@{ Revision = $revision; Files = $files }
}

function Test-DlssgSm86File($Path, $Record) {
    (Test-Path -LiteralPath $Path -PathType Leaf) -and
        (Get-Item -LiteralPath $Path).Length -eq $Record.Bytes -and
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Record.SHA256
}

function Assert-DlssgSm86Files($Directory) {
    $contract = Get-DlssgSm86Contract
    foreach ($record in $contract.Files) {
        if (!(Test-DlssgSm86File (Join-Path $Directory $record.Name) $record)) {
            throw "DLSSG community file missing or checksum mismatch: $($record.Name)"
        }
    }
    $contract
}
