# Phantasy Star IV remake: scope

Status: draft for review. Nothing here is approved.

Each choice is marked **Decided** (stated by the project lead) or **Proposed** (a recommendation that still needs a yes).

## Intent

Remake Phantasy Star IV as a modern, AAA-quality game, faithful to the original story and script. The remake comes first. A follow-up with new story (the "caretaker centuries later" idea) is a later project, and this one should leave the door open for it.

## Decisions so far

| Topic | Choice | Status |
|---|---|---|
| Remake or sequel | Remake first, expand after | Decided |
| System text | Menu labels and the names of items, techniques, skills and enemies are restored to full names now that the length limits are gone. Restore and clarify only | Decided |
| Dialogue | The English text as shipped in the 1990s release | Decided |
| Visuals | Full 3D, AAA quality | Decided |
| Static art | None. Story scenes, characters and environments are rendered in real time: no illustrated stills | Decided |
| Pre-rendered video | None at the start. Revisit after the vertical slice if a scene cannot be done in real time | Decided |
| Menu art | Character portraits are rendered live. Icons, frames and fonts are ordinary 2D interface art | Decided |
| Platforms | Current consoles and PC. No handheld-class target | Decided |
| Frame rate | 60 frames per second in the field and in battle. Story panels may run at 30 if the heaviest scenes need it | Proposed |
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

The text splits into two kinds, with different rules.

**Dialogue and story text** is the English text as shipped in the 1990s release. It is locked.

**System text** is menu labels and the names of items, techniques, skills and enemies. The shipped names were cut to fit tight length limits, which no longer apply, so these may change.

Rules for system text. The first is decided, the rest are proposed:

- A name changes only to restore what the length limit cut, or to make a menu clearer. It is not a chance to rebrand.
- The Japanese original is the reference for what the full name should be.
- Every change goes in a glossary: shipped name, new name, reason. Fans will look for this list, so plan to publish it.
- Where dialogue mentions a renamed item or technique, the dialogue uses the new name. This is the one permitted change to dialogue, and each instance is listed.
- An option to show the shipped names is cheap to add and worth considering.

Size of the text, from a fan project's published tooling. Treat as an estimate until the audit counts it:

- About 2,170 dialogue messages. This is the voice recording and localization workload.
- About 865 names, descriptions, banners and menu entries. This is the system text that the glossary covers.
- Dialogue carries inline codes for line breaks, page pauses and which portrait to show. The portrait codes are a ready-made map of who speaks each line, which casting and the panel tool can both use.
- The original fonts have no accented characters, which is why fan translations into other languages had to replace the text engine.

What follows for the rest of production:

- **Voice.** Restored names are easier to say than shortened ones. The glossary also records pronunciation.
- **Other languages.** Decide whether new translations work from the shipped English or from the Japanese script.
- **Delivery, not wording.** Text boxes that read well can sound stiff when spoken. Budget for a pass that sets delivery notes and timing and changes no words.
- **Errors.** Proposed: fix plain typos in dialogue, and list every fix.
- **Languages shipped.** Not decided. Japanese and English at minimum is the assumption.

## What the fan community has already told us

Thirty years of fan patches are a record of what players wanted fixed. All ten patches listed for the game on the main patch site were read, plus the public page on the English prototype builds. Each has a proposed verdict.

### Text and names

| Finding | Source patches | Proposed verdict |
|---|---|---|
| Names of techniques, skills, items and enemies were cut to a few letters. Every text patch expands them | Three text patches | Adopt. Already decided under system text |
| The Western release dropped in-battle status messages, such as the line telling you a character fell asleep | Retranslation | Adopt. These are system text, restore them |
| Feedback messages are unclear, for example when a character has no usable field skill, or the label for discarding an item | Retranslation | Adopt. System text, rewrite for clarity |
| Some late-game equipment has no description at all | Relocalization | Adopt. Write the missing descriptions from the Japanese source |
| Terms drifted from the first two games, so links between the games are lost | Three text patches | Names: adopt through the glossary. Dialogue is locked, so the rest goes in the in-game encyclopedia |
| Location labels, menu choices and soundtrack titles are abbreviated or awkward | Text cleanup | Adopt |
| Dialogue retranslated from the Japanese, with jokes that did not survive the 1990s translation | Retranslation, relocalization | Not adopted. Dialogue is locked |
| A villain's motivation, cut for deadline and known from developer interviews, written back into the story | Relocalization | Not adopted in dialogue. Material for the encyclopedia or the follow-up, if the rights holder's own archives confirm it |
| Players disagree on names so strongly that one author offers custom builds per preference | Relocalization | Supports the option to show the shipped names |

### Defects

| Finding | Source patches | Proposed verdict |
|---|---|---|
| Stats applied wrongly at the level cap, and one late-game technique misbehaving | Level cap fix | Fix |
| An enemy's technique leaves a window stuck on screen, and its sprite partly vanishes under one status effect | Retranslation | Fix |
| A character portrait missing in one rebuilt town, in the Western release only | Retranslation | Fix |
| Slowdown in busy battles, traced to the party display redrawing every frame | Retranslation | Not a carry-over risk in a new engine. Keep battle frame rate as a tested target |
| A community bug fix patch that four other patches build on | Standalone bug fix | Its change list was not on the page. Get the full list in phase 0 |

### Content removed or cut

| Finding | Source patches | Proposed verdict |
|---|---|---|
| Two enemies were removed from every release outside Japan | Enemy restoration, and three others that include it | Use the Japanese enemy roster, subject to ratings review |
| Two unused enemies and one unused technique remain in the game data | General improvement | List all cut content in phase 0, then decide. Not restored by default |
| One ring has no practical use, and one character's weapon choices are narrow. Patches repurpose the ring and widen the weapon list | Text cleanup | Not adopted by default. List dead items in phase 0 |

### Balance and difficulty

| Finding | Source patches | Proposed verdict |
|---|---|---|
| The game is easy for returning players. Five patches make it harder | Rebalance, arranged version, three hard-mode variants | Difficulty settings, with the original balance as the default |
| The best-regarded rebalance changes behavior, not only numbers: enemies react to the player's tactics, grow more aggressive when hurt, resist instant-defeat techniques, and bosses gain abilities. Party members get clearer roles, and healing techniques are weakened so items matter | Rebalance | Use as the model for the hard setting. Raising numbers alone only adds grinding |
| The same patch raises experience by half and money by a third, so harder does not mean slower | Rebalance, double-reward variants | Hard setting keeps leveling pace. A reward multiplier is an option, off by default |
| One patch slows rewards so players use items and weapon effects they normally ignore | Arranged version | Evidence that much of the item system goes unused at original balance. Surface item and weapon effects better in menus |
| A patch that was not harder got a bad review from a player who assumed it was | General improvement | Label each difficulty setting plainly, in the menu, with what it changes |

### Accessibility and play modes

| Finding | Source patches | Proposed verdict |
|---|---|---|
| Critical hits flash the whole screen white. One patch softens the flash | Retranslation | Adopt as a requirement. Full-screen flashes are a photosensitivity risk and a certification matter |
| A patch strips story text so speedrunners can practice battles and menus | Speedrun practice | Every scene skippable. A dedicated practice mode is not in scope |

### Languages

Fans have translated the game into German, French, Polish, Korean and Portuguese (twice). That is direct evidence of demand, and it feeds the open question on which languages ship.

### Prototype builds

Five English builds are documented publicly, all from after the Japanese release. They show the translation in progress and nothing from the game's development. With dialogue locked to the shipped text, their only use is as background for the name glossary.

### Rules for using this material

- Fan patches are their authors' own work. Use them as a list of problems, not as a source of text. Every fix is sourced from the rights holder's Japanese script and data.
- Prototype builds and public disassemblies are not project inputs unless the rights holder's legal team clears them.
- The authors know this game's text and data better than almost anyone. Consider paying them as consultants.

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
- **Quality of life:** autosave, quest log, fast travel where the original already has it, every scene skippable.
- **Difficulty:** original balance by default, a hard setting built on smarter enemies, and an optional reward multiplier.
- **Accessibility:** no full-screen flashes, with a reduced-flash setting for effects.

## Source material

Needed from the rights holder before pre-production can start. The project lead reports that some or all of the original masters may be lost, so the first item is to confirm what survives:

- The original script: the shipped English text, and the Japanese for reference
- Original character and world design sheets
- Game data: stats, formulas, encounter tables, item and technique lists, maps
- Music masters or scores
- Written confirmation of what may be stored where, and who may access it

Large assets belong in a dedicated asset store (Perforce or equivalent), not in git. This repo stays documents and tooling only.

### Working with a cartridge dump

A dump of the retail cartridge is enough to start comparing names and text before the rights holder's script arrives.

- Put the dump in `source-material/`. That folder is ignored by git, and nothing in it or extracted from it is ever committed.
- Check it is a clean dump of the Western retail release before trusting anything read from it. The published checksum for that release is CRC32 `FE236442`, for a file of 3,145,728 bytes.
- The text is stored compressed, so reading it needs a small extraction tool. That tool belongs in this repo. Its output does not.
- First output wanted: every shipped name next to its Japanese original and a proposed restored name. That is the first draft of the glossary.

Measured from the dumps with `tools/extract_names.py`:

- The Western dump matches the published checksum, so it is a clean copy of the retail release.
- The European cartridge is a later build (August 1995 against November 1994) and differs in many bytes, but its name tables are identical. One English glossary covers both regions.
- Names are stored uncompressed, one byte per character, with a hard cap of 10 characters in the main tables.
- About 610 distinct names across items, techniques, skills, enemies and places. The tool also picks up a few short runs that are not names, so the audit should confirm the count.
- 266 of them, more than four in ten, are exactly 10 characters long, which is the cap. Those are the ones most likely to have been cut to fit.
- 107 use a hyphen to join shortened words.
- Dialogue is stored compressed and is not extracted. It is locked to the shipped text, so the glossary does not need it.

What the dump is and is not for: it is a reference for measuring and comparing. It is not a source of assets. Art, music and script for the remake come from the rights holder's own masters under the project's contract.

## Phases

Durations are planning assumptions for a AAA production, not estimates from a costed plan.

| Phase | Length | Output |
|---|---|---|
| 0. Source audit | 2 to 3 months | Full content counts, name glossary (shipped name, new name, pronunciation), art direction brief, go or no-go |
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
| Script handling | Fans notice any changed line or name | Dialogue is locked. Every renamed menu, item, technique or skill is in the glossary and signed off |
| Original masters may not exist | The project lead reports that the rights holder no longer holds the original art, music data or scripts. If so, the retail cartridge is the only surviving copy | Confirm in writing in phase 0 what the rights holder does hold. If recovery from the cartridge is needed, it is done under written authorization from the rights holder, and it recovers reference material, not shipping assets |
| Encounter change | Visible enemies change difficulty and leveling curves | Prototype in pre-production, keep random battles as a fallback |

## Open questions

1. Which languages ship?
2. Do new translations work from the shipped English or from the Japanese script?
3. Frame rate target: is 60 in play, with 30 allowed in the heaviest story panels, acceptable?
4. Who owns art direction day to day, and how much time the original designer has for it.
5. Is the follow-up assumed in the business case, or decided after launch? This changes how much is spent on reusable tooling.
6. Budget range and internal versus external team split.
7. Does the remake use the Japanese enemy roster, restoring the two enemies removed elsewhere?
8. Is cut content (unused enemies, an unused technique) restored, and are dead items fixed?
