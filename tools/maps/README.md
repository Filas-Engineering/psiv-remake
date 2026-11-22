# Map tools

Convert between [Tiled](https://www.mapeditor.org/) maps and the game's area files ([`game/README.md`, "Areas"](../../game/README.md#areas)), so areas can be drawn in Tiled and existing areas opened there.

Python 3.10 or later, standard library only.

```bash
python3 -I tools/maps/tiled_to_area.py MAP.tmj [-o AREA.json]        # Tiled map to area file (default: standard output)
python3 -I tools/maps/tiled_to_area.py MAP.tmj --check AREA.json     # exit 1 unless AREA.json is exactly what MAP.tmj converts to
python3 -I tools/maps/area_to_tiled.py AREA.json -o MAP.tmj [--tileset FILE.tsj | --embed] [--tile-size 16]
python3 -I tools/maps/make_tileset.py DIR                            # writes DIR/cells.tsj and DIR/cells.png
python3 -I -m unittest discover -s tools/maps/tests
```

Every problem in a map is printed as `MAP.tmj: text`, all of them in one run, and nothing is written. Exit codes: 0 done, 1 a problem, 2 bad arguments. The converter checks the Tiled side only; load the result with `game/build/game_cli --check game/data` to check it against the rest of the game (markers on floor, exits to areas that exist, events, items, reachability).

`area_to_tiled.py` refers the map to `game/data/maps/cells.tsj` by default, by a path relative to the map. `--embed` puts the tileset in the map instead.

## In this repo

`game/data/maps/` holds the Tiled sources of the areas drawn there (`saltgrass.tmj`, `kiln_upper.tmj`, `kiln_lower.tmj`), the cell tileset `cells.tsj` and its image `cells.png`. `make -C game maps` regenerates `game/data/areas/NAME.json` from each `NAME.tmj`; `make -C game test` fails when a checked-in area file is stale. The older areas (`tidewater`, `reed_path`, `bell_cove`) are kept as area files; `area_to_tiled.py` opens them in Tiled.

## Map requirements

* Orthogonal and finite (Map Properties: Orientation Orthogonal, Infinite unchecked). Any tile size.
* Tile layer data in CSV or uncompressed base64 (Map Properties, Tile Layer Format). Compressed layers (zlib, gzip, zstd) are refused.
* Tilesets embedded in the map or saved as external JSON tilesets (`.tsj`). XML tilesets (`.tsx`) are refused: save them as JSON.
* Only top-level layers named `terrain` and `objects` are read. Other layers (decoration, image layers, groups) are ignored, so they can hold anything.

## Mapping

### Map properties

| Custom property | Type | Area field |
| --- | --- | --- |
| `id` | string, required | `id` |
| `kind` | string | `kind` (`town`, `field`, `dungeon`, `vessel`) |
| `encounter_mode` | string | `encounter_mode` (`visible`, `random`) |
| `step_ms` | int or float | `step_ms` (a float keeps a fraction of a millisecond) |
| `music` | string | `music` (an audio id from `game.json`) |
| `region` | string | `region` (the world or region the area belongs to) |
| `arrival` | string | `arrival` (a town's marker where a return to it lands) |
| `arrival_from`, `arrival_not_from` | string holding a JSON list | `arrival_from`, `arrival_not_from` (the areas entering from which registers the town, or does not: `["reed_path"]`) |
| `dungeon_exit` | string holding a JSON object | `dungeon_exit` (a dungeon's way out: `{"area": "saltgrass", "marker": "east_gate", "facing": "west"}`, or `at` `[x, y]` in place of `marker`) |
| `arrive_notice` | bool, or string holding a JSON list or object | `arrive_notice` (false: entering the area announces nothing; `[{"from": ["reed_path"], "notice": false}, {"notice": true}]` by where the party came from) |
| `field_status`, `field_poison`, `field_paralysis` | bool | the same keys (field poison and paralysis by steps here) |
| `counters` | string holding a JSON list | `counters` (cells marked as counters besides the `=` tiles: `[[4, 2], [5, 2]]`) |

A property left out is left out of the area, so the game's default applies. Any other map property is refused.

### Layer `terrain`

A tile layer the size of the map. Each tile's tileset entry carries a custom string property `kind`: `floor`, `wall`, `water`, `soft` (soft ground), `barrier` or `counter`, the game's six cell kinds, written `.`, `#`, `~`, `:`, `%` and `=` in the area's `map` rows. Any tileset works as long as its tiles have a `kind`; `cells.tsj` has one tile per kind, in that order (tile ids 0 to 5), so a new kind is appended and older maps keep their tiles. Flip and rotation bits are ignored. An empty cell, a tile without `kind` or an unknown kind is refused, with the first cell it happens at.

### Layer `objects`

An object layer. Each object's class (Tiled's Class field, `type` in older versions) says what it is, its name is the id, and its custom properties carry the area format's fields. Objects are points or rectangles; ellipses, polygons, polylines, text, tile objects, rotated objects and objects that still use a template are refused. A point or rectangle stands on the cell under its center (x and y divided by the tile size, rounded down).

| Class | Name | Properties | Area entry |
| --- | --- | --- | --- |
| `marker` | the marker name, unique | none | `markers[name]: [x, y]` |
| `exit` | ignored | `to` (string), `marker` (string), optional `fall` (bool), `require_flag`, `block_flag`, `closed_tile`, `sound`, `fires` (strings) | `exits[]: {at, to, marker, fall, require_flag, block_flag, closed_tile, sound, fires}` |
| `hook` | the hook id | `event` (string), optional `trigger`, `require_flag`, `block_flag` (strings), `actor_at` (JSON list `[x, y]`, the cell its actor stands on), `across` (bool), `cells` (JSON list of `[x, y]`, further cells it covers), `actor` (string, a `touch` hook's actor; such a hook has no cell, like `load` and `flag`) | `hooks[]` |
| `shop` | the shop id | `stock` (JSON list), optional `kind`, `require_flag`, `block_flag` (strings), `sell_percent` (int), `script_only` (bool), `keeper` (string, the actor who keeps it), `voice` (string, the keeper's text key prefix), `name` (string, a text key) | `shops[]` |
| `rest` | the rest point id | optional `cost` (int), `script_only` (bool), `keeper` (string), `per_member` (bool), `voice`, `name` (strings) | `rests[]` |
| `chest` | the chest id | `flag` (string), optional `items` and `equipment` (JSON objects), `money` (int) | `chests[]` |
| `port` | the port id | `marker` (string), optional `flag`, `ship` (strings) | `ports[]` |
| `zone` | the zone id | `table` (JSON list), optional `visible`, `random` (JSON objects) | `zones[]` |
| `helm` | the helm id | none | `helms[]: {id, at}` |
| `gangway` | ignored | none | `gangway[]: [x, y]`, one object per cell |
| `change` | ignored | `flag` (string), `tile` (string, one map character), `cells` (JSON list of cells) | `changes[]: {flag, tile, cells}` |

* "JSON" properties are string properties holding JSON text in the area format, for example a shop's stock `["tonic", {"id": "spark_flask", "require_flag": "fells.bridge_mended"}]`, a chest's items `{"tonic": 2}` or a zone's table `[{"encounter": "training", "weight": 2}]`. Spawn points inside `visible` are cells, `[[4, 2], [8, 6]]`.
* A hook whose `trigger` is `load`, `flag` or `touch` has no cell: its position is ignored, and `area_to_tiled.py` puts it in a column just left of the map.
* `script_only: true` on a shop or rest point leaves out `at` (one reached only from a script), and the position is ignored the same way.
* A change's cells are in its `cells` property, `[[7, 4], [8, 4]]`, since they need not touch; its position is ignored the same way.
* A zone is a rectangle; its `from` and `to` are the first and last cell it covers. Edges that do not fall on the grid widen it to whole cells.
* Property types are checked (an int property for `money`, a bool for `script_only`), and an unknown property or class is refused, so a typo cannot drop a field.
* Entries keep the order of the objects in the layer, so list order (which zone wins, which hook is first) is the drawing order. Markers keep it too.

## Round trip and the canonical form

Area to map to area gives back the same area. "The same" means equal once normalized: object keys in any order, any whitespace, and an empty optional list (`"hooks": []`) the same as none; every value, and the order of every list, must match. `mapkit.areajson.same()` compares that way and `mapkit.areajson.dumps()` writes the canonical text: tabs, fields in the order of the area format, one map row, marker or list entry per line (a gangway cell is an entry), a zone one field per line. `tiled_to_area.py` always writes the canonical text, so regenerating an unchanged map changes nothing. The tests convert every file in `game/data/areas` to a map and back and require both equality and identical canonical text.
