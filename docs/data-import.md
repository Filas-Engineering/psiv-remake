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

It exits 0 with `OK: no problems.` or 1 with `FAILED: N problems.` It runs the same loader the game uses (core's battle data, text tables, areas, journal, glossary and scripts, compiled and checked strictly, then the references between them) and adds checks the loader does not make: two things on one cell, a marker, exit, step hook or spawn on a solid cell, duplicate hook, zone and chest ids, zones reversed, off the map or overlapping, shops with nothing in stock, exits, markers, hooks, chests, shops, inns and ports the party cannot walk to from any way into the area (in a field area a vehicle of `game.json` may carry it over the terrain it crosses), and areas that no exit, script warp or start position leads to or that the start cannot reach. A problem in the battle data does not stop the game's own files from being read, so one run reports as much as it can; when problems stop the load early, the report says which checks did not run. The function behind it is `game::checkData` (`game/include/game/check.h`).

**The map importer** converts Tiled maps to area files and back: `python3 -I tools/maps/tiled_to_area.py MAP.tmj -o AREA.json` and `tools/maps/area_to_tiled.py`. See [tools/maps/README.md](../tools/maps/README.md) for the mapping from Tiled layers, classes and properties to the area format.

## Order to bring files in

Start from a copy of the placeholder data, which passes the check, and replace one file at a time in this order, running the check after each. Each step only refers to files brought in before it, so every problem the check prints is about the file you just changed.

1. Core battle data, in the loader's order: `rules.json`, `statuses.json`, `abilities.json`, `items.json`, `equipment.json`, `party.json`, `enemies.json` (enemies, then encounters), `combos.json`, `difficulties.json`, then `text_en.json`. Each core file may refer only to the files before it.
2. `game.json`'s lists that point at core data: `start.lineup`, `start.items`, `join_levels`, `prices`, `bosses`, `vehicles` (each names a vehicle of `party.json`).
3. Text tables, the source language first: `text/<code>.json` (area names and the game's own messages) and `text/<code>.strings` (dialogue, speaker names, journal text). Then every other language with the same keys.
4. Areas: one file per area, listed in `game.json`'s `areas`. Bring in an area together with the areas its exits lead to, or point its exits at an area that already exists.
5. `journal.json`: objectives, jobs, encyclopedia entries, party talk.
6. Event scripts, listed in `game.json`'s `events`, with their flags in `game.json`'s `flags` and their actors in `actors`.
7. Routes for the pacing bot.

Keeping the placeholder files until yours replace them keeps the references working: placeholder areas still have the exits the start needs, placeholder scripts still define the events placeholder hooks run. Remove the placeholder content last, and run the check once more.

## Core battle data

All in `core/data/` (or the directory `--core` names). Full reference: [core/README.md, "Data formats"](../core/README.md#data-formats).

### rules.json

Optional rule switches, each defaulting to the placeholder's rule: `affinity_divisor` (what affinities are divided by: 100 for percents, 4 for the original's factors), `magic_defense` (elemental damage subtracts the `magic_defense` stat in place of mental) and `instant_defeat_kinds` (party member kinds that instant defeat can fell). Reference: [rules.json](../core/README.md#rulesjson).

Battle rules that wait on measurements of the original, such as surprise rounds and whether rest revives. Reference: [rules.json](../core/README.md#rulesjson). Every field is required.

### statuses.json

Statuses: what each does to a turn, how long it lasts, what cures it, how it stacks, whether it persists outside battle. A status `defeated` is required. Reference: [statuses.json](../core/README.md#statusesjson).

### abilities.json

Basic attacks, techniques, skills and combo results, each with its target rule and a list of effects. Reference: [abilities.json](../core/README.md#abilitiesjson).

### items.json

Consumables, using the same targets and effects as abilities with flat power. An item without `effects` is a key item (a key, a quest item, a vehicle's call item), which the game gives its use. Reference: [items.json](../core/README.md#itemsjson).

Fast travel the original has (FLD-051, FLD-052) is an effect: `{"type": "travel", "to": "town"}` (back to a visited town of the region, outside dungeons) or `{"type": "travel", "to": "dungeon_exit"}` (out of the dungeon the party is in), alone on a `field` technique or item that targets `all_allies`. The game decides where the party may go; core takes only the cost and refuses it in battle.

### equipment.json

Weapons, shields and armor: slot (`weapon` and `shield` go in either hand), category, `hands: 2` for a two-handed weapon, stat bonuses, a weapon's element or replacement attack, granted abilities and status resistances. Reference: [equipment.json](../core/README.md#equipmentjson).

### party.json

Characters (base stats, growth, basic attack, abilities known and learned, what they can wear and start in, `vulnerable` to enemy instant defeat), vehicles, the starting lineup, inventory and spare equipment, and the macros. Reference: [party.json](../core/README.md#partyjson) and [Vehicle battles](../core/README.md#vehicle-battles).

### enemies.json

Enemies (stats with an optional `magic_defense`, abilities, the eight AI slots, overrides, affinities, rewards, `flying` for the renderer, `next_form` and `next_form_at` for a form taken at a percent of hit points) and encounters (enemy groups in formation order, escape and opening numbers, drops). Reference: [enemies.json](../core/README.md#enemiesjson).

### combos.json

Combination recipes, compared in data order, and `enemy_combos`, the enemies' joint attacks. Reference: [combos.json](../core/README.md#combosjson).

### difficulties.json

Difficulty overlays that scale enemies, abilities and rewards. Reference: [difficulties.json](../core/README.md#difficultiesjson).

### text_en.json

Core's English message templates for battle and field results. The loader requires a template for every key core can emit with your data. Reference: [text_en.json](../core/README.md#text_enjson).

## Game data

All in `game/data/` (or the directory `--check` names). Full reference: [game/README.md, "Data formats"](../game/README.md#data-formats).

### game.json

Everything about the adventure that is not an area, a script or battle data: the file lists, languages, actors, presentation names and data, every flag and variable, the start, join levels, prices, caps and timings, the bosses, and the field rules of vehicles. Reference: [game.json](../game/README.md#gamejson).

Vehicles in the field (FLD-060 to FLD-062): one `vehicles` entry per vehicle of `party.json` that moves in the field, with the terrain it crosses (`floor`, `water`), its speed against walking and the encounter table of battles while riding it. When the real traversal rules arrive (FLD-062), they go here as data:

```json
"vehicles": [{"id": "reed_skimmer", "crosses": ["floor", "water"], "speed_percent": 200, "encounters": [{"encounter": "marsh", "weight": 1}]}]
```

Optional presentation data, none of which is part of the content hash, so it can be filled in or changed without touching saves or records:

* Cameras: each entry of `presentation.cameras` is a name, as before, or an object that also says what the camera looks at and how close. `area` and `marker` aim it at a marker; `follow` follows an actor or a character, or the party leader with `"leader"`; `zoom` (0.25 to 4, default 1) is its scale against the field's.

  ```json
  "cameras": [
  	"wide_shot",
  	{"id": "lookout_east", "area": "reed_path", "marker": "lookout_east", "zoom": 1.25},
  	{"id": "peddler", "follow": "peddler", "zoom": 1.5},
  	{"id": "party", "follow": "leader"}
  ]
  ```

* Music outside scripts: `presentation.music` with an optional `title`, `victory` (a won battle's result), `defeat` (a game over) and `encounters` (encounter id to track). Every track must also be in `presentation.audio`.

  ```json
  "music": {"title": "title_harbor_dawn", "victory": "victory_bright_sail", "defeat": "defeat_low_tide", "encounters": {"warden": "battle_brass_warden"}}
  ```

* Action durations: `presentation.durations`, the table `event_cli time` reads from its timing configuration, so the game reports each presentation action's authored seconds to the engine. `action` names an action as timing files do, `subject` (optional) one actor, camera, panel or track, `seconds` 0 to 60.

  ```json
  "durations": [{"action": "panel_show", "seconds": 0.35}, {"action": "move", "subject": "dockhand", "seconds": 0.9}]
  ```

### Areas

One JSON file per area: the cell grid, markers, exits, hooks, shops, rest points, chests and encounter zones, and an optional `music` track (`"music": "town_tide_bells"`, an id from `presentation.audio`; the Tiled map property of the same name). Reference: [Areas](../game/README.md#areas). Draw them in Tiled and convert with [tools/maps](../tools/maps/README.md), or write the JSON directly. The display name is the text key `area.<id>`. An interact hook may stand on a wall or water cell (a sign, a ladder): the party uses it from a floor cell beside it, which the check makes sure it can reach. Give a person's interact hook the id of its actor in `game.json`'s `actors`: the actor then stands on that cell until a script moves it (see [Actors](../game/README.md#actors)).

Travel data lives in areas too: `region` groups areas into a world or region (a return to a town reaches the visited towns of the party's region), a town's `arrival` names the marker a return lands on, and `ports` are the transport between regions (a port stands on any cell like an interact hook, lands travellers on its `marker`, and opens once its `flag` is set). The Tiled converter carries `region` and `arrival` as map properties and ports as objects of class `port`. A port with a `ship` puts the party aboard a `vessel` area (its `arrival` marker, `helms`, `gangway`).

Story and dungeon changes are area data too: `changes` turn cells to another tile while a flag is set (bridges, sealed places, gates, illusions), an exit with `fall` is a pit, an exit within the area a transport tile, map rows may hold soft ground `:` and barriers `%` for vehicles (`game.json` `crosses`, `breaks`, `call_item`), and shops take a `kind`, flag-gated `stock` entries and `require_flag`/`block_flag`. The rest (switches, locked doors, story barriers) are scripts setting those flags, with the script tests `item <id>` and `party <member>`.

### Events

Event scripts (`.ev`): dialogue, choices, flags, battles, warps, shops and rests, vehicles given, placed and removed (only in field areas, and only vehicles with field rules in `game.json`), compiled against a registry the game builds from the rest of the data. Reference: [Event scripts and text](../game/README.md#event-scripts-and-text) and the script format, [events/FORMAT.md](../events/FORMAT.md). Every animation an `anim` names must be listed in `game.json`'s `presentation.animations`. The game follows the moves, turns, shows and hides of `game.json`'s actors; a script that moves, turns or animates a party member, and every `anim`, is presentation the engine shows on its own (see [Actors](../game/README.md#actors)).

### Routes

A route (`routes/*.route`) is a list of terminal commands the pacing bot plays from a new game with every battle auto-resolved: `game_cli --bot --route FILE`, which finishes when every boss flag is set, or a flag given with `--goal`. Commands: [game/README.md, the command table](../game/README.md#build-and-test). `routes/kiln.route` is the shortest example of a route that proves new areas can be reached and walked through.

### Text tables

Per language, a dialogue string table `text/<code>.strings` (format: [events/FORMAT.md, "String tables"](../events/FORMAT.md#string-tables)) and a text table `text/<code>.json` (core's `{"text": {...}}` format). Every language must have the source language's keys, except descriptions and names.

Names are optional keys too: `name.<kind>.<id>` (kind `character`, `ability`, `item`, `equipment`, `status`, `enemy`, `vehicle` or `difficulty`) gives a thing's name in that language, in place of the battle data's English name; a key naming nothing the battle data has is reported. They are left out of the content hash.

Descriptions (UI-007) are optional keys of the text tables: `desc.<kind>.<id>`, where kind is `item`, `equipment`, `ability`, `status`, `enemy` or `difficulty` and id is an id of that kind in the battle data. Each language has its own or none; a missing one shows as empty. They are left out of the content hash. `python3 -I tools/text/cli.py descriptions --text game/data/text/en.json --text game/data/text/ja.json --core core/data` counts them per kind and lists what has none.

```json
"desc.item.tonic": "A bitter herbal draught. Restores 30 HP to one ally.",
"desc.status.sleep": "Cannot act. Ends with an even chance at each round's end, or when hurt.",
"desc.difficulty.hard": "Built on smarter enemies: bosses learn new moves and resist sleep, healing techniques are weaker, and rewards rise by a fifth."
``` Speaker names (`speaker.<actor>`), journal text and the yes/no answers are in the dialogue tables; area names and the game's own messages in the text tables. Reference: [Languages](../game/README.md#languages). The journal (`journal.json`, reference: [journal.json](../game/README.md#journaljson)) and the name glossary (`text/glossary.txt`, reference: [events/FORMAT.md, "Name glossary"](../events/FORMAT.md#name-glossary)) refer to these tables.

## Common mistakes the check reports

These are the messages a converter usually trips first. The line number points at the entry; where a message names no line, the problem is about the whole file or a key in a table.

* `unknown field 'nmae'`, `missing field 'id'`, `field 'cost' must be a number, found string`: a field the format does not have, one it requires, or the wrong JSON type. Every loader checks every field.
* `names missing ability 'bite'` (core): a reference to an id defined nowhere, or defined in a file that loads later. Core files refer only to the files before them.
* `needs exactly 8 slots` (enemies): every enemy lists eight AI slots, each its basic attack or one of its abilities.
* `missing message key 'status.burn.applied'`: a new status or ability brought keys core emits that `text_en.json` lacks.
* `marker 'gate' is not on a floor cell`: markers, exits, step hooks, shops, rest points, chests and spawn points must sit on `.` cells; interact hooks only inside the map (`hook 'sign' is outside the map`). A converter that is off by one, or swaps x and y, shows up here first: cells are `[x, y]` from the top left.
* `marker 'gate' is on the cell of chest 'box', which is solid, so the party would stand inside it`: shops, rest points, chests and interact hooks make their cell solid.
* `the exit to 'cave' at [12, 0] cannot be walked to from any way into the area`, `chest 'box' has no floor cell beside it that the party can walk to`: a wall or a solid object cuts it off from every marker an exit, a warp or the start leads to.
* `exit leads to area 'cave', which no file in game.json's areas defines`, `exit leads to missing marker 'cave west_gate'`: the target area is not listed in `game.json`, or has no such marker.
* `no exit, script warp or start position leads into area 'cave'`, `area 'cave' cannot be reached through exits from the start area 'town'`: an area that is listed but not wired in.
* `flag 'cave.opened' is not declared`, `flag 'chest.box' is declared twice`: every flag a script, hook or journal entry uses is listed in `game.json`'s `flags`, except chest flags, which their chests declare.
* `shop 'stall' stocks 'potion', which is unknown or has no price`: everything a shop stocks needs a price in `game.json`.
* `text 'area.cave' is not in the text table`, `key 'area.cave' of the 'en' table is missing`: every area needs a display name, in every language.
* `string 'speaker.smith' is not in the dialogue table`: every actor needs a speaker name string.
* `'desc.item.potion' describes nothing`: a description for an id the battle data does not have, or of an unknown kind (`desc.weapon.x`). Rename it to the id, or drop it.
* `the camera looks at marker 'gate' of area 'cave', which does not exist`, `the camera follows 'smith', which is no actor or character`, `camera 'x' names a marker or an area without the other`: a camera's target names something the data lacks.
* `music 'cave_theme' is not one of game.json's presentation audio ids`, `music is given for encounter 'bats', which the battle data does not have`: list every track in `presentation.audio`, and give music only to encounters that exist.
* `unknown action 'panel show'`: durations write actions with underscores (`panel_show`), as timing files do.
* `hook 'door' runs unknown event 'cave_door'`, `zone 'hall' lists unknown encounter 'bats'`: a name the scripts or the battle data do not define.
* `vehicle 'raft' is not one of the battle data's vehicles`, `vehicle 'skiff' has no field rules in game.json's vehicles, so a script cannot give or place it`, `vehicle 'skiff' is put in area 'town', which is not a field area`: field vehicles name a vehicle of `party.json`, and scripts put them only in field areas.
* `port 'pier' lands at marker 'dock', which does not exist in this area`, `only a town has an arrival marker`: travel data names markers of its own area.
* `inline text in say in event 'cave_door': player-facing text must be a string ID (@id) from the string tables` and `the name "Bell Warden" is typed out`: the scripts' strict check. Every line is a string ID, and glossary names are written as name tokens.
* `string ID 'cave.old_line' is not used by any script or by the game`: a dialogue string left over, or one language has a key another lacks. The strict check treats warnings as problems.

The scripted playthrough, the bot runs and the acceptance checks then test behavior on top of loading.

## Pictures and sounds

The data formats above hold no pictures or sounds. The MonoGame sample generates them and reads files in their place from an asset directory, keyed by the ids this data defines (area kinds and ids, actors, characters, vehicles, enemies, encounters, statuses, abilities and items, cameras, languages, `presentation.audio`, `presentation.animations` and `presentation.portraits`). [`assets.md`](assets.md) describes its manifest and the battle presentation table; `Psiv.Sample --check-assets --assets DIR` checks a directory against the data the way `game_cli --check` checks the data, and `--export-placeholders DIR` writes a complete starting set. After bringing in new placeholder content, run `make test` and `make acceptance`; `make acceptance` also checks that every character the text tables use is in the placeholder glyph lists (`game/data/text/glyphs_<code>.txt`).
