# Manutenção

Notas de quem mantém o mod. Pra jogar ou compilar, veja o [README](../README.pt-BR.md).

## Publicar atualização

1. Nova entrada no `CHANGELOG.md`, `VERSION` no `src/Plugin.cpp` e `steam/changenotes-<versão>.bbcode`.
2. `tests\rodar-testes.ps1` e `build.ps1` (compila e instala em `Kenshi\mods\KenshiAchievements`, junto com o
   `_KenshiAchievements.info`). Conferir no jogo e deixar `@debug = 0` na pasta instalada.
3. Tag `vX.Y.Z` + release no GitHub com o zip da pasta `KenshiAchievements` (dll, .mod, RE_Kenshi.json,
   achievements.txt, achievement.wav, lang/).
4. Abrir o FCS com **só** o `KenshiAchievements.mod` marcado (nada de Genesis & cia., senão viram dependência)
   e **não salvar**. Botão **Steam Workshop** → Upload. O `.info` guarda o ID `3809336824`: sem ele o FCS
   cria outro item. Colar o changenotes na aba *Change Notes*.
5. Trazer pro repo o `.info`/`.mod` que o FCS gravou na pasta instalada.
6. Descrição completa (BBCode) em `steam/description.bbcode`, editada direto na página da Workshop.

## Imagens

- Capa da Workshop (1024×1024): `steam/gerar-capa.py <arte> steam/preview.jpg`.
- Banner do README (1280×400): `python steam/gerar-banner.py steam/preview1.png assets/banner.jpg`.
- Painel de contribuidores: `assets/contributors.svg`, gerado pelo workflow `contributors.yml`
  (`.github/scripts/contributors.mjs`). Quem contribuiu pela Workshop entra em `.github/contributors.json`.

## Validado no jogo

- Plugin carrega, hooks instalam, save/load dos contadores (tipo 4242) funciona.
- Morte: `lastGuyWhoDefeatedMe` já vem preenchido em `declareDead` e atribui ao personagem certo.
- Com o Genesis ativo (nenhum mod da Workshop tem DLL; raças/facções conferidas).
- Remover o mod é seguro: o save carrega normal sem ele. Salvar com o mod desativado perde os contadores
  desse save novo (o Kenshi descarta o registro desconhecido); os saves antigos mantêm.
- KO em luta: pelo último golpe do grupo (`addWound`) ou `lastGuyWhoDefeatedMe`, resolvido em até 3 s.
- KO/assassinato furtivo: pela tarefa `STEALTH_KNOCKOUT`/`STEALTH_KILL` de quem está do grupo com a vítima
  como alvo.
- Homônimos no grupo (dois "The Arbiter") ficam separados; o painel mostra a raça.
- Chave do save com 64 bytes (#38): o leitor do Kenshi corrigido na memória (log: `patched Kenshi's 64-byte
  save key overflow`), save carregado e salvo de novo com os mesmos contadores.
