# game

A headless game session library that joins the battle rules (`../core`, namespace `rules`) and the event interpreter (`../events`, namespace `events`) into something playable, plus a terminal front end and a small original placeholder adventure. Plain C++17, standard library only, no exceptions across the public API, integer rules arithmetic. Namespace `game`.

## Layout

| Path | Contents |
| --- | --- |
| `include/game/world.h` | `World`: flags, variables, roster and lineup, chosen names, items, equipment bag, money, macros, combo log, area and position, visible enemies, encounter stream, job states, encyclopedia unlocks, play time, per-save settings |
| `include/game/area.h` | `Area` data (grid, markers, exits, hooks, shops, rest points, chests, encounter zones) and its JSON reader |
| `include/game/data.h` | `GameConfig` (`game.json`), `Journal` (`journal.json`), `LanguageTables`, `DataDirectories` and `loadGameData`, which loads everything, builds the script registry and the list of string IDs the game shows itself, compiles the scripts against them and checks every cross-reference; `problemText()`, the English template of every load problem key |
| `include/game/modes.h` | `Mode` and `ModeMachine`: the transition table of `docs/spec/game-flow.md` |
| `include/game/encounters.h` | Visible enemies (spawn, detection, chase, leash, contact openings) and the random-battle counter |
| `include/game/save.h` | The chunked save format, migrations, the clear-data record, atomic writes and `SaveStore` (manual and rotating autosave slots) |
| `include/game/profile.h` | `Profile`: the global settings file (SAV-005) |
| `include/game/pacing.h` | `PacingRecorder`: the encounter pacing metrics per area and per boss |
| `include/game/session.h` | `Session`: the game itself, including the `events::Host` implementation, the presentation action queue, battle driving, path walking and auto play |
| `include/game/runner.h` | The terminal command language shared by `game_cli` and the pacing bot |
| `src/` | Library sources, built into `build/libgame.a` |
| `cli/game_cli.cpp` | Terminal front end, scripted runs and the pacing bot |
| `tests/` | One test executable with a minimal assert helper (`tests/test.h`); `tests/expected/playthrough.txt` is the golden transcript |
| `data/` | The placeholder adventure: `game.json`, `journal.json`, `areas/`, `events/`, per language `text/<code>.strings` (dialogue, speaker names, journal text) and `text/<code>.json` (the game's own text) for `en` and the placeholder `ja`, `text/glossary.txt`, `text/glyphs_<code>.txt` (placeholder font glyph lists for `tools/text glyphs`), `scripts/playthrough.txt`, `routes/main.route` |

Battle data (characters, enemies, encounters, items, equipment, vehicles) and core's English text table are read from `../core/data` through `core_data` in `game.json`, or from the directory `DataDirectories::core` (`game_cli --core DIR`) names; the adventure adds no battle content of its own.

## Build and test

```
make          # builds ../core and ../events libraries if needed, then build/libgame.a (with -fPIC), build/game_cli, build/game_tests
make test     # unit tests, event_cli check --strict on the scripts with the registry, the game's string IDs and both languages, the scripted
              # playthrough compared byte for byte with tests/expected/playthrough.txt, then the bot
make clean
```

Run tests from `game/`, because they load `data/`. One test by name: `./build/game_tests saveRoundTrips`.

```
./build/game_cli                                    # interactive; type help
./build/game_cli --data data --core ../core/data    # the data directories given explicitly
./build/game_cli --seed 7 --auto-battle --script data/scripts/playthrough.txt --saves build/cli_saves
./build/game_cli --bot --seed 7                     # pacing tables for visible and random encounters
./build/game_cli --bot --seed 7 --encounters random --route data/routes/main.route
./build/game_cli --registry                         # the registry built from the data, in the events registry format
./build/game_cli --used                             # the string IDs the game shows itself, for event_cli --used
```

A scripted run writes autosaves, the profile and the clear-data record, so start it with an empty `--saves` directory to get the same transcript (`make test` does). Exit codes: 0 done, 1 data error, 2 bad arguments, 3 a scripted command failed or the bot did not finish.

Commands (one per line; `#` starts a comment):

| Mode | Commands |
| --- | --- |
| Title | `new`, `continue`, `load SLOT`, `retry` (after a game over that has a pre-battle autosave), `quit` |
| Field | `n`/`e`/`s`/`w` `[COUNT]`, `walk MARKER`, `walk X Y` (shortest path; bumping into an enemy on the way starts its battle), `face DIR`, `talk`, `chat` (party talk), `menu`, `look`, `status`, `wait MS` |
| Event | `choose N`, `skip`. Lines are printed and confirmed automatically; a yes/no question prints the dialogue table's `choice.yes` and `choice.no` |
| Battle | `attack M [eN]`, `use M ABILITY [eN\|pN]`, `item M ITEM [eN\|pN]`, `defend M`, `escape M`, `macro NAME`, `round`, `auto` (auto-battle with the auto macro, then core's policy), `cancel` (members by index or character id) |
| Menu | `item ITEM [MEMBER]`, `tech USER ABILITY [MEMBER]`, `equip MEMBER PIECE`, `unequip MEMBER SLOT`, `save manual_N` (field menu or save prompt), `saves`, `close`; name entry: `name TEXT` |
| Shop, inn | `buy ID [N]`, `sell ID [N]`, `leave`; `rest`, `close` |
| Any | `set difficulty\|reward\|auto VALUE` (per save), `set text\|battle\|shipped VALUE` or `set PROFILE_KEY VALUE` (profile; `set text_language ja` switches the language), `quests`, `codex`, `log`, `pacing`, `title` |

## How an engine drives a frame

The engine owns one `GameData` (loaded once) and one `Session`. Every frame:

1. Call `session.update(dtMs, input)`. In Field, `input` is the held direction; the field runs in fixed ticks (`tick_ms`), moving the leader one cell per `step_ms` of held input, ticking visible enemies, and starting battles and events. A direction pressed and released between two ticks is kept until the next tick, which plays it as held once, so a tap turns the leader. In Event it steps the interpreter. Battle and Title ignore it.
2. Read `session.mode()` and present that mode:
   * Field: `world()` (position, facing, `enemies`) and `area()`. Interact with `interact()`, turn in place with `face()`, open the menu with `openMenu()`, party talk with `partyTalk()`. To walk to a cell along the shortest path, `beginWalk(goal)` and then `walkStep(walk)` until it returns anything but `Walking`, running events and battles in between; it stops at the goal, on leaving the area, when the field does not come back, without a path or after 4000 steps.
   * Event: `line()` is the pending line as data (`LineView`: speaker id, speaker name, text ID, rendered text with name tokens resolved for the profile, portrait), or empty; `lineText()` is the same as "Speaker: text". Call `confirmLine()` on the button. `choiceView()` is the pending choice (`ChoiceView`: rendered prompt and options; an ask's options are the dialogue table's `choice.yes` and `choice.no`); call `choose(index)`. `canSkip()` and `skip()` drive skipping; skipped lines go to `textLog()`.
   * Battle: `battle()` is the live `rules::Battle`. Set commands with `setCommand` (its refusal is core's message) or `applyMacro`. For auto-battle call `startAuto()` once, then `fillFromAuto()` at each command phase until it returns false (the stop reason is in the output). `fillFromPolicy()` is core's simple policy, and `fillAutoRound()` fills a round the way the terminal plays (the save's auto macro from the first round, then the policy). Then `resolveRound()`, which returns the round's core events for playback. `playBattle()` plays a whole battle with `fillAutoRound` and `resolveRound` and returns each round's output and events. The battle ends inside `resolveRound()`: rewards, level-ups, grace period, and the return to Field, Event or (game over) Title happen there. `endedBattle()` then keeps the battle as it ended (final combatants, outcome, record, the rewards paid out, drops and level-ups) until the next battle starts, `dismissBattle()`, a new game, a load or `quitToTitle()`.
   * Menu and Shop: `menuKind()` says field menu, inn, save prompt or name entry. `shop()`, `inn()`, `buy`, `sell`, `rest`, `useItem`, `useAbility`, `equip`, `unequip`, `saveManual`, `enterName`, `closeMenu`. A shop, save prompt or name entry an event opened closes back to the event.
   * Title: `newGame`, `continueGame`, `load`, `retryBattle`.
   * Any time: `questLog()`, `encyclopedia()`, `textLog()`, `profile()` and `setProfile()`, `setSettings()` and `setAutoMacro()`.
3. Drain `drainOutput()`. Each `Output` has a text key with parameters (`message`) and its rendering in the profile's language (`text`); warnings about broken data have no key.

Every operation that can refuse (`newGame`, `walk`, `interact`, `buy`, `saveManual`, `setProfile` and the rest) returns a `rules::Message`: empty when it ran, otherwise a text key (`game.refuse.*`, or core's key for a battle command, field use or equipment refusal) and its parameters. `render(message)` shows it in the profile's language.

Presentation-only script actions (moves, cameras, panels, audio, fades) go to the session's own action queue, which `SessionOptions::presentation` sets up. With `Presentation::Instant` (the default) every action is done when it starts, which the terminal and the tests need. With `Presentation::Queued` the event waits on each action until the engine calls `finishAction(handle)`, so a renderer can play animations of real length. Either way `pollActions()` hands out each action once (`PresentedAction`: handle, `Begin` or `Cancel`, the action and its script text; Instant keeps the last 256), and `SessionOptions::onAction` is also called as each is queued. An action with `instant` set (the rebuild after a skip or a restore) is done at once: put its end state up. `Cancel` names an action a skip abandoned. An engine with its own scheduling can pass its own `Presenter` instead. Warps change the world before the queue sees them. The terminal front end is only `Runner` plus `game_cli.cpp`; an engine replaces both and keeps everything else.

## Data formats

### game.json

`title`, `core_data` (battle data directory, relative to `data/`; `DataDirectories::core` overrides it), `areas`, `events`, `glossary`, `languages` (see [Languages](#languages)), `journal`, `actors` (script actors that are not characters; each needs a `speaker.<id>` string), `presentation` (`audio`, `cameras`, `layouts`, `animations`, `portraits` the scripts may name), `flags` (every flag a script, hook or journal entry uses; chest flags are declared by their chests), `vars` (name to starting value), `clear_flag`, `start` (`area`, `marker`, `facing`, `lineup`, `level`, `money`, `items`), `join_levels` (level a character has when an event first adds it), `prices` (buy price per item or equipment id; sell price is the shop's `sell_percent` of it), `item_cap`, `money_cap`, `tick_ms`, `grace_ms`, `round_ms`, `autosave_slots`, `manual_slots`, `bosses` (`encounter` and the `flag` its event sets; pacing keys M5 and M6 by it). Unknown fields are errors.

### journal.json

```json
{
	"objectives": [{"id": "reach_cove", "text": "objective.reach_cove", "after": "tidewater.met_guide", "until": "cove.warden_beaten"}],
	"jobs": [{"id": "lost_cache", "title": "job.lost_cache", "available": "tidewater.arrived", "accepted": "job.lost_cache.accepted", "completed": "job.lost_cache.done"}],
	"encyclopedia": [{"id": "bell_warden", "title": "codex.bell_warden.title", "text": "codex.bell_warden.text", "flag": "cove.warden_seen"}],
	"party_talk": [{"id": "reeds", "area": "reed_path", "after": "tidewater.met_guide", "lines": [{"speaker": "mystic", "text": "talk.reeds.1"}]}]
}
```

* Texts are string IDs of the dialogue tables, so writers translate them with the scripts and the glossary check covers them. A flag range holds while `after` is set (or left out) and `until` is not set (or left out).
* Objectives (UI-012): the last listed one whose range holds is the quest log's line.
* Jobs: available once `available` is set (or from the start), accepted when `accepted` is set, completed when `completed` is set. Scripts set the flags; the state never moves back.
* Encyclopedia entries (TXT-041) unlock when their flag is first set and stay unlocked.
* Party talk sets (FLD-072): a set with an `area` is for that area, one without is a chapter set. Among the sets whose area and range hold, an area set beats a chapter set, then the set with more range bounds wins, then the first listed. Speakers are characters.

### Languages

```json
"languages": [
	{"code": "en", "strings": "text/en.strings", "text": "text/en.json"},
	{"code": "ja", "strings": "text/ja.strings", "text": "text/ja.json"}
]
```

* Each language has a dialogue string table (events format; its `language` line must match `code`) and a text table in core's `{"text": {...}}` format. Core's `text_en.json` is merged into every language's text table, since core ships English only; a game key that core also has is an error.
* The first language is the source: every other text table must have exactly its keys, and the string check (errors for missing script and game IDs, warnings, which fail the strict check, for anything else one table lacks) runs over every dialogue table.
* The profile's `text_language` picks the tables. `setProfile` refuses a language that is not listed; an unknown language in a profile file falls back to the first.
* Text keys the game itself uses: `area.<id>` for each area's display name, `game.*` for notices, `game.refuse.*` for refusals. String IDs the game shows itself, outside scripts: `choice.yes` and `choice.no` (the answers of every yes/no question), `speaker.<actor>` and every journal text. `GameData::gameStrings` lists them; the loader passes it to the string check so they are not reported unused, and `game_cli --used` prints it for `event_cli --used`.

### Areas

```json
{
	"id": "reed_path", "kind": "field", "encounter_mode": "visible", "step_ms": 250,
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

* The display name is the text key `area.<id>`, in every language.
* Map rows: `.` floor, `#` wall (blocks sight), `~` water. Cells are `[x, y]` from the top left.
* `kind`: `town` (no random battles), `field` or `dungeon` (M6 runs from entry to the boss's end).
* `encounter_mode` (ENC-002): `visible` or `random`; `SessionOptions::encounterMode` overrides it for every area, which is how the bot compares the two.
* Hook `trigger`: `interact` (the cell the leader faces; the cell is solid), `step` (entering the cell), `load` (area entry, once per entry) or `flag` (whenever control is in the field and the flags hold; needs `block_flag`). `require_flag` and `block_flag` gate every hook.
* Shops, rest points, chests and interact hooks stand on solid cells, interacted with from the next cell while facing them. A chest's flag is set when opened and never cleared.
* Visible zone values are the ENC-012/013 defaults when left out. Random thresholds are drawn uniformly between `min_steps` and `max_steps` (the original's distribution is ENC-005).

### Event scripts and text

The `events/` format (`../events/FORMAT.md`). The loader builds an `events::Registry` from the data (actors, characters, the markers of each area with their area, the area each hook's event starts in, presentation names, items and equipment, encounters, enemies, vehicles, areas, shops, rest points, flags and variables) and compiles against it, so an unknown name, an undeclared flag, a warp to a marker in another area, or a move to a marker outside the area the party is in is a load error. It then applies the strict check: compiler warnings are errors, every player-facing text is a string ID in every language's table, the tables match the scripts and each other, and glossary names are written as name tokens. A `battle ... until defeat <enemy>` must name an enemy of that encounter. A `battle <encounter> aboard <vehicle>` is a vehicle battle (core's `flags.vehicleBattle`): the vehicle fights and the active party rides aboard. Load problems from every part of the data (core's battle data, the scripts and the game's own files) are in `Result::problems` with a text key and parameters; `problemText()` has the English template of every key, and `error` lists the same problems as English lines. `game_cli --registry` and `game_cli --used` print the registry and the game's own string IDs, so `event_cli check --strict --registry --used` can run on the same scripts; `make test` does.

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
* Event battles: escape needs both the script's `flee` section and the encounter's escape flag. The opening comes from the script. The scripted end comes from the script (`until survive N`, `until defeat ENEMY`, using the enemy's first formation position); it pays the fallen enemies' rewards and returns to the `win` branch.
* Battle drops (core's `rewards().items`) go to the inventory after experience and money, up to `item_cap`, each with a `game.dropped` notice. The reward multiplier does not scale them (SAV-023).
* Autosaves (FLOW-050): area entry when the area was not visited since the last autosave, event end when the script changed any flag, before every event battle (written from inside `start_battle`, with interpreter state), after rest (inn or script). The pre-battle autosave holds the encounter stream from before the battle seed was drawn, so loading it sets up the same battle; setting that battle up again from its own autosave writes no second one.
* Rest (FLD-034/035): `rules::rest` for the whole roster, active or not, which cures the statuses with `rest_cures`. A script's `rest` with no rest point is free; with one it charges the point's cost, and without the money the party does not rest and the script carries on.
* Statuses that persist outside battle stay on the members between battles, show in `status`, are saved, and are cured by rest or by field items and techniques.
* Experience goes to the active party only (SAV-032). A member who leaves stays in the roster with its equipment and comes back as it left (SAV-035).
* Inventory caps (SAV-036): `item_cap` per id, `money_cap`. Worn pieces cannot be sold.
* Field items and techniques go through core's `useItemOutsideBattle` and `useAbilityOutsideBattle`, on the active party. An item that changes nothing is not used up.
* Manual saves (SAV-003/004): from the field menu, anywhere, never during an event. A script's `save prompt` writes an ordinary save without interpreter state and with player control on, so loading it returns to a playable field.
* Name entry: 1 to 16 characters, no control characters, spaces at the ends dropped. Any character can be named, joined or not. The name replaces the character's name in lines, speakers, notices and battle combatants (core's `PartyMember::name` on the battle's copy of the party; the name itself stays in `World::memberNames`).
* Play time (SAV-037) counts field, event and menu time, plus `round_ms` per battle round, because headless battles have no clock. Pacing battle time (M3) uses the same number.
* Pacing: M1 is battles per area visit (no exit landmarks yet), M2 the median field time and steps between battles, M5 the active party's average level when a boss battle is set up, M6 dungeon entry to boss victory.
* Settings: difficulty, reward multiplier and the auto-battle macro are per save; everything else in UI-013 is the profile. UI-013 lists the auto-battle macro as a setting, but it names one of the save's macros and SAV-001 puts it in the save.
* Quests and unlocks: job states and encyclopedia unlocks are saved, because they never move back even if their flags are cleared. The objective line is not saved; it follows the flags.
* Party talk: lines of characters not in the active party are left out; a set with no line left says nothing. Party talk is not added to the text log.
* Text log (TXT-027 to TXT-029): event lines shown and lines a skip passed over (marked), the last 500 since the game was loaded or started, rendered with the current names setting, not saved.
* The clear-data record is written when control returns after `clear_flag` is first set, and replaces any earlier one.
* Speaker names are string IDs (`speaker.<id>`) in the dialogue tables, or the character's chosen or data name. Character data names come from core's data, which has one language.
* A yes/no question's answers are the dialogue table strings `choice.yes` and `choice.no`, in every language, so the loader requires them in every table.
* The ended battle stays readable until the next battle starts, `dismissBattle()`, a new game, a load (Continue and Retry included) or `quitToTitle()`: those replace the world it was fought in. Its rewards are what was paid out, so they are empty after a defeat or an escape.
* Field input: the last direction pressed since the previous tick plays as held for one tick, which turns the leader and starts no step on its own (a step needs `step_ms` of held input). Leaving the field clears it. Ticks stay fixed, so a run is still determined by the updates it gets.
* `face()` is refused outside the field and while a script holds player control, like walking.
* Path walking (`walkStep`) uses walkable cells only, never steps on an exit other than the goal, and ignores visible enemies, so bumping into one starts its battle; after a refused step (an enemy in the way during grace) it lets one tick pass. It gives up after 4000 steps. `playBattle` stops after 60 rounds by default.
* Presentation: Instant keeps the last 256 actions for `pollActions()`, so a host that never polls does not grow the queue without bound; Queued keeps them all, since the host must finish each.
* Vehicle battles come only from scripts (`aboard`); field vehicles and vehicle encounter tables are not built.

## Changes in this round

* Front ends stop touching the world: `face()` and `setAutoMacro()` replace writing `World::facing` and `World::autoMacro`, and the terminal front end uses them.
* Structured event text: `line()` gives the pending line as data and `choiceView()` the pending choice, with an ask's answers from the dialogue tables (`choice.yes`, `choice.no`, in `en` and `ja`).
* The ended battle is kept (`endedBattle()`, `dismissBattle()`), with the final combatants, outcome, rewards paid out, drops and level-ups.
* An action queue in the session with Instant and Queued presentation, so a renderer can play animations of real length without writing a `Presenter`.
* `loadGameData(DataDirectories)` takes the game and core data directories; load problems carry text keys and parameters, from core, the events library and the game.
* Field input keeps a direction pressed between two ticks until the next tick.
* Party combatants carry the names chosen at name entry.
* The terminal's path walk and auto-battle loop moved into the session (`beginWalk`, `walkStep`, `fillAutoRound`, `playBattle`), with no output stream.
* Vehicle battles from scripts (`battle <encounter> aboard <vehicle>`).
* The library builds with `-fPIC`.
* The placeholder adventure has a fight in the ferry hold (`tidewater_hold`, no `lose` section, so the fighter alone loses it, and Retry sets it up again), a name entry for the fighter in the guide's scene, portraits on the guide's, the scout's and the mystic's first lines, a battle aboard the reed skimmer (`reed_path_skimmer`), and a save prompt after the campfire rest. The scripted playthrough and the bot route walk them; `tests/expected/playthrough.txt` is re-recorded.

### Changes a host must make

* `Session`: new `line()`, `choiceView()`, `face()`, `beginWalk()`, `walkStep()`, `fillAutoRound()`, `playBattle()`, `endedBattle()`, `dismissBattle()`, `setAutoMacro()`, `pollActions()`, `finishAction()`. Replace any write to `world().facing` with `face()` and to `world().autoMacro` with `setAutoMacro()`; replace a hand-made walk or auto loop with the session's. Read a line from `line()` instead of splitting `lineText()`, and an ask's answers from `choiceView()`.
* `session.h`: new `LineView`, `ChoiceView`, `Presentation`, `ActionPhase`, `PresentedAction`, `LevelUpView`, `EndedBattle`, `AutoRound`, `AutoBattle`, `WalkStatus`, `PathWalk`, and `toString` for `OutputKind`, `MenuKind` and `BattleSource`. `SessionOptions` adds `presentation` and `onAction`; `presenter` now replaces the session's own queue, and without one actions are recorded for `pollActions()`.
* `FieldInput`: a held direction now also counts at the next tick if it was released before one passed. A host that sent a direction for one frame expecting nothing to happen will see the leader turn.
* `data.h`: new `DataDirectories { game, core }`, `loadGameData(const DataDirectories&)`, `problemText()`, `kChoiceYes` and `kChoiceNo`. On failure `Result::problems` lists every problem; `error` is the same in English (script diagnostics keep their `file:line: error: text` form).
* `area.h`: new `toString(AreaKind)`. `pacing.h`: new `PacingRecorder::areaOrder()`.
* Data: every dialogue table needs `choice.yes` and `choice.no`; `presentation.portraits` lists the portraits scripts use; scripts may write `aboard <vehicle>`. Event type and reward multiplier names from core changed (see `../core/README.md`): saves now write the reward multiplier as `x1`, `x1_5` or `x2` and still read `1`, `1.5` and `2`; `set reward` takes either. Saves stay schema 2.
* `runner.h`: `Runner::walkTo` returns nothing; `RunnerOptions::autoBattle` plays battles with `Session::playBattle`.

## Changes in the previous round

* Builds against core rules version 4 (item drops, vehicles in data) and the events marker maps, scripted ends and game strings.
* Battle drops are added to the inventory and shown. Scripted ends come from the script's `until`; `game.json` no longer takes `scripted_ends`. The registry knows each marker's area and each hook event's starting area, so the game's own warp check is gone.
* Several languages: `game.json` takes `languages` instead of `strings` and `text`; a placeholder Japanese pair (`text/ja.strings`, `text/ja.json`) is added; the profile's text language picks the tables.
* Speaker names and journal text moved from the text table to the dialogue tables. Area display names moved from the area files to `area.<id>` text keys (areas no longer take `name`). `Session` refusals are text keys.

### Changes a host must make

* `Session`: `setProfile`, `newGame`, `continueGame`, `load`, `retryBattle`, `walk`, `interact`, `partyTalk`, `choose`, `setCommand`, `applyMacro`, `startAuto`, `openMenu`, `closeMenu`, `buy`, `sell`, `rest`, `useItem`, `useAbility`, `equip`, `unequip`, `saveManual`, `enterName` and `setSettings` return `rules::Message` instead of `std::string`; `.empty()` still means it ran. Show a refusal with `render()`. New `language()` (the profile's tables) and `stringText(id)`.
* `data.h`: `GameConfig::stringsFile` and `textFile` are replaced by `languages` (`LanguageFiles { code, strings, text }`); `GameConfig::scriptedEnds` is removed. `GameData::text` and `strings` are replaced by `languages` (`LanguageTables { code, text, strings }`) and `language(code)`. New `GameData::gameStrings` and `gameStringsText()`. `Journal` texts are string IDs.
* `area.h`: `Area::name` is removed; the name is the text key `area.<id>`.
* `runner.h`: `Runner::report` takes a `rules::Message` too.
* Data: `game.json` `languages`; no `scripted_ends`; areas without `name`; `speaker.*`, `objective.*`, `job.*`, `codex.*` and `talk.*` move to `text/<code>.strings`; new keys `area.*`, `game.dropped`, `game.refuse.*`.
* Saves are unchanged (schema 2); the content hash changed with the data, which only the header records.

## Changes in earlier rounds

* Builds against core rules version 3 and events `events-save 3`: core's event types, `CancelReason`, `Message` returns, `equip`/`unequip` with the bag, the new `ComboLog`; the new Host methods.
* Dropped the game's own workarounds: the equipment bag bookkeeping (core moves pieces), field item formulas (core's field use, now also techniques), the macro and combo log save lines (core's forms), the encounter stream type (`rules::Rng`), English battle text (core's keys and table), the hand-written auto policy loop (core's auto-battle and stop rules), and the post-compile walk over scripts (the registry, except warps).
* New Host methods: `open_shop`, `rest_party`, `open_save_prompt`, `open_name_entry`, `log_skipped_line`; the battle opening from scripts; the pre-battle autosave written inside `start_battle`; instant presentation after a restore and a skip.
* New: quest log, party talk, encyclopedia, global profile file, clear-data record, save schema 2 with the 1 to 2 migration, string tables, the game's own text table, the registry.
* The placeholder adventure has a dockhand's job, a peddler's shop opened from a script, a campfire rest from a script, two encyclopedia entries, party talk in the reeds and in the chapter, and its dialogue in `text/en.strings`. Poison from the reed battles is carried out of battle and cured at the campfire, and the scripted run turns auto-battle on with the `tempest` macro until it stops.
* Save schema 2 (`PSR-SAVE 2`); `parseSave` no longer takes settings. Game build `game-0.2`.

## Not built

* Vehicles in the field and vehicle encounter tables (vehicle battles come only from scripts); camera volumes (the location chunk writes them empty).
* Ambient party talk (FLD-073); the profile has its switch, nothing reads it.
* Battle text and names from core in other languages: core ships one English text table and English names in its data, so battle messages and character, item and enemy names show in English whatever the language. The terminal front end's own prompts and errors (`Runner`, `game_cli`) are English; an engine replaces them.
* Text speed, auto-advance, text size, battle speed, reduced flash and volumes are stored in the profile, but nothing reads them headless.
* The pause overlay; macro editor and combo log screens (the combo log's play times and participants are kept).
* Analog movement and capsule collision: the field is a grid.
