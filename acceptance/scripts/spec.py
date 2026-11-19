"""Reads the behavior spec in docs/spec: requirement rows, acceptance checks, trace keys and links."""

import re
from dataclasses import dataclass, field
from pathlib import Path

SYSTEMS = "FLOW|FLD|ENC|BAT|TXT|EVT|UI|SAV"
# Requirements are PREFIX-NNN; acceptance checks are ACC-PREFIX-NN.
_ID = rf"(?:(?:{SYSTEMS})-\d{{3}}|ACC-(?:{SYSTEMS})-\d{{2}})"
ID_PATTERN = re.compile(rf"\b{_ID}\b")
RANGE_PATTERN = re.compile(rf"\b({_ID}) to ({_ID})\b")
LINK_PATTERN = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
LABELS = ("Decided", "Proposed (scope)", "Proposed (spec)", "To measure")
RUNS_ON = ("core", "events", "tools", "engine", "playtest")
AUTOMATED = ("core", "events", "tools")


@dataclass
class Requirement:
	id: str
	text: str
	label: str
	source: str
	file: str
	line: int

	@property
	def system(self):
		return self.id.split("-")[0]


@dataclass
class Check:
	id: str
	text: str
	verifies_text: str
	runs_on_text: str
	file: str
	line: int
	verifies: list = field(default_factory=list)
	baselines: list = field(default_factory=list)

	@property
	def runs_on(self):
		return self.runs_on_text.split()[0] if self.runs_on_text.split() else ""

	@property
	def system(self):
		return self.id.split("-")[1]


@dataclass
class Table:
	file: str
	line: int
	header: list
	rows: list  # (line number, cells)


@dataclass
class Spec:
	root: Path
	files: dict  # name -> text
	tables: list
	requirements: dict
	# Every requirement row, duplicates included, so each row's own fields are checked.
	rows: list
	checks: dict
	duplicates: list
	trace_keys: set
	prefixes: dict  # file name -> ID prefix
	summary: list  # (runs on, check list text, line)


def split_row(line):
	cells = line.strip().strip("|").split("|")
	return [c.strip() for c in cells]


def read_tables(name, text):
	tables = []
	lines = text.splitlines()
	i = 0
	while i < len(lines):
		if lines[i].startswith("|") and i + 1 < len(lines) and re.match(r"^\|[\s:|-]+\|$", lines[i + 1].strip()):
			table = Table(name, i + 1, split_row(lines[i]), [])
			i += 2
			while i < len(lines) and lines[i].startswith("|"):
				table.rows.append((i + 1, split_row(lines[i])))
				i += 1
			tables.append(table)
		else:
			i += 1
	return tables


def number(identifier):
	return int(identifier.rsplit("-", 1)[1])


def prefix_of(identifier):
	return identifier.rsplit("-", 1)[0]


def expand(text, known):
	"""IDs a cell names, with "A to B" ranges expanded to the defined IDs between them."""
	ids = []
	consumed = set()
	for m in RANGE_PATTERN.finditer(text):
		start, end = m.group(1), m.group(2)
		prefix = prefix_of(start)
		ids.extend(sorted((k for k in known if prefix_of(k) == prefix and number(start) <= number(k) <= number(end)), key=number))
		consumed.update(range(m.start(), m.end()))
	for m in ID_PATTERN.finditer(text):
		if m.start() not in consumed:
			ids.append(m.group(0))
	seen = []
	for i in ids:
		if i not in seen:
			seen.append(i)
	return seen


def expand_keys(cell):
	"""Trace keys from the README key column: "A, B", or "S-PILLAR-1 to S-PILLAR-4"."""
	keys = set()
	for part in cell.replace("`", "").split(","):
		part = part.strip()
		m = re.match(r"^(.*-)(\d+) to \1(\d+)$", part)
		if m:
			keys.update(f"{m.group(1)}{n}" for n in range(int(m.group(2)), int(m.group(3)) + 1))
		elif part:
			keys.add(part)
	return keys


def load(root):
	root = Path(root)
	spec_dir = root / "docs" / "spec"
	files = {p.name: p.read_text(encoding="utf-8") for p in sorted(spec_dir.glob("*.md"))}
	tables = []
	for name, text in files.items():
		tables.extend(read_tables(name, text))

	requirements, checks, duplicates, rows = {}, {}, [], []
	trace_keys, prefixes, summary = set(), {}, []
	for table in tables:
		header = [h.lower() for h in table.header]
		if table.file == "README.md" and header[:2] == ["key", "points to in `scope.md`"]:
			for _, cells in table.rows:
				trace_keys |= expand_keys(cells[0])
		elif table.file == "README.md" and header[:3] == ["file", "system", "id prefix"]:
			for _, cells in table.rows:
				m = re.search(r"\(([^)]+\.md)\)", cells[0])
				if m:
					prefixes[m.group(1)] = cells[2].strip("`")
		elif header[:4] == ["id", "requirement", "label", "source"]:
			for line, cells in table.rows:
				cells += [""] * (4 - len(cells))
				r = Requirement(cells[0], cells[1], cells[2], cells[3], table.file, line)
				rows.append(r)
				if r.id in requirements or r.id in checks:
					duplicates.append((r.id, table.file, line))
				else:
					requirements[r.id] = r
		elif header[:4] == ["id", "check", "verifies", "runs on"]:
			for line, cells in table.rows:
				cells += [""] * (4 - len(cells))
				c = Check(cells[0], cells[1], cells[2], cells[3], table.file, line)
				if c.id in checks or c.id in requirements:
					duplicates.append((c.id, table.file, line))
				else:
					checks[c.id] = c
		elif table.file == "acceptance.md" and header[:2] == ["runs on", "checks"]:
			for line, cells in table.rows:
				summary.append((cells[0], cells[1], line))

	for c in checks.values():
		c.verifies = expand(c.verifies_text, requirements)
		m = re.search(r"\(baseline ([^)]*)\)", c.runs_on_text)
		c.baselines = expand(m.group(1), requirements) if m else []
	return Spec(root, files, tables, requirements, rows, checks, duplicates, trace_keys, prefixes, summary)


def heading_anchors(text):
	anchors = set()
	for line in text.splitlines():
		m = re.match(r"^#+\s+(.*)$", line)
		if m:
			slug = re.sub(r"[^\w\s-]", "", m.group(1).lower()).strip()
			anchors.add(re.sub(r"\s+", "-", slug))
	return anchors


def feeds(requirement, known):
	m = re.search(r"Feeds (.*?)(?:\.\s|\.$|$)", requirement.text)
	return expand(m.group(1), known) if m else []
