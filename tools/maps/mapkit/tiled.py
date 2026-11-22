"""Tiled JSON maps (.tmj) to the game's area format and back. The mapping is in ../README.md."""

import base64
import json
import math
import struct
from pathlib import Path

from . import areajson

KINDS = {"floor": ".", "wall": "#", "water": "~", "soft": ":", "barrier": "%", "counter": "="}
KIND_OF_CHARACTER = {c: k for k, c in KINDS.items()}
TILE_IDS = {"floor": 0, "wall": 1, "water": 2, "soft": 3, "barrier": 4, "counter": 5}
FLIP_BITS = 0xF0000000
MAP_PROPERTIES = {"id": "string", "kind": "string", "encounter_mode": "string", "step_ms": "number", "music": "string", "region": "string", "arrival": "string",
	"dungeon_exit": "json object", "arrive_notice": "bool", "counters": "json list"}
# Map properties in the area format's order, as the importer copies them and the exporter writes them.
AREA_PROPERTIES = ("kind", "encounter_mode", "step_ms", "music", "region", "arrival", "dungeon_exit", "arrive_notice", "counters")
REQUIRED_MAP_PROPERTIES = ["id"]

# Per object class: property name to type. "json" is a string property holding JSON text of the given shape.
OBJECT_PROPERTIES = {
	"marker": {},
	"exit": {"to": "string", "marker": "string", "fall": "bool", "require_flag": "string", "block_flag": "string", "closed_tile": "string"},
	"hook": {"trigger": "string", "event": "string", "require_flag": "string", "block_flag": "string", "once": "bool", "tile": "string", "solid": "string",
		"actor_at": "json list", "across": "bool", "cells": "json list", "actor": "string"},
	"shop": {"kind": "string", "stock": "json list", "sell_percent": "int", "require_flag": "string", "block_flag": "string", "script_only": "bool", "keeper": "string", "voice": "string"},
	"rest": {"cost": "int", "script_only": "bool", "keeper": "string", "per_member": "bool", "voice": "string"},
	"chest": {"flag": "string", "items": "json object", "equipment": "json object", "money": "int"},
	"port": {"marker": "string", "flag": "string", "ship": "string", "block_flag": "string"},
	"zone": {"table": "json list", "visible": "json object", "random": "json object"},
	"helm": {},
	"gangway": {},
	"change": {"flag": "string", "tile": "string", "cells": "json list"},
	"conveyor": {"cells": "json list", "direction": "string", "step_ms": "number", "require_flag": "string", "block_flag": "string"},
}
REQUIRED_PROPERTIES = {"exit": ["to", "marker"], "hook": ["event"], "shop": ["stock"], "chest": ["flag"], "port": ["marker"], "zone": ["table"],
	"change": ["flag", "tile", "cells"], "conveyor": ["cells", "direction"]}
NAMED_CLASSES = {"marker", "hook", "shop", "rest", "chest", "port", "zone", "helm"}
# Hooks with these triggers have no cell; their object's position is ignored.
CELL_FREE_TRIGGERS = {"load", "flag", "touch"}
CLASS_OF_LIST = {"exits": "exit", "hooks": "hook", "shops": "shop", "rests": "rest", "chests": "chest", "ports": "port", "zones": "zone", "helms": "helm",
	"gangway": "gangway", "changes": "change", "conveyors": "conveyor"}
TILED_VERSION = "1.10.2"
FORMAT_VERSION = "1.10"


class MapError(Exception):
	"""Every reason a map cannot be converted, one per line."""

	def __init__(self, problems):
		super().__init__("\n".join(problems))
		self.problems = list(problems)


def tileset(image="cells.png", tile_size=16):
	"""The cell tileset: one tile per cell kind, each with a string property `kind`."""
	return {
		"type": "tileset",
		"version": FORMAT_VERSION,
		"tiledversion": TILED_VERSION,
		"name": "cells",
		"tilewidth": tile_size,
		"tileheight": tile_size,
		"tilecount": len(KINDS),
		"columns": len(KINDS),
		"margin": 0,
		"spacing": 0,
		"image": image,
		"imagewidth": tile_size * len(KINDS),
		"imageheight": tile_size,
		"tiles": [{"id": TILE_IDS[kind], "properties": [{"name": "kind", "type": "string", "value": kind}]} for kind in KINDS],
	}


# Reading a map.


def _properties(owner, where, problems):
	"""A Tiled property list as name to (type, value)."""
	props = owner.get("properties", [])
	if isinstance(props, dict):
		problems.append(f"{where}: properties are in the old Tiled format (an object); save the map with Tiled 1.2 or later")
		return {}
	if not isinstance(props, list):
		problems.append(f"{where}: 'properties' must be a list")
		return {}
	out = {}
	for p in props:
		if not isinstance(p, dict) or not isinstance(p.get("name"), str):
			problems.append(f"{where}: a property needs a name")
			continue
		if p["name"] in out:
			problems.append(f"{where}: property '{p['name']}' is set twice")
		out[p["name"]] = (p.get("type", "string"), p.get("value"))
	return out


def _typed(props, schema, where, problems):
	"""Checks each property against the schema and returns name to value, JSON text parsed."""
	out = {}
	for name, (kind, value) in props.items():
		if name not in schema:
			allowed = ", ".join(sorted(schema)) or "none"
			problems.append(f"{where}: unknown property '{name}' (allowed: {allowed})")
			continue
		want = schema[name]
		if want == "int":
			if kind != "int" or not isinstance(value, int) or isinstance(value, bool):
				problems.append(f"{where}: property '{name}' must be an int property")
				continue
		elif want == "number":
			# A whole number of milliseconds, or one with a fraction (step_ms).
			if kind not in ("int", "float") or not isinstance(value, (int, float)) or isinstance(value, bool):
				problems.append(f"{where}: property '{name}' must be an int or float property")
				continue
		elif want == "bool":
			if kind != "bool" or not isinstance(value, bool):
				problems.append(f"{where}: property '{name}' must be a bool property")
				continue
		else:
			if kind != "string" or not isinstance(value, str):
				problems.append(f"{where}: property '{name}' must be a string property")
				continue
			if want.startswith("json"):
				try:
					value = json.loads(value)
				except json.JSONDecodeError as e:
					problems.append(f"{where}: property '{name}' is not valid JSON text: {e.msg} at character {e.pos}")
					continue
				shape = want.split()[1]
				if (shape == "list") != isinstance(value, list) or (shape == "object") != isinstance(value, dict):
					problems.append(f"{where}: property '{name}' must hold a JSON {shape}")
					continue
			elif value == "":
				problems.append(f"{where}: property '{name}' is empty")
				continue
		out[name] = value
	return out


def _load_tilesets(tmj, base_dir, problems):
	"""(firstgid, tiles) per tileset, sorted by firstgid; tiles maps a tile id to its `kind` or None."""
	sets = []
	for i, ts in enumerate(tmj.get("tilesets") or []):
		if not isinstance(ts, dict) or not isinstance(ts.get("firstgid"), int):
			problems.append(f"tileset {i}: needs a firstgid")
			continue
		name = f"tileset {i}"
		data = ts
		if "source" in ts:
			source = ts["source"]
			name = f"tileset '{source}'"
			if not str(source).lower().endswith((".tsj", ".json")):
				problems.append(f"{name} is not a JSON tileset; save it as .tsj in Tiled (File > Export As, or Save As with the JSON format)")
				continue
			path = Path(base_dir) / source
			try:
				data = json.loads(path.read_text(encoding="utf-8"))
			except OSError:
				problems.append(f"{name}: cannot read {path}")
				continue
			except json.JSONDecodeError as e:
				problems.append(f"{name}: not valid JSON: {e.msg} at line {e.lineno}")
				continue
			if not isinstance(data, dict):
				problems.append(f"{name}: a tileset is a JSON object")
				continue
		tiles = {}
		for tile in data.get("tiles") or []:
			if not isinstance(tile, dict) or not isinstance(tile.get("id"), int):
				problems.append(f"{name}: a tile needs an id")
				continue
			props = _properties(tile, f"{name} tile {tile['id']}", problems)
			kind = props.get("kind")
			if kind is None:
				tiles[tile["id"]] = None
			elif kind[0] != "string" or not isinstance(kind[1], str):
				problems.append(f"{name} tile {tile['id']}: property 'kind' must be a string property")
			else:
				tiles[tile["id"]] = kind[1]
		sets.append((ts["firstgid"], name, tiles))
	sets.sort(key=lambda s: s[0])
	return sets


def _layer_data(layer, width, height, problems):
	if "chunks" in layer:
		problems.append("layer 'terrain' is stored in chunks, which only infinite maps use; uncheck Infinite in Map Properties")
		return None
	encoding = layer.get("encoding", "csv")
	compression = layer.get("compression", "")
	data = layer.get("data")
	if encoding == "csv":
		if not isinstance(data, list) or not all(isinstance(g, int) and not isinstance(g, bool) for g in data):
			problems.append("layer 'terrain': CSV data must be a list of tile numbers")
			return None
		gids = data
	elif encoding == "base64":
		if compression:
			problems.append(f"layer 'terrain' uses {compression} compression; set Tile Layer Format to CSV or Base64 (uncompressed) in Map Properties")
			return None
		try:
			raw = base64.b64decode(data, validate=True)
		except (TypeError, ValueError):
			problems.append("layer 'terrain': the base64 data does not decode")
			return None
		if len(raw) % 4:
			problems.append("layer 'terrain': the base64 data is not a whole number of tiles")
			return None
		gids = list(struct.unpack(f"<{len(raw) // 4}I", raw))
	else:
		problems.append(f"layer 'terrain' has encoding '{encoding}'; only CSV and uncompressed base64 are read")
		return None
	if len(gids) != width * height:
		problems.append(f"layer 'terrain' has {len(gids)} tiles, but the map is {width} by {height} ({width * height} tiles)")
		return None
	return gids


def _cell(obj, tw, th):
	x = obj.get("x", 0) + obj.get("width", 0) / 2
	y = obj.get("y", 0) + obj.get("height", 0) / 2
	return [math.floor(x / tw), math.floor(y / th)]


def tmj_to_area(tmj, base_dir="."):
	"""The area for a parsed .tmj map. External tilesets are read relative to base_dir. Raises MapError."""
	problems = []
	if not isinstance(tmj, dict):
		raise MapError(["a Tiled map is a JSON object"])
	if tmj.get("type", "map") != "map":
		problems.append(f"this is a Tiled '{tmj.get('type')}' file, not a map")
	if tmj.get("orientation") != "orthogonal":
		problems.append(f"the map is {tmj.get('orientation')}; only orthogonal maps are read")
	if tmj.get("infinite"):
		problems.append("the map is infinite; uncheck Infinite in Map Properties so it has a fixed size")
	width, height = tmj.get("width"), tmj.get("height")
	tw, th = tmj.get("tilewidth"), tmj.get("tileheight")
	for name, value in (("width", width), ("height", height), ("tilewidth", tw), ("tileheight", th)):
		if not isinstance(value, int) or value <= 0:
			problems.append(f"the map's {name} must be a positive whole number")
	if problems:
		raise MapError(problems)

	map_props = _typed(_properties(tmj, "the map", problems), MAP_PROPERTIES, "the map", problems)
	for name in REQUIRED_MAP_PROPERTIES:
		if name not in map_props and not any(p.startswith(f"the map: property '{name}'") for p in problems):
			problems.append(f"the map needs a string property '{name}'")

	layers = [layer for layer in tmj.get("layers") or [] if isinstance(layer, dict)]
	terrain = [layer for layer in layers if layer.get("name") == "terrain"]
	objects = [layer for layer in layers if layer.get("name") == "objects"]
	if not terrain:
		problems.append("the map has no tile layer named 'terrain'")
	if len(terrain) > 1 or len(objects) > 1:
		problems.append("the map has more than one layer named 'terrain' or 'objects'")

	rows = []
	if len(terrain) == 1:
		layer = terrain[0]
		if layer.get("type") != "tilelayer":
			problems.append(f"layer 'terrain' is a {layer.get('type')}; it must be a tile layer")
		else:
			if layer.get("width", width) != width or layer.get("height", height) != height:
				problems.append("layer 'terrain' is not the size of the map")
			gids = _layer_data(layer, width, height, problems)
			sets = _load_tilesets(tmj, base_dir, problems)
			if gids is not None:
				rows = _rows(gids, width, sets, problems)

	area = {"id": map_props.get("id", "")}
	for name in AREA_PROPERTIES:
		if name in map_props:
			area[name] = map_props[name]
	area["map"] = rows
	area["markers"] = {}
	lists = {field: [] for field in areajson.LIST_FIELDS}

	if len(objects) == 1:
		layer = objects[0]
		if layer.get("type") != "objectgroup":
			problems.append(f"layer 'objects' is a {layer.get('type')}; it must be an object layer")
		else:
			for obj in layer.get("objects") or []:
				_read_object(obj, tw, th, area, lists, problems)
	for field in areajson.LIST_FIELDS:
		if lists[field]:
			area[field] = lists[field]
	if problems:
		raise MapError(problems)
	return area


def _rows(gids, width, sets, problems):
	unknown = {}
	rows = []
	for y in range(len(gids) // width):
		row = []
		for x in range(width):
			gid = gids[y * width + x] & ~FLIP_BITS
			kind = None
			reason = None
			if gid == 0:
				reason = "has no tile"
			else:
				owner = None
				for first, name, tiles in sets:
					if first <= gid:
						owner = (first, name, tiles)
				if owner is None:
					reason = f"has tile {gid}, which no tileset holds"
				else:
					first, name, tiles = owner
					kind = tiles.get(gid - first)
					if kind is None:
						reason = f"has tile {gid - first} of {name}, which has no 'kind' property"
					elif kind not in KINDS:
						reason = f"has tile {gid - first} of {name}, whose kind '{kind}' is not one of {', '.join(KINDS)}"
						kind = None
			if reason:
				unknown.setdefault(reason, []).append((x, y))
				row.append("#")
			else:
				row.append(KINDS[kind])
		rows.append("".join(row))
	for reason, cells in unknown.items():
		more = f" and {len(cells) - 1} other cell{'s' if len(cells) > 2 else ''}" if len(cells) > 1 else ""
		problems.append(f"layer 'terrain': cell [{cells[0][0]}, {cells[0][1]}]{more} {reason}")
	return rows


def _read_object(obj, tw, th, area, lists, problems):
	if not isinstance(obj, dict):
		problems.append("layer 'objects': an object must be a JSON object")
		return
	cls = obj.get("type") or obj.get("class") or ""
	name = obj.get("name", "")
	where = f"object {obj.get('id', '?')}" + (f" ('{name}')" if name else "")
	if "template" in obj:
		problems.append(f"{where} uses a template; detach it (Detach Template) so its fields are in the map")
		return
	if cls not in OBJECT_PROPERTIES:
		problems.append(f"{where} has class '{cls}'; use one of {', '.join(OBJECT_PROPERTIES)}")
		return
	where = f"{where}, {'an' if cls[0] in 'aeiou' else 'a'} {cls}"
	if "gid" in obj:
		problems.append(f"{where} is a tile object; draw it as a point or a rectangle")
		return
	for shape in ("ellipse", "polygon", "polyline", "text"):
		if shape in obj and obj[shape] is not False:
			problems.append(f"{where} is a {shape}; draw it as a point or a rectangle")
			return
	if obj.get("rotation", 0):
		problems.append(f"{where} is rotated; set its rotation to 0")
		return
	props = _typed(_properties(obj, where, problems), OBJECT_PROPERTIES[cls], where, problems)
	missing = [p for p in REQUIRED_PROPERTIES.get(cls, []) if p not in props and not any(m.startswith(f"{where}: property '{p}'") for m in problems)]
	for p in missing:
		problems.append(f"{where} needs the property '{p}'")
	if missing:
		return
	if cls in NAMED_CLASSES and not name:
		problems.append(f"{where} needs a name: it is the {'marker name' if cls == 'marker' else 'id'}")
		return

	if cls == "zone":
		w, h = obj.get("width", 0), obj.get("height", 0)
		if obj.get("point") or w <= 0 or h <= 0:
			problems.append(f"{where} must be a rectangle covering its cells")
			return
		x, y = obj.get("x", 0), obj.get("y", 0)
		zone = {"id": name, "from": [math.floor(x / tw), math.floor(y / th)], "to": [math.ceil((x + w) / tw) - 1, math.ceil((y + h) / th) - 1]}
		zone.update(props)
		lists["zones"].append(zone)
		return
	if cls == "change":
		if props["tile"] not in areajson.CELL_CHARACTERS:
			problems.append(f"{where}: property 'tile' must be one map character: {', '.join(areajson.CELL_CHARACTERS)}")
			return
		lists["changes"].append({"flag": props["flag"], "tile": props["tile"], "cells": props["cells"]})
		return
	if cls == "conveyor":
		lists["conveyors"].append(props)
		return
	cell = _cell(obj, tw, th)
	if cls == "marker":
		if name in area["markers"]:
			problems.append(f"{where}: marker '{name}' is defined twice")
		area["markers"][name] = cell
	elif cls == "exit":
		entry = {"at": cell}
		entry.update(props)
		lists["exits"].append(entry)
	elif cls == "hook":
		hook = {"id": name}
		if props.get("trigger") not in CELL_FREE_TRIGGERS and "tile" not in props:
			hook["at"] = cell
		hook.update(props)
		lists["hooks"].append(hook)
	elif cls in ("shop", "rest"):
		entry = {"id": name}
		if not props.pop("script_only", False):
			entry["at"] = cell
		entry.update(props)
		lists["shops" if cls == "shop" else "rests"].append(entry)
	elif cls == "chest":
		chest = {"id": name, "at": cell}
		chest.update(props)
		lists["chests"].append(chest)
	elif cls == "port":
		port = {"id": name, "at": cell}
		port.update(props)
		lists["ports"].append(port)
	elif cls == "helm":
		lists["helms"].append({"id": name, "at": cell})
	elif cls == "gangway":
		lists["gangway"].append(cell)


def load_tmj(path):
	"""The area for a .tmj file. Raises MapError (OSError and bad JSON become MapError too)."""
	path = Path(path)
	try:
		tmj = json.loads(path.read_text(encoding="utf-8"))
	except OSError as e:
		raise MapError([f"cannot read {path}: {e.strerror}"]) from None
	except json.JSONDecodeError as e:
		raise MapError([f"{path} is not valid JSON: {e.msg} at line {e.lineno}"]) from None
	return tmj_to_area(tmj, path.parent)


# Writing a map.


def _prop(name, value):
	if isinstance(value, bool):
		return {"name": name, "type": "bool", "value": value}
	if isinstance(value, int):
		return {"name": name, "type": "int", "value": value}
	if isinstance(value, float):
		return {"name": name, "type": "float", "value": value}
	if isinstance(value, (list, dict)):
		return {"name": name, "type": "string", "value": json.dumps(value, ensure_ascii=False, separators=(", ", ": "))}
	return {"name": name, "type": "string", "value": value}


def area_to_tmj(area, tileset_ref, tile_size=16):
	"""A .tmj map for an area. tileset_ref is the path of an external .tsj relative to the map, or a tileset
	object to embed. Raises areajson.AreaError for an area the format does not allow."""
	area = areajson.normalize(area)
	problems = []
	rows = area.get("map")
	if not isinstance(rows, list) or not rows or not all(isinstance(r, str) for r in rows):
		problems.append("'map' must be a list of rows")
		rows = []
	elif any(len(r) != len(rows[0]) for r in rows):
		problems.append("every map row must have the same width")
	elif any(c not in KIND_OF_CHARACTER for r in rows for c in r):
		problems.append("map rows use " + ", ".join(f"'{c}' for {k}" for k, c in KINDS.items()))
	if not isinstance(area.get("id"), str) or not area.get("id"):
		problems.append("the area needs an id")
	if problems:
		raise areajson.AreaError(problems)
	width, height = len(rows[0]), len(rows)
	t = tile_size

	data = [TILE_IDS[KIND_OF_CHARACTER[c]] + 1 for r in rows for c in r]
	objects = []
	off_map = 0

	def add(cls, name, props, cell=None, rect=None):
		nonlocal off_map
		obj = {"id": len(objects) + 1, "name": name, "type": cls}
		if rect is not None:
			(fx, fy), (tx, ty) = rect
			obj.update({"x": fx * t, "y": fy * t, "width": (tx - fx + 1) * t, "height": (ty - fy + 1) * t})
		else:
			if cell is None:
				# Objects without a cell stand in a column left of the map; the importer ignores their position.
				cell = [-1, off_map]
				off_map += 1
			x, y = (cell[0] + 0.5) * t, (cell[1] + 0.5) * t
			obj.update({"point": True, "x": int(x) if x.is_integer() else x, "y": int(y) if y.is_integer() else y, "width": 0, "height": 0})
		obj.update({"rotation": 0, "visible": True})
		if props:
			obj["properties"] = [_prop(k, v) for k, v in props.items()]
		objects.append(obj)

	def is_cell(value):
		return isinstance(value, list) and len(value) == 2 and all(isinstance(v, int) for v in value)

	def cell_of(entry, where):
		at = entry.get("at")
		if not is_cell(at):
			problems.append(f"{where}: 'at' must be [x, y]")
			return [0, 0]
		return at

	for name, cell in area.get("markers", {}).items():
		add("marker", name, {}, cell=cell)
	for i, e in enumerate(area.get("exits", [])):
		add("exit", "", {k: v for k, v in e.items() if k != "at"}, cell=cell_of(e, f"exits[{i}]"))
	for i, h in enumerate(area.get("hooks", [])):
		props = {k: v for k, v in h.items() if k not in ("id", "at")}
		if h.get("trigger") in CELL_FREE_TRIGGERS or "tile" in h:
			add("hook", h.get("id", ""), props)
		else:
			add("hook", h.get("id", ""), props, cell=cell_of(h, f"hooks[{i}]"))
	for field in ("shops", "rests"):
		for i, s in enumerate(area.get(field, [])):
			props = {k: v for k, v in s.items() if k not in ("id", "at")}
			if "at" in s:
				add(CLASS_OF_LIST[field], s.get("id", ""), props, cell=cell_of(s, f"{field}[{i}]"))
			else:
				props["script_only"] = True
				add(CLASS_OF_LIST[field], s.get("id", ""), props)
	for i, c in enumerate(area.get("chests", [])):
		add("chest", c.get("id", ""), {k: v for k, v in c.items() if k not in ("id", "at")}, cell=cell_of(c, f"chests[{i}]"))
	for i, p in enumerate(area.get("ports", [])):
		add("port", p.get("id", ""), {k: v for k, v in p.items() if k not in ("id", "at")}, cell=cell_of(p, f"ports[{i}]"))
	for i, z in enumerate(area.get("zones", [])):
		corners = [z.get("from"), z.get("to")]
		if not all(is_cell(v) for v in corners):
			problems.append(f"zones[{i}]: 'from' and 'to' must be [x, y]")
			continue
		add("zone", z.get("id", ""), {k: v for k, v in z.items() if k not in ("id", "from", "to")}, rect=corners)
	for i, h in enumerate(area.get("helms", [])):
		add("helm", h.get("id", ""), {}, cell=cell_of(h, f"helms[{i}]"))
	for i, cell in enumerate(area.get("gangway", [])):
		if not is_cell(cell):
			problems.append(f"gangway[{i}] must be [x, y]")
			continue
		add("gangway", "", {}, cell=cell)
	for c in area.get("changes", []):
		add("change", "", dict(c))
	for c in area.get("conveyors", []):
		add("conveyor", "", dict(c))
	if problems:
		raise areajson.AreaError(problems)

	props = [_prop(k, area[k]) for k in ("id",) + AREA_PROPERTIES if k in area]
	if isinstance(tileset_ref, dict):
		tilesets = [dict(tileset_ref, firstgid=1)]
		tilesets[0].pop("type", None)
		tilesets[0].pop("version", None)
		tilesets[0].pop("tiledversion", None)
	else:
		tilesets = [{"firstgid": 1, "source": tileset_ref}]
	return {
		"type": "map",
		"version": FORMAT_VERSION,
		"tiledversion": TILED_VERSION,
		"orientation": "orthogonal",
		"renderorder": "right-down",
		"infinite": False,
		"compressionlevel": -1,
		"width": width,
		"height": height,
		"tilewidth": t,
		"tileheight": t,
		"nextlayerid": 3,
		"nextobjectid": len(objects) + 1,
		"properties": props,
		"tilesets": tilesets,
		"layers": [
			{"id": 1, "name": "terrain", "type": "tilelayer", "x": 0, "y": 0, "width": width, "height": height, "opacity": 1, "visible": True, "data": data},
			{"id": 2, "name": "objects", "type": "objectgroup", "draworder": "index", "x": 0, "y": 0, "opacity": 1, "visible": True, "objects": objects},
		],
	}


def dumps_tmj(tmj):
	"""JSON text for a map or tileset: tabs, the terrain one map row per line, one object, property or tile per line."""

	def compact(value):
		return json.dumps(value, ensure_ascii=False, separators=(",", ":"))

	def dump(value, indent, key=None, width=None):
		pad = "\t" * indent
		inner = "\t" * (indent + 1)
		if isinstance(value, dict):
			if not value:
				return "{}"
			parts = []
			for k, v in value.items():
				w = value.get("width") if k == "data" else None
				parts.append(f"{inner}{json.dumps(k)}: {dump(v, indent + 1, k, w)}")
			return "{\n" + ",\n".join(parts) + "\n" + pad + "}"
		if isinstance(value, list):
			if not value:
				return "[]"
			if key == "data" and width:
				rows = [",".join(str(g) for g in value[i:i + width]) for i in range(0, len(value), width)]
				return "[\n" + ",\n".join(inner + r for r in rows) + "\n" + pad + "]"
			if key == "layers":
				return "[\n" + ",\n".join(inner + dump(v, indent + 1) for v in value) + "\n" + pad + "]"
			if key in ("objects", "properties", "tiles", "tilesets"):
				return "[\n" + ",\n".join(inner + compact(v) for v in value) + "\n" + pad + "]"
			return compact(value)
		return compact(value)

	return dump(tmj, 0) + "\n"
