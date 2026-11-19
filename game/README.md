# game

A headless game session library that joins the battle rules (`../core`, namespace `rules`) and the event interpreter (`../events`, namespace `events`) into something playable, plus a terminal front end and a small original placeholder adventure. Plain C++17, standard library only, no exceptions across the public API, integer rules arithmetic. Namespace `game`.

## Layout

| Path | Contents |
| --- | --- |
| `include/game/world.h` | `World`: flags, variables, roster and lineup, items, equipment bag, money, macros, combo log, area and position, visible enemies, encounter stream, play time, settings |
| `include/game/area.h` | `Area` data (grid, markers, exits, hooks, shops, rest points, chests, encounter zones) and its JSON reader |
| `include/game/data.h` | `GameConfig` (`game.json`) and `loadGameData`, which loads everything and checks every cross-reference |
| `include/game/modes.h` | `Mode` and `ModeMachine`: the transition table of `docs/spec/game-flow.md` |
| `include/game/encounters.h` | Visible enemies (spawn, detection, chase, leash, contact openings) and the random-battle counter |
| `include/game/save.h` | The chunked save format, atomic writes and `SaveStore` (manual and rotating autosave slots) |
| `include/game/pacing.h` | `PacingRecorder`: the encounter pacing metrics per area and per boss |
| `include/game/session.h` | `Session`: the game itself, including the `events::Host` implementation and battle driving |
| `include/game/runner.h` | The terminal command language shared by `game_cli` and the pacing bot |
| `src/` | Library sources, built into `build/libgame.a` |
| `cli/game_cli.cpp` | Terminal front end, scripted runs and the pacing bot |
| `tests/` | One test executable with a minimal assert helper (`tests/test.h`); `tests/expected/playthrough.txt` is the golden transcript |
| `data/` | The placeholder adventure: `game.json`, `areas/`, `events/`, `text/glossary.txt`, `scripts/playthrough.txt`, `routes/main.route` |

Battle data (characters, enemies, encounters, items, equipment) is read from `../core/data` through `core_data` in `game.json`; the adventure adds no battle content of its own.

## Build and test

```
make          # builds ../core and ../events libraries if needed, then build/libgame.a, build/game_cli, build/game_tests
make test     # unit tests, the scripted playthrough compared byte for byte with tests/expected/playthrough.txt, then the bot
make clean
```

Run tests from `game/`, because they load `data/`. One test by name: `./build/game_tests saveRoundTrips`.

```
./build/game_cli                                    # interactive; type help
./build/game_cli --seed 7 --auto-battle --script data/scripts/playthrough.txt --saves build/cli_saves
./build/game_cli --bot --seed 7                     # pacing tables for visible and random encounters
./build/game_cli --bot --seed 7 --encounters random --route data/routes/main.route
```

A scripted run writes autosaves, so start it with an empty `--saves` directory to get the same transcript (`make test` does). Exit codes: 0 done, 1 data error, 2 bad arguments, 3 a scripted command failed or the bot did not finish.

Commands (one per line; `#` starts a comment):

| Mode | Commands |
| --- | --- |
| Title | `new`, `continue`, `load SLOT`, `retry` (after a game over that has a pre-battle autosave), `quit` |
| Field | `n`/`e`/`s`/`w` `[COUNT]`, `walk MARKER`, `walk X Y` (shortest path; bumping into an enemy on the way starts its battle), `face DIR`, `talk`, `menu`, `look`, `status`, `wait MS` |
| Event | `choose N`, `skip`. Lines are printed and confirmed automatically |
| Battle | `attack M [eN]`, `use M ABILITY [eN\|pN]`, `item M ITEM [eN\|pN]`, `defend M`, `escape M`, `macro NAME`, `round`, `auto` (members by index or character id) |
| Menu | `item ITEM MEMBER`, `equip MEMBER PIECE`, `unequip MEMBER SLOT`, `save manual_N`, `saves`, `close` |
| Shop, inn | `buy ID [N]`, `sell ID [N]`, `leave`; `rest`, `close` |
| Any | `set difficulty\|reward\|text\|battle\|shipped\|auto VALUE`, `pacing`, `title` |

## How an engine drives a frame

The engine owns one `GameData` (loaded once) and one `Session`. Every frame:

1. Call `session.update(dtMs, input)`. In Field, `input` is the held direction; the field runs in fixed ticks (`tick_ms`), moving the leader one cell per `step_ms` of held input, ticking visible enemies, and starting battles and events. In Event it steps the interpreter. Battle and Title ignore it.
2. Read `session.mode()` and present that mode:
   * Field: `world()` (position, facing, `enemies`) and `area()`. Interact with `interact()`, open the menu with `openMenu()`.
   * Event: when `linePending()`, show `lineText()` (names already rendered for the "show shipped names" setting) and call `confirmLine()` on the button. When `choice()` is not null, show it and call `choose(index)`. `canSkip()` and `skip()` drive skipping.
   * Battle: `battle()` is the live `rules::Battle` (combatants, `sideActsNextRound`, `escapeAllowed`). Set commands with `setCommand` or `applyMacro`, then `resolveRound()`, which returns the round's core events for playback. `autoRound()` uses the auto-battle macro or core's auto policy. The battle ends inside `resolveRound()`: rewards, level-ups, grace period, and the return to Field, Event or (game over) Title happen there.
   * Menu and Shop: `shop()`, `inn()`, `buy`, `sell`, `rest`, `useItem`, `equip`, `unequip`, `saveManual`, `closeMenu`.
   * Title: `newGame`, `continueGame`, `load`, `retryBattle`.
3. Drain `drainOutput()` for notices, action descriptions, warnings and autosave messages.

Presentation-only script actions (moves, cameras, panels, audio, fades) go to the optional `Presenter` in `SessionOptions`. The default treats each as instant; an engine returns false from `begin` and true from `finished` once its animation ends, and the interpreter waits on it. Warps change the world before the presenter sees them. The terminal front end is only `Runner` plus `game_cli.cpp`; an engine replaces both and keeps everything else.

## Data formats

### game.json

`title`, `core_data` (battle data directory, relative to `data/`), `areas`, `events`, `glossary`, `speakers` (speaker id to display name), `start` (`area`, `marker`, `facing`, `lineup`, `level`, `money`, `items`), `join_levels` (level a character has when an event first adds it), `prices` (buy price per item or equipment id; sell price is the shop's `sell_percent` of it), `item_cap`, `money_cap`, `tick_ms`, `grace_ms`, `round_ms`, `autosave_slots`, `manual_slots`, `bosses` (`encounter` and the `flag` its event sets; pacing keys M5 and M6 by it). Unknown fields are errors.

### Areas

```json
{
	"id": "reed_path", "name": "Reed Path", "kind": "field", "encounter_mode": "visible", "step_ms": 250,
	"map": ["#####", "#...#", "....."],
	"markers": {"west_gate": [1, 2]},
	"exits": [{"at": [0, 2], "to": "tidewater", "marker": "east_gate"}],
	"hooks": [{"id": "scout", "at": [3, 1], "trigger": "step", "event": "reed_path_scout", "block_flag": "reed.scout_joined"}],
	"shops": [{"id": "stall", "at": [3, 1], "stock": ["tonic"], "sell_percent": 50}],
	"rests": [{"id": "inn", "at": [3, 1], "cost": 10}],
	"chests": [{"id": "cache", "at": [3, 1], "flag": "chest.cache", "items": {"tonic": 1}, "equipment": {}, "money": 30}],
	"zones": [{"id": "reeds", "from": [1, 1], "to": [3, 2], "table": [{"encounter": "training", "weight": 1}],
		"visible": {"spawns": [[2, 1]], "cap": 2, "detection_radius": 3, "leash_radius": 2, "chase_speed_percent": 110, "chase_ms": 6000, "wander_speed_percent": 50, "wander_percent": 50},
		"random": {"min_steps": 6, "max_steps": 12, "step_distance": 1, "party_first_percent": 8, "enemy_first_percent": 8}}]
}
```

* Map rows: `.` floor, `#` wall (blocks sight), `~` water. Cells are `[x, y]` from the top left.
* `kind`: `town` (no random battles), `field` or `dungeon` (M6 runs from entry to the boss's end).
* `encounter_mode` (ENC-002): `visible` or `random`; `SessionOptions::encounterMode` overrides it for every area, which is how the bot compares the two.
* Hook `trigger`: `interact` (the cell the leader faces; the cell is solid), `step` (entering the cell), `load` (area entry, once per entry) or `flag` (whenever control is in the field and the flags hold; needs `block_flag`). `require_flag` and `block_flag` gate every hook.
* Shops, rest points, chests and interact hooks stand on solid cells, interacted with from the next cell while facing them. A chest's flag is set when opened and never cleared.
* Visible zone values are the ENC-012/013 defaults when left out. Random thresholds are drawn uniformly between `min_steps` and `max_steps` (the original's distribution is ENC-005).

### Event scripts

The `events/` format (`../events/FORMAT.md`). The loader also checks what the events compiler cannot: battle encounters, given or taken items, party members and warp targets must exist. String IDs (`@id`) are rejected because no string table is loaded.

### Saves

A text file, platform independent:

```
PSR-SAVE 1
chunk header 1 <payload bytes>
schema 1
...
chunk location 1 <n>
...
end <FNV-1a 64 of everything before this line, 16 hex digits>
```

Chunks: `header` (schema, build, rules version, content hash, write time, sequence, autosave, play time, area, party, difficulty: enough for the slot list), `location`, `party`, `inventory`, `world` (flags, variables, visited areas, control lock, visible enemies of the current area, grace, random counter, play time), `battle` (macros, auto-battle macro, combo log), `random` (encounter stream state), `difficulty`, and `midevent` (only in the autosave before an event battle: `events::save_state_to_text`). Values are tokens with letters, digits, `_` and `.` kept and every other byte as `%XX`; empty is `-`. Unknown chunks are kept in `World::unknownChunks` and written back unchanged after the known ones. A known chunk with a higher version, or a higher schema, is refused as "from a newer build". A checksum mismatch is refused as damaged and listed as damaged. Writes go to `slot.sav.tmp` and are renamed over the slot.

Slots are `manual_0` .. `manual_9` and `auto_0` .. `auto_2` in the save directory. An autosave writes an empty or damaged autosave slot, otherwise the one with the lowest sequence number. Continue loads the newest undamaged save by write time.

## Rules this library chose

Where the spec is "To measure" the proposed default is implemented and kept in data; where it is silent the choice is listed here.

* Modes: Title, Field, Event (the spec's Cutscene), Battle, Menu, Shop. Shop follows the Menu rows. A Menu or Shop only closes back to the mode that opened it. Quit to Title (FLOW-022) resets the machine instead of using a transition, since the table has no Field to Title row.
* Grid field: interaction range is the faced cell (FLD-030); contact (ENC-011) is one body trying to step into the other's cell; the rear 90-degree arc is ±45 degrees, inclusive, around the back direction (ENC-020/021).
* Grace (ENC-015): for `grace_ms` after any battle no enemy chases or starts a battle, and the leader cannot walk through one. An escaped group returns to its leash. Defeated groups come back on area re-entry (ENC-014).
* Random mode: the counter freezes in towns, during grace and outside zones (ENC-042); openings are drawn from the zone's percents (ENC-022/024 proposed defaults 8 and 8).
* Battle seeds come from the encounter stream; a retry XORs in a retry counter (BAT-134). Retry (FLOW-042) exists only where an autosave before the lost battle exists, which is event battles.
* Defeat without a defeat branch is a game over (FLOW-041 to measure): back to Title, where Continue loads the newest save. A defeat with a `lose` branch stands fallen members up with 1 hit point before the branch runs.
* Event battles: escape needs both the script's `flee` section and the encounter's escape flag. The opening is always Normal.
* Autosaves (FLOW-050): area entry when the area was not visited since the last autosave, event end when the script changed any flag, before every event battle (with interpreter state), after rest. Loading the pre-battle autosave takes it again, because the battle is set up again before the first command.
* Rest (FLD-034/035): costs the rest point's `cost` and calls `rules::rest` for the whole roster, active or not.
* Experience goes to the active party only (SAV-032). A member who leaves stays in the roster with its equipment and comes back as it left (SAV-035).
* Inventory caps (SAV-036): `item_cap` per id, `money_cap`. Equipment is counted in an equipment bag, since core does not count it. Worn pieces cannot be sold.
* Field item use applies heal and revive with core's formulas and flat item power. An item that changes nothing is not used up, matching core's battle rule.
* Manual saves (SAV-003/004): from the field menu, anywhere, never during an event.
* Play time (SAV-037) counts field, event and menu time, plus `round_ms` per battle round, because headless battles have no clock. Pacing battle time (M3) uses the same number.
* Pacing: M1 is battles per area visit (no exit landmarks yet), M2 the median field time and steps between battles, M5 the active party's average level when a boss battle is set up, M6 dungeon entry to boss victory.
* Settings other than difficulty and reward multiplier are not written to saves (SAV-005); they live in the session.

## Not built

* Vehicles, vehicle battles and vehicle encounter tables; camera volumes (the location chunk writes them empty).
* Party talk and ambient talk, the quest log, the encyclopedia and the clear-data record (SAV-024).
* The global settings profile file; text speed and battle speed are stored but nothing reads them headless.
* String tables and localization; name entry; the pause overlay; macro editor and combo log screens.
* Statuses that persist outside battle (core has none), so cure items do nothing in the field.
* Save migrations: schema 1 is the first, and the place for ordered migrations is marked in `save.cpp`.
* Analog movement and capsule collision: the field is a grid.
* Opening a shop, inn or save prompt from an event: the script format has no command for it.
