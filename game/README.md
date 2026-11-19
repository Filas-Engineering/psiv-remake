# game

A headless game session library that joins the battle rules (`../core`, namespace `rules`) and the event interpreter (`../events`, namespace `events`) into something playable, plus a terminal front end and a small original placeholder adventure. Plain C++17, standard library only, no exceptions across the public API, integer rules arithmetic. Namespace `game`.

## Layout

| Path | Contents |
| --- | --- |
| `include/game/world.h` | `World`: flags, variables, roster and lineup, chosen names, items, equipment bag, money, macros, combo log, area and position, visible enemies, encounter stream, job states, encyclopedia unlocks, play time, per-save settings |
| `include/game/area.h` | `Area` data (grid, markers, exits, hooks, shops, rest points, chests, encounter zones) and its JSON reader |
| `include/game/data.h` | `GameConfig` (`game.json`), `Journal` (`journal.json`) and `loadGameData`, which loads everything, builds the script registry, compiles the scripts against it and checks every cross-reference |
| `include/game/modes.h` | `Mode` and `ModeMachine`: the transition table of `docs/spec/game-flow.md` |
| `include/game/encounters.h` | Visible enemies (spawn, detection, chase, leash, contact openings) and the random-battle counter |
| `include/game/save.h` | The chunked save format, migrations, the clear-data record, atomic writes and `SaveStore` (manual and rotating autosave slots) |
| `include/game/profile.h` | `Profile`: the global settings file (SAV-005) |
| `include/game/pacing.h` | `PacingRecorder`: the encounter pacing metrics per area and per boss |
| `include/game/session.h` | `Session`: the game itself, including the `events::Host` implementation and battle driving |
| `include/game/runner.h` | The terminal command language shared by `game_cli` and the pacing bot |
| `src/` | Library sources, built into `build/libgame.a` |
| `cli/game_cli.cpp` | Terminal front end, scripted runs and the pacing bot |
| `tests/` | One test executable with a minimal assert helper (`tests/test.h`); `tests/expected/playthrough.txt` is the golden transcript |
| `data/` | The placeholder adventure: `game.json`, `journal.json`, `areas/`, `events/`, `text/en.strings` (dialogue), `text/en.json` (the game's own text), `text/glossary.txt`, `scripts/playthrough.txt`, `routes/main.route` |

Battle data (characters, enemies, encounters, items, equipment) and core's English text table are read from `../core/data` through `core_data` in `game.json`; the adventure adds no battle content of its own.

## Build and test

```
make          # builds ../core and ../events libraries if needed, then build/libgame.a, build/game_cli, build/game_tests
make test     # unit tests, event_cli check --strict on the scripts with the registry from the data, the scripted
              # playthrough compared byte for byte with tests/expected/playthrough.txt, then the bot
make clean
```

Run tests from `game/`, because they load `data/`. One test by name: `./build/game_tests saveRoundTrips`.

```
./build/game_cli                                    # interactive; type help
./build/game_cli --seed 7 --auto-battle --script data/scripts/playthrough.txt --saves build/cli_saves
./build/game_cli --bot --seed 7                     # pacing tables for visible and random encounters
./build/game_cli --bot --seed 7 --encounters random --route data/routes/main.route
./build/game_cli --registry                         # the registry built from the data, in the events registry format
```

A scripted run writes autosaves, the profile and the clear-data record, so start it with an empty `--saves` directory to get the same transcript (`make test` does). Exit codes: 0 done, 1 data error, 2 bad arguments, 3 a scripted command failed or the bot did not finish.

Commands (one per line; `#` starts a comment):

| Mode | Commands |
| --- | --- |
| Title | `new`, `continue`, `load SLOT`, `retry` (after a game over that has a pre-battle autosave), `quit` |
| Field | `n`/`e`/`s`/`w` `[COUNT]`, `walk MARKER`, `walk X Y` (shortest path; bumping into an enemy on the way starts its battle), `face DIR`, `talk`, `chat` (party talk), `menu`, `look`, `status`, `wait MS` |
| Event | `choose N`, `skip`. Lines are printed and confirmed automatically |
| Battle | `attack M [eN]`, `use M ABILITY [eN\|pN]`, `item M ITEM [eN\|pN]`, `defend M`, `escape M`, `macro NAME`, `round`, `auto` (auto-battle with the auto macro, then core's policy), `cancel` (members by index or character id) |
| Menu | `item ITEM [MEMBER]`, `tech USER ABILITY [MEMBER]`, `equip MEMBER PIECE`, `unequip MEMBER SLOT`, `save manual_N` (field menu or save prompt), `saves`, `close`; name entry: `name TEXT` |
| Shop, inn | `buy ID [N]`, `sell ID [N]`, `leave`; `rest`, `close` |
| Any | `set difficulty\|reward\|auto VALUE` (per save), `set text\|battle\|shipped VALUE` or `set PROFILE_KEY VALUE` (profile), `quests`, `codex`, `log`, `pacing`, `title` |

## How an engine drives a frame

The engine owns one `GameData` (loaded once) and one `Session`. Every frame:

1. Call `session.update(dtMs, input)`. In Field, `input` is the held direction; the field runs in fixed ticks (`tick_ms`), moving the leader one cell per `step_ms` of held input, ticking visible enemies, and starting battles and events. In Event it steps the interpreter. Battle and Title ignore it.
2. Read `session.mode()` and present that mode:
   * Field: `world()` (position, facing, `enemies`) and `area()`. Interact with `interact()`, open the menu with `openMenu()`, party talk with `partyTalk()`.
   * Event: when `linePending()`, show `lineText()` (string IDs resolved, names rendered for the profile's "show shipped names") and call `confirmLine()` on the button. When `choice()` is not null, show it through `scriptText()` and call `choose(index)`. `canSkip()` and `skip()` drive skipping; skipped lines go to `textLog()`.
   * Battle: `battle()` is the live `rules::Battle`. Set commands with `setCommand` (its refusal comes back as text from core's message keys) or `applyMacro`. For auto-battle call `startAuto()` once, then `fillFromAuto()` at each command phase until it returns false (the stop reason is in the output). `fillFromPolicy()` is core's simple policy. Then `resolveRound()`, which returns the round's core events for playback. The battle ends inside `resolveRound()`: rewards, level-ups, grace period, and the return to Field, Event or (game over) Title happen there.
   * Menu and Shop: `menuKind()` says field menu, inn, save prompt or name entry. `shop()`, `inn()`, `buy`, `sell`, `rest`, `useItem`, `useAbility`, `equip`, `unequip`, `saveManual`, `enterName`, `closeMenu`. A shop, save prompt or name entry an event opened closes back to the event.
   * Title: `newGame`, `continueGame`, `load`, `retryBattle`.
   * Any time: `questLog()`, `encyclopedia()`, `textLog()`, `profile()` and `setProfile()`.
3. Drain `drainOutput()`. Each `Output` has a text key with parameters (`message`) and its English rendering (`text`); warnings about broken data have no key.

Presentation-only script actions (moves, cameras, panels, audio, fades) go to the optional `Presenter` in `SessionOptions`. The default treats each as instant; an engine returns false from `begin` and true from `finished` once its animation ends, and the interpreter waits on it. An action with `instant` set (the rebuild after a skip or a restore) is done when `begin` returns: the presenter puts its end state up at once. Warps change the world before the presenter sees them. The terminal front end is only `Runner` plus `game_cli.cpp`; an engine replaces both and keeps everything else.

## Data formats

### game.json

`title`, `core_data` (battle data directory, relative to `data/`), `areas`, `events`, `glossary`, `strings` (the dialogue string table, events format), `text` (the game's own text table, core's `{"text": {...}}` format; merged with core's `text_en.json`, and a key in both is an error), `journal`, `actors` (script actors that are not characters; each needs a `speaker.<id>` text), `presentation` (`audio`, `cameras`, `layouts`, `animations`, `portraits` the scripts may name), `flags` (every flag a script, hook or journal entry uses; chest flags are declared by their chests), `vars` (name to starting value), `clear_flag`, `scripted_ends` (encounter to `{"survive_rounds": N}` or `{"enemy_defeated": slot}`, for event battles), `start` (`area`, `marker`, `facing`, `lineup`, `level`, `money`, `items`), `join_levels` (level a character has when an event first adds it), `prices` (buy price per item or equipment id; sell price is the shop's `sell_percent` of it), `item_cap`, `money_cap`, `tick_ms`, `grace_ms`, `round_ms`, `autosave_slots`, `manual_slots`, `bosses` (`encounter` and the `flag` its event sets; pacing keys M5 and M6 by it). Unknown fields are errors.

### journal.json

```json
{
	"objectives": [{"id": "reach_cove", "text": "objective.reach_cove", "after": "tidewater.met_guide", "until": "cove.warden_beaten"}],
	"jobs": [{"id": "lost_cache", "title": "job.lost_cache", "available": "tidewater.arrived", "accepted": "job.lost_cache.accepted", "completed": "job.lost_cache.done"}],
	"encyclopedia": [{"id": "bell_warden", "title": "codex.bell_warden.title", "text": "codex.bell_warden.text", "flag": "cove.warden_seen"}],
	"party_talk": [{"id": "reeds", "area": "reed_path", "after": "tidewater.met_guide", "lines": [{"speaker": "mystic", "text": "talk.reeds.1"}]}]
}
```

* Texts are keys of the game's text table. A flag range holds while `after` is set (or left out) and `until` is not set (or left out).
* Objectives (UI-012): the last listed one whose range holds is the quest log's line.
* Jobs: available once `available` is set (or from the start), accepted when `accepted` is set, completed when `completed` is set. Scripts set the flags; the state never moves back.
* Encyclopedia entries (TXT-041) unlock when their flag is first set and stay unlocked.
* Party talk sets (FLD-072): a set with an `area` is for that area, one without is a chapter set. Among the sets whose area and range hold, an area set beats a chapter set, then the set with more range bounds wins, then the first listed. Speakers are characters.

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

### Event scripts and text

The `events/` format (`../events/FORMAT.md`). The loader builds an `events::Registry` from the data (actors, characters, markers, presentation names, items and equipment, encounters, areas, shops, rest points, flags and variables) and compiles against it, so an unknown name or an undeclared flag is a load error. It then applies the strict check: compiler warnings are errors, every player-facing text is a string ID in `text/en.strings`, the table matches the scripts, and glossary names are written as name tokens. Only warp targets are still checked by the game itself, because the registry knows markers but not which area holds them. `game_cli --registry` prints the registry so `event_cli check --strict --registry` can run on the same scripts; `make test` does.

Shop and rest point ids are unique across areas, so a script names them without an area. A shop or rest point without `at` is reached only from a script.

### Saves

A text file, platform independent:

```
PSR-SAVE 2
chunk header 1 <payload bytes>
schema 2
...
chunk location 1 <n>
...
end <FNV-1a 64 of everything before this line, 16 hex digits>
```

Chunks and versions: `header` 1 (schema, build, rules version, content hash, write time, sequence, autosave, play time, area, party, difficulty: enough for the slot list), `location` 1, `party` 2 (members with statuses that persist outside battle, chosen names), `inventory` 1, `world` 1 (flags, variables, visited areas, control lock, visible enemies of the current area, grace, random counter, play time), `battle` 2 (the auto macro, then core's `serializeMacros` and `serializeComboLog` blocks), `random` 1 (the encounter stream's `rules::Rng` state), `difficulty` 1, `quests` 1 (job states), `unlocks` 1 (encyclopedia entries), and `midevent` 1 (only in the autosave before an event battle: `events::save_state_to_text`, `events-save 3`). Every known chunk but `midevent` must be present. Values are tokens with letters, digits, `_` and `.` kept and every other byte as `%XX`; empty is `-`. Unknown chunks are kept in `World::unknownChunks` and written back unchanged after the known ones. A known chunk with a higher version, or a higher schema, is refused as "from a newer build". A checksum mismatch is refused as damaged and listed as damaged. Writes go to `slot.sav.tmp` and are renamed over the slot.

Migrations (SAV-021) run in order on the chunk list before it is read. Schema 1 to 2: the battle chunk's own macro and combo lines become core's blocks (combinations found before get play time 0 and no participants), the party chunk moves to version 2 unchanged, empty `quests` and `unlocks` chunks are added and filled from the flags on load, and a `midevent` chunk is dropped, because its `events-save 2` text no longer loads; such a save continues in the field. A test loads a hand-written schema 1 save.

Slots are `manual_0` .. `manual_9` and `auto_0` .. `auto_2` in the save directory. An autosave writes an empty or damaged autosave slot, otherwise the one with the lowest sequence number. Continue loads the newest undamaged save by write time.

The same directory holds `profile.cfg`, the global profile (`PSR-PROFILE 1`, one `key value` line per setting, then `end` and the checksum; unknown keys are kept and written back; a damaged file means default settings and a warning), and `clear.sav`, the clear-data record (SAV-024: `PSR-CLEAR 1` with the header, party, inventory and world chunks; `parseClearRecord` reads it).

## Rules this library chose

Where the spec is "To measure" the proposed default is implemented and kept in data; where it is silent the choice is listed here.

* Modes: Title, Field, Event (the spec's Cutscene), Battle, Menu, Shop. Shop follows the Menu rows. A Menu or Shop only closes back to the mode that opened it. Quit to Title (FLOW-022) resets the machine instead of using a transition, since the table has no Field to Title row.
* Grid field: interaction range is the faced cell (FLD-030); contact (ENC-011) is one body trying to step into the other's cell; the rear 90-degree arc is ±45 degrees, inclusive, around the back direction (ENC-020/021).
* Grace (ENC-015): for `grace_ms` after any battle no enemy chases or starts a battle, and the leader cannot walk through one. An escaped group returns to its leash. Defeated groups come back on area re-entry (ENC-014).
* Random mode: the counter freezes in towns, during grace and outside zones (ENC-042); openings are drawn from the zone's percents (ENC-022/024 proposed defaults 8 and 8).
* Battle seeds come from the encounter stream; a retry XORs in a retry counter (BAT-134). Retry (FLOW-042) exists only where an autosave before the lost battle exists, which is event battles.
* Defeat without a defeat branch is a game over (FLOW-041 to measure): back to Title, where Continue loads the newest save. A defeat with a `lose` branch stands fallen members up with 1 hit point before the branch runs.
* Event battles: escape needs both the script's `flee` section and the encounter's escape flag. The opening comes from the script. A scripted end comes from `scripted_ends` in `game.json`, because the script format cannot say one; it pays the fallen enemies' rewards and returns to the `win` branch.
* Autosaves (FLOW-050): area entry when the area was not visited since the last autosave, event end when the script changed any flag, before every event battle (written from inside `start_battle`, with interpreter state), after rest (inn or script). The pre-battle autosave holds the encounter stream from before the battle seed was drawn, so loading it sets up the same battle; setting that battle up again from its own autosave writes no second one.
* Rest (FLD-034/035): `rules::rest` for the whole roster, active or not, which cures the statuses with `rest_cures`. A script's `rest` with no rest point is free; with one it charges the point's cost, and without the money the party does not rest and the script carries on.
* Statuses that persist outside battle stay on the members between battles, show in `status`, are saved, and are cured by rest or by field items and techniques.
* Experience goes to the active party only (SAV-032). A member who leaves stays in the roster with its equipment and comes back as it left (SAV-035).
* Inventory caps (SAV-036): `item_cap` per id, `money_cap`. Worn pieces cannot be sold.
* Field items and techniques go through core's `useItemOutsideBattle` and `useAbilityOutsideBattle`, on the active party. An item that changes nothing is not used up.
* Manual saves (SAV-003/004): from the field menu, anywhere, never during an event. A script's `save prompt` writes an ordinary save without interpreter state and with player control on, so loading it returns to a playable field.
* Name entry: 1 to 16 characters, no control characters, spaces at the ends dropped. Any character can be named, joined or not. The name replaces the character's name in lines, speakers and notices.
* Play time (SAV-037) counts field, event and menu time, plus `round_ms` per battle round, because headless battles have no clock. Pacing battle time (M3) uses the same number.
* Pacing: M1 is battles per area visit (no exit landmarks yet), M2 the median field time and steps between battles, M5 the active party's average level when a boss battle is set up, M6 dungeon entry to boss victory.
* Settings: difficulty, reward multiplier and the auto-battle macro are per save; everything else in UI-013 is the profile. UI-013 lists the auto-battle macro as a setting, but it names one of the save's macros and SAV-001 puts it in the save.
* Quests and unlocks: job states and encyclopedia unlocks are saved, because they never move back even if their flags are cleared. The objective line is not saved; it follows the flags.
* Party talk: lines of characters not in the active party are left out; a set with no line left says nothing. Party talk is not added to the text log.
* Text log (TXT-027 to TXT-029): event lines shown and lines a skip passed over (marked), the last 500 since the game was loaded or started, rendered with the current names setting, not saved.
* The clear-data record is written when control returns after `clear_flag` is first set, and replaces any earlier one.
* Speaker names are text keys (`speaker.<id>`), or the character's chosen or data name.

## Changes in this round

* Builds against core rules version 3 and events `events-save 3`: core's event types, `CancelReason`, `Message` returns, `equip`/`unequip` with the bag, the new `ComboLog`; the new Host methods.
* Dropped the game's own workarounds: the equipment bag bookkeeping (core moves pieces), field item formulas (core's field use, now also techniques), the macro and combo log save lines (core's forms), the encounter stream type (`rules::Rng`), English battle text (core's keys and table), the hand-written auto policy loop (core's auto-battle and stop rules), and the post-compile walk over scripts (the registry, except warps).
* New Host methods: `open_shop`, `rest_party`, `open_save_prompt`, `open_name_entry`, `log_skipped_line`; the battle opening from scripts; the pre-battle autosave written inside `start_battle`; instant presentation after a restore and a skip.
* New: quest log, party talk, encyclopedia, global profile file, clear-data record, save schema 2 with the 1 to 2 migration, string tables, the game's own text table, the registry.
* The placeholder adventure has a dockhand's job, a peddler's shop opened from a script, a campfire rest from a script, two encyclopedia entries, party talk in the reeds and in the chapter, and its dialogue in `text/en.strings`. Poison from the reed battles is carried out of battle and cured at the campfire, and the scripted run turns auto-battle on with the `tempest` macro until it stops.
* Save schema 2 (`PSR-SAVE 2`); `parseSave` no longer takes settings. Game build `game-0.2`.

## Not built

* Vehicles, vehicle battles and vehicle encounter tables; camera volumes (the location chunk writes them empty).
* Ambient party talk (FLD-073); the profile has its switch, nothing reads it.
* Localization beyond English: the profile's text language only picks the name set; one dialogue table and one text table are loaded. Area names stay in the area files, and the reasons `Session` operations return when they refuse are English strings in code.
* Text speed, auto-advance, text size, battle speed, reduced flash and volumes are stored in the profile, but nothing reads them headless.
* The pause overlay; macro editor and combo log screens (the combo log's play times and participants are kept).
* Analog movement and capsule collision: the field is a grid.
