# Kenshi Achievements

**English** · [Português (Brasil)](README.pt-BR.md)

An [RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi) plugin that counts the **kills** and
**knockouts** made by your squad and unlocks configurable **achievements**. Single-player only.

**[Steam Workshop](https://steamcommunity.com/sharedfiles/filedetails/?id=3809336824)** ·
**[Download (GitHub releases)](https://github.com/LucasSKrewer/kenshi-achievements/releases/latest)** ·
[Changelog](CHANGELOG.md) · [Issues](https://github.com/LucasSKrewer/kenshi-achievements/issues)

⭐ **Like it? Star the repository.** That's all I ask. Forks are welcome.

## Features

- Counts per character and by the victim's race and faction: kills, knockouts (stealth knockouts
  counted separately) and severed limbs.
- **68 achievements** across these groups:
  - kill and KO milestones;
  - streaks ("5 kills within a minute");
  - factions and creatures;
  - bosses and unique NPCs;
  - Genesis content.
  
  Achievements for content you don't have installed are hidden.
- When an achievement unlocks you get a popup at the top of the screen, a line in the message log and
  a sound.
- **F6** opens and closes the panel (change the key with `@key`). It has four tabs:
  - **Statistics**: the selected character, the squad total and the most frequent victims at the top,
    and a **per-character table** (Kills, KOs, Stealth, Limbs) below it. Click a column header to sort.
  - **Achievements**: grouped by category, done and in progress, with progress like `(3/10)`.
  - **Memorial**: the squad members who died, who killed them, on which day, and what they had done.
  - **Victims**: every race and faction you've killed or knocked out, in two sortable tables, for the
    whole squad or just the selected character.
- **Export** button: writes everything to `exports/stats-<date>.txt` and `.csv` (opens in Excel) in the
  mod folder.
- The counters live **inside your save** (in a `GameData` record of the mod's own type, `4242`), so
  every save has its own. Saves from before the mod load fine, and counting starts from then on.
- **Everything is configurable** in `achievements.txt`, without recompiling (see
  [Customizing](#customizing)).

## Installing

Requires **[RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi/releases) v0.3.5 or newer**.
The main menu shows "RE_Kenshi v0.3.5" when it's installed.

**Steam:** subscribe on the [Steam Workshop](https://steamcommunity.com/sharedfiles/filedetails/?id=3809336824)
and enable **KenshiAchievements** in the Kenshi launcher.

**Manual (GOG etc.):**
1. Download the zip from [Releases](https://github.com/LucasSKrewer/kenshi-achievements/releases/latest).
2. Copy the `KenshiAchievements` folder into `Kenshi\mods\`.
3. Enable **KenshiAchievements** in the Kenshi launcher.

**Upgrading:** replace the folder. Your progress is stored in your saves and is kept.

**Removing the mod is safe:** saves load normally without it. A save made *while the mod is disabled*
loses the counters, because Kenshi drops records it doesn't know when saving. Older saves keep theirs.

Load order doesn't matter: the mod doesn't change any game data. It works with Genesis and typical
Workshop mod lists. It isn't designed for multiplayer mods.

## Customizing

Everything lives in `mods/KenshiAchievements/achievements.txt`, which the game reads at startup. The
format is explained at the top of the file:

```
id | metric | target | title | description
```

| Metric | Counts |
|---|---|
| `kills`, `kos`, `takedowns` | whole squad (takedowns = kills + KOs) |
| `limbs`, `stealth_kos`, `squad_deaths` | limbs severed, stealth knockouts, squad members lost |
| `char_kills`, `char_kos`, `char_limbs`, `char_stealth_kos` | best single character |
| `race_kills:<Race>`, `race_kos:<Race>` | e.g. `race_kills:Shek` |
| `faction_kills:<Faction>`, `faction_kos:<Faction>` | e.g. `faction_kills:The Holy Nation` |
| `npc_kills:<Name>`, `npc_kos:<Name>`, `npc_takedowns:<Name>` | unique NPCs only, e.g. `npc_takedowns:Tinfist` |
| `burst_kills:<s>`, `burst_takedowns:<s>` | most kills (or kills + KOs) within `<s>` seconds of game time |

Rules for names:
- Races, factions and NPCs always use the game's **English** names, whatever language you play in.
- `*` matches anything, e.g. `*Skeleton*` matches Skeleton MKI, MKII...
- `,` separates alternatives, e.g. `Dust Bandits,Hungry Bandits`.

Other options:
- `[Name]` starts a category.
- `?id` makes an achievement secret: it shows as `???` until it's unlocked.

Settings in the same file:

| Setting | What it does |
|---|---|
| `@key = F6` | panel key: F1–F12, a letter/digit or NUM0–NUM9 |
| `@sound = Notifications:Building_Complete` | unlock sound: a Kenshi sound event (default, follows the game volume), any `.wav` in the mod folder (`achievement.wav` is included), or `none` |
| `@victims = 5` | how many races and factions "Most frequent victims" lists; a number or `all` |
| `@debug = 0` | `1` logs every kill, KO and hit to `RE_Kenshi_log.txt`, for bug reports |

Never change an `id` after you've played with it: saves track unlocks by id.

## Languages

The mod follows the language you picked in Kenshi (`language=` in `settings.cfg`):

- **The mod's own texts** come from `lang/<language>.txt` (e.g. `pt_BR.txt`), on top of `lang/en.txt`.
  Included: English and Português (Brasil).
- **Race and faction names** arrive translated by the game ("Bandidos da Poeira"). The mod uses
  Kenshi's own dictionary (`locale/<language>/gamedata.po`) to map them back to English ("Dust Bandits").
  That's why `achievements.txt` is the same in every language.

**Translations are welcome!** Copy `en.txt`, name the copy after the language code, and translate it.
Anything you leave out stays in English. See [#15](https://github.com/LucasSKrewer/kenshi-achievements/issues/15).

## How kills and knockouts are credited

When someone dies (`Character::declareDead`) or is knocked out, the mod looks for the attacker in this
order:

1. A squad member running a **stealth knockout / assassination** on the victim. Stealth attacks don't
   wound the victim, so this has to be checked first.
2. `victim->lastGuyWhoDefeatedMe`, if it's one of your characters.
3. Otherwise, the last squad member who **wounded** the victim (`MedicalSystem::addWound`, melee or
   ranged) in the last 30 seconds.

Details:
- **Knockouts.** Going down from damage doesn't go through the game's knockout function, so the mod
  checks every 250 ms whether a victim your squad recently hit has gone down.
- **Waiting for the attacker.** A KO with no attacker yet waits up to 3 s, because the game often fills
  the attacker in a moment later.
- **Downed victims.** If a victim dies while still down, the kill belongs to whoever downed it. For
  example, butchering a knocked-out animal doesn't give the kill to the looter, and an animal downed by
  an outsider doesn't count for you.
- **What doesn't count.** Your own squad members never count as victims, and each death counts once.
  Squad members who die go to the Memorial.

## Building

KenshiLib plugins **must** be built with the Visual Studio 2010 compiler (toolset `v100`, x64): Kenshi
was built with it, and the plugin shares the STL/CRT with the game. Keep the code VS2010-compatible: no
range-for, initializer lists or `enum class`. Source files are UTF-8 without BOM.

### Compiler (no Visual Studio, no admin rights)

You can **extract** VC++ 2010 x64 from the Windows SDK 7.1 instead of installing it:

1. Download the official `GRMSDKX_EN_DVD.iso` (SDK 7.1 x64, 571 MB) from download.microsoft.com and
   mount it.
2. Extract `Setup\vc_stdamd64`, `Setup\vc_stdx86` and `Setup\WinSDKBuild_amd64` with
   [lessmsi](https://github.com/activescott/lessmsi) (`lessmsi x <msi> <folder>\`).
3. Assemble a `vc2010\` folder:

   | Folder in `vc2010\` | Copy from |
   |---|---|
   | `bin\amd64` | `vc_stdamd64\...\VC\bin\amd64`, plus `Win\System64\msvcr100.dll` |
   | `include` and `lib\amd64` | `vc_stdx86\...\VC` |
   | `sdk\include` and `sdk\lib\x64` | `...\Windows\v7.1` |

   The result is cl 16.00.30319.01.

The `KenshiAchievements.vcxproj` is still in the repo, for anyone with Visual Studio and the v100
toolset installed.

### Dependencies (`deps/`, not in git)

- `deps/KenshiLib`: a clone of [BFrizzleFoShizzle/KenshiLib](https://github.com/BFrizzleFoShizzle/KenshiLib)
  at tag **v0.5.1**, with `KenshiLib.lib` from the release zip copied into `Libraries/`.
  **It must match the KenshiLib.dll installed in the game**. See `scripts/atualizar-rekenshi.ps1`.
- `deps/boost_1_60_0`: `boost.zip` from
  [KenshiLib_Examples_deps](https://github.com/BFrizzleFoShizzle/KenshiLib_Examples_deps) (git LFS),
  which already has the vc100 `stage/lib`.

### Build

```
powershell -ExecutionPolicy Bypass -File build.ps1 -VcDir <path to vc2010> -KenshiDir <Kenshi folder>
```

The DLL is written to `mod/KenshiAchievements/`, and the mod folder is copied into
`<Kenshi>\mods\KenshiAchievements`. Use `-NoInstall` to only build. The plugin logs to
`RE_Kenshi_log.txt` in the game folder, with the prefix `KenshiAchievements:`.

### Tests (no game needed)

```
powershell -ExecutionPolicy Bypass -File tests\rodar-testes.ps1
```

This builds `tests/teste_logica.cpp` with the same VC++ 2010 compiler. It tests:
- the name dictionary and the per-language texts;
- counting, achievements and character merging;
- the text of the panel tabs.

## License

[GPLv3](LICENSE), the same license as [KenshiLib](https://github.com/BFrizzleFoShizzle/KenshiLib), which
the plugin links against. You're free to use, modify and fork it; distributed forks stay open under the
GPLv3. If it's useful to you, leave a ⭐: it helps other people find the mod.

## Credits

- [BFrizzleFoShizzle](https://github.com/BFrizzleFoShizzle) for RE_Kenshi and KenshiLib, which make all
  of this possible.
- Everyone who reported bugs and suggested features, on GitHub and in the Workshop comments.
- Kenshi © Lo-Fi Games. This project doesn't include any game files. `achievement.wav` is synthesized
  by `scripts/gerar-som.py`.
