# Atualiza o RE_Kenshi instalado para v0.3.5 + KenshiLib v0.5.1 (as versões com que o mod é compilado),
# com backup dos arquivos substituídos, e instala o mod. Equivale ao instalador oficial num upgrade:
# o jogo já está preparado para o RE_Kenshi, só trocam as DLLs e as tabelas de RVA.
#
# Espera os zips extraídos em deps\dl\rk (RE_Kenshi_v0.3.5_loose.zip) e deps\dl\kl (KenshiLib_v0.5.1.zip).
param([string]$KenshiDir = "E:\steam\steamapps\common\Kenshi")
$ErrorActionPreference = "Stop"

$root = Split-Path $PSScriptRoot -Parent
$rk   = Join-Path $root "deps\dl\rk\install"
$kl   = Join-Path $root "deps\dl\kl"

if (Get-Process kenshi_x64 -ErrorAction SilentlyContinue) { throw "Feche o Kenshi antes." }
foreach ($f in @("$rk\RE_Kenshi.dll", "$kl\KenshiLib.dll")) { if (-not (Test-Path $f)) { throw "Não encontrado: $f" } }

# Backup
$bak = Join-Path $root ("deps\backup-rekenshi-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
New-Item -ItemType Directory -Force "$bak\RE_Kenshi" | Out-Null
foreach ($f in "RE_Kenshi.dll", "KenshiLib.dll", "CompressToolsLib.dll") {
    if (Test-Path "$KenshiDir\$f") { Copy-Item "$KenshiDir\$f" $bak }
}
Copy-Item "$KenshiDir\RE_Kenshi\RVAs" "$bak\RE_Kenshi" -Recurse
Write-Host "Backup em $bak"

# RE_Kenshi 0.3.5
Copy-Item "$rk\*" $KenshiDir -Recurse -Force
# KenshiLib 0.5.1 por cima (DLL + RVAs)
Copy-Item "$kl\KenshiLib.dll" $KenshiDir -Force
Copy-Item "$kl\RE_Kenshi\*" "$KenshiDir\RE_Kenshi" -Recurse -Force
Write-Host "RE_Kenshi 0.3.5 + KenshiLib 0.5.1 instalados"

# Mod
$dest = Join-Path $KenshiDir "mods\KenshiAchievements"
New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item "$root\mod\KenshiAchievements\*" $dest -Include *.dll, *.mod, *.json, *.txt, *.wav -Force
Write-Host "Mod instalado em $dest"
