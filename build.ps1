# Compila o plugin com o compilador do VC++ 2010 x64 (extraído do Windows SDK 7.1, sem instalar)
# e copia o mod pra pasta do Kenshi. Não precisa de Visual Studio nem de admin.
#
#   powershell -ExecutionPolicy Bypass -File build.ps1            # compila + instala
#   powershell -ExecutionPolicy Bypass -File build.ps1 -NoInstall # só compila
param(
    [string]$VcDir     = "F:\Claude\_ferramentas\vc2010",
    [string]$KenshiDir = "E:\steam\steamapps\common\Kenshi",
    [switch]$NoInstall
)
$ErrorActionPreference = "Stop"

$root   = $PSScriptRoot
$deps   = Join-Path $root "deps"
$klib   = Join-Path $deps "KenshiLib"
$boost  = Join-Path $deps "boost_1_60_0"
$obj    = Join-Path $root "build\obj"
$modDir = Join-Path $root "mod\KenshiAchievements"

$cl   = Join-Path $VcDir "bin\amd64\cl.exe"
$link = Join-Path $VcDir "bin\amd64\link.exe"
foreach ($f in @($cl, $link, "$klib\Libraries\KenshiLib.lib", "$boost\boost\version.hpp")) {
    if (-not (Test-Path $f)) { throw "Não encontrado: $f (ver README.md > Compilar)" }
}

# O cl de 2010 acha as DLLs dele (c1.dll, mspdb100.dll...) pelo PATH
$env:PATH    = "$VcDir\bin\amd64;$VcDir\bin;$env:PATH"
$env:INCLUDE = "$VcDir\include;$VcDir\sdk\include;$klib\Include;$boost"
$env:LIB     = "$VcDir\lib\amd64;$VcDir\sdk\lib\x64;$klib\Libraries;$klib\Libraries\mygui;$klib\Libraries\ogre;$boost\stage\lib"

New-Item -ItemType Directory -Force $obj | Out-Null

# Fontes em UTF-8 SEM BOM: o cl de 2010 lê como ANSI e repassa os bytes UTF-8 intactos
# pras strings, que é o que o MyGUI espera.
$sources = Get-ChildItem (Join-Path $root "src") -Filter *.cpp | ForEach-Object { $_.FullName }
& $cl /nologo /c /O2 /GL /MD /EHsc /W3 /Zi /GS `
    /DNDEBUG /DWIN32 /D_WINDOWS /DUNICODE /D_UNICODE /D_USRDLL /DBOOST_ALL_NO_LIB /D_CRT_SECURE_NO_WARNINGS `
    "/Fo$obj\\" "/Fd$obj\\vc100.pdb" $sources
if ($LASTEXITCODE -ne 0) { throw "Compilação falhou" }

$objs = Get-ChildItem $obj -Filter *.obj | ForEach-Object { $_.FullName }
& $link /nologo /DLL /MACHINE:X64 /LTCG /DEBUG /OPT:REF /OPT:ICF `
    "/OUT:$modDir\KenshiAchievements.dll" "/PDB:$obj\KenshiAchievements.pdb" "/IMPLIB:$obj\KenshiAchievements.lib" `
    $objs kenshilib.lib MyGUIEngine_x64.lib OgreMain_x64.lib libboost_thread-vc100-mt-1_60.lib libboost_system-vc100-mt-1_60.lib `
    kernel32.lib user32.lib winmm.lib
if ($LASTEXITCODE -ne 0) { throw "Link falhou" }
Write-Host "OK: $modDir\KenshiAchievements.dll"

if (-not $NoInstall) {
    if (Get-Process kenshi_x64 -ErrorAction SilentlyContinue) {
        Write-Warning "Kenshi está aberto: feche o jogo e rode de novo pra instalar (a DLL fica travada)."
        exit 0
    }
    $dest = Join-Path $KenshiDir "mods\KenshiAchievements"
    New-Item -ItemType Directory -Force $dest | Out-Null
    Copy-Item "$modDir\*" $dest -Include *.dll, *.mod, *.json, *.txt, *.wav -Force
    Copy-Item "$modDir\lang" $dest -Recurse -Force
    Write-Host "Instalado em $dest"
}
