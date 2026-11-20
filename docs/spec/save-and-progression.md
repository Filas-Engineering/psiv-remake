# Save and progression

What a save holds, saving rules, round trip and versioning, and how the party progresses: experience, levels and rewards. When autosaves happen is in [game-flow.md](game-flow.md). Rest is [FLD-034](field.md).

## Save contents

| ID | Requirement | Label | Source |
|---|---|---|---|
| SAV-001 | A save holds every field in the table below and nothing else that affects play | Proposed (spec) | S-SYS-QOL |
| SAV-002 | There are at least 10 manual save slots and the three rotating autosave slots from [FLOW-054](game-flow.md). The slot list shows location, play time, party, active difficulty and write time, and marks autosaves. Published sources say the original has three save files, so the larger slot count is an addition; see [published-mechanics.md](../research/published-mechanics.md) | Proposed (spec) | S-SYS-QOL |
| SAV-003 | Manual saves are allowed only where the original allows saving (SAV-004), only in the Field mode, and never while an event runs ([EVT-053](events-and-live-panels.md)). Published sources say the original saves anywhere outside dungeons and battle, from a separate system window and not the field menu (SAV-004); see [published-mechanics.md](../research/published-mechanics.md) | Proposed (spec) | S-GOALS |
| SAV-004 | To measure: where and how the original lets the player save (locations, objects, services, any cost) and whether it has any suspend feature. Method: rights holder data and a reference build playthrough. Feeds SAV-003 | To measure | S-GOALS |
| SAV-005 | Settings other than difficulty and reward multiplier are stored in a global profile, not in saves. Difficulty and reward multiplier are per save | Proposed (spec) | S-SYS-DIFF |

| Group | Contents |
|---|---|
| Header | Format magic, schema version, game build, rules version, content hash, platform-independent checksum, write time, play time |
| Location | Area ID, position, facing, active camera volume, vehicle in use |
| Party | Roster and active order; per member: level, experience, HP, TP, skill uses left, statuses that persist outside battle, equipment |
| Inventory | Items and counts, money |
| World | Global flags, opened chests, visited areas, vehicle positions, visible-enemy defeat state for the current area |
| Quests | Guild job states, main objective flag range |
| Battle-side | Macros, auto-battle macro, combo log |
| Unlocks | Encyclopedia entries |
| Randomness | Encounter stream state ([ENC-006](encounters.md)) |
| Mid-event | Present only in a pre-battle event autosave: interpreter state and event-start state ([EVT-050](events-and-live-panels.md)) |
| Difficulty | Preset and reward multiplier |

## Round trip and integrity

| ID | Requirement | Label | Source |
|---|---|---|---|
| SAV-010 | Loading a save and saving again with no play in between produces a byte-identical file, except write time | Proposed (spec) | S-SYS-QOL |
| SAV-011 | Every field in SAV-001 round-trips: the state after load equals the state before save, compared field by field | Proposed (spec) | S-SYS-QOL |
| SAV-012 | Content is referenced by stable string IDs, never by table index, so adding content does not break saves | Proposed (spec) | S-GOALS |
| SAV-013 | Writes are atomic: an interrupted write leaves the previous file intact | Proposed (spec) | D-PLATFORMS |
| SAV-014 | A save whose checksum fails is shown as damaged and cannot be loaded. It never crashes the game | Proposed (spec) | D-PLATFORMS |
| SAV-015 | The format is platform-independent (fixed byte order, no platform types), so a save can move between platforms if the business allows it | Proposed (spec) | D-PLATFORMS |

## Versioning

| ID | Requirement | Label | Source |
|---|---|---|---|
| SAV-020 | The save format is chunked. Each chunk has a type and a version. A loader keeps chunks it does not recognize and writes them back unchanged | Proposed (spec) | S-GOALS |
| SAV-021 | Every released build loads saves from every earlier released build through ordered migrations. A save from a newer build is refused with a clear message | Proposed (spec) | S-SYS-QOL |
| SAV-024 | Finishing the main story writes a clear-data record with the final party state and flags, so a later product can read it | Proposed (spec) | D-REMAKE, S-GOALS |

## Progression

| ID | Requirement | Label | Source |
|---|---|---|---|
| SAV-030 | At Original difficulty, experience curves, level cap, stat growth and battle rewards match the original (SAV-031) | Proposed (scope) | S-FAN-BALANCE |
| SAV-031 | To measure: per character, the experience curve, level cap and stat growth; per enemy, experience, money and drops with drop rates. Method: rights holder data; cross-check level-ups and rewards for 30 battles in the reference build. Feeds SAV-030 | To measure | S-SOURCE |
| SAV-032 | To measure: how the original splits battle experience: among living, fallen and temporary members, and whether members outside the active party gain any. Method: rights holder data; cross-check with a fallen member in the reference build. Feeds SAV-033 | To measure | S-SOURCE |
| SAV-033 | Experience is shared out by the original's rule from SAV-032. Read from the community disassembly ([original-battle-rules.md](../research/original-battle-rules.md)) and implemented in `core/`: the defeated enemies' experience is halved in a vehicle battle, divided by the number of party members who are not defeated, and each of those gets that share. Money is given in full. A won battle makes one drop roll against the encounter's rate. A level-up raises maximums and does not restore hit points or technique points | Proposed (spec) | S-GOALS |
| SAV-022 | Stats at the level cap are applied as the growth data intends. The original's wrong stat application at the cap is fixed. The exact defect comes from SAV-038 | Proposed (scope) | S-FAN-DEFECTS |
| SAV-023 | The reward multiplier multiplies experience and money from battles only. Chests, shops and guild job rewards are not affected. Results round down to whole numbers | Proposed (spec) | S-FAN-BALANCE |
| SAV-034 | The party changes only through events, as in the original. When a temporary member leaves, the equipment and items it carries follow the original's rule (SAV-035) | Proposed (spec) | S-NONGOALS, S-CONTENT |
| SAV-035 | To measure: what happens to a temporary party member's equipment and items, level and state when it leaves and when it rejoins. Method: rights holder data and a reference build playthrough. Feeds SAV-034 | To measure | S-CONTENT |
| SAV-036 | To measure: inventory limits (slots, stack sizes, money cap) and what happens when the party receives an item it has no room for. Method: rights holder data; cross-check in the reference build. Feeds SAV-001 inventory rules | To measure | S-SOURCE |
| SAV-038 | To measure: which stats the original applies wrongly at the level cap, and the intended values. Method: get the community bug fix and level cap fix change lists in phase 0 (S-FAN-DEFECTS), then confirm intended growth against the rights holder's data. Feeds SAV-022 | To measure | S-FAN-DEFECTS |
| SAV-037 | Play time counts time in every mode except the system pause overlay and the title screen | Proposed (spec) | S-SYS-QOL |
