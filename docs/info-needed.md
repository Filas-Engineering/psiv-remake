# Information needed

Facts about the original game that the remake needs before the placeholder rules and data can be replaced. Each row comes from a "To measure" requirement in the [behavior spec](spec/README.md), and the ID links back to it.

Deliver answers to `source-material/`, which git ignores. Nothing extracted from the cartridge goes into this repo.

## Priority

1. **Battle and encounters.** They replace every placeholder formula in `core/` and set the pacing targets.
2. **Save and progression, field.** They fix leveling, rest, saving and travel rules.
3. **The rest.** Timing, menus and text presentation.

## Published sources

[research/published-mechanics.md](research/published-mechanics.md) lists what fan sources have already published for each of the 42 items, with links and a confidence rating. It is unconfirmed community work: use it to plan and to cross-check, not as the answer.

| Confidence | Items |
|---|---|
| High (10) | BAT-033, BAT-043, BAT-057, ENC-024, ENC-035, SAV-004, SAV-032, SAV-038, FLD-052, FLOW-012 |
| Medium (9) | BAT-023, BAT-046, BAT-063, BAT-082, ENC-005, FLD-035, FLD-062, TXT-026, UI-016 |
| Low (10) | BAT-014, BAT-016, BAT-072, BAT-093, BAT-105, BAT-141, SAV-035, SAV-036, FLD-074, FLOW-041 |
| Nothing published (12) | BAT-005, BAT-102, ENC-036, ENC-041, FLD-022, FLD-041, FLD-043, FLOW-006, FLOW-033, EVT-007, EVT-061, TXT-006 |
| Split (1) | SAV-031: the level cap is published, the curves and rewards are not |

Many of the pages could only be read through search excerpts, so even the High rows need a check against the full page and the reference build.

Turn order and macro order (BAT-030, BAT-052), the hit, critical, damage, healing, Defend and status formulas (BAT-042), the opening roll (ENC-022) and the escape roll (ENC-031) were read from the community disassembly and `core/` now follows them. The per-ability, per-enemy and per-formation numbers they use are content and still come from the rights holder. Enemy action and target choice, retargeting, macro targeting, combination triggers and damage, reward sharing, stat changes, end conditions and vehicle battles were read the same way ([original-battle-rules.md](research/original-battle-rules.md)) and `core/` follows them. Where the published rule differs from what the spec still proposes, the spec row says so: BAT-013, BAT-020, BAT-103, ENC-030, SAV-002, SAV-003 and UI-003.

## Data tables

`core/` loads its data from JSON. Deliver the original's tables in this shape so they load without code changes. Field names are in `core/README.md`.

| Table | One record per | Fields |
|---|---|---|
| Abilities | Technique, skill, enemy skill, basic attack | ID, name, kind, cost, target mode, effects (type, power, element, status, chance) |
| Statuses | Status effect | ID, name, per-turn effect, duration, what cures it |
| Characters | Party member | ID, name, base stats, growth per level, abilities and the level each is learned |
| Enemies | Enemy | ID, name, stats, elemental affinities, action list with weights and conditions, experience, currency, drops |
| Encounters | Enemy group | ID, enemies and positions, where it appears, rate |
| Combinations | Combination attack | ID, required abilities, order rule, resulting effect |
| Items | Item | ID, name, target mode, effects, price, where sold |
| Macros | Default macro, if any | Name, ordered commands |

## Script

One record per message for all dialogue and system text: tree ID, message index, speaker or portrait ID, text, control codes in order, branch targets, and the map and trigger that shows it. See [TXT-006](spec/dialogue-and-text.md).

## Battle

| ID | What we need | How to get it | Unblocks |
|---|---|---|---|
| [BAT-005](spec/battle.md) | The original's maximum enemy group size, formation positions, and whether enemies are arranged in rows that affect targeting | Rights holder data; cross-check by inspecting the largest groups in the reference build | BAT-004 and BAT-070 |
| [BAT-014](spec/battle.md) | Which party states count toward Defeat in the original (HP zero only, or also statuses that prevent acting), and whether enemy group defeat ignores enemies that cannot act | Rights holder data; cross-check by applying each disabling status to the whole party in the reference build | BAT-013 |
| [BAT-016](spec/battle.md) | When status effects tick and durations count down in the original (round end, or the affected combatant's own action), and what happens to an action whose target fell earlier in the round | Rights holder data; cross-check in the reference build by targeting an enemy that a faster ally defeats first | BAT-010, BAT-015 and BAT-071 |
| [BAT-023](spec/battle.md) | The original's full battle command set per party member, what each does, and whether item use and equipment-granted abilities are separate commands | Rights holder data and a reference build session | BAT-020 |
| [BAT-033](spec/battle.md) | The original's initiative formula, including the size and shape of the random factor and any modifiers from statuses or equipment, and whether the order is shown | Rights holder formulas; cross-check by logging the order of at least 500 rounds with known agility values in the reference build and fitting the distribution | BAT-030 to BAT-032 |
| [BAT-043](spec/battle.md) | Every battle formula (damage, healing, hit, evasion, critical, resistance, status success), per-ability numbers, and per-enemy stats | Rights holder data. Cross-check 20 samples per formula against the reference build by recording damage numbers | BAT-042 |
| [BAT-046](spec/battle.md) | Which technique misbehaves in the original, how, and its intended behavior | Get the community bug fix change list in phase 0 (S-FAN-DEFECTS), then confirm intent against the rights holder's Japanese data | BAT-045 |
| [BAT-072](spec/battle.md) | The original's targeting modes per ability and its retarget behavior | Rights holder data; cross-check in the reference build | BAT-070 and BAT-071 |
| [BAT-082](spec/battle.md) | The original's full status list and each status's duration, persistence after battle, tick effect, cures and action rule | Rights holder data; cross-check by inflicting each status in the reference build | BAT-081 |
| [BAT-093](spec/battle.md) | Each enemy's action selection in the original: which abilities, how often, under what conditions, and how it picks targets | Rights holder data; cross-check by observing at least 50 rounds for each of 15 sampled enemies including every boss in the vertical slice | BAT-092 |
| [BAT-057](spec/battle.md) | How the original exposes macros: slot count, name rules, menu depth, what a macro can contain, and whether it guarantees that party actions run back to back with no enemy action between them | Rights holder data and a reference build session, including 50 rounds that use a macro against fast enemies | BAT-052 and BAT-055 |
| [BAT-063](spec/battle.md) | The original's combination trigger rules: whether order matters, whether enemy actions or cancelled actions break it, whether components' own effects also apply, whether it can span rounds, and how costs are paid | Rights holder data; cross-check by attempting each of the 15 combinations with and without an enemy acting between components in the reference build | BAT-061 and BAT-062 |
| [BAT-102](spec/battle.md) | Per-action presentation time in the reference build for basic attacks, techniques, enemy actions and each combination, plus whole-battle time for normal encounters in five areas | Frame counting on captures, at least 20 samples per kind | BAT-101 |
| [BAT-105](spec/battle.md) | Whether the original has any repeat or automatic battle command, and its rules | Rights holder data and a reference build session | BAT-103 and BAT-104 |
| [BAT-141](spec/battle.md) | Per vehicle, the original's vehicle battle rules: who issues commands, which commands exist, how vehicle damage and defeat work, and which enemies appear | Rights holder data and a reference build session per vehicle | BAT-140 |

## Encounters

| ID | What we need | How to get it | Unblocks |
|---|---|---|---|
| [ENC-005](spec/encounters.md) | Per area, the original's encounter table (groups and weights), encounter rate (distribution of steps between battles), which areas have no encounters, and the enemy group size range | Rights holder data; cross-check by recording at least 100 encounters in each of five sampled areas of the reference build | ENC-004, ENC-040 and ENC-031 |
| [ENC-024](spec/encounters.md) | Whether the original has preemptive or ambush openings, their probabilities, what affects them, and their exact effect on the first round | Rights holder data; cross-check over the encounters recorded for ENC-005 | ENC-020 to ENC-023 and BAT-004 |
| [ENC-035](spec/encounters.md) | The original's escape rule: who can choose it, success chance and its inputs (agility, level, group), what happens to the rest of the party's commands on failure, and which battles forbid it | Rights holder formulas; cross-check with at least 200 attempts across three areas in the reference build | ENC-030 and ENC-031 |
| [ENC-036](spec/encounters.md) | The reference build baselines for M1 to M6 on the critical path of every area | At least three players play the reference build along the critical path without deliberate grinding or avoidance; a logging tool or video review records each battle's start, end, area, rewards and party levels | ENC-033 |
| [ENC-041](spec/encounters.md) | The world distance one original step represents, as the ratio between the original's tile size and the remake's scale for each area kit | Derive from the area's measured walk time in FLD-022 divided by its step count over the same route | ENC-040 |

## Save and progression

| ID | What we need | How to get it | Unblocks |
|---|---|---|---|
| [SAV-004](spec/save-and-progression.md) | Where and how the original lets the player save (locations, objects, services, any cost) and whether it has any suspend feature | Rights holder data and a reference build playthrough | SAV-003 |
| [SAV-031](spec/save-and-progression.md) | Per character, the experience curve, level cap and stat growth; per enemy, experience, money and drops with drop rates | Rights holder data; cross-check level-ups and rewards for 30 battles in the reference build | SAV-030 |
| [SAV-032](spec/save-and-progression.md) | How the original splits battle experience: among living, fallen and temporary members, and whether members outside the active party gain any | Rights holder data; cross-check with a fallen member in the reference build | SAV-033 |
| [SAV-035](spec/save-and-progression.md) | What happens to a temporary party member's equipment and items, level and state when it leaves and when it rejoins | Rights holder data and a reference build playthrough | SAV-034 |
| [SAV-036](spec/save-and-progression.md) | Inventory limits (slots, stack sizes, money cap) and what happens when the party receives an item it has no room for | Rights holder data; cross-check in the reference build | SAV-001 inventory rules |
| [SAV-038](spec/save-and-progression.md) | Which stats the original applies wrongly at the level cap, and the intended values | Get the community bug fix and level cap fix change lists in phase 0 (S-FAN-DEFECTS), then confirm intended growth against the rights holder's data | SAV-022 |

## Field

| ID | What we need | How to get it | Unblocks |
|---|---|---|---|
| [FLD-022](spec/field.md) | Walking speed and the time to cross each town and each dungeon floor on its critical path in the reference build, with no battles (or battle time subtracted) | Frame counting on captures, one route per area, start and end at named landmarks recorded in the area inventory | FLD-021 and FLD-042 |
| [FLD-035](spec/field.md) | What rest does in the original: which statuses it cures, whether it revives, its cost and how cost scales, and whether any other action refills skill uses | Rights holder data, cross-checked by resting with each status applied in the reference build | FLD-034 |
| [FLD-041](spec/field.md) | An inventory per town of buildings, interiors, shops, NPCs, exits and event hooks, with their relative positions | Rights holder maps, cross-checked against the reference build's map entries (417 per the scope) | FLD-040 |
| [FLD-043](spec/field.md) | Per dungeon, the number of floors, the landmark list (entrances, chests, event points, branches, boss room) in critical-path order, and every mechanism or puzzle with its rule | Rights holder maps and data, cross-checked by a reference build playthrough | FLD-042 and FLD-044 |
| [FLD-052](spec/field.md) | Every fast-travel mechanism in the original (techniques, items, transport services, automatic returns) with its allowed sources, destinations and conditions | Rights holder data, cross-checked in the reference build | FLD-051 |
| [FLD-062](spec/field.md) | Per vehicle, its speed, passable and blocking terrain, boarding and exit rules, and the areas it reaches | Rights holder data, cross-checked in the reference build with timed routes | FLD-061 |
| [FLD-074](spec/field.md) | Where the original allows party talk, how it picks the conversation, and how many conversation sets exist | Rights holder script and data; cross-check by triggering party talk in every area of a reference build playthrough | FLD-070 and FLD-072 |

## Game flow

| ID | What we need | How to get it | Unblocks |
|---|---|---|---|
| [FLOW-006](spec/game-flow.md) | Median time to finish the main story in the reference build | At least three full critical-path playthroughs by players new to the game and three by experienced players, timed from new game to the final scene, with the guild jobs done and not done as two separate numbers | FLOW-005 and [ACC-FLOW-05](acceptance.md) |
| [FLOW-012](spec/game-flow.md) | The full list of the original's ten field sub-states and what each allows | From the rights holder's engine documentation if it exists; otherwise enumerate every distinct field interface state in the reference build (what input does, what is on screen) and map each to a remake sub-state | FLOW-011 |
| [FLOW-033](spec/game-flow.md) | In the reference build, the time from encounter trigger to first command input, from the last battle input to field control, and from a door or exit to control in the next map | Frame counting on captures, 20 samples each across at least five areas | FLOW-030 to FLOW-032 |
| [FLOW-041](spec/game-flow.md) | What the original does after party defeat (return to title, return to a town, any penalty to money or items, what state is kept) | Lose battles on purpose at three points in the reference build (early field, mid-game dungeon, a boss) and record the result | FLOW-040 |

## Events and live panels

| ID | What we need | How to get it | Unblocks |
|---|---|---|---|
| [EVT-007](spec/events-and-live-panels.md) | Every player choice in the original, its options, and what each option changes | Rights holder script and data; cross-check by taking each option in the reference build | EVT-005 |
| [EVT-061](spec/events-and-live-panels.md) | For every scene, the original's total time from trigger to control returning, and its presentation overhead, with text advanced as soon as each page completes | Frame counting on reference build captures, using the text reveal speed from TXT-026 | EVT-060 |

## Dialogue and text

| ID | What we need | How to get it | Unblocks |
|---|---|---|---|
| [TXT-006](spec/dialogue-and-text.md) | The exact count of dialogue messages (estimated at about 2,170) and of system text entries (estimated at about 865), and every inline code in use | Phase 0 audit of the rights holder's script; cross-check against the cartridge text count | TXT-030 and localization budgets |
| [TXT-026](spec/dialogue-and-text.md) | The original's text reveal speed (characters per second), page pause behavior, and whether any text auto-advances | Frame counting on reference build captures over at least 30 pages | TXT-023 and EVT-061 |

## Menus and UI

| ID | What we need | How to get it | Unblocks |
|---|---|---|---|
| [UI-016](spec/menus-and-ui.md) | The original's menu tree, labels and depth for every field and battle menu | A reference build session that records every screen; compared with UI-003 so nothing the original offers is missing |  |
