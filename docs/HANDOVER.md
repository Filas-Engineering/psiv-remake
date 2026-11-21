# Handover

State of the repo on 2026-11-21, for whoever picks it up next.

## What this is

Planning documents, engine-neutral C++17 libraries and tooling for a 3D remake of Phantasy Star IV. The libraries hold the game rules and run headless. The engine team renders on top of them. All data in the repo is a small invented placeholder adventure; the real content is not in it.

## Layout

| Path | What it holds |
| --- | --- |
| `docs/scope.md` | What the remake is, phases, open questions |
| `docs/spec/` | 300 numbered requirements per system (`BAT-`, `ENC-`, `FLD-`, `SAV-`, `EVT-`, `TXT-`, `UI-`, `FLOW-`) |
| `docs/info-needed.md` | The 40 facts about the original still to confirm, and the data tables to deliver |
| `docs/research/published-mechanics.md` | What is known about each of those facts, with sources and confidence |
| `docs/data-import.md` | How a team brings its own data into these formats: what each file holds, the order, the checks, common mistakes |
| `docs/dev-tools.md` | The MonoGame sample's developer tools (`--dev`): console commands, data hot reload and its limits, overlays, the panel and scene previewer, frame step and speed, the dev mark that marks games they touch, and the session API behind them |
| `docs/assets.md` | The MonoGame sample's asset directory: the manifest that puts pictures and sounds in place of the generated ones, the battle presentation table, the export, the check and hot reload |
| `docs/research/original-battle-rules.md` | The original's battle rules as read from the disassembly: AI, targeting, combinations, rewards, statuses, end conditions, vehicles |
| `core/` | Battle rules library (`librules.a`), placeholder data, `battle_cli`, 183 tests |
| `events/` | Event script format, compiler and resumable interpreter, `event_cli`, 96 tests |
| `game/` | Session layer: areas, encounters, shops, saves, joins events to battles, vehicles in the field, travel (ports between regions, returns to a town, the way out of a dungeon), the field menu's party order, macro editor (and make macro from the combo log), member detail, equipment comparison, bag sorting, descriptions, difficulty presets, ambient party talk, developer commands and the game as save text. `game_cli` (play, bot, `--check` for data), 112 tests |
| `tools/text/` | Line database, glossary, rename review, localization export and the descriptions report (Python, 101 tests) |
| `tools/maps/` | Tiled maps to area files and back (Python, 46 tests) |
| `acceptance/` | The spec's automatable checks, run against the libraries. `report.md` maps every requirement to a status |
| `bindings/` | C API over the session, C# wrapper, MonoGame sample |
| `tools/linux-check.sh`, `tools/windows-check.sh` | Platform checks in Docker; `tools/check_png.py` checks the Linux window screenshot |
| `tools/package.sh`, `tools/build-native.sh` | `make package`: the MonoGame sample as self-contained builds per platform under `dist/`, with the native library built per target |

Each library has a README that documents its data formats, API and rules. Start with `core/README.md`.

## Build and test

Needs a C++17 compiler, GNU Make and Python 3. The .NET SDK is optional and expected at `~/.dotnet`.

```bash
make test          # core, events, game, text and map tooling
make acceptance    # spec checks and report
make bindings      # C tests, C# tests, MonoGame sample headless runs
make linux-check   # all three in Docker with gcc and clang, then the sample's window under Xvfb
tools/windows-check.sh   # unit tests, the C library and its tests cross-compiled for Windows, run under Wine
make package       # the sample, self-contained, for macOS (with .app), Linux and Windows under dist/, each checked (bindings/README.md, "Packaging")
```

Other compilers: `make CXX=g++ CC=gcc test`. Flags are `-std=c++17 -Wall -Wextra -Werror`, standard library only.

Try it by hand:

```bash
cd core && ./build/battle_cli --seed 1 --auto
cd game && ./build/game_cli --seed 6 --auto-battle --script data/scripts/playthrough.txt --saves build/cli_saves
cd game && ./build/game_cli --seed 6 --auto-battle --script data/scripts/travel.txt --saves build/travel_saves   # vehicles and travel
cd game && ./build/game_cli --bot --seed 2
cd game && ./build/game_cli --check data     # every data problem with file and line, and what loaded
cd game && make maps                         # area files from the Tiled sources in data/maps
```

Bringing in real data: [`data-import.md`](data-import.md). Pictures and sounds for the sample: [`assets.md`](assets.md). Developer tools in the sample (`--dev`): [`dev-tools.md`](dev-tools.md).

## Current results

| Suite | Result |
| --- | --- |
| core | 183 tests pass |
| events | 96 tests pass |
| game | 112 tests pass, data check passes, area files match their Tiled sources, scripted playthrough and the vehicle and travel script match their expected transcripts, bot finishes both encounter modes on the main route and the kiln route |
| text tooling | 101 tests pass |
| map tooling | 46 tests pass |
| acceptance | PASS 26, FAIL 0, NOT IMPLEMENTED 12, NOT AUTOMATABLE 26 |
| C API | 1121 checks pass; the C playthrough and the C vehicle and travel script match `game_cli`, saves included |
| C# | 24 tests pass |
| MonoGame sample | twenty-four headless runs (with `--dev`: the front battle layout on a copy of the data with a six-enemy encounter 5600, the developer console and every command 1500, the overlays 1500, the previewer 1500, frame step and speed 1500, the data hot reload on a copy of the data 700; without it, the tools refused 400; vehicles and travel 3200, smoke 2000 frames, large areas 1600, scripted actors and the tide chart scene 1300, the board scene with a full party 1100, gamepad input and rebinding 600, every field menu page and option with descriptions, the equipment comparison, the difficulty and item sorting 900, the macro editor end to end with a macro made from the combo log 760, save, quit to title and load 550, options and their effects with the Controls page 1200, gamepad name entry through the character grid 500, the lookout's panels with camera zooms, camera, shake, load and skip 1200, audio from the recording layer's call log 1000, click-to-walk, ambient party talk and the letterbox 540, the battle presentation 5200, assets from files 2000, hot reload 400), 0 failures; the asset round trip (the smoke, actors, panels, battle and vehicles runs with the exported placeholders draw every frame and print every line as the generated ones); `--check-assets` passing on the export and the override directory and reporting the expected twenty problems on the broken copy; the synthesizer's self-check (39 sounds, each generated twice) and the dumped WAV files' check pass. Every screen before the last round was looked at in window screenshots at 100% and 150% text size; the lookout's panels at 100%; this round's equipment comparison, descriptions (Items, shop, battle), sorted items, the difficulty option, zoomed panels and the window at 1600x1000 and 960x540 at 100%; all on macOS only. This round's battle presentation (combination, numbers, statuses, the ambush banner, escape, the log, defeat, victory with level-ups) and the asset override run (the recolored town, the guide's portrait in the skinned dialogue box, the battle on a backdrop from file) were looked at in window screenshots, and the battle screen and a skinned menu at 150% text size, on macOS only. This round's field with the skimmer moored and ridden, the crossed water, the vehicle battle (the vehicle alone on the field), the islet, the ferry's destination screen and the return and dungeon-exit screens were looked at in window screenshots at 100% text size, on macOS only (not at 150%). This round's developer tools (the console, the marked save list, each overlay and all of them over the lookout scene's panels, the previewer's picker, a previewed event paused at a panel, a battle presentation entry on dummies, the paused and 4x speed badges with the frame time, a reload that worked and one whose problems show) were looked at in window screenshots at 100% text size, on macOS only. This round's front battle layout (the command window, target and description, an enemy hitting a member, the sweep along a row and along two ranks, the combination's name plate, a status, the log, a retarget and a miss, a full party of five, a vehicle battle, victory with level-ups, a defeat, one battle in the side layout, and the developer overlays, console and a battle presentation preview over it) was looked at in window screenshots at 100% and, for most of those moments, 150% text size, on macOS only. No one has listened to the generated sound or to sound from files |
| Packages | `make package` builds all five targets on this Mac (each package about 90 MB): the headless smoke run passes from the osx-arm64 and osx-x64 packages and their .app launchers (x86-64 under Rosetta) and from the linux-x64 and linux-arm64 packages in Docker, where their windows also run `scripts/smoke.txt` under Xvfb; the win-x64 package passes its layout and export checks and has not run. The title screen with its version stamp was looked at from the packaged osx-arm64 build |

Platforms where all of the above pass: macOS arm64 (Apple clang 21), Linux aarch64 and Linux x86-64 (GCC 13.3 and clang 18.1, Ubuntu 24.04 in Docker). The Linux check also runs the sample's window on a virtual display (Xvfb, Mesa llvmpipe) through `scripts/smoke.txt` and checks its screenshot. Windows, cross-compiled with MinGW-w64 (GCC 14) and run under Wine 10 (`tools/windows-check.sh`): the core, events and game unit tests, the C API as a DLL exporting exactly the header's 123 `PSIV_API` functions, with its 1049 checks, and the C playthrough against `game_cli.exe` and the golden transcript, saves included. That check does not cover MSVC, the acceptance suites, the C# wrapper or the sample. This round (vehicles and travel) passed on macOS arm64, in `tools/linux-check.sh` on Linux aarch64 (GCC 13.3 and clang 18.1, the full run and the Xvfb window) and in `tools/windows-check.sh`; Linux x86-64 was not run again this round. The packages (`make package`) run on macOS arm64 (and x86-64 under Rosetta) and on Linux aarch64 and x86-64 in Docker; the Windows package is built and checked but not run.

## Versions

* Rules version 10 and event schema 6 (`core/include/rules/version.h`). Bump the rules version whenever a rule, a formula or the order of random draws changes. `replayBattle` refuses records from another rules version or event schema. Version 10 added the `travel` effect, which a battle refuses; no battle of the existing data plays differently. The event schema is the battle event stream's, which did not change. The script format gained the `vehicle` commands and tests without a save or code layout change: `events-save 3` saves still load and scripts that do not use them keep their fingerprint.
* Battle record format 5. `parseRecord` still reads 3 and 4.
* C API `PSIV_ABI_VERSION` 9 (library version 0.8.0): the developer tools' calls, the event position, the area's zones, save text and the session reload, and the `dev` fields of `psiv_world_info` and `psiv_save_summary` (141 `PSIV_API` functions).
* Save schema 6 (`PSR-SAVE 6`, adding the `dev` chunk and the header's `dev` line: the game was changed by developer tools, [`dev-tools.md`](dev-tools.md), "The dev mark"). Schemas 1 to 5 migrate when they load, unmarked. Schema 5 added the `travel` chunk: the party's vehicles, the one it rides and the way out of the dungeon it is in.
* The game's content hash (save headers, the clear-data record) is built from the parsed data without presentation (`game.json`'s `presentation`, area music, descriptions); its value changed once when that started, and nothing compares it on load. Battle records carry core's hash, which did not change.
* Everything is deterministic from the seed: the same setup, data and commands give the same event stream on every platform and optimization level. ACC-BAT-15 checks this.

## Battle rules that match the original

These were read from the public community disassembly (`https://github.com/squidfeatures/ps4disasm`) and are implemented in `core/`. Details and formulas are in `core/README.md` and `docs/research/published-mechanics.md`.

* **Turn order:** battle agility plus a random 0 to just under half the highest agility. 0 agility gets no roll. Ties go to the party, then the lower slot.
* **Macros:** members are taken in pairs in macro order. A member who would act before the one listed ahead of it moves to right after that one. Enemies can still act in between.
* **Chance score:** twice (a roll of 0 to 63, plus the actor's stat, less the target's stat).
* **Hit and critical:** basic attacks only, dexterity against agility. Miss at 8 or below, critical above 116, never critical against a group. Everything else always lands.
* **Damage:** attack times a spread (8 plus sixteen rolls of 0 to 7) over 64, plus attack, plus twice a bonus, scaled by the element factor, less defense, clamped to 1–999. The bonus is the ability's power, plus a quarter of attack on a critical.
* **Healing:** half of (mental times the spread over 64, plus mental, plus twice the power).
* **Defend:** a physical hit does a quarter of its damage before defense is subtracted. No effect on anything else.
* **Status success:** (roll, plus actor stat, less target stat) times the target's factor must be above the ability's threshold.
* **Sleep and paralysis:** a sleeper wakes with an even chance at each round end. Paralysis on an enemy ends with the round; on a party member it lasts until cured.
* **Openings (random battles):** the party's highest agility against a per-encounter number. Enemy first at 12 or below, party first above 116.
* **Escape:** always works when no enemy can act. Otherwise the party's highest agility against a per-encounter number, failing at 40 or below. A number of 240 or more forbids escape.
* **Enemy targets:** when the round's order is resolved each enemy draws a party member who is not defeated, weighted by party order (2:1, 3:2:1 and so on), and draws again at its turn if that one fell.
* **Enemy actions:** at its turn an enemy draws one of eight action slots, never the slot index any enemy drew last, then up to four overrides (hit point bands, last enemy standing, what hit it, blocked techniques) can replace it.
* **Retargeting:** a party action whose foe fell turns to the living enemy missing the most hit points if it deals damage, the fewest otherwise, with a coin flip for each later tie. A one-ally action on a fallen ally does nothing but still costs. In a macro, foes are picked afresh every round, and heals, cures and revives pick their own ally.
* **Combinations:** up to four consecutive queued technique or skill turns, compared with the recipes in data order. Damage adds up the participants' stats and the components' powers, and always lands. Every participant pays.
* **Rewards:** experience, halved in a vehicle battle, is split evenly among the members who are not defeated; money is paid in full; one drop roll per won battle against the encounter's rate. Level-ups raise maximums only.
* **Statuses and stat changes:** poison does nothing in battle. Sleep holds agility at 1 and paralysis agility and dexterity; receiving paralysis removes sleep; an enemy's paralysis that runs out leaves the stats at 1. Paralysis counts toward defeat for the party; an enemy is out only when defeated. Blocked techniques still cost their points, last the battle and survive defeat. A stat change sets the stat to base plus or minus the user's stat, the latest replacing the earlier, until the battle ends; lowering a foe's stat rolls like a status.
* **Vehicle battles:** the vehicle starts at full hit points, attacks, uses skills or escapes, and takes the crew down with it when it falls.

## Stand-ins inside those rules

The formulas are right but some inputs are not known yet:

* Techniques and status rolls use mental against mental. The original names the stat pair per ability.
* A resistance percent stands in for the element factor (100 percent is the original's factor 4).
* `awareness` and `escape_rating` on encounters default to the highest enemy agility.
* The `critical` and `evasion` stats are additions to the hit roll that the original does not have. At 0 the roll is the original's.
* Level curve, stat growth, revive amounts, the enemies' slots and overrides, and the combination recipes are placeholders.
* An enemy that cannot pay for the action it drew (technique points, skill uses), or finds no target for it, makes its basic attack instead; the original's enemies have no point costs.

## Vehicles in the field and travel

Built this round in `game/`, with a `travel` effect in `core/`, the `vehicle` commands and tests in `events/`, the C API, the C# wrapper and the sample. What the spec asks, by requirement:

* FLD-060: vehicles move in the field and fight their own vehicle battles (core's existing BAT-140 rules). FLD-061: terrain each crosses, speed against walking and boarding live in data (`game.json` `vehicles`), so FLD-062's measured rules are a data change. FLD-063: vehicle positions and the vehicle in use are saved (SAV-001's Location and World groups, in the save's `travel` chunk), and a vehicle left out of the party's reach goes back to its last boarding point. FLD-030: a vehicle is an interactable; facing it and interacting boards it. ENC-007: battles aboard are vehicle battles drawn from the vehicle's own encounter table. FLOW-050: an autosave after a vehicle is acquired. FLD-002, FLD-050: areas carry a region (a planet), and a region is connected zones (areas and exits). FLD-051 with FLD-052's published notes (`docs/research/published-mechanics.md`): a return to a visited town of the region (technique and item, outside battle and dungeons), a way out of the dungeon to where the party entered (technique and item, outside battle), and a transport between regions from a destination menu at ports, with destinations the story unlocks by flags.
* Choices made here, which the spec does not make (each listed in `game/README.md`, "Rules this library chose"): terrain is the map's own floor and water; vehicles exist only in field areas; leaving needs a walkable neighbor, the faced cell first; visible enemies neither notice nor chase a vehicle, and battles aboard use the zone's random counter in either encounter mode; exits into towns and dungeons park the vehicle outside, exits into field areas take it along; the way out returns to the cell the party stood on before entering; returns and the way out are menu uses that go through `Session::travel`, not `useItem`; ports cost nothing.
* The placeholder adventure has one field vehicle (the reed skimmer, at the Brine Flats west of Saltgrass Market), a water crossing to an islet with a crate and the beacon that opens the ferry, two regions (`coast` and `isles`: Cinder Quay and Ember Steps), and the travel items and techniques (Homing Shell, Waymark Chalk, Homeward, Outpath; the techniques are learned at levels no seeded run reaches).
* Still open: the real vehicles (three per FLD-060), their terrain, speeds and boarding spots (FLD-062), the original's fast-travel sources and conditions in full (FLD-052), vehicle skill uses carried between battles, and ACC-FLD-06, which stays a playtest check against those measurements. No acceptance check became automatable: ACC-FLD-06 and ACC-FLOW-04 run against measurements and the engine; ACC-SAV-01, ACC-SAV-02 and ACC-SAV-04 now also cover the travel chunk, saves aboard and the schema 4 migration.

## Battle layouts

Built this round in the MonoGame sample only (no C API, core, events or game change). The battle stage has two layouts, chosen by the battle presentation table's `layout` (`front`, the default, or `side`, the old one), overridable per encounter or area (`layouts`), by `--battle-layout` and by the `battlelayout` script verb ([`assets.md`](assets.md), "The battle presentation table"). The front layout: enemies in a row across the middle facing the camera (two ranks from five on), the party seen from behind along the bottom over a status strip of five equal slots, the command window beside the member choosing, the action's name plate above the strip; back figures are a new pose set in the asset manifest (`"view": "back"`, exported with the rest). The timeline is the same in both layouts: every transcript and `present:` line is unchanged, only the headless runs' draw counts and frame digests moved. `scripts/battle_front.txt` covers it on a copy of the data with a six-enemy encounter (`scripts/make_front_data.py`), since the placeholder's encounters have at most three; no data marks flying enemies, so none floats. A scripted window run now passes the game 16 ms frames as headless runs do, so its screenshots show the moments the headless run checks. Open: the developer `win` and `lose` commands end a battle outside the events the playback shows, so the shown numbers differ from the session's until the round's playback ends (`battlesync` reports it; true in both layouts and before this round).

## Developer tools

Built this round in the MonoGame sample, behind `--dev`, on new calls in `game::Session`, the C API and the C# wrapper ([`dev-tools.md`](dev-tools.md)): a console (the backquote key, and the `dev` script verb) that warps, sets flags, gives items and money, sets levels, starts and ends battles, gives vehicles, turns encounters off, runs events and lists ids; a data hot reload (F6) that saves the game to memory, builds a session on the reloaded data and restores it, with its limits (an event resumes at its waiting command, a battle in progress does not survive) and problems shown on screen; overlays for collision, hooks, zones, actor ids, the camera's target, the frame rate and the running event's script line; a previewer that plays any event from a clean state panel by panel, and any battle presentation entry on dummy targets; and pause, single frames and 0.25x to 8x speed. A game a command changes is marked: the mark is saved (schema 6), shown in the sample, keeps the game out of the clear-data record and stops the pacing numbers. Without `--dev` nothing changed: every transcript, golden and seed is the same, and `scripts/dev_off.txt` checks the tools stay off. `acceptance/report.md` changed in three rows, from the save schema and the text: ACC-SAV-02 names the schema 5 migration, ACC-SAV-03's pre-battle autosave is 45 bytes longer, ACC-TXT-05 counts the eight more distinct Japanese characters.

## Seeds that tests depend on

The seeded tests are tuned to the placeholder balance. Any rule or data change shifts them.

| Use | Seed | Where |
| --- | --- | --- |
| Scripted playthrough | 6 | `game/Makefile`, `game/tests/test_playthrough.cpp`, `acceptance/game/support.cpp`, `bindings/Makefile`, C# `SessionTests` |
| Vehicle and travel script | 6 | `game/Makefile`, `game/tests/test_travel.cpp`, `acceptance/game/acc_sav_01.cpp` and `acc_sav_04.cpp` (through `support.cpp`), `bindings/Makefile`, `bindings/c/tests/test_api.c`, C# `PlaythroughTests` and `SessionTests` |
| Binding session tests | 285 | `bindings/c/tests/test_api.c`, C# `SessionTests.NewSession` |
| Bot run (main route and kiln route) | 2 | `game/Makefile`, `game/tests/test_playthrough.cpp` |
| Hard-difficulty record golden | 11 | `core/Makefile`, `core/tests/test_battle.cpp` |

The scripted playthrough needs the first battle lost twice and then won. After a change, scan seeds until the script's expectations hold, update every place above, and regenerate `game/tests/expected/playthrough.txt` and `acceptance/report.md` with their Makefile targets. The golden transcript last changed by four ambient party talk lines (`~ ` lines); no battle in it changed. Vehicles and travel left it byte-identical; they have their own script, `game/data/scripts/travel.txt`, with its golden transcript `game/tests/expected/travel.txt` (re-recorded with `make -C game expected`, which records both). The MonoGame vehicles run uses the sample's default seed 1.

## Open work

**Content.** The libraries are built to take the real game as data: abilities, enemies, encounters, statuses, items, equipment, areas, events and text. None of it is loaded yet. The scope counts 128 event hooks, 211 chests and 50 shops against the placeholder's 19, 9 and 4. `docs/info-needed.md` lists every table to deliver and its format, and [`data-import.md`](data-import.md) how to bring it in: `game_cli --check` validates a data directory and `tools/maps` converts Tiled maps. Three of the placeholder's six areas (Saltgrass Market and the two floors of the Drowned Kiln) are drawn in Tiled as an example.

**Facts still to confirm.** Of the 40 "to measure" items: 8 are known with high confidence, 9 medium, 10 low, 12 unknown, 1 split. The tally and the per-item notes are in `docs/info-needed.md` and `docs/research/published-mechanics.md`.

**Acceptance checks.**
* 12 are NOT IMPLEMENTED because their baseline fact is still marked "to measure". Turn order and macros (ACC-BAT-05, ACC-BAT-10) pass: their baselines were measured from the disassembly. ACC-BAT-06 (formulas), ACC-BAT-09 (enemy AI) and ACC-ENC-04 (escape) check the disassembly-read rule too, but stay open because they also depend on per-ability, per-enemy and per-encounter numbers.
* 26 are NOT AUTOMATABLE here: they need the renderer, captures or playtests.

**Rules read but not built.** The original's AI conditions on particular enemy kinds, spawning beside an enemy and an ambush reaction; vehicle skill uses carried between battles (vehicles now move in the field, but core refills their skills each battle); android recovery and the experience of characters outside the party (see `docs/research/original-battle-rules.md`).

**Measurements.** Per-action presentation times (BAT-102; `game.json`'s `presentation.durations` now carries them for the game and the engine once measured), encounter pacing baselines (ENC-036), walk speeds and step distance (FLD-022, ENC-041).

**Front end gaps left.** Names stay English in every language (core's data holds them inline; descriptions are text keys and translate). The session does not report where each panel looks: a host frames panels from the camera data and the panel actions (`events/README.md`, save contract rule 9). Shops compare equipment through the same call as the Equip page, but the session offers no "best piece" suggestion or batch comparison across members. Ambient party talk is reported as notices; where a front end shows it is its own choice. Ports have no display name of their own (the travel screen shows the destination area's).

**Developer tools.** Not built: completion and a history that outlives the session in the console; hot reload of a battle in progress; a previewer that steps an event command by command (it steps frames and panel shows); overlays in the battle screen; sound in previews. The tools have run on macOS only.

**Engine.** Nothing is integrated with Unreal yet. The contract is the event stream (`core/README.md`, "Events") and the C API in `bindings/c`. The MonoGame sample shows the full loop with generated pictures, sound and text, which an asset directory replaces id by id with files ([`assets.md`](assets.md): tilesets, character sheets with poses, portraits, battle figures, backdrops, effects, sprites, status icons, a UI skin and fonts, music, loops and cues, checked by `--check-assets`, exported from the generated set by `--export-placeholders`, reloaded with F5), plays battles from the typed event stream as staged steps (figures stepping forward, banners, animations, effects, floating numbers, bars, status icons, falls, combinations with every participant, result screens with level-ups) driven by a data table of how each ability and item looks, with fallbacks by kind and element (`bindings/README.md`, "The sample", Battle), script actors included, plays party members' scripted moves and turns and every `anim` as procedural poses, and plays comic panels through named cameras, camera moves, shakes, music, ambient loops and cues (`bindings/README.md`, "The sample" and "Audio"). Cameras can carry a target and a zoom in `game.json`, areas and encounters name their music, and the data carries action durations; the placeholder fills them in and the sample uses them, falling back to its old rules (a camera's marker or person of the same name, derived track ids) where the data is silent. Real content needs them filled in. Where a scene puts party members is presentation by design (the game keeps only the leader's cell); an engine follows those actions itself, as the sample does (`bindings/README.md`, "The sample"), and draws nothing for an action of an actor outside the party's area (in `psiv_actor_ids`, not in `psiv_field_actors`).

**Platforms.** MSVC has not been tried. On Windows the acceptance suites, the C# wrapper and the MonoGame sample have not run (the C library and its tests have, under Wine). The sample's window has run on macOS and under Xvfb with software rendering on Linux, not in a real Linux desktop session.

## Working conventions

* Tabs, American English, no dependencies beyond the standard library.
* Rules live in data wherever the original's value is unknown, so an answer is a data change.
* Every spec requirement has an ID. Code comments and tests refer to IDs, and `make acceptance` checks that the spec, the checks and the report stay consistent.
* Each rule change updates, in the same commit: the spec row, `core/README.md`, the research row if a fact changed, the rules version, the tests and the regenerated transcript and report.
