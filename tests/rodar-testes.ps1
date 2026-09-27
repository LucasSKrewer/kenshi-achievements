# Compila e roda tests\teste_logica.cpp com o mesmo VC++ 2010 do build (sem o jogo).
param([string]$VcDir = "F:\Claude\_ferramentas\vc2010")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$out  = Join-Path $root "build\tests"
New-Item -ItemType Directory -Force $out | Out-Null

$env:PATH    = "$VcDir\bin\amd64;$VcDir\bin;$env:PATH"
$env:INCLUDE = "$VcDir\include;$VcDir\sdk\include"
$env:LIB     = "$VcDir\lib\amd64;$VcDir\sdk\lib\x64"

Push-Location $root
try {
    & cl.exe /nologo /EHsc /O2 /MD "/Fo$out\\" "/Fe$out\teste_logica.exe" `
        tests\teste_logica.cpp src\Lang.cpp src\Stats.cpp
    if ($LASTEXITCODE -ne 0) { throw "Compilação dos testes falhou" }
    # O console do Windows precisa de UTF-8 pra mostrar os acentos
    [Console]::OutputEncoding = [Text.Encoding]::UTF8
    & "$out\teste_logica.exe"
    exit $LASTEXITCODE
} finally { Pop-Location }
