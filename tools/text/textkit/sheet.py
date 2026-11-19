import csv
import hashlib
from dataclasses import dataclass
from pathlib import Path

from . import codes, mentions, names

COLUMNS = (
	"change_id", "kind", "message_id", "speaker", "map", "start", "old", "new",
	"before", "after", "after_shipped", "after_restored", "reason", "note", "base", "approve",
)
KINDS = ("rename", "typo")
APPROVE_VALUES = {"": False, "no": False, "yes": True}


@dataclass
class Row:
	change_id: str
	kind: str
	message_id: str
	start: int
	old: str
	new: str
	reason: str
	base: str
	approved: bool = False
	speaker: str = ""
	map: str = ""
	before: str = ""
	after: str = ""
	after_shipped: str = ""
	after_restored: str = ""
	note: str = ""
	line: int = 0

	def show_names(self, by_id):
		"""Fill the two views of the line: with shipped names and with restored names."""
		line = self.after or self.before
		self.after_shipped = names.render(line, by_id, "shipped")[0]
		self.after_restored = names.render(line, by_id, "restored")[0]


class SheetError(Exception):
	def __init__(self, problems):
		super().__init__(f"{len(problems)} problem(s) in the review sheet")
		self.problems = problems


def text_hash(text):
	return hashlib.sha256(text.encode("utf-8")).hexdigest()[:16]


def file_digest(path):
	return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def proposal(change_id, kind, message, start, old, new, reason, note=""):
	text = message["current"]
	after = text[:start] + new + text[start + len(old):] if new else ""
	return Row(
		change_id=change_id,
		kind=kind,
		message_id=message["id"],
		speaker=message["speaker"] or "",
		map=message["map"] or "",
		start=start,
		old=old,
		new=new,
		before=text,
		after=after,
		reason=reason,
		note=note,
		base=text_hash(text),
	)


def write(path, rows):
	with Path(path).open("w", encoding="utf-8", newline="") as f:
		writer = csv.writer(f, lineterminator="\n")
		writer.writerow(COLUMNS)
		for r in rows:
			writer.writerow([
				r.change_id, r.kind, r.message_id, r.speaker, r.map, r.start, r.old, r.new,
				r.before, r.after, r.after_shipped, r.after_restored, r.reason, r.note, r.base,
				"yes" if r.approved else "",
			])


def read(path):
	path = Path(path)
	if not path.is_file():
		raise SheetError([f"{path}: no such sheet"])
	problems = []
	rows = []
	with path.open(encoding="utf-8", newline="") as f:
		reader = csv.DictReader(f)
		missing = [c for c in COLUMNS if c not in (reader.fieldnames or [])]
		if missing:
			raise SheetError([f"{path}: missing columns {', '.join(missing)}"])
		seen = set()
		for raw in reader:
			where = f"{path.name} line {reader.line_num}"
			if None in raw:
				problems.append(f"{where}: more cells than columns")
				continue
			approve = (raw["approve"] or "").strip().lower()
			if approve not in APPROVE_VALUES:
				problems.append(f"{where}: approve must be yes, no or empty, got {raw['approve']!r}")
				continue
			if raw["kind"] not in KINDS:
				problems.append(f"{where}: kind must be rename or typo, got {raw['kind']!r}")
				continue
			try:
				start = int(raw["start"])
			except ValueError:
				problems.append(f"{where}: start must be an integer, got {raw['start']!r}")
				continue
			if raw["change_id"] in seen or not raw["change_id"]:
				problems.append(f"{where}: change_id {raw['change_id']!r} is empty or repeated")
				continue
			seen.add(raw["change_id"])
			rows.append(Row(
				change_id=raw["change_id"],
				kind=raw["kind"],
				message_id=raw["message_id"],
				start=start,
				old=raw["old"],
				new=raw["new"],
				reason=raw["reason"],
				base=raw["base"],
				approved=APPROVE_VALUES[approve],
				speaker=raw["speaker"],
				map=raw["map"],
				before=raw["before"],
				after=raw["after"],
				after_shipped=raw["after_shipped"],
				after_restored=raw["after_restored"],
				note=raw["note"],
				line=reader.line_num,
			))
	if problems:
		raise SheetError(problems)
	return rows


class RenameCheck:
	"""Confirms a rename row is exactly a glossary substitution of a real mention."""

	def __init__(self, entries, registry):
		self.compiled = mentions.compile_names(entries)
		self.registry = registry

	def __call__(self, text, row):
		for m in mentions.find(text, self.registry, self.compiled):
			if m.start != row.start or m.text != row.old:
				continue
			if not m.entry.renamed:
				return f"{m.entry.shipped!r} is not renamed in the glossary"
			if not m.entry.signoff.strip():
				return f"glossary entry {m.entry.id} ({m.entry.shipped!r}) has no sign-off"
			expected, why = mentions.substitute(m)
			if expected is None:
				return why
			if row.new != expected:
				return f"new text {row.new!r} is not the glossary substitution {expected!r}"
			return None
		return f"{row.old!r} at {row.start} is not a glossary mention"


def replay(texts, rows, rename_check, registry):
	"""Apply the approved rows of one sheet to texts.

	texts maps message ID to text and is not modified. Returns (new_texts,
	applied_rows, problems); new_texts is only meaningful when problems is empty.
	"""
	problems = []
	by_message = {}
	for row in rows:
		if row.approved:
			by_message.setdefault(row.message_id, []).append(row)
	out = dict(texts)
	applied = []
	for message_id, group in by_message.items():
		def bad(row, msg):
			problems.append(f"{row.change_id} ({message_id}): {msg}")

		if message_id not in texts:
			for row in group:
				bad(row, "no such message")
			continue
		text = texts[message_id]
		group.sort(key=lambda r: r.start)
		ok = True
		for row in group:
			if row.base != text_hash(text):
				bad(row, "the line has changed since this sheet was proposed (base mismatch)")
				ok = False
			elif not row.old or text[row.start:row.start + len(row.old)] != row.old or row.start < 0:
				bad(row, f"old text {row.old!r} is not at position {row.start}")
				ok = False
			elif not row.new:
				# A deletion is written with context ("the the" -> "the"), so a
				# blank cell is always a reviewer who forgot to fill it in.
				bad(row, "approved with no new text")
				ok = False
			elif row.new == row.old:
				bad(row, "new text is the same as old text")
				ok = False
			elif row.kind == "rename":
				problem = rename_check(text, row)
				if problem:
					bad(row, problem)
					ok = False
			else:
				if not row.reason.strip():
					bad(row, "typo fix has no reason")
					ok = False
				_, new_problems = codes.problems_in(row.new, registry)
				if new_problems:
					bad(row, "; ".join(new_problems))
					ok = False
				elif [t.text for t in codes.scan(row.old)[0]] != [t.text for t in codes.scan(row.new)[0]]:
					bad(row, "a typo fix may not add, remove or reorder control codes")
					ok = False
		for a, b in zip(group, group[1:]):
			if b.start < a.start + len(a.old):
				bad(b, f"overlaps {a.change_id}; approve only one of them")
				ok = False
		if not ok:
			continue
		for row in sorted(group, key=lambda r: r.start, reverse=True):
			text = text[:row.start] + row.new + text[row.start + len(row.old):]
		out[message_id] = text
		applied.extend(group)
	return out, applied, problems
