# Bringing your own data in

For a team that holds game data in its own form (a spreadsheet, a database, an older engine's tables, a level editor) and needs it in this repo's formats. This page says which file holds what, where its full field reference is, the order to bring the files in, and how to check each step. It does not repeat the field references; follow the links.

Every format here is plain JSON or plain text, and every loader rejects unknown fields, so a converter that writes a misspelled field fails loudly instead of losing data. Write your converter in any language; the repo's own tools are C++17 and Python 3 standard library only.

## The two checks

**The data check** loads a whole data directory, battle data included, and prints every problem with its file and line, then a count of what loaded:

```bash
make -C game                                     # builds game/build/game_cli
game/build/game_cli --check game/data            # the game data; battle data from game.json's core_data
game/build/game_cli --check my/game --core my/core   # your own directories
```

It exits 0 with `OK: no problems.` or 1 with `FAILED: N problems.` It runs the same loader the game uses (core's battle data, text tables, areas, journal, glossary and scripts, compiled and checked strictly, then the references between them) and adds checks the loader does not make: two things on one cell, a marker, exit, step hook or spawn on a solid cell, duplicate hook, zone and chest ids, zones reversed, off the map or overlapping, shops with nothing in stock, exits, markers, hooks, chests, shops and inns the party cannot walk to from any way into the area, and areas that no exit, script warp or start position leads to or that the start cannot reach. A problem in the battle data does not stop the game's own files from being read, so one run reports as much as it can; when problems stop the load early, the report says which checks did not run. The function behind it is `game::checkData` (`game/include/game/check.h`).

**The map importer** converts Tiled maps to area files and back: `python3 -I tools/maps/tiled_to_area.py MAP.tmj -o AREA.json` and `tools/maps/area_to_tiled.py`. See [tools/maps/README.md](../tools/maps/README.md) for the mapping from Tiled layers, classes and properties to the area format.

## Order to bring files in

Start from a copy of the placeholder data, which passes the check, and replace one file at a time in this order, running the check after each. Each step only refers to files brought in before it, so every problem the check prints is about the file you just changed.

1. Core battle data, in the loader's order: `rules.json`, `statuses.json`, `abilities.json`, `items.json`, `equipment.json`, `party.json`, `enemies.json` (enemies, then encounters), `combos.json`, `difficulties.json`, then `text_en.json`. Each core file may refer only to the files before it.
2. `game.json`'s lists that point at core data: `start.lineup`, `start.items`, `join_levels`, `prices`, `bosses`.
3. Text tables, the source language first: `text/<code>.json` (area names and the game's own messages) and `text/<code>.strings` (dialogue, speaker names, journal text). Then every other language with the same keys.
4. Areas: one file per area, listed in `game.json`'s `areas`. Bring in an area together with the areas its exits lead to, or point its exits at an area that already exists.
5. `journal.json`: objectives, jobs, encyclopedia entries, party talk.
6. Event scripts, listed in `game.json`'s `events`, with their flags in `game.json`'s `flags` and their actors in `actors`.
7. Routes for the pacing bot.

Keeping the placeholder files until yours replace them keeps the references working: placeholder areas still have the exits the start needs, placeholder scripts still define the events placeholder hooks run. Remove the placeholder content last, and run the check once more.

## Core battle data

All in `core/data/` (or the directory `--core` names). Full reference: [core/README.md, "Data formats"](../core/README.md#data-formats).

### rules.json

Battle rules that wait on measurements of the original, such as surprise rounds and whether rest revives. Reference: [rules.json](../core/README.md#rulesjson). Every field is required.

### statuses.json

Statuses: what each does to a turn, how long it lasts, what cures it, how it stacks, whether it persists outside battle. A status `defeated` is required. Reference: [statuses.json](../core/README.md#statusesjson).

### abilities.json

Basic attacks, techniques, skills and combo results, each with its target rule and a list of effects. Reference: [abilities.json](../core/README.md#abilitiesjson).

### items.json

Consumables, using the same targets and effects as abilities with flat power. Reference: [items.json](../core/README.md#itemsjson).

### equipment.json

Weapons and armor: slot, category, stat bonuses, a weapon's element or replacement attack, granted abilities and status resistances. Reference: [equipment.json](../core/README.md#equipmentjson).

### party.json

Characters (base stats, growth, basic attack, abilities known and learned, what they can wear and start in), vehicles, the starting lineup, inventory and spare equipment, and the macros. Reference: [party.json](../core/README.md#partyjson) and [Vehicle battles](../core/README.md#vehicle-battles).

### enemies.json

Enemies (stats, abilities, the eight AI slots, overrides, affinities, rewards) and encounters (enemy groups in formation order, escape and opening numbers, drops). Reference: [enemies.json](../core/README.md#enemiesjson).

### combos.json

Combination recipes, compared in data order. Reference: [combos.json](../core/README.md#combosjson).

### difficulties.json

Difficulty overlays that scale enemies, abilities and rewards. Reference: [difficulties.json](../core/README.md#difficultiesjson).

### text_en.json

Core's English message templates for battle and field results. The loader requires a template for every key core can emit with your data. Reference: [text_en.json](../core/README.md#text_enjson).

## Game data

All in `game/data/` (or the directory `--check` names). Full reference: [game/README.md, "Data formats"](../game/README.md#data-formats).

### game.json

Everything about the adventure that is not an area, a script or battle data: the file lists, languages, actors, presentation names, every flag and variable, the start, join levels, prices, caps and timings, and the bosses. Reference: [game.json](../game/README.md#gamejson).

### Areas

One JSON file per area: the cell grid, markers, exits, hooks, shops, rest points, chests and encounter zones. Reference: [Areas](../game/README.md#areas). Draw them in Tiled and convert with [tools/maps](../tools/maps/README.md), or write the JSON directly. The display name is the text key `area.<id>`. Give a person's interact hook the id of its actor in `game.json`'s `actors`: the actor then stands on that cell until a script moves it (see [Actors](../game/README.md#actors)).

### Events

Event scripts (`.ev`): dialogue, choices, flags, battles, warps, shops and rests, compiled against a registry the game builds from the rest of the data. Reference: [Event scripts and text](../game/README.md#event-scripts-and-text) and the script format, [events/FORMAT.md](../events/FORMAT.md).

### Routes

A route (`routes/*.route`) is a list of terminal commands the pacing bot plays from a new game with every battle auto-resolved: `game_cli --bot --route FILE`, which finishes when every boss flag is set, or a flag given with `--goal`. Commands: [game/README.md, the command table](../game/README.md#build-and-test). `routes/kiln.route` is the shortest example of a route that proves new areas can be reached and walked through.

### Text tables

Per language, a dialogue string table `text/<code>.strings` (format: [events/FORMAT.md, "String tables"](../events/FORMAT.md#string-tables)) and a text table `text/<code>.json` (core's `{"text": {...}}` format). Every language must have the source language's keys. Speaker names (`speaker.<actor>`), journal text and the yes/no answers are in the dialogue tables; area names and the game's own messages in the text tables. Reference: [Languages](../game/README.md#languages). The journal (`journal.json`, reference: [journal.json](../game/README.md#journaljson)) and the name glossary (`text/glossary.txt`, reference: [events/FORMAT.md, "Name glossary"](../events/FORMAT.md#name-glossary)) refer to these tables.

## Common mistakes the check reports

These are the messages a converter usually trips first. The line number points at the entry; where a message names no line, the problem is about the whole file or a key in a table.

* `unknown field 'nmae'`, `missing field 'id'`, `field 'cost' must be a number, found string`: a field the format does not have, one it requires, or the wrong JSON type. Every loader checks every field.
* `names missing ability 'bite'` (core): a reference to an id defined nowhere, or defined in a file that loads later. Core files refer only to the files before them.
* `needs exactly 8 slots` (enemies): every enemy lists eight AI slots, each its basic attack or one of its abilities.
* `missing message key 'status.burn.applied'`: a new status or ability brought keys core emits that `text_en.json` lacks.
* `marker 'gate' is not on a floor cell`: markers, exits, hooks, shops, rest points, chests and spawn points must sit on `.` cells. A converter that is off by one, or swaps x and y, shows up here first: cells are `[x, y]` from the top left.
* `marker 'gate' is on the cell of chest 'box', which is solid, so the party would stand inside it`: shops, rest points, chests and interact hooks make their cell solid.
* `the exit to 'cave' at [12, 0] cannot be walked to from any way into the area`, `chest 'box' has no floor cell beside it that the party can walk to`: a wall or a solid object cuts it off from every marker an exit, a warp or the start leads to.
* `exit leads to area 'cave', which no file in game.json's areas defines`, `exit leads to missing marker 'cave west_gate'`: the target area is not listed in `game.json`, or has no such marker.
* `no exit, script warp or start position leads into area 'cave'`, `area 'cave' cannot be reached through exits from the start area 'town'`: an area that is listed but not wired in.
* `flag 'cave.opened' is not declared`, `flag 'chest.box' is declared twice`: every flag a script, hook or journal entry uses is listed in `game.json`'s `flags`, except chest flags, which their chests declare.
* `shop 'stall' stocks 'potion', which is unknown or has no price`: everything a shop stocks needs a price in `game.json`.
* `text 'area.cave' is not in the text table`, `key 'area.cave' of the 'en' table is missing`: every area needs a display name, in every language.
* `string 'speaker.smith' is not in the dialogue table`: every actor needs a speaker name string.
* `hook 'door' runs unknown event 'cave_door'`, `zone 'hall' lists unknown encounter 'bats'`: a name the scripts or the battle data do not define.
* `inline text in say in event 'cave_door': player-facing text must be a string ID (@id) from the string tables` and `the name "Bell Warden" is typed out`: the scripts' strict check. Every line is a string ID, and glossary names are written as name tokens.
* `string ID 'cave.old_line' is not used by any script or by the game`: a dialogue string left over, or one language has a key another lacks. The strict check treats warnings as problems.

The scripted playthrough, the bot runs and the acceptance checks then test behavior on top of loading. After bringing in new placeholder content, run `make test` and `make acceptance`; `make acceptance` also checks that every character the text tables use is in the placeholder glyph lists (`game/data/text/glyphs_<code>.txt`).
