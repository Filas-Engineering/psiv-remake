"""The event library's text files (events/FORMAT.md): the name glossary and string tables."""

import re
from collections import Counter
from dataclasses import dataclass, replace
from pathlib import Path

from . import codes, glossary

GLOSSARY_HEADER = "# id | shipped name | restored name | reason"


class FormatError(Exception):
	def __init__(self, problems):
		super().__init__(f"{len(problems)} problem(s)")
		self.problems = problems


def shared(entries):
	"""Entries the event glossary lists: renamed names. Menu labels are written as literal text, not tokens."""
	return [e for e in entries if e.renamed and e.category != "menu"]


def write_glossary(path, entries, source_name):
	problems = []
	lines = [f"# Name glossary exported from {source_name} by tools/text glossary export-events.", GLOSSARY_HEADER]
	for e in shared(entries):
		fields = [e.id, e.shipped, e.restored] + ([e.reason] if e.reason.strip() else [])
		for f in fields:
			if re.search(r"[|\r\n]", f):
				problems.append(f"{e.id}: {f!r} contains '|' or a line break, which the event glossary format cannot hold")
		lines.append(" | ".join(f.strip() for f in fields))
	if problems:
		raise FormatError(problems)
	Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")
	return len(lines) - 2


@dataclass
class EventEntry:
	id: str
	shipped: str
	restored: str
	reason: str
	line: int


def read_glossary(path):
	path = Path(path)
	if not path.is_file():
		raise FormatError([f"{path}: no such file"])
	problems = []
	entries = []
	seen = {}
	for n, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
		line = raw.strip()
		if not line or line.startswith("#"):
			continue
		fields = [f.strip() for f in line.split("|")]
		where = f"{path.name} line {n}"
		if len(fields) not in (3, 4):
			problems.append(f"{where}: expected 'id | shipped name | restored name | reason', got {len(fields)} field(s)")
			continue
		entry_id, shipped, restored = fields[:3]
		if not codes.NAME_ID_RE.match(entry_id):
			problems.append(f"{where}: id {entry_id!r} is not a valid name")
			continue
		if not shipped or not restored:
			problems.append(f"{where}: {entry_id} has an empty name")
			continue
		if entry_id in seen:
			problems.append(f"{where}: duplicate id {entry_id} (also line {seen[entry_id]})")
			continue
		seen[entry_id] = n
		entries.append(EventEntry(entry_id, shipped, restored, fields[3] if len(fields) == 4 else "", n))
	if problems:
		raise FormatError(problems)
	return entries


def merge(base, imported, category=None, drop_missing=False):
	"""Fold an event glossary into the CSV glossary entries.

	Returns (entries, notes, problems). A changed shipped or restored name
	clears the sign-off. An empty reason keeps the CSV reason. Category,
	pronunciation and sign-off exist only in the CSV.
	"""
	notes = []
	problems = []
	incoming = {e.id: e for e in imported}
	expected = {e.id for e in shared(base)}
	out = []
	for e in base:
		new = incoming.get(e.id)
		if new is None:
			if e.id in expected:
				if drop_missing:
					notes.append(f"{e.id}: not in the event glossary; removed")
					continue
				problems.append(f"{e.id}: renamed in the CSV glossary but not in the event glossary (pass --drop-missing to remove it)")
			out.append(e)
			continue
		updated = replace(e, shipped=new.shipped, restored=new.restored, reason=new.reason or e.reason)
		if (updated.shipped, updated.restored) != (e.shipped, e.restored):
			updated.signoff = ""
			notes.append(f"{e.id}: {e.shipped} -> {e.restored} is now {new.shipped} -> {new.restored}; sign-off cleared")
		elif updated.reason != e.reason:
			notes.append(f"{e.id}: reason updated")
		out.append(updated)
	known = {e.id for e in base}
	for new in imported:
		if new.id in known:
			continue
		if category is None:
			problems.append(f"{new.id}: new entry needs a category (pass --category)")
			continue
		out.append(glossary.Entry(id=new.id, category=category, shipped=new.shipped, restored=new.restored, reason=new.reason))
		notes.append(f"{new.id}: added as {category}, not signed off")
	for i, e in enumerate(out, 2):
		e.line = i
	return out, notes, problems


def string_id(message_id, prefix=""):
	tree, index = message_id.rsplit(":", 1)
	return f"{prefix}{tree}.{index}"


def write_strings(path, lang, items, registry, source_name, prefix=""):
	"""Write one string table. items is a list of (message ID, text).

	Line breaks become \\n and name tokens stay as they are. Other control
	codes have no meaning in the event format; they are kept verbatim and
	counted so the caller can report them. Returns that Counter.
	"""
	problems = []
	lines = [f"# String table exported from {source_name} by tools/text export-strings.", f"language {lang}", ""]
	undefined = Counter()
	for message_id, text in items:
		sid = string_id(message_id, prefix)
		if not codes.NAME_ID_RE.match(sid):
			problems.append(f"{message_id}: string ID {sid!r} is not a valid name (letters, digits, '_' and '.', not starting with a digit)")
			continue
		if re.search(r"[\r\n]", text):
			problems.append(f"{message_id}: raw line break in the text; the string table format has only \\n")
			continue
		out = []
		pos = 0
		for t in codes.scan(text)[0]:
			out.append(text[pos:t.start].replace("\\", "\\\\"))
			if registry.shows(t.name) == "\n":
				out.append("\\n")
			else:
				out.append(text[t.start:t.end])
				if t.name != "name":
					undefined[t.name] += 1
			pos = t.end
		out.append(text[pos:].replace("\\", "\\\\"))
		value = "".join(out)
		if value != value.strip():
			problems.append(f"{message_id}: leading or trailing spaces, which the string table format drops")
			continue
		lines.append(f"{sid} = {value}")
	if problems:
		raise FormatError(problems)
	Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")
	return undefined
