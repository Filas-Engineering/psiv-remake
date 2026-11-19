import csv
from dataclasses import dataclass, field
from pathlib import Path

FIELDS = ("text", "speaker", "kind", "map", "trigger", "context", "limit", "branches")


@dataclass
class Diff:
	added: list = field(default_factory=list)
	removed: list = field(default_factory=list)
	moved: list = field(default_factory=list)
	changed: list = field(default_factory=list)
	same: int = 0


def compare(old, new):
	"""Compare two validated deliveries by message ID.

	A removed and an added message with the same, unique text are reported as
	moved, since regional builds sometimes renumber a tree.
	"""
	old_by_id = {m.id: m for m in old}
	new_by_id = {m.id: m for m in new}
	result = Diff()
	for message_id, m in old_by_id.items():
		other = new_by_id.get(message_id)
		if other is None:
			result.removed.append(m)
			continue
		fields = [(f, getattr(m, f), getattr(other, f)) for f in FIELDS if getattr(m, f) != getattr(other, f)]
		if fields:
			result.changed.append((m, other, fields))
		else:
			result.same += 1
	result.added = [m for i, m in new_by_id.items() if i not in old_by_id]

	def unique_texts(messages):
		counts = {}
		for m in messages:
			counts.setdefault(m.text, []).append(m)
		return {t: ms[0] for t, ms in counts.items() if len(ms) == 1}

	removed_texts = unique_texts(result.removed)
	added_texts = unique_texts(result.added)
	for text in removed_texts.keys() & added_texts.keys():
		result.moved.append((removed_texts[text], added_texts[text]))
	moved_old = {a.id for a, _ in result.moved}
	moved_new = {b.id for _, b in result.moved}
	result.removed = [m for m in result.removed if m.id not in moved_old]
	result.added = [m for m in result.added if m.id not in moved_new]
	result.moved.sort(key=lambda p: p[0].id)
	return result


def render(diff, old_name, new_name):
	out = [
		f"Script diff: {old_name} -> {new_name}",
		f"  unchanged {diff.same}  changed {len(diff.changed)}  added {len(diff.added)}"
		f"  removed {len(diff.removed)}  moved {len(diff.moved)}",
	]
	if diff.changed:
		out += ["", "Changed"]
		for old, _, fields in diff.changed:
			out.append(f"  {old.id}")
			for name, a, b in fields:
				out.append(f"    {name}: {a!r}")
				out.append(f"    {' ' * len(name)}  -> {b!r}")
	if diff.added:
		out += ["", "Added"]
		out += [f"  {m.id} [{m.speaker or '-'}] {m.text!r}" for m in diff.added]
	if diff.removed:
		out += ["", "Removed"]
		out += [f"  {m.id} [{m.speaker or '-'}] {m.text!r}" for m in diff.removed]
	if diff.moved:
		out += ["", "Moved (same text, new ID)"]
		out += [f"  {a.id} -> {b.id} {a.text!r}" for a, b in diff.moved]
	return "\n".join(out) + "\n"


def write_csv(path, diff):
	with Path(path).open("w", encoding="utf-8", newline="") as f:
		writer = csv.writer(f, lineterminator="\n")
		writer.writerow(("status", "old_id", "new_id", "field", "old", "new"))
		for old, new, fields in diff.changed:
			for name, a, b in fields:
				writer.writerow(("changed", old.id, new.id, name, _cell(a), _cell(b)))
		for m in diff.added:
			writer.writerow(("added", "", m.id, "text", "", m.text))
		for m in diff.removed:
			writer.writerow(("removed", m.id, "", "text", m.text, ""))
		for a, b in diff.moved:
			writer.writerow(("moved", a.id, b.id, "text", a.text, b.text))


def _cell(value):
	if value is None:
		return ""
	if isinstance(value, list):
		return " ".join(value)
	return str(value)
