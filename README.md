# Kenshi Achievements

> **EN:** An [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi) plugin that tracks **kills** and
> **knockouts** per squad member and unlocks configurable **achievements** (popup + sound, F6 panel with
> Statistics / Achievements tabs). Stats are stored inside your save. Single-player.
> **Works in any game language**: the UI follows Kenshi's language (`lang/<language>.txt`, English fallback —
> translations welcome!) and translated race/faction names are mapped back to English using the game's own
> dictionary, so `achievements.txt` is the same for everyone. Docs below are in Portuguese.

Plugin [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi) que conta **kills** e **KOs**
feitos pelo seu grupo e libera **conquistas** configuráveis. Só single-player.

⭐ **Gostou? Deixa uma estrela no repositório** — é o único pedido. Fork à vontade.

- Contagem por personagem, por raça e por facção da vítima; mortes e KOs separados.
- Os contadores ficam **dentro do save** (um `GameData` próprio, tipo `4242`); cada save tem os seus.
- Conquista liberada → janela no topo da tela + linha no log de mensagens + som.
- **F6** abre/fecha o painel (tecla configurável com `@key`), com duas abas:
  - **Estatísticas** — o personagem selecionado no topo, depois o total do grupo e a lista por personagem;
  - **Conquistas** — concluídas e em andamento, com progresso (ex.: `(3/10)`).
- Conquistas em `mod/KenshiAchievements/achievements.txt` — formato explicado no próprio arquivo.
- Som configurável (`@sound`): padrão é a notificação "construção concluída" do próprio Kenshi; dá pra usar outro som do jogo, o `achievement.wav` incluso ou qualquer `.wav` seu.

### Idiomas

O mod segue o idioma escolhido no Kenshi (`language=` no `settings.cfg`):

- **Textos do mod** vêm de `lang/<idioma>.txt` (ex.: `pt_BR.txt`, `de_DE.txt`), por cima do `lang/en.txt`.
  Pra traduzir, copie o `en.txt` com o código do idioma e traduza — o que faltar fica em inglês. PRs são bem-vindos.
- **Nomes de raça/facção** chegam traduzidos do jogo ("Bandidos da Poeira"); o mod usa o dicionário do próprio
  Kenshi (`locale/<idioma>/gamedata.po`) pra voltar ao inglês ("Dust Bandits"). Por isso o `achievements.txt`
  usa sempre os nomes em inglês e vale pra qualquer idioma. Dá pra listar alternativas com vírgula.

## Instalar (jogador)

1. Tenha o [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi/releases) **v0.3.5+** instalado
   (o menu principal mostra "RE_Kenshi v0.3.5").
2. Copie a pasta `KenshiAchievements` para `Kenshi\mods\`.
3. Ative **KenshiAchievements** no launcher do Kenshi.

## Como a autoria é decidida

Quando alguém morre (`Character::declareDead`) ou cai inconsciente (`MedicalSystem::knockout`):

1. `vítima->lastGuyWhoDefeatedMe`, se for personagem do jogador;
2. senão, o último personagem do jogador que acertou a vítima (melee ou projétil) nos últimos 30 s.

Vítimas do próprio grupo não contam. Uma morte conta uma vez só por vítima; um KO conta na chamada de
`knockout()` com a vítima de pé (o jogo só marca "inconsciente" depois), com intervalo mínimo de 10 s
por vítima — a mesma pessoa pode ser nocauteada de novo depois.

`@debug = 1` no `achievements.txt` registra cada kill/KO/golpe no `RE_Kenshi_log.txt`.

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

### Testes (sem o jogo)

```
powershell -ExecutionPolicy Bypass -File tests\rodar-testes.ps1
```

Compila `tests/teste_logica.cpp` com o mesmo VC++ 2010 e testa o dicionário de nomes, os textos por idioma,
a contagem, as conquistas e o texto das duas abas.

## Validado no jogo

- Plugin carrega, hooks instalam, save/load dos contadores (tipo 4242) funciona.
- Morte: `lastGuyWhoDefeatedMe` já vem preenchido em `declareDead` e atribui ao personagem certo.
- Com o Genesis ativo (nenhum mod da Workshop tem DLL; raças/facções conferidas).

Bugs e ideias: [issues](https://github.com/LucasSKrewer/kenshi-achievements/issues).

## Licença

[GPLv3](LICENSE) — o mesmo do [KenshiLib](https://github.com/BFrizzleFoShizzle/KenshiLib), ao qual o plugin é ligado.
Pode usar, modificar e forkar; forks distribuídos continuam abertos sob a GPLv3.
Se for útil pra você, deixa uma ⭐ — ajuda outras pessoas a achar o mod.

## Créditos

- [BFrizzleFoShizzle](https://github.com/BFrizzleFoShizzle) — RE_Kenshi e KenshiLib, sem os quais nada disso existe.
- Kenshi © Lo-Fi Games. Este projeto não inclui nenhum arquivo do jogo; o `achievement.wav` é sintetizado
  por `scripts/gerar-som.py`.
