# Handover

State of the repo on 2026-11-20, for whoever picks it up next.

## What this is

Planning documents, engine-neutral C++17 libraries and tooling for a 3D remake of Phantasy Star IV. The libraries hold the game rules and run headless. The engine team renders on top of them. All data in the repo is a small invented placeholder adventure; the real content is not in it.

## Layout

| Path | What it holds |
| --- | --- |
| `docs/scope.md` | What the remake is, phases, open questions |
| `docs/spec/` | 300 numbered requirements per system (`BAT-`, `ENC-`, `FLD-`, `SAV-`, `EVT-`, `TXT-`, `UI-`, `FLOW-`) |
| `docs/info-needed.md` | The 42 facts about the original still to confirm, and the data tables to deliver |
| `docs/research/published-mechanics.md` | What is known about each of those facts, with sources and confidence |
| `core/` | Battle rules library (`librules.a`), placeholder data, `battle_cli`, 162 tests |
| `events/` | Event script format, compiler and resumable interpreter, `event_cli`, 92 tests |
| `game/` | Session layer: areas, encounters, shops, saves, joins events to battles. `game_cli`, 70 tests |
| `tools/text/` | Line database, glossary, rename review and localization export (Python, 98 tests) |
| `acceptance/` | The spec's automatable checks, run against the libraries. `report.md` maps every requirement to a status |
| `bindings/` | C API over the session, C# wrapper, MonoGame sample |
| `tools/linux-check.sh`, `tools/windows-check.sh` | Platform checks in Docker |

Each library has a README that documents its data formats, API and rules. Start with `core/README.md`.

## Build and test

Needs a C++17 compiler, GNU Make and Python 3. The .NET SDK is optional and expected at `~/.dotnet`.

```bash
make test          # core, events, game, text tooling
make acceptance    # spec checks and report
make bindings      # C tests, C# tests, MonoGame sample headless run
make linux-check   # all three in Docker with gcc and clang
tools/windows-check.sh   # unit tests cross-compiled for Windows, run under Wine
```

Other compilers: `make CXX=g++ CC=gcc test`. Flags are `-std=c++17 -Wall -Wextra -Werror`, standard library only.

Try it by hand:

```bash
cd core && ./build/battle_cli --seed 1 --auto
cd game && ./build/game_cli --seed 6 --auto-battle --script data/scripts/playthrough.txt --saves build/cli_saves
cd game && ./build/game_cli --bot --seed 2
```

## Current results

| Suite | Result |
| --- | --- |
| core | 162 tests pass |
| events | 92 tests pass |
| game | 70 tests pass, scripted playthrough matches its expected transcript, bot finishes both encounter modes |
| text tooling | 98 tests pass |
| acceptance | PASS 24, FAIL 0, NOT IMPLEMENTED 14, NOT AUTOMATABLE 26 |
| C API | 646 checks pass |
| C# | 13 tests pass |
| MonoGame sample | headless run, 2000 frames, 0 failures |

Platforms where all of the above pass: macOS arm64 (Apple clang 21), Linux aarch64 and Linux x86-64 (GCC 13.3 and clang 18.1, Ubuntu 24.04 in Docker). Windows: the core, events and game unit tests pass when cross-compiled with MinGW-w64 (GCC 14) and run under Wine 10 (`tools/windows-check.sh`). That check does not cover MSVC, the acceptance suites or the bindings.

## Versions

* Rules version 8 (`core/include/rules/version.h`). Bump it whenever a rule, a formula or the order of random draws changes. `replayBattle` refuses records from another rules version.
* Battle record format 5. `parseRecord` still reads 3 and 4.
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

## Stand-ins inside those rules

The formulas are right but some inputs are not known yet:

* Techniques and status rolls use mental against mental. The original names the stat pair per ability.
* A resistance percent stands in for the element factor (100 percent is the original's factor 4).
* `awareness` and `escape_rating` on encounters default to the highest enemy agility.
* The `critical` and `evasion` stats are additions to the hit roll that the original does not have. At 0 the roll is the original's.
* Level curve, stat growth, status tick damage, revive amounts, enemy AI weights and combination rules are placeholders.

## Seeds that tests depend on

The seeded tests are tuned to the placeholder balance. Any rule or data change shifts them.

| Use | Seed | Where |
| --- | --- | --- |
| Scripted playthrough | 6 | `game/Makefile`, `game/tests/test_playthrough.cpp`, `acceptance/game/support.cpp`, `bindings/Makefile`, C# `SessionTests` |
| Binding session tests | 96 | `bindings/c/tests/test_api.c`, C# `SessionTests.NewSession` |
| Bot run | 2 | `game/Makefile`, `game/tests/test_playthrough.cpp` |
| Hard-difficulty record golden | 11 | `core/Makefile`, `core/tests/test_battle.cpp` |

The scripted playthrough needs the first battle lost twice and then won. After a change, scan seeds until the script's expectations hold, update every place above, and regenerate `game/tests/expected/playthrough.txt` and `acceptance/report.md` with their Makefile targets.

## Open work

**Content.** The libraries are built to take the real game as data: abilities, enemies, encounters, statuses, items, equipment, areas, events and text. None of it is loaded yet. The scope counts 128 event hooks, 211 chests and 50 shops against the placeholder's 10, 3 and 2. `docs/info-needed.md` lists every table to deliver and its format.

**Facts still to confirm.** Of the 42 "to measure" items: 10 are known with high confidence, 9 medium, 10 low, 12 unknown, 1 split. The tally and the per-item notes are in `docs/info-needed.md` and `docs/research/published-mechanics.md`.

**Acceptance checks.**
* 14 are NOT IMPLEMENTED because their baseline fact is still marked "to measure". Four of them (ACC-BAT-05 turn order, ACC-BAT-06 formulas, ACC-BAT-10 macros, ACC-ENC-04 escape) already match the disassembly-read rule; flipping them to PASS is a decision about whether the disassembly counts as the baseline.
* 26 are NOT AUTOMATABLE here: they need the renderer, captures or playtests.

**Rules not yet read from the disassembly.** Enemy action selection, technique and skill targeting, how macros retarget heals and cures, experience and level-up, vehicle battles, combination triggers.

**Measurements.** Per-action presentation times (BAT-102), encounter pacing baselines (ENC-036), walk speeds and step distance (FLD-022, ENC-041).

**Engine.** Nothing is integrated with Unreal yet. The contract is the event stream (`core/README.md`, "Events") and the C API in `bindings/c`. The MonoGame sample shows the full loop with rectangles and text.

**Platforms.** MSVC has not been tried, and the acceptance suites and bindings have not run on Windows. The MonoGame sample's window has only run on macOS.

## Working conventions

* Tabs, American English, no dependencies beyond the standard library.
* Rules live in data wherever the original's value is unknown, so an answer is a data change.
* Every spec requirement has an ID. Code comments and tests refer to IDs, and `make acceptance` checks that the spec, the checks and the report stay consistent.
* Each rule change updates, in the same commit: the spec row, `core/README.md`, the research row if a fact changed, the rules version, the tests and the regenerated transcript and report.
