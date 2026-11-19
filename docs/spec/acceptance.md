# Acceptance

Testable checks for each system. Each check names the requirements it verifies and where it runs. Checks carry no label of their own (see [README](README.md)). A check that verifies a requirement still waiting on a To measure row cannot pass until that row is answered; it is marked "baseline" below.

| Runs on | Meaning |
|---|---|
| core | Automated, headless, against `core/` only. No engine, no rendering |
| events | Automated, headless, against `events/` with a mock or timing Host. No engine, no rendering |
| tools | Automated build or data check (string tables, glossary, compiled scripts, save files) with no rendering |
| engine | Automated test inside the engine build (functional test, performance capture) |
| playtest | Measured in playtests or by a reviewer against the reference build |

## Automation summary

| Runs on | Checks |
|---|---|
| core | ACC-BAT-01 to ACC-BAT-17, ACC-ENC-04, ACC-SAV-05 |
| events | ACC-EVT-01 to ACC-EVT-09 |
| tools | ACC-TXT-01 to ACC-TXT-05, ACC-SAV-01 to ACC-SAV-04, ACC-FLD-04 |
| engine | ACC-FLOW-01 to ACC-FLOW-04, ACC-FLD-01 to ACC-FLD-03, ACC-ENC-01, ACC-ENC-02, ACC-BAT-18, ACC-BAT-19, ACC-TXT-06, ACC-EVT-10, ACC-EVT-11, ACC-UI-01 to ACC-UI-05 |
| playtest | ACC-FLOW-05, ACC-FLD-05, ACC-FLD-06, ACC-ENC-03, ACC-BAT-20, ACC-EVT-12, ACC-UI-06 |

## Game flow

| ID | Check | Verifies | Runs on |
|---|---|---|---|
| ACC-FLOW-01 | Drive every transition in the transition table and three not in it. Listed ones succeed; others are refused and logged | FLOW-010, FLOW-013 | engine |
| ACC-FLOW-02 | Performance capture on every FLOW-001 target: field and battle hold 60 fps over a scripted route and 20 scripted battles (99th-percentile frame time at most 16.7 ms) | FLOW-001, FLOW-004 | engine |
| ACC-FLOW-03 | Time battle start, battle end and area transitions over 50 samples each; all within budget, and battle start no slower than the measured baseline | FLOW-030 to FLOW-032, FLOW-034 | engine (baseline FLOW-033) |
| ACC-FLOW-04 | Trigger each autosave point and confirm a write; confirm no write happens during battle, choice or text; confirm input stall under 100 ms; inject a write failure and confirm play continues | FLOW-050 to FLOW-054 | engine |
| ACC-FLOW-05 | Median critical-path completion time in playtests compared with the baseline | FLOW-005 | playtest (baseline FLOW-006) |

## Field

| ID | Check | Verifies | Runs on |
|---|---|---|---|
| ACC-FLD-01 | For every area, sample the navmesh on a 0.5 m grid: each point lies in a camera volume, and with the leader there, the leader is inside the central 80% of the screen and not fully occluded | FLD-011, FLD-013, FLD-015 | engine |
| ACC-FLD-02 | Bot walks random routes for 30 minutes per area: no stuck state longer than 2 seconds, no fall out of the world, no direction reversal at volume edges | FLD-016, FLD-024, FLD-025 | engine |
| ACC-FLD-03 | Bot walks each area's critical path; time within 15% of the baseline | FLD-021, FLD-042 | engine (baseline FLD-022, FLD-043) |
| ACC-FLD-04 | Data check: chest, shop and hook counts match the audited counts; every chest, shop and hook has a unique stable ID; every hook points at a compiled script; every party talk line ID exists in the locked dialogue table | FLD-023, FLD-032, FLD-033, FLD-071 | tools |
| ACC-FLD-05 | Reviewer compares each town and dungeon with its inventory and landmark list | FLD-040, FLD-042, FLD-044 | playtest (baseline FLD-022, FLD-041, FLD-043) |
| ACC-FLD-06 | Reviewer checks each vehicle's traversal and each fast-travel mechanism against the measured rules | FLD-051, FLD-061 | playtest (baseline FLD-052, FLD-062) |

## Encounters

| ID | Check | Verifies | Runs on |
|---|---|---|---|
| ACC-ENC-01 | Scripted contacts from front, side and rear, chasing and not: each produces the opening ENC-020 and ENC-021 require, and the battle uses the group drawn at spawn | ENC-011, ENC-020, ENC-021 | engine (baseline ENC-024) |
| ACC-ENC-02 | After a battle or escape, no battle starts for 3 seconds; defeated groups stay gone until area re-entry; switching an area to random mode needs only the mode flag | ENC-002, ENC-014, ENC-015, ENC-042 | engine |
| ACC-ENC-03 | Playtest telemetry per area: M1, M2, M5, M6 within ENC-033 tolerances for natural players; avoider cohort within ENC-034 | ENC-003, ENC-033, ENC-034, ENC-037 | playtest (baseline ENC-036) |
| ACC-ENC-04 | 10,000 seeded escape attempts per tested configuration: success rate matches the measured rule within 1 percentage point; disabled escape never succeeds; a successful escape drops remaining actions and grants no rewards | ENC-030, ENC-031 | core (baseline ENC-005, ENC-035) |

## Battle

| ID | Check | Verifies | Runs on |
|---|---|---|---|
| ACC-BAT-01 | Build `core/` with no engine headers on the include path and run its suite on every target compiler | BAT-002 | core |
| ACC-BAT-02 | Round phase order: a test ability set logs each phase; events appear in the BAT-010 order, and end conditions are checked after each action | BAT-010, BAT-013 | core (baseline BAT-014, BAT-016) |
| ACC-BAT-03 | AI isolation: run the same round with two different party command sets; enemy commands are identical | BAT-011 | core |
| ACC-BAT-04 | Execution-time validation: target falls before the action, cost drained before the action, actor disabled before the action; each produces the specified retarget or cancel event | BAT-015, BAT-071 | core (baseline BAT-016, BAT-072) |
| ACC-BAT-05 | Order: over 100,000 seeded rounds, the order distribution matches the measured formula; ties resolve per BAT-030; an agility change mid-round does not reorder | BAT-030, BAT-031 | core (baseline BAT-033) |
| ACC-BAT-06 | Formula samples: each measured sample from BAT-043 reproduces exactly at Original difficulty | BAT-042 | core (baseline BAT-043) |
| ACC-BAT-07 | Status table tests: each status applies, ticks, blocks or forces commands, and cures per its data | BAT-081 | core (baseline BAT-082) |
| ACC-BAT-08 | Every status application, resistance and removal emits an event with a message key that exists in the string table | BAT-083 | core |
| ACC-BAT-09 | AI: for each sampled enemy, 10,000 seeded rounds give action frequencies within 2 percentage points of the measured ones | BAT-090, BAT-092 | core (baseline BAT-093) |
| ACC-BAT-10 | Macro fill and order: a macro fills the round; party actions run in macro order; enemy positions match a run without the macro; absent and unaffordable entries skip or cancel as specified | BAT-051 to BAT-054 | core (baseline BAT-057) |
| ACC-BAT-11 | Combination: each of the 15 definitions triggers when components run back to back; inserting an enemy action or a cancelled action between them prevents it; component effects are replaced; costs are paid | BAT-060 to BAT-062 | core (baseline BAT-063) |
| ACC-BAT-12 | Combo log: first trigger adds an entry and emits ComboDiscovered; a second trigger does not; auto-battle triggers count | BAT-064 | core |
| ACC-BAT-13 | Event sourcing: for 1,000 seeded random battles, applying the event stream to the start snapshot equals the end state | BAT-110, BAT-111, BAT-115 | core |
| ACC-BAT-14 | Event payload audit: no event field holds text other than a message key | BAT-112 | core |
| ACC-BAT-15 | Determinism: replay 1,000 battle records on every target platform and compiler; serialized streams are byte-identical; static check finds no floating-point type in rules code | BAT-130, BAT-131, BAT-132 | core |
| ACC-BAT-16 | Difficulty overlay: Original and Hard runs use the same `core/` binary; Hard enemy stats within 20% of Original; reward multiplier scales battle rewards only | BAT-122, BAT-124, BAT-125, SAV-023 | core |
| ACC-BAT-17 | Auto-battle: the auto macro runs each round and stops on cancel, a fallen member, an unaffordable entry, and battle end | BAT-103, BAT-104 | core (baseline BAT-105) |
| ACC-BAT-18 | Battle speed: the same battle record played at each speed gives the same final state; presentation time scales with speed | BAT-100 | engine |
| ACC-BAT-19 | Presentation timing per action kind at 1x is no longer than baseline; no interface element remains after its action; no status visual hides the model | BAT-084, BAT-101 | engine (baseline BAT-102) |
| ACC-BAT-20 | Hard playtest: players on Hard meet ENC-033 M5 tolerance for Hard's target level without extra battles; reviewers confirm Hard's changes are behavioral | BAT-121, BAT-123 | playtest |

## Dialogue and text

| ID | Check | Verifies | Runs on |
|---|---|---|---|
| ACC-TXT-01 | Locked dialogue diff: every English dialogue string equals the shipped text or matches a change list entry | TXT-002, TXT-003, TXT-004 | tools |
| ACC-TXT-02 | Glossary check: every system text name that differs from its shipped name has a signed-off glossary entry; every renamed name used in dialogue appears through a name token | TXT-005, TXT-011, TXT-014 | tools |
| ACC-TXT-03 | Portrait codes: every code resolves to a portrait asset; every translation keeps the same portrait codes and name tokens in the same order | TXT-021, TXT-034 | tools |
| ACC-TXT-04 | No player-facing literal text in code, scripts or art manifests | TXT-030 | tools (baseline TXT-006) |
| ACC-TXT-05 | Font coverage: every character in every shipped string table renders with a glyph | TXT-033 | tools |
| ACC-TXT-06 | Pseudo-localization and 150% text size on every screen: no truncation or overlap; "Show shipped names" switches every glossaried name in menus, battle and dialogue at once | TXT-016, TXT-018, TXT-035, UI-019 | engine |

## Events and live panels

| ID | Check | Verifies | Runs on |
|---|---|---|---|
| ACC-EVT-01 | Compiler rejects scripts with each error class in EVT-003 | EVT-003, EVT-004 | events |
| ACC-EVT-02 | Every shipped script compiles; every battle has a Victory branch | EVT-001, EVT-009 | events |
| ACC-EVT-03 | Determinism: each script run twice against the same scripted mock Host produces the same Host call log | EVT-021 | events |
| ACC-EVT-04 | Suspend and resume: run each script, suspend at every yield point, serialize, restore into a new interpreter, finish; the final state equals an uninterrupted run | EVT-022, EVT-051 | events |
| ACC-EVT-05 | Skip equivalence: for every script and every choice path, final game state after skipping from every yield point equals the state after watching, excluding play time and text log | EVT-041, EVT-042 | events |
| ACC-EVT-06 | Skip stops at choices and battles and can resume skipping after them | EVT-041, EVT-044 | events |
| ACC-EVT-07 | Parallel joins: wait-all and wait-any behave as specified; stopped branches apply nothing after their last yield | EVT-008 | events |
| ACC-EVT-08 | Script hash mismatch on load restores event-start state and restarts the event | EVT-052 | events |
| ACC-EVT-09 | Timing Host: each scene's overhead within 110% of its baseline | EVT-060, EVT-062 | events (baseline EVT-061) |
| ACC-EVT-10 | Performance capture of every panel page on FLOW-001 targets: 60 fps, or 30 on pages marked for it with text and input at 60; no page animates more than four panels | EVT-027, EVT-037 | engine |
| ACC-EVT-11 | Asset audit: no story scene references a still image or video asset | EVT-031, EVT-032 | engine |
| ACC-EVT-12 | Reviewer compares each scene's beats and choices with the original | EVT-005, EVT-030 | playtest (baseline EVT-007) |

## Menus and UI

| ID | Check | Verifies | Runs on |
|---|---|---|---|
| ACC-UI-01 | Input-count test: Macros reachable with one input from the field menu root and the battle command root | UI-004 | engine |
| ACC-UI-02 | Menu open time and input latency within UI-010 on FLOW-001 targets | UI-010 | engine |
| ACC-UI-03 | Flash analysis of captured footage (all battle effects, all panel transitions, at both reduced flash settings): no full-screen flash, at most three flashes per second, no flash over 25% of the screen; reduced flash lowers peak change by at least 50% | UI-015, UI-017, UI-021, EVT-038 | engine |
| ACC-UI-04 | Every setting in the settings table exists with its values and default, and each has a description | UI-013, UI-014 | engine |
| ACC-UI-05 | Every item and equipment has a description and a complete effect list on screen | UI-007, TXT-012 | engine |
| ACC-UI-06 | Reviewer compares the menu tree with the original's | UI-003 | playtest (baseline UI-016) |

## Save and progression

| ID | Check | Verifies | Runs on |
|---|---|---|---|
| ACC-SAV-01 | Round trip: load then save is byte-identical except write time, for saves taken at 50 points in an automated playthrough | SAV-010, SAV-011 | tools |
| ACC-SAV-02 | Migration: saves from every released schema version load and pass ACC-SAV-01 after migration; unknown chunks survive; newer saves are refused | SAV-020, SAV-021 | tools |
| ACC-SAV-03 | Integrity: truncated, bit-flipped and interrupted writes leave the previous file usable or are reported damaged, with no crash | SAV-013, SAV-014 | tools |
| ACC-SAV-04 | Content IDs: no save chunk stores a table index | SAV-012 | tools |
| ACC-SAV-05 | Progression samples: level-ups, stat growth (including the cap fix) and battle rewards match the measured data at Original difficulty | SAV-022, SAV-030, SAV-033 | core (baseline SAV-031, SAV-032, SAV-038) |
