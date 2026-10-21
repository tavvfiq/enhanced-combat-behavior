# package_release.ps1 - copy the built DLL + package/ assets into releases/<version>, run the
# ship-gate tree validator, zip it, print the zip hash. Usage: pwsh scripts/package_release.ps1 0.2.3
param([Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$rel = Join-Path $root "releases\$Version"; $pkg = Join-Path $rel "CombatAITree $Version"
New-Item -ItemType Directory -Force (Join-Path $pkg 'SKSE\Plugins'), (Join-Path $rel 'symbols') | Out-Null
Copy-Item (Join-Path $root 'package\*') $pkg -Recurse -Force
$out = Join-Path $root 'build\SkyrimSEAE\skse\plugins'
Copy-Item (Join-Path $out 'CombatAITree.dll') (Join-Path $pkg 'SKSE\Plugins') -Force
Copy-Item (Join-Path $out 'CombatAITree.pdb') (Join-Path $rel 'symbols') -Force   # symbols stay out of the mod tree
$gate = Join-Path $env:USERPROFILE '.claude\skills\skyrim-ship-gate\scripts\validate_release_tree.py'
if (Test-Path $gate) { python -I $gate $pkg; if ($LASTEXITCODE) { throw 'release tree validation failed' } }
$zip = Join-Path $rel "CombatAITree-$Version.zip"
Compress-Archive -Path (Join-Path $pkg '*') -DestinationPath $zip -Force
"$zip  sha256=$((Get-FileHash $zip).Hash)"