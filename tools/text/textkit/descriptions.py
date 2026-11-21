"""Descriptions in the game's text tables (UI-007): the optional keys desc.<kind>.<id>.

A description is optional, so an id without one is only counted. A description key whose kind is not one of KINDS,
or whose id the battle data does not have, describes nothing and is a problem, as the game's loader says too.
"""

import json
from dataclasses import dataclass, field
from pathlib import Path

PREFIX = "desc."
# Kind, then the battle data file and its list that holds the ids.
KINDS = {
	"item": ("items.json", "items"),
	"equipment": ("equipment.json", "equipment"),
	"ability": ("abilities.json", "abilities"),
	"status": ("statuses.json", "statuses"),
	"enemy": ("enemies.json", "enemies"),
	"difficulty": ("difficulties.json", "difficulties"),
}


class DescriptionError(Exception):
	pass


def core_ids(core_dir):
	"""Every id per kind in the battle data."""
	ids = {}
	for kind, (file, key) in KINDS.items():
		path = Path(core_dir) / file
		try:
			entries = json.loads(path.read_text(encoding="utf-8"))[key]
		except (OSError, ValueError, KeyError, TypeError) as e:
			raise DescriptionError(f"cannot read the {kind} ids from {path}: {e}")
		ids[kind] = [entry["id"] for entry in entries]
	return ids


@dataclass
class TableReport:
	path: str
	described: dict = field(default_factory=dict)
	undescribed: dict = field(default_factory=dict)
	unknown: list = field(default_factory=list)


def check(table_paths, core_dir):
	ids = core_ids(core_dir)
	reports = []
	for path in table_paths:
		try:
			table = json.loads(Path(path).read_text(encoding="utf-8"))["text"]
		except (OSError, ValueError, KeyError, TypeError) as e:
			raise DescriptionError(f"cannot read {path}: expected an object with a 'text' object ({e})")
		report = TableReport(str(path))
		keys = {k for k in table if k.startswith(PREFIX)}
		for kind, known in ids.items():
			report.described[kind] = sum(1 for i in known if f"{PREFIX}{kind}.{i}" in keys)
			report.undescribed[kind] = [i for i in known if f"{PREFIX}{kind}.{i}" not in keys]
		for key in sorted(keys):
			kind, _, thing = key[len(PREFIX):].partition(".")
			if kind not in ids or thing not in ids[kind]:
				report.unknown.append(key)
		reports.append(report)
	return reports


def render(reports):
	lines = []
	for r in reports:
		counts = ", ".join(f"{kind} {r.described[kind]} of {r.described[kind] + len(r.undescribed[kind])}" for kind in KINDS)
		lines.append(f"{r.path}: {counts}")
		for kind in KINDS:
			if r.undescribed[kind]:
				lines.append(f"  no description ({kind}): {', '.join(r.undescribed[kind])}")
		for key in r.unknown:
			lines.append(f"  problem: '{key}' describes nothing: desc.<{'|'.join(KINDS)}>.<id> with an id the battle data has")
	problems = sum(len(r.unknown) for r in reports)
	lines.append(f"{problems} problem(s)")
	return "\n".join(lines) + "\n"
