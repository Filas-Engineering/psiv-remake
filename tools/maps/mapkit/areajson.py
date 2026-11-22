"""The game's area format (game/README.md, "Areas"): field order, normalizing and the canonical text form."""

import json

TOP_FIELDS = ["id", "kind", "encounter_mode", "step_ms", "music", "region", "arrival", "dungeon_exit", "arrive_notice", "field_status", "name", "map", "markers", "exits", "hooks", "shops", "rests",
	"chests", "ports", "zones", "helms", "gangway", "changes", "conveyors", "counters", "platforms"]
LIST_FIELDS = ["exits", "hooks", "shops", "rests", "chests", "ports", "zones", "helms", "gangway", "changes", "conveyors"]
ENTRY_FIELDS = {
	"exits": ["at", "to", "marker", "fall", "require_flag", "block_flag", "closed_tile", "sound"],
	"hooks": ["id", "at", "trigger", "event", "require_flag", "block_flag", "once", "tile", "solid", "actor_at", "across", "cells", "actor"],
	"shops": ["id", "at", "kind", "stock", "sell_percent", "require_flag", "block_flag", "keeper", "voice"],
	"rests": ["id", "at", "cost", "keeper", "per_member", "voice"],
	"chests": ["id", "at", "flag", "items", "equipment", "money"],
	"ports": ["id", "at", "marker", "flag", "ship", "block_flag"],
	"zones": ["id", "from", "to", "table", "visible", "random"],
	"helms": ["id", "at"],
	"changes": ["flag", "tile", "cells"],
	"conveyors": ["cells", "direction", "step_ms", "require_flag", "block_flag"],
}
DUNGEON_EXIT_FIELDS = ["area", "at", "marker", "facing"]
STOCK_FIELDS = ["id", "require_flag", "block_flag"]
TABLE_FIELDS = ["encounter", "weight"]
VISIBLE_FIELDS = ["spawns", "cap", "detection_radius", "leash_radius", "chase_speed_percent", "chase_ms", "wander_speed_percent", "wander_percent"]
RANDOM_FIELDS = ["min_steps", "max_steps", "step_distance", "grace_steps", "step_chance", "vehicle_step_chance", "safe_near_exits", "grace_after_battle"]
CELL_CHARACTERS = {".": "floor", "#": "wall", "~": "water", ":": "soft", "%": "barrier", "=": "counter"}


class AreaError(Exception):
	"""Every problem found in an area file, one per line."""

	def __init__(self, problems):
		super().__init__("\n".join(problems))
		self.problems = list(problems)


def _ordered(value, order, where, problems):
	if not isinstance(value, dict):
		problems.append(f"{where} must be an object")
		return {}
	for key in value:
		if key not in order:
			problems.append(f"{where}: unknown field '{key}'")
	return {key: value[key] for key in order if key in value}


def normalize(area):
	"""The area with its fields in the format's order and empty lists dropped, which is what two area files that
	describe the same area have in common. Raises AreaError for a field the format does not have."""
	problems = []
	out = _ordered(area, TOP_FIELDS, "the area", problems)
	if isinstance(out.get("markers"), dict):
		out["markers"] = dict(out["markers"])
	if "dungeon_exit" in out:
		out["dungeon_exit"] = _ordered(out["dungeon_exit"], DUNGEON_EXIT_FIELDS, "dungeon_exit", problems)
	for field in LIST_FIELDS:
		if field not in out:
			continue
		entries = out[field]
		if not isinstance(entries, list):
			problems.append(f"'{field}' must be a list")
			continue
		if not entries:
			del out[field]
			continue
		if field == "gangway":
			continue
		normalized = []
		for i, entry in enumerate(entries):
			where = f"{field}[{i}]"
			e = _ordered(entry, ENTRY_FIELDS[field], where, problems)
			if field == "shops" and isinstance(e.get("stock"), list):
				e["stock"] = [_ordered(s, STOCK_FIELDS, f"{where}.stock[{j}]", problems) if isinstance(s, dict) else s for j, s in enumerate(e["stock"])]
			if field == "zones":
				if isinstance(e.get("table"), list):
					e["table"] = [_ordered(t, TABLE_FIELDS, f"{where}.table", problems) for t in e["table"]]
				if "visible" in e:
					e["visible"] = _ordered(e["visible"], VISIBLE_FIELDS, f"{where}.visible", problems)
				if "random" in e:
					e["random"] = _ordered(e["random"], RANDOM_FIELDS, f"{where}.random", problems)
			normalized.append(e)
		out[field] = normalized
	if problems:
		raise AreaError(problems)
	return out


def _compact(value):
	return json.dumps(value, ensure_ascii=False, separators=(", ", ": "))


def dumps(area):
	"""The canonical text of an area: tabs, fields in the format's order, one map row, marker or list entry per
	line, zones one field per line. tiled_to_area writes this, so `make -C game maps` output is stable."""
	area = normalize(area)
	lines = ["{"]
	fields = list(area.items())
	for n, (key, value) in enumerate(fields):
		comma = "," if n + 1 < len(fields) else ""
		if key == "map":
			lines.append('\t"map": [')
			lines.extend(f"\t\t{_compact(row)}" + ("," if i + 1 < len(value) else "") for i, row in enumerate(value))
			lines.append("\t]" + comma)
		elif key == "markers":
			if not value:
				lines.append('\t"markers": {}' + comma)
				continue
			lines.append('\t"markers": {')
			items = list(value.items())
			lines.extend(f"\t\t{_compact(name)}: {_compact(cell)}" + ("," if i + 1 < len(items) else "") for i, (name, cell) in enumerate(items))
			lines.append("\t}" + comma)
		elif key == "zones":
			lines.append('\t"zones": [')
			for i, zone in enumerate(value):
				lines.append("\t\t{")
				zone_fields = list(zone.items())
				lines.extend(f"\t\t\t{_compact(k)}: {_compact(v)}" + ("," if j + 1 < len(zone_fields) else "") for j, (k, v) in enumerate(zone_fields))
				lines.append("\t\t}" + ("," if i + 1 < len(value) else ""))
			lines.append("\t]" + comma)
		elif key in LIST_FIELDS:
			lines.append(f'\t"{key}": [')
			lines.extend(f"\t\t{_compact(entry)}" + ("," if i + 1 < len(value) else "") for i, entry in enumerate(value))
			lines.append("\t]" + comma)
		else:
			lines.append(f"\t{_compact(key)}: {_compact(value)}{comma}")
	lines.append("}")
	return "\n".join(lines) + "\n"


def same(a, b):
	"""True when two areas are the same once normalized: object key order, whitespace and empty lists aside,
	every value matches, list order included."""
	return normalize(a) == normalize(b)
