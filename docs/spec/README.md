# Behavior specification

This folder turns [`docs/scope.md`](../scope.md) into system specs that the Unreal Engine team builds and tests against. The scope is the source of truth. If this spec and the scope disagree, the scope wins and this spec has a bug.

## Files

| File | System | ID prefix |
|---|---|---|
| [game-flow.md](game-flow.md) | Top-level modes, transitions, loading, autosave points, global budgets | `FLOW` |
| [field.md](field.md) | Traversal, field camera, interaction, towns, dungeons, world maps, vehicles, party talk | `FLD` |
| [encounters.md](encounters.md) | Visible enemies, random-battle fallback, pacing measurement, opening advantage, escape | `ENC` |
| [battle.md](battle.md) | Round lifecycle, commands, order, abilities, macros, combinations, statuses, AI, event stream, difficulty, determinism | `BAT` |
| [dialogue-and-text.md](dialogue-and-text.md) | Text presentation, script lock, name glossary, localization, encyclopedia, text log | `TXT` |
| [events-and-live-panels.md](events-and-live-panels.md) | Event scripts, interpreter, live panels, skip, mid-event saves, scene pacing | `EVT` |
| [menus-and-ui.md](menus-and-ui.md) | Menu structure, portraits, macro and combo screens, quest log, settings, accessibility | `UI` |
| [save-and-progression.md](save-and-progression.md) | Save contents, round trip, versioning, leveling, rewards | `SAV` |
| [acceptance.md](acceptance.md) | Testable checks per system and how each is run | `ACC` |

## How to read a requirement

Each requirement is one table row with a stable ID. IDs are never reused or renumbered. A deleted requirement keeps its row, marked "Withdrawn".

Each requirement carries exactly one label:

| Label | Meaning |
|---|---|
| Decided | Traces to a row marked Decided in `scope.md`. Change it only through the project lead |
| Proposed (scope) | Comes from a row or verdict marked Proposed in `scope.md`, or from an unlabeled scope section (goals, pillars, anti-goals, systems, risks). Needs a yes |
| Proposed (spec) | This spec's own proposal, filling a gap the scope leaves. Needs a yes. The ambiguity it fills is noted in the row or the section |
| To measure | A behavior of the original game that matters and that the scope does not state. The row says what to measure, how, and which requirements wait on it |

`scope.md` says "Nothing here is approved". Only rows explicitly marked Decided get the Decided label. Everything else in the scope is Proposed (scope).

The Source column cites a trace key from the table below. `L-CORE` and `L-EVENTS` mean the requirement matches the shape of the `core/` or `events/` library being built in parallel. Those rows are Proposed (spec) unless the scope also states the rule.

A requirement lives in exactly one file. Other files link to it by ID.

Acceptance checks (`ACC-*`) are tests, not requirements. They carry no label of their own and inherit the label of the requirements they verify.

## Reference build

"The original" means the 16-bit game this project remakes. "Reference build" means its USA retail release, which the scope names as the reference for speed and pacing (S-ENGINE). Measurements taken from it run at its native 60 Hz output and are timed by frame counting on lossless captures, unless a row says otherwise. Game data (formulas, tables, curves) comes from the rights holder's data (S-SOURCE). Where a measurement can be read from that data, the data wins over observation, and observation is used only to cross-check.

## Trace keys

| Key | Points to in `scope.md` |
|---|---|
| D-REMAKE | Decisions: remake or sequel |
| D-SYSTEXT | Decisions: system text |
| D-DIALOGUE | Decisions: dialogue |
| D-VISUALS | Decisions: visuals |
| D-STATIC | Decisions: static art |
| D-VIDEO | Decisions: pre-rendered video |
| D-VOICE | Decisions: voice acting |
| D-MENUART | Decisions: menu art |
| D-PLATFORMS | Decisions: platforms |
| D-NAMERULE | Script: first rule for system text (marked decided) |
| P-FPS | Decisions: frame rate (Proposed) |
| P-COMBAT | Decisions: combat (Proposed) |
| P-CAMERA | Decisions: camera (Proposed) |
| P-STORY | Decisions: story changes (Proposed) |
| P-LENGTH | Decisions: length (Proposed) |
| P-ENGINE | Decisions: engine (Proposed) |
| S-GOALS | Goals |
| S-NONGOALS | Non-goals |
| S-ANTIGOALS | Anti-goals |
| S-PILLAR-1 to S-PILLAR-4 | Pillars: speed, panels, a party of people, three worlds |
| S-SCRIPT | Script section (rules after the first, errors, languages, sizes) |
| S-FAN-TEXT | Fan findings: text and names |
| S-FAN-DEFECTS | Fan findings: defects |
| S-FAN-CUT | Fan findings: content removed or cut |
| S-FAN-BALANCE | Fan findings: balance and difficulty |
| S-FAN-ACCESS | Fan findings: accessibility and play modes |
| S-ENGINE | How the original engine works |
| S-CONTENT | Content to rebuild, categories |
| S-COUNTS | Content to rebuild, first counts |
| S-SYS-BATTLE, S-SYS-ENC, S-SYS-PANEL, S-SYS-FIELD, S-SYS-QOL, S-SYS-DIFF, S-SYS-ACCESS | Systems section, one bullet each |
| S-SOURCE | Source material |
| S-RISKS | Risks |
| S-OPEN | Open questions |
| L-CORE | Shape of the `core/` battle library |
| L-EVENTS | Shape of the `events/` script library |

## Glossary

Terms used across the specs. Terms are generic on purpose. The specs do not name any character, place, item, technique or enemy from the original.

| Term | Meaning |
|---|---|
| Ability | Anything a combatant can use as a command: basic attack, technique, skill, item use, enemy skill. Defined as data |
| Action | One command being executed during the execution phase of a round |
| Area | One loaded region of the field: a town, a dungeon floor, a world-map zone, a station interior |
| Auto-battle | A battle mode where a chosen macro supplies the party's commands each round |
| Basic attack | The default weapon attack every combatant has |
| Battle event stream | The ordered, typed list of events `core/` returns after resolving a round. The renderer animates it |
| Battle seed | The random seed injected into `core/` at battle setup |
| Combatant | A party member or enemy taking part in a battle |
| Combination attack (combo) | A data-defined effect that triggers when its required abilities execute consecutively |
| Combo log | The saved record of combinations the player has triggered |
| Command | A choice made for one combatant for one round: an ability plus its target |
| Critical path | The shortest route through an area that completes its story objective |
| Dialogue | Story text spoken by characters. Locked to the shipped English text |
| Encounter table | Per-area data listing enemy groups and their weights |
| Enemy group | The set of enemies in one battle |
| Event | A scripted sequence written in the `events/` format: a cutscene or a field event |
| Field | The mode where the player moves the party through areas |
| Flag | A named saved value (boolean or integer) that records story and world state |
| Glossary (names) | The table of every renamed system text name: shipped name, new name, reason, pronunciation |
| Guild jobs | The side quests the scope lists under "side quests", kept as they are |
| Host | The abstract interface through which the `events/` interpreter asks the engine to do things |
| Initiative | The per-round value that sets a combatant's place in the execution order |
| Interpreter | The `events/` runtime that executes compiled event scripts and can suspend and resume |
| Live panel | A screen region that shows a real-time render of a 3D scene through its own camera, used for story scenes |
| Macro | A named, ordered list of party commands. Using it fills the round and forces the party's relative order |
| Opening | How a battle starts: normal, party first, or enemy first |
| Pacing metric | A measured quantity (time, count, level) used to compare the remake with the reference build |
| Page (panels) | One screen layout of live panels |
| Party | Up to five active party members |
| Party talk | Conversation among party members in the field, using existing script lines |
| Reference build | The USA retail release of the original |
| Rest | Recovery at an inn or equivalent. Refills HP, technique points and skill uses |
| Reward multiplier | Optional setting that scales battle experience and money. Off by default |
| Round | One cycle of command selection followed by execution of every command |
| Shipped name | A system text name as it appeared in the 1990s English release |
| Skill | An ability with a limited number of uses per character, refilled on rest |
| Skip | Ending the presentation of an event early while applying all of its state changes |
| Status | A data-defined condition on a combatant (for example one that prevents acting) with duration and cure rules |
| System text | Menu labels and the names of items, techniques, skills and enemies, plus descriptions and messages |
| Technique | An ability that costs technique points |
| Technique points (TP) | The resource techniques spend |
| Vehicle | One of the three traversal vehicles. Each has its own field movement and vehicle battles |
| Visible enemy | An enemy group shown in the field that starts a battle on contact |
