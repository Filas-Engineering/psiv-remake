import csv
import re
from dataclasses import dataclass
from pathlib import Path

from .codes import NAME_ID_RE

COLUMNS = ("id", "category", "shipped", "restored", "reason", "pronunciation", "signoff")
CATEGORIES = ("enemy", "item", "location", "menu", "skill", "technique")
# The shipped name tables cap names at 10 characters (scope, "Working with a cartridge dump").
SHIPPED_CAP = 10
DEFAULT_MAX_LENGTH = 24


@dataclass
class Entry:
	id: str
	category: str
	shipped: str
	restored: str
	reason: str = ""
	pronunciation: str = ""
	signoff: str = ""
	line: int = 0

	@property
	def renamed(self):
		return self.restored != self.shipped


class GlossaryError(Exception):
	def __init__(self, problems):
		super().__init__("; ".join(problems))
		self.problems = problems


def init(path):
	path = Path(path)
	if path.exists():
		raise GlossaryError([f"{path} already exists"])
	save(path, [])


def load(path):
	path = Path(path)
	if not path.is_file():
		raise GlossaryError([f"{path}: no such glossary"])
	with path.open(encoding="utf-8", newline="") as f:
		reader = csv.DictReader(f)
		header = reader.fieldnames or []
		missing = [c for c in COLUMNS if c not in header]
		unknown = [c for c in header if c not in COLUMNS]
		if missing or unknown:
			problems = []
			if missing:
				problems.append(f"{path}: missing columns {', '.join(missing)}")
			if unknown:
				problems.append(f"{path}: unknown columns {', '.join(unknown)}")
			raise GlossaryError(problems)
		entries = []
		for row in reader:
			if None in row:
				raise GlossaryError([f"{path} line {reader.line_num}: more cells than columns"])
			entries.append(Entry(**{c: row[c] or "" for c in COLUMNS}, line=reader.line_num))
		return entries


def save(path, entries):
	with Path(path).open("w", encoding="utf-8", newline="") as f:
		writer = csv.writer(f, lineterminator="\n")
		writer.writerow(COLUMNS)
		for e in entries:
			writer.writerow([getattr(e, c) for c in COLUMNS])


def validate(entries, max_length=DEFAULT_MAX_LENGTH):
	"""Return (errors, warnings) as lists of strings."""
	errors = []
	warnings = []

	def where(e):
		return f"line {e.line} ({e.id or e.shipped or '?'})"

	ids = {}
	for e in entries:
		if not e.id.strip():
			errors.append(f"{where(e)}: id is empty")
		elif not NAME_ID_RE.match(e.id):
			errors.append(
				f"{where(e)}: id {e.id!r} must be letters, digits, '_' and '.', not starting with a digit,"
				" so it can be a name token and an event script glossary ID"
			)
		elif e.id in ids:
			errors.append(f"{where(e)}: duplicate id {e.id} (also line {ids[e.id].line})")
		else:
			ids[e.id] = e
		if e.category not in CATEGORIES:
			errors.append(f"{where(e)}: category {e.category!r} is not one of {', '.join(CATEGORIES)}")
		for column in ("shipped", "restored"):
			value = getattr(e, column)
			if not value:
				errors.append(f"{where(e)}: {column} name is empty")
			elif value != value.strip() or "  " in value:
				errors.append(f"{where(e)}: {column} name {value!r} has leading, trailing or doubled spaces")
			elif re.search(r"[{}\n\t]", value):
				errors.append(f"{where(e)}: {column} name {value!r} contains braces or control characters")
		if e.renamed and not e.reason.strip():
			errors.append(f"{where(e)}: {e.shipped!r} is renamed to {e.restored!r} with no reason")

	by_shipped = {}
	for e in entries:
		key = e.shipped.casefold()
		if key not in by_shipped:
			by_shipped[key] = e
			continue
		first = by_shipped[key]
		if first.shipped == e.shipped:
			errors.append(f"{where(e)}: duplicate shipped name {e.shipped!r} (also {where(first)})")
		else:
			errors.append(
				f"{where(e)}: shipped name {e.shipped!r} differs only in case from {first.shipped!r}"
				f" ({where(first)}), so mentions cannot tell them apart"
			)

	by_restored = {}
	for e in entries:
		key = e.restored.casefold()
		other = by_restored.setdefault(key, e)
		if other is not e:
			errors.append(
				f"{where(e)}: shipped names {other.shipped!r} and {e.shipped!r} both restore to"
				f" {e.restored!r}, so the shipped-names setting cannot map it back"
			)

	for e in entries:
		other = by_shipped.get(e.restored.casefold())
		if other is not None and other is not e:
			errors.append(
				f"{where(e)}: restored name {e.restored!r} is the shipped name of {where(other)},"
				" so renamed dialogue would read as a shipped mention"
			)

	for e in entries:
		words = e.shipped.casefold().split(" ")
		for other in entries:
			if other is e:
				continue
			longer = other.shipped.casefold().split(" ")
			if len(longer) > len(words) and _contains(longer, words):
				warnings.append(
					f"{where(e)}: {e.shipped!r} is part of {other.shipped!r} ({where(other)});"
					" mentions take the longer name"
				)

	for e in entries:
		if len(e.shipped) > SHIPPED_CAP:
			warnings.append(
				f"{where(e)}: shipped name {e.shipped!r} is {len(e.shipped)} characters,"
				f" over the shipped cap of {SHIPPED_CAP}; check it is the shipped spelling"
			)
		if len(e.shipped) == SHIPPED_CAP and not e.renamed:
			warnings.append(f"{where(e)}: {e.shipped!r} is exactly at the cap and unchanged; check it was not cut")
		if e.renamed and len(e.restored) < len(e.shipped):
			warnings.append(f"{where(e)}: restored name {e.restored!r} is shorter than shipped {e.shipped!r}")
		if len(e.restored) > max_length:
			warnings.append(
				f"{where(e)}: restored name {e.restored!r} is {len(e.restored)} characters,"
				f" over the layout check of {max_length}"
			)
	return errors, warnings


def _contains(haystack, needle):
	n = len(needle)
	return any(haystack[i:i + n] == needle for i in range(len(haystack) - n + 1))


def require_valid(entries):
	errors, _ = validate(entries)
	if errors:
		raise GlossaryError(errors)
	return entries
