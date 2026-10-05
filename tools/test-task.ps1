[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Id,[ValidateSet('Red','Green')][string]$Phase='Green')
$ErrorActionPreference='Stop'
try { & py -3 "$PSScriptRoot/test_task.py" --id $Id --phase $Phase --preset win-debug; exit $LASTEXITCODE }
catch { Write-Output "BLOCKED: Windows development host unavailable: $_"; exit 2 }
