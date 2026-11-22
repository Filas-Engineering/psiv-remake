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

Optional rule switches, each defaulting to the placeholder's rule: `affinity_divisor` (what affinities are divided by: 100 for percents, 4 for the original's factors), `magic_defense` (elemental damage subtracts the `magic_defense` stat in place of mental) `instant_defeat_kinds` (party member kinds that instant defeat can fell), `neutral_affinity` (the affinity a hit without an element takes; the divisor by default, so 100 at a divisor of 200 halves plain hits), `defend_affinity` (a defender's affinity for a hit without an element; a quarter of the divisor by default) `defend_elemental` (Defend also lowers an elemental hit's affinity to at most `defend_affinity`; off by default, as the research reads Defend), `defend_elements` (the elements Defend reaches, `none` for hits without one; in place of `defend_elemental`), `effectiveness_baseline` (what an elemental hit's affinity is compared with to label it weak, resisted or immune; the divisor by default; damage unchanged), `element_baselines` (a built-in element's own baseline; a declared element writes `{"id": ..., "normal": N}` in `elements` instead of a plain id) and `damage_variance` (a percent and a `uniform` or `bell` distribution that replace the original's spread on every damage roll; left out, the original's spread). Reference: [rules.json](../core/README.md#rulesjson).

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

Characters (base stats, growth or per-level tables of stats and experience (`level_stats`, `experience_table`; past a table's end the growth and the curve go on), basic attack, abilities known and learned, what they can wear and start in, `vulnerable` to enemy instant defeat), vehicles, the starting lineup, inventory and spare equipment, and the macros (an item step may name an equipment piece with a battle use with `"piece": true`). Reference: [party.json](../core/README.md#partyjson) and [Vehicle battles](../core/README.md#vehicle-battles).

### enemies.json

Enemies (stats with an optional `magic_defense`, abilities, the eight AI slots, overrides, affinities, rewards, `flying` for the renderer, `next_form` and `next_form_at` for a form taken at a percent of hit points) and encounters (enemy groups in formation order, escape and opening numbers, drops). Reference: [enemies.json](../core/README.md#enemiesjson).

### combos.json

Combination recipes, compared in data order, and `enemy_combos`, the enemies' joint attacks. Reference: [combos.json](../core/README.md#combosjson).

### difficulties.json

Difficulty overlays that scale enemies, abilities and rewards. Reference: [difficulties.json](../core/README.md#difficultiesjson).

### text_en.json

Core's English message templates for battle and field results. The loader requires a template for every key core can emit with your data. Reference: [text_en.json](../core/README.md#text_enjson).

### This round's options

Every one is optional; data without them loads and plays as before. Core: on `apply_status`, a `modify_stat` with direction `down` and `instant_defeat`, the roll keys `user_stat` and `target_stat` (stat names; not `user_stat` on items or combinations), `user_power` (0 to 999, not with `user_stat`; an item's or a weapon attack's own power), `element` (the target's affinity scales the factor; 0 is immunity) and `always_lands` (no roll; not with `threshold` or the other roll keys); a weapon's `stats.attack` (weapons only) and the stat name `attack` (strength plus the weapons' attack bonuses, what basic attacks now hit with) for `attack_stat`, `user_stat` and `target_stat`. To bring the original's rolls in: `user_stat` for a skill's named stat, `user_power` for an item's power field or a weapon attack's own fields, `target_stat` for the resistance stat, `always_lands` where the resistance stat is 0, and `user_stat`/`target_stat` `strength` with an `element` on an enemy basic attack's secondary effect. Reference: [abilities.json](../core/README.md#abilitiesjson), [equipment.json](../core/README.md#equipmentjson). `rules.json` `macro_ally_pick` (`original`, the default, or `would_change`: a macro's one-ally picks pass by members the effect would not change, [rules.json](../core/README.md#rulesjson)).

Game: `game.json` `actors` entries may be objects: `id`, `sprite` (`{"enemy": id}` draws the actor with a field enemy's figure, which never fights, or `{"look": name}` names one the engine knows), a home without a talk point (`area` and `at`, a marker of that area or a cell `[x, y]`, with `facing`) and `solid` (default true: the cell blocks walking while the actor is shown). `presentation.animations` entries may be objects with `id` and `seconds`; the standard animations (`fade_in`, `fade_out`, `dissolve`, `blink_out`, `hop`, `shake`) need not be listed. An interact hook takes `actor_at`, the cell its actor stands on apart from the hook's own (a keeper behind a counter); the data check treats that cell as solid but not as a point, so it must not cut off anything else, and needs no floor beside it. Scripts gain `place`, `move ... via ... pathfind`, `face <actor> <direction>` and `remove <actor> [until reload]`. To bring the original's people in: an NPC drawn with a monster's figure becomes an object actor with `sprite.enemy` and a home; a shopkeeper behind a counter becomes a talk point on the counter cell with `actor_at` on the keeper's cell; a figure that disappears or is taken out of the map by an event becomes a standard animation and `remove` (for good when the original keeps it gone, `until reload` when it is back once the map loads again). Reference: [game/README.md](../game/README.md#actors), [events/FORMAT.md](../events/FORMAT.md#actors). Scripts may use `party save [NAME]`, `party restore [NAME]` and `party swap <slot|member> <member> [equipment keep|bag]` ([FORMAT.md](../events/FORMAT.md#game-state)).

Interaction (each optional, today's rule the default; [game/README.md](../game/README.md#interaction)): `game.json` `interact` (`faced` or `nearby`: when the faced cell gives nothing, the first neighbor that answers, the leader turning to it, and an actor answering on the cell it stands on), `interact_across` (every point is used across a counter, a cell that is not floor and holds nothing), `run_speed_percent` (running, with the run input or the profile's `always_run`) and `vehicle_opening` (`roll` or `normal`; a vehicle's own `opening` wins). In area files: an interact hook's `across: true` and `cells` (further cells it covers, each solid), and an exit's `closed_tile` (`wall`, `normal` or a map character: how a closed exit is drawn; it still blocks). To bring the original's maps in: a shopkeeper or clerk standing behind a counter whose hook sits on the person rather than on the counter becomes a hook with `across` (or turn on `interact_across` for the whole game); a table or bed two cells wide that the party can use from either side becomes one hook with `cells`; a door that is drawn shut until the story opens it becomes an exit with `require_flag` and `closed_tile: "normal"` (the engine draws its door over the floor). The check judges reach by the rules in effect: a hook boxed in behind a counter is reported unless it has `across` or the game rule is on, and a talk point with `actor_at` counts as reached from beside its actor's cell under `nearby`. `tools/maps` reads and writes the new fields.

Background events and wanderers (each optional; [game/README.md](../game/README.md#background-events) and [events/FORMAT.md](../events/FORMAT.md#background-events)): an event marked `background` (in place of `skippable`/`unskippable`) runs beside the field when a hook fires it or a script `spawn`s it, and may hold only actor commands, `look`, `wait`, `sync`, `sound`, flags, variables, conditions, loops, `call` of other background events and `spawn` (the compiler reports anything else at its line); `game.json` `background_limit` (1 to 32, default 4). A `game.json` actor object with a home may `wander`: `radius` (1 to 99) or `from` and `to` (a rectangle holding its home), `speed_percent` (default 50), `pause_ms` (`[least, most]`, default `[1000, 3000]`) and `terrain` (map characters, default `"."`). A hook with `trigger: "touch"` and `actor` (an actor of `game.json`; no `at`) fires when the leader walks into that actor, the actor wanders into the leader, or the leader interacts with it. Shops and rest points take `keeper` (an actor of `game.json`). Scripts may write `look <actor> enemy <enemy>|<look>|default`, `show|hide <actor> with <animation>`, and address a visible enemy as `enemy.<zone>.<n>`. The check reports `data.wander_radius_or_region`, `data.wander_needs_home`, `data.wander_home_outside`, `data.bad_wander_pause`, `data.bad_wander_terrain`, `data.touch_unknown_actor`, `data.actor_touch_hooks_only` and `data.keeper_not_actor`; `tools/maps` carries `actor` and `keeper` through Tiled.

Later this round (each optional, today's behavior the default; [core/README.md](../core/README.md)): a `heal` power and an item's `elemental_damage` power may be 0, as may a piece's `use_power`, and base `strength`, `mental`, `agility` and `dexterity` (characters, `level_stats`, enemies, vehicles) may be 0; hit points stay at least 1 (core/README.md, "Zero powers and stats", lists every clamp left). `rules.json` `auto_policy` (`power`, the default, or `expected`: automatic choices weigh each attack by its expected effect, instant defeats against vulnerable foes included) and an ability's `auto` (`prefer`, `avoid` or `never`) steer auto-battle, a vehicle's own choice and the automatic macro fallback. An effect roll that names `user_stat: "strength"` reads plain strength, without the weapons' `stats.attack`; only `user_stat: "attack"` adds it. A data macro step's `member` may be a vehicle id (its skills, `attack` or `escape`; no member target), used only in battles aboard that vehicle.

### The round before's options

Every one is optional; data without them loads and plays as before. Core: `rules.json` `defend_timing` (`round`, `turn`), `immune_min_damage`, `critical_bonus` (`quarter`, `low_byte`), `damage_wraparound`, `escape_party_first_round`, `target_weights` (by party size, weights in 256ths that add up to 256) and `stat_wraparound` (stat names); damage effects' `stat_permille` (in place of `stat_percent`, not both); items' `stat_value` on heal and damage effects; a macro step's `"piece": true`. Reference: [rules.json](../core/README.md#rulesjson), [abilities.json](../core/README.md#abilitiesjson), [items.json](../core/README.md#itemsjson), [party.json](../core/README.md#partyjson).

### Two rounds before's options

Every one is optional; data without them loads and plays as before. Core: `rules.json` `defend_elements` and `damage_variance`; abilities' `variance`; damage effects' `stat_percent` and a negative `power` (abilities only; items stay flat); `"element": "weapon"` on `physical_damage` (abilities only); the effect `remove_beside` (`rewards`, enemies); equipment `use_power` (needs `use`), a `use` that transforms another combatant, and `{"set": N}` in `affinities` (a set wins over scaling, the lowest set wins); AI overrides with `all` or `any` in place of `when`, and `count` with `compare` on `ally_present`. Game: zone `random` `grace_after_battle` (needs `grace_steps`) and `game.json` `grace_after_battle`. The data check now treats an interact hook with `solid: "while_active"` that its flags or its actor can switch off as passable, so a converter does not need to move such a hook off a path to pass the check. `weapon` can no longer be declared as an element id. Field references: [core/README.md](../core/README.md#data-formats), [game/README.md](../game/README.md#areas).

### Three rounds before's options

Every one is optional; data without them loads and plays as before. Core: `rules.json` `ambush_opening` and `elements` (name each declared element as `name.element.<id>` in every language's game text table, or `game_cli --check` reports `data.element_name_missing`); abilities' `attack_stat` (also the stat a combination component adds) and a stat change's `power` (and the stat `magic_defense`); a `physical_damage` effect's `element`; a `transform` effect aimed at another combatant, also on items (only a `self` transform needs the user's `transforms_into`); effects `transform`, `split` (`count`) and `self_destruct` (`rewards`), which need the enemy's `transforms_into` and `splits_into`; enemies' `merges_with` and `attack_element`; the override `after_turns` (`turns`) and `first_turn`; enemies' `keeps_ambush` (needs `next_form` or `transforms_into`); `null` for a free place in an encounter's `enemies` (at least one place must hold an enemy); a drop's `equipment` in place of `item` (an id that is not also an item); items' `consumed`; equipment's `use_from_bag` (needs `use`) and `affinities`; characters' `teachable` (abilities events teach, which recipes then count), `level_stats` (in place of `base`) and `experience_table` (rising totals). Game: exits' `require_flag`/`block_flag`, ports' `block_flag`, hooks' `once`, `tile` and `solid`, `conveyors`, zone `random` `grace_steps`, `step_chance`, `vehicle_step_chance`, `safe_near_exits`, and `game.json` `nameless_actors` (no `speaker.<id>` string for those). Area files: `tools/maps` reads and writes the new area fields, and its tests require every area file in the canonical form `mapkit.areajson.dumps` writes; `python3 -I tools/maps/tiled_to_area.py` and `area_to_tiled.py` keep them. Sand is soft ground (`:`).

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

* Music outside scripts: `presentation.music` with an optional `title`, `victory` (a won battle's result), `defeat` (a game over), `encounters` (encounter id to track), `vehicles` (a `vehicles` id to the track of random battles fought aboard it; an encounter's own track wins) and `areas` (area id to a track, or to a list of `{"flag": ..., "track": ...}` entries with an optional plain track last as the default: the first entry whose flag is set gives the area's track in place of its own `music`, read again when a listed flag changes in the area; a script's `music area` still wins until the party leaves). Every track must also be in `presentation.audio`, every flag declared, every vehicle in `vehicles` and every area in `areas`. For example `"areas": {"tidewater": [{"flag": "cove.warden_beaten", "track": "town_tide_bells"}, "field_marsh_reeds"]}`.

  ```json
  "music": {"title": "title_harbor_dawn", "victory": "victory_bright_sail", "defeat": "defeat_low_tide", "encounters": {"warden": "battle_brass_warden"}}
  ```

* Action durations: `presentation.durations`, the table `event_cli time` reads from its timing configuration, so the game reports each presentation action's authored seconds to the engine. `action` names an action as timing files do, `subject` (optional) one actor, camera, panel or track, `seconds` 0 to 60.

  ```json
  "durations": [{"action": "panel_show", "seconds": 0.35}, {"action": "move", "subject": "dockhand", "seconds": 0.9}]
  ```

* Credits: `credits`, by id, each with an optional `music` (an audio id) and `entries` in order, a heading or a line as a key of the text tables (every language), with optional `gap` (empty lines before it) and `pause_ms` (how long it holds, default 2000). The last story event plays them with `credits <id>` and ends the game with `end game`, which writes the clear-data record and goes back to the title; `clear_flag` can stay for a story that writes the record earlier. Credits are not part of the content hash, so a staff list can change without touching saves.

  ```json
  "credits": {"ending": {"music": "staff_roll", "entries": [{"heading": "credits.ending.title", "pause_ms": 3000}, {"line": "credits.ending.director", "gap": 1}]}}
  ```

### Areas

One JSON file per area: the cell grid, markers, exits, hooks, shops, rest points, chests and encounter zones, and an optional `music` track (`"music": "town_tide_bells"`, an id from `presentation.audio`; the Tiled map property of the same name). Reference: [Areas](../game/README.md#areas). Draw them in Tiled and convert with [tools/maps](../tools/maps/README.md), or write the JSON directly. The display name is the text key `area.<id>`. An interact hook may stand on a wall or water cell (a sign, a ladder): the party uses it from a floor cell beside it, which the check makes sure it can reach. Give a person's interact hook the id of its actor in `game.json`'s `actors`: the actor then stands on that cell until a script moves it (see [Actors](../game/README.md#actors)).

Travel data lives in areas too: `region` groups areas into a world or region (a return to a town reaches the visited towns of the party's region), a town's `arrival` names the marker a return lands on, and `ports` are the transport between regions (a port stands on any cell like an interact hook, lands travellers on its `marker`, and opens once its `flag` is set). A dungeon area may name its own way out for the dungeon-exit item and technique, `"dungeon_exit": {"area": "saltgrass", "marker": "east_gate", "facing": "west"}` (or `at` `[x, y]` in place of `marker`; `facing` optional), where the original sends the party somewhere other than the door it came in by; without it the party goes back to where it stood before going in. The Tiled converter carries `region`, `arrival` and `dungeon_exit` as map properties and ports as objects of class `port`. A port with a `ship` puts the party aboard a `vessel` area (its `arrival` marker, `helms`, `gangway`).

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
* `the exit to 'cave' at [12, 0] cannot be walked to from any way into the area`, `chest 'box' has no floor cell beside it that the party can walk to`: a wall or a solid object cuts it off from every marker an exit, a warp or the start leads to. An exit cell beside a chest does not count: stepping on it leaves the area.
* `exit leads to area 'cave', which no file in game.json's areas defines`, `exit leads to missing marker 'cave west_gate'`: the target area is not listed in `game.json`, or has no such marker.
* `no exit, script warp or start position leads into area 'cave'`, `area 'cave' cannot be reached through exits from the start area 'town'`: an area that is listed but not wired in.
* `flag 'cave.opened' is not declared`, `flag 'chest.box' is declared twice`: every flag a script, hook or journal entry uses is listed in `game.json`'s `flags`, except chest flags, which their chests declare.
* `shop 'stall' stocks 'potion', which is unknown or has no price`: everything a shop stocks needs a price in `game.json`.
* `text 'area.cave' is not in the text table`, `key 'area.cave' of the 'en' table is missing`: every area needs a display name, in every language.
* `string 'speaker.smith' is not in the dialogue table`: every actor needs a speaker name string.
* `'desc.item.potion' describes nothing`: a description for an id the battle data does not have, or of an unknown kind (`desc.weapon.x`). Rename it to the id, or drop it.
* `the camera looks at marker 'gate' of area 'cave', which does not exist`, `the camera follows 'smith', which is no actor or character`, `camera 'x' names a marker or an area without the other`: a camera's target names something the data lacks.
* `music 'cave_theme' is not one of game.json's presentation audio ids`, `music is given for encounter 'bats', which the battle data does not have`: list every track in `presentation.audio`, and give music only to encounters that exist. `music is given for vehicle 'raft', which game.json's vehicles do not list` and `music is given for area 'cave', which the data does not have`: likewise for `music.vehicles` and `music.areas`. `music 'x' follows an entry without a flag, so it never plays`: put the plain track last in an area's list.
* `unknown action 'panel show'`: durations write actions with underscores (`panel_show`), as timing files do.
* `hook 'door' runs unknown event 'cave_door'`, `zone 'hall' lists unknown encounter 'bats'`: a name the scripts or the battle data do not define.
* `vehicle 'raft' is not one of the battle data's vehicles`, `vehicle 'skiff' has no field rules in game.json's vehicles, so a script cannot give or place it`, `vehicle 'skiff' is put in area 'town', which is not a field area`: field vehicles name a vehicle of `party.json`, and scripts put them only in field areas.
* `port 'pier' lands at marker 'dock', which does not exist in this area`, `only a town has an arrival marker`: travel data names markers of its own area.
* `the dungeon exit leads into 'kiln_lower', which is a dungeon too`, `the dungeon exit leads to (0,0) in area 'tidewater', which is not floor the party can stand on`, `the dungeon exit needs either at or marker`, `only a dungeon has a dungeon exit`: a `dungeon_exit` names an area outside any dungeon and one place in it.
* `inline text in say in event 'cave_door': player-facing text must be a string ID (@id) from the string tables` and `the name "Bell Warden" is typed out`: the scripts' strict check. Every line is a string ID, and glossary names are written as name tokens.
* `string ID 'cave.old_line' is not used by any script or by the game`: a dialogue string left over, or one language has a key another lacks. The strict check treats warnings as problems.

The scripted playthrough, the bot runs and the acceptance checks then test behavior on top of loading.

## Pictures and sounds

The data formats above hold no pictures or sounds. The MonoGame sample generates them and reads files in their place from an asset directory, keyed by the ids this data defines (area kinds and ids, actors, characters, vehicles, enemies, encounters, statuses, abilities and items, cameras, languages, `presentation.audio`, `presentation.animations` and `presentation.portraits`). [`assets.md`](assets.md) describes its manifest and the battle presentation table; `Psiv.Sample --check-assets --assets DIR` checks a directory against the data the way `game_cli --check` checks the data, and `--export-placeholders DIR` writes a complete starting set. After bringing in new placeholder content, run `make test` and `make acceptance`; `make acceptance` also checks that every character the text tables use is in the placeholder glyph lists (`game/data/text/glyphs_<code>.txt`).
