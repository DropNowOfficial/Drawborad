[CmdletBinding()]
param([ValidateSet('Acquire','Verify')][string]$Mode='Verify',[string]$Cache='.deps')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
try { & py -3 "$PSScriptRoot/dependency_gate.py" --mode $Mode --cache $Cache --lock "$root/dependencies/lock.json" --profile windows-x64; exit $LASTEXITCODE }
catch { Write-Output "BLOCKED: development Python launcher unavailable: $_"; exit 2 }
