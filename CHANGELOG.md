# Changelog

All notable changes to Kenshi Achievements. Versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed
- A kill now belongs to whoever **knocked the victim down** when it dies while still down
  ([#18](https://github.com/LucasSKrewer/kenshi-achievements/issues/18), reported by @az455862):
  - butchering a knocked-out animal (taking its hide/meat kills it) no longer gives the kill to the looter;
  - if it was downed by a creature or NPC outside your squad, it doesn't count for you.

  If the victim got back up and died later in a new fight, the normal rule applies.
- **Knockouts in regular fights now count.** Going down from damage doesn't go through the game's
  knockout function, which only caught stealth knockouts, so most combat KOs were missed. The mod
  now notices when a victim your squad recently hit goes down, and credits whoever hit it last.

## [1.1.0] — 2026-09-29

### Added
- **25 new achievements**:
  - 14 from the base game ([#5](https://github.com/LucasSKrewer/kenshi-achievements/issues/5)):
    Cannibals, Fogmen, Skin Bandits, United Cities, Crab Raiders, Reavers, Starving Bandits,
    Gorillos, Leviathan, Hive Queen, Bonedogs, spiders, Fishmen, Beak Master;
  - 11 for Genesis ([#6](https://github.com/LucasSKrewer/kenshi-achievements/issues/6)): Primordial
    Hive, MKIV skeletons, Giant, Broodmother, Oni Gorillo, Winged Beak Ogre, wolves, The Wolven Order,
    Cult of Narko, Ironsides.
- **Achievements for content that isn't installed are hidden**
  ([#7](https://github.com/LucasSKrewer/kenshi-achievements/issues/7)): the mod reads the races and
  factions in the loaded game data, and an achievement whose race/faction matches none of them
  disappears from the panel and from the X/Y total. Genesis achievements vanish without Genesis;
  already unlocked ones stay.
- **Upper tiers**, based on the real pace (~90 kills per session with Genesis)
  ([#9](https://github.com/LucasSKrewer/kenshi-achievements/issues/9)): Myth (2500 kills), Walking Legend
  (one character with 200), Scrap Yard (100 skeletons), Exterminator (100 spiders). Existing targets are
  unchanged, so nobody loses an unlocked achievement.
- **Secret achievements** ([#8](https://github.com/LucasSKrewer/kenshi-achievements/issues/8)): a `?`
  before the id shows `[?] ???` until unlocked. Leviathan, Regicide and Ironsides are secret.

### Fixed
- Race names that the game translates through non-race entries (e.g. Leviathan → "Leviatã", Swamp
  Raptor) weren't mapped back to English, so they didn't count and could be hidden. The name map now
  uses every name entry in `gamedata.po` (141 → 2050 names), with race/faction entries taking priority.
- Old saves with translated race/faction keys (before 1.0's language support) are normalized to
  English on load, so those kills count toward achievements.
- The X/Y summary counted unlocked ids that no longer exist in `achievements.txt`.
- `heretic` matched Holy Nation Outlaws; now only The Holy Nation.
- The F6 panel cut the text at 2048 characters (MyGUI's default limit), so the end of the
  Achievements tab was missing. The limit is now raised.

### Documented
- Removing the mod is safe: saves load normally without it. A save made while the mod is disabled
  loses the counters, because Kenshi drops the unknown record on save. Older saves keep theirs
  ([#10](https://github.com/LucasSKrewer/kenshi-achievements/issues/10)).

## [1.0.0] — 2026-09-27

First public release — [Steam Workshop](https://steamcommunity.com/sharedfiles/filedetails/?id=3809336824)
and [GitHub release](https://github.com/LucasSKrewer/kenshi-achievements/releases/tag/v1.0.0).

**Requires [RE_Kenshi](https://www.nexusmods.com/kenshi/mods/847) v0.3.5+** (KenshiLib 0.5.x).

### Added
- **Kill and knockout counter per squad member**, kept separately, plus totals by victim race
  and faction.
- Attacker detection that covers:
  - melee, ranged and animal attacks, via every wound the victim takes (`MedicalSystem::addWound`);
  - knockouts where the game only fills in the attacker a moment later: the KO waits up to 3 s;
  - **stealth knockouts / assassinations**, which don't wound the victim: the attacker is the
    squad member running the stealth task on it.
- **16 achievements** with an on-screen popup, a message-log entry and Kenshi's own notification
  sound (`Notifications:Building_Complete`, follows the game volume).
- **`achievements.txt`** to add or edit achievements without recompiling:
  - metrics: `kills`, `kos`, `takedowns`, `char_kills`, `char_kos`, `race_kills`, `race_kos`,
    `faction_kills`, `faction_kos`;
  - `*` wildcards and `,` alternatives in race/faction names (handy with mods that add variants,
    like Genesis).
- **F6 panel** (configurable with `@key`) with two tabs:
  - **Statistics**: the selected character at the top, then the squad total and a per-character
    list;
  - **Achievements**: completed and in progress, with progress like `(3/10)`.
- Characters with the same name are told apart by race, e.g. "The Arbiter (Skeleton MKI)".
- **Stats stored inside each save**, in a GameData record of the mod's own type. Saves from
  before the mod work, and counting starts from then on.
- **Any game language**:
  - the UI follows Kenshi's `language` setting through `lang/<language>.txt`, falling back to
    English. English and Português (Brasil) are included;
  - translated race/faction names are mapped back to English with the game's own dictionary
    (`locale/<language>/gamedata.po`), so `achievements.txt` works the same in every language.
- `@sound` to pick the unlock sound: a Kenshi event, a `.wav` in the mod folder, or `none`.
- `@debug = 1` logs every kill, KO and squad hit to `RE_Kenshi_log.txt`, for bug reports.

### Notes
- Single-player only; not designed for multiplayer mods.
- Tested with Genesis and a typical Workshop mod list. The mod doesn't change game data, so load
  order doesn't matter.

[1.1.0]: https://github.com/LucasSKrewer/kenshi-achievements/releases/tag/v1.1.0
[1.0.0]: https://github.com/LucasSKrewer/kenshi-achievements/releases/tag/v1.0.0
