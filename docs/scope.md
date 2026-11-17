# Phantasy Star IV remake: scope

Status: draft for review. Nothing here is approved.

Each choice is marked **Decided** (stated by the project lead) or **Proposed** (a recommendation that still needs a yes).

## Intent

Remake Phantasy Star IV as a modern, AAA-quality game, faithful to the original story and script. The remake comes first. A follow-up with new story (the "caretaker centuries later" idea) is a later project, and this one should leave the door open for it.

## Decisions so far

| Topic | Choice | Status |
|---|---|---|
| Remake or sequel | Remake first, expand after | Decided |
| Script | Keep the original copy: the English text as shipped, shortened names included | Decided |
| Visuals | Full 3D, AAA quality | Decided |
| Static art | None. Everything on screen is rendered in real time: no illustrated stills, no pre-rendered video | Decided (the "no pre-rendered video" reading is an interpretation, confirm) |
| Combat | Turn-based, with macros and combination techniques | Proposed |
| Camera | Pulled-back, mostly fixed camera in the field. Closer, directed cameras in battle and cutscenes | Proposed |
| Story changes | None to plot or outcomes. No route that changes who lives or dies | Proposed |
| Length | Stay near the original's roughly 20 hours. No padding | Proposed |
| Engine | Unreal Engine 5 | Proposed |

## Goals

- A new player with no history with the series can play it and understand why the original is loved.
- A returning player recognizes every beat, place and line.
- The systems (combat, party, world traversal, cutscene tooling) are built to be reused by a follow-up.

## Non-goals

- New story chapters, new playable characters, or a new ending.
- An action combat system.
- Open-world structure.
- Multiplayer or live-service features.

## Anti-goals

These would make the remake a failure even if it ships:

- It feels slow. The original's speed in battle and in story is the thing fans praise most.
- It is stretched to 50 hours to justify the price.
- The manga-panel storytelling is replaced by ordinary cutscenes.
- The script is rewritten in a modern voice.

## Pillars

1. **Speed.** Battles resolve in seconds. Macros are a first-class feature, not a menu buried three levels deep.
2. **Panels.** Story scenes play as manga panels, as in the original. Here each panel is a live viewport into the 3D scene, with its own camera, lighting and animation. This meets the "nothing static" rule and keeps the game's signature.
3. **A party of people.** Characters talk to each other in the field. The original's party talk feature is kept and expanded with existing script lines only.
4. **Three worlds.** Desert, ice and the third world each need a distinct look and their own traversal vehicle.

## Approaches considered

**Stylized 3D, pulled-back camera, turn-based (picked).** Modern look, keeps combat and pacing, and lets the character designs lead. It costs more than 2D but far less than a full third-person rebuild, because the fixed camera limits how much of each environment has to be built to close-up quality.

**HD-2D.** Cheaper and proven in the market. Ruled out by the full-3D, AAA decision.

**Third-person action, close camera.** The highest cost, and it replaces the combat and the pacing that define the game. Ruled out by the pillars.

**First-person.** Belongs to the first game's dungeons, not this one.

## Script

The source of truth is the English text as shipped in the 1990s release. That text was written under tight length limits, so some item, technique and character names are shortened. They stay as shipped.

What follows from that:

- **Voice.** Shortened names will be spoken aloud. Agree a pronunciation for each one in phase 0 and record it in a glossary, so casting and recording do not improvise.
- **Other languages.** Decide whether new translations work from the shipped English or from the Japanese script. Translating from the English carries its shortened names into every language.
- **Delivery, not wording.** Text boxes that read well can sound stiff when spoken. Budget for a pass that sets delivery notes and timing and changes no words.
- **Errors.** Decide in phase 0 whether plain typos in the shipped text are fixed or kept. Proposed: fix typos only, and list every fix.

## Content to rebuild

Counts come from the source audit in phase 0. The categories are:

| Category | Notes |
|---|---|
| Playable characters | Eleven, including temporary party members. Each needs field, battle and panel-quality models, plus equipment variants if weapons are shown |
| Enemies and bosses | Full bestiary. Many are palette or scale variants in the original, which will not pass at AAA quality. Decide early how many get unique models |
| Towns and villages | Every tile-based town becomes a full 3D space. This is the largest environment cost |
| Dungeons | Original layouts are grid mazes built around random battles. Layouts need a redesign pass that keeps landmarks and length |
| World maps | Three planetary surfaces plus the space stations. Decide between a scaled overworld and connected zones |
| Vehicles | Three, each with its own traversal and vehicle battles |
| Story scenes | Every panel sequence rebuilt as live panels |
| Techniques, skills, combination attacks | All need animation and effects. Combination attacks need a discoverability feature |
| Side quests | The hunter's guild jobs, kept as they are |
| Music | Full re-recording or rearrangement of the soundtrack |
| Text | Every line, in every shipped language, plus voice |

## Systems

- **Battle:** turn-based, party of five, macros, combination techniques. Proposed additions: a combo log that records discovered combinations, battle speed settings, and auto-battle driven by macros.
- **Encounters:** the original uses random battles. Proposed: visible enemies in the field, with the encounter rate tuned to match original pacing. This is the largest feel change in the project and needs a prototype before it is committed.
- **Live panel cutscene tool:** an editor for laying out panels, each bound to a camera in a live scene. This is the main custom technology and the main reusable asset for a follow-up.
- **Field:** town and dungeon traversal, party talk, vehicles.
- **Quality of life:** autosave, quest log, fast travel where the original already has it.

## Source material

Needed from the rights holder before pre-production can start:

- The original script: the shipped English text, and the Japanese for reference
- Original character and world design sheets
- Game data: stats, formulas, encounter tables, item and technique lists, maps
- Music masters or scores
- Written confirmation of what may be stored where, and who may access it

Large assets belong in a dedicated asset store (Perforce or equivalent), not in git. This repo stays documents and tooling only.

## Phases

Durations are planning assumptions for a AAA production, not estimates from a costed plan.

| Phase | Length | Output |
|---|---|---|
| 0. Source audit | 2 to 3 months | Full content counts, name pronunciation glossary, art direction brief, go or no-go |
| 1. Pre-production | 9 to 12 months | Art bible, engine and pipeline, live panel tool prototype, combat prototype, encounter prototype |
| 2. Vertical slice | Inside phase 1 | One town, one dungeon, one boss, one panel sequence, at shipping quality. Proves cost per town and cost per scene |
| 3. Production | 24 to 30 months | All content |
| 4. Finish | 9 to 12 months | Localization, voice, certification, performance, polish |

Total: roughly four to five years. Peak team in the low hundreds, counting external art partners. The vertical slice is what turns these into real numbers.

## Risks

| Risk | Why it matters | Mitigation |
|---|---|---|
| Fidelity multiplies scope | A screen of tiles becomes a hand-built 3D town. A 20-hour game can cost like a 60-hour one | Fixed camera, a strict environment kit, cost per town measured in the slice |
| "Nothing static" is expensive | Every panel is a staged, lit, animated scene | Build the panel tool first. Reuse sets and rigs across panels |
| Pacing drifts | Voice, animation and camera work all slow a game down | A pacing budget per scene, measured against the original |
| Enemy count | Palette swaps do not survive at this quality | Decide the unique-model budget in phase 0 |
| Script handling | Fans notice any changed line | The shipped English text is locked. Any typo fix is listed and signed off |
| Encounter change | Visible enemies change difficulty and leveling curves | Prototype in pre-production, keep random battles as a fallback |

## Open questions

1. Do new translations work from the shipped English or from the Japanese script?
2. Does "nothing static" also rule out pre-rendered video and 2D user interface art such as menu portraits?
3. Target platforms and target frame rate.
4. Who owns art direction day to day, and how much time the original designer has for it.
5. Is the follow-up assumed in the business case, or decided after launch? This changes how much is spent on reusable tooling.
6. Budget range and internal versus external team split.
