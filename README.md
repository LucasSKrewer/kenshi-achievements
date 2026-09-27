# Kenshi Achievements

> **EN:** An [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi) plugin that tracks **kills** and
> **knockouts** per squad member and unlocks configurable **achievements** (popup + sound, F9 stats panel).
> Stats are stored inside your save. Single-player. Docs below are in Portuguese.

Plugin [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi) que conta **kills** e **KOs**
feitos pelo seu grupo e libera **conquistas** configuráveis. Só single-player.

⭐ **Gostou? Deixa uma estrela no repositório** — é o único pedido. Fork à vontade.

- Contagem por personagem, por raça e por facção da vítima; mortes e KOs separados.
- Os contadores ficam **dentro do save** (um `GameData` próprio, tipo `4242`); cada save tem os seus.
- Conquista liberada → janela no topo da tela + linha no log de mensagens + som.
- **F9** abre/fecha o painel com o placar e o progresso das conquistas.
- Conquistas em `mod/KenshiAchievements/achievements.txt` — formato explicado no próprio arquivo.
- Som configurável (`@som`): o `achievement.wav` incluso, qualquer `.wav` seu, ou um som do próprio Kenshi.

## Instalar (jogador)

1. Tenha o [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi/releases) **v0.3.5+** instalado
   (o menu principal mostra "RE_Kenshi v0.3.5").
2. Copie a pasta `KenshiAchievements` para `Kenshi\mods\`.
3. Ative **KenshiAchievements** no launcher do Kenshi.

## Como a autoria é decidida

Quando alguém morre (`Character::declareDead`) ou cai inconsciente (`MedicalSystem::knockout`):

1. `vítima->lastGuyWhoDefeatedMe`, se for personagem do jogador;
2. senão, o último personagem do jogador que acertou a vítima (melee ou projétil) nos últimos 30 s.

Vítimas do próprio grupo não contam. Uma morte conta uma vez só por vítima; um KO conta na
transição consciente → inconsciente (a mesma pessoa pode ser nocauteada de novo depois).

## Compilar

Os plugins do KenshiLib **precisam** do compilador do Visual Studio 2010 (toolset `v100`, x64),
porque o Kenshi foi compilado com ele e o plugin divide STL/CRT com o jogo.

### Compilador (sem Visual Studio, sem admin)

O VC++ 2010 x64 vem **extraído** (não instalado) do Windows SDK 7.1 em `F:\Claude\_ferramentas\vc2010`
(`bin\amd64`, `include`, `lib\amd64`, `sdk\include`, `sdk\lib\x64` — cl 16.00.30319.01). Para refazer:

1. Baixar a ISO oficial `GRMSDKX_EN_DVD.iso` (SDK 7.1 x64, 571 MB) do download.microsoft.com e montar.
2. Extrair com [lessmsi](https://github.com/activescott/lessmsi) (`lessmsi x <msi> <pasta>\`):
   `Setup\vc_stdamd64`, `Setup\vc_stdx86` e `Setup\WinSDKBuild_amd64`.
3. Montar `vc2010\`: `bin\amd64` ← `vc_stdamd64\...\VC\bin\amd64` (+ `Win\System64\msvcr100.dll`);
   `include` e `lib\amd64` ← `vc_stdx86\...\VC`; `sdk\include` e `sdk\lib\x64` ← `...\Windows\v7.1`.

### Dependências (`deps/`, fora do git)

- `deps/KenshiLib` — clone de [BFrizzleFoShizzle/KenshiLib](https://github.com/BFrizzleFoShizzle/KenshiLib)
  na tag **v0.5.1**, com o `KenshiLib.lib` do zip da release copiado em `Libraries/`.
  **Tem que bater com o KenshiLib.dll instalado no jogo** — ver `scripts/atualizar-rekenshi.ps1`.
- `deps/boost_1_60_0` — `boost.zip` do [KenshiLib_Examples_deps](https://github.com/BFrizzleFoShizzle/KenshiLib_Examples_deps)
  (git LFS), já com `stage/lib` vc100.

### Build

```
powershell -ExecutionPolicy Bypass -File build.ps1
```

A DLL sai em `mod/KenshiAchievements/` e é copiada para `E:\steam\steamapps\common\Kenshi\mods\KenshiAchievements`
(use `-NoInstall` para só compilar). Depois ative **KenshiAchievements** no launcher do Kenshi.
O `KenshiAchievements.vcxproj` continua aí para quem tiver Visual Studio com o toolset v100.

Logs do plugin: `RE_Kenshi_log.txt` na pasta do jogo (prefixo `KenshiAchievements:`).

## A validar no jogo

- `lastGuyWhoDefeatedMe` já está preenchido quando `knockout`/`declareDead` rodam? (se não, o fallback de golpes cobre)
- `Character::iShotYou`: `this` é a vítima e `attacker` o atirador — confirmar pelo log.
- O `GameData` tipo 4242 sobrevive ao save/load (mesma técnica do exemplo `WorldStates`).
- Skins MyGUI `Kenshi_WindowCX`, `Kenshi_EditBox`, `Kenshi_TextboxStandardText` e o layer `Overlapped`.
- Nomes exatos de raças/facções usados no `achievements.txt`.

## Licença

[GPLv3](LICENSE) — o mesmo do [KenshiLib](https://github.com/BFrizzleFoShizzle/KenshiLib), ao qual o plugin é ligado.
Pode usar, modificar e forkar; forks distribuídos continuam abertos sob a GPLv3.
Se for útil pra você, deixa uma ⭐ — ajuda outras pessoas a achar o mod.

## Créditos

- [BFrizzleFoShizzle](https://github.com/BFrizzleFoShizzle) — RE_Kenshi e KenshiLib, sem os quais nada disso existe.
- Kenshi © Lo-Fi Games. Este projeto não inclui nenhum arquivo do jogo; o `achievement.wav` é sintetizado
  por `scripts/gerar-som.py`.
