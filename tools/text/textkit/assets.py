"""Asset lists the text is checked against: portrait assets (TXT-021) and font glyphs (TXT-033)."""

import json
import re
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

from . import codes, db, names, workload

PORTRAIT_CODE = "portrait"
RANGE_RE = re.compile(r"U\+([0-9A-Fa-f]{4,6})(?:-U\+([0-9A-Fa-f]{4,6}))?\Z")


class AssetError(Exception):
	def __init__(self, message, problems=()):
		super().__init__(message)
		self.problems = list(problems)


def _lines(path):
	try:
		text = Path(path).read_text(encoding="utf-8")
	except (OSError, UnicodeDecodeError) as e:
		raise AssetError(f"cannot read {path}: {e}")
	for number, line in enumerate(text.splitlines(), 1):
		if line.lstrip().startswith("#"):
			continue
		if line.strip():
			yield number, line


def load_portraits(path):
	"""One portrait asset ID per line; '#' starts a comment line."""
	portraits = set()
	problems = []
	for number, line in _lines(path):
		asset = line.strip()
		if not codes.ARG_RES["id"].match(asset):
			problems.append(f"{path}:{number}: {asset!r} is not a portrait ID (letters, digits and '_')")
		elif asset in portraits:
			problems.append(f"{path}:{number}: {asset} is listed twice")
		portraits.add(asset)
	if problems:
		raise AssetError(f"{path}: invalid portrait list", problems)
	return portraits


def portrait_problems(texts, portraits):
	"""texts is a list of (label, text). Returns one problem per portrait code that names no asset."""
	problems = []
	for label, text in texts:
		for token in codes.scan(text)[0]:
			if token.name == PORTRAIT_CODE and token.arg not in portraits:
				problems.append(f"{label}: {{{token.text}}} names no portrait asset")
	return problems


def load_glyphs(path):
	"""A line is U+XXXX, a range U+XXXX-U+YYYY, or characters that each are a glyph."""
	glyphs = set()
	problems = []
	for number, line in _lines(path):
		entry = line.strip()
		m = RANGE_RE.match(entry)
		if m:
			low = int(m.group(1), 16)
			high = int(m.group(2) or m.group(1), 16)
			if low > high or high > 0x10FFFF:
				problems.append(f"{path}:{number}: {entry} is not an ascending range of code points")
				continue
			glyphs.update(chr(c) for c in range(low, high + 1))
		elif entry.startswith("U+"):
			problems.append(f"{path}:{number}: {entry!r} is not U+XXXX or U+XXXX-U+YYYY")
		else:
			glyphs.update(entry)
	if problems:
		raise AssetError(f"{path}: invalid glyph list", problems)
	return glyphs


@dataclass
class GlyphReport:
	glyphs: int = 0
	texts: int = 0
	characters: Counter = field(default_factory=Counter)
	first_use: dict = field(default_factory=dict)
	missing: list = field(default_factory=list)


def _shown(text, registry, by_id):
	"""Every way the text can reach the screen: with shipped and with restored names when a glossary is given."""
	variants = [names.render(text, by_id, mode)[0] for mode in names.MODES] if by_id else [text]
	return [codes.visible(v, registry)[0] for v in variants]


def check_glyphs(glyphs, texts, registry, by_id=None):
	"""texts is a list of (label, text). Line breaks, page pauses and runtime values are not glyphs."""
	report = GlyphReport(glyphs=len(glyphs), texts=len(texts))
	for label, text in texts:
		seen = set()
		for shown in _shown(text, registry, by_id):
			for c in shown:
				if c in "\n\f" or c == codes.PLACEHOLDER or c in seen:
					continue
				seen.add(c)
				report.characters[c] += 1
				report.first_use.setdefault(c, label)
	report.missing = sorted(c for c in report.characters if c not in glyphs)
	return report


def render_glyph_report(report):
	out = [
		f"glyphs {report.glyphs}  texts {report.texts}  distinct characters {len(report.characters)}  missing {len(report.missing)}",
	]
	for c in report.missing:
		out.append(f"  missing U+{ord(c):04X} {c!r}: in {report.characters[c]} text(s), first {report.first_use[c]}")
	return "\n".join(out) + "\n"


def db_texts(conn, langs=None):
	"""(label, text) for the current English, the reference language and every stored translation."""
	out = []
	wanted = lambda lang: not langs or lang in langs
	if wanted(workload.SOURCE_LANG):
		out += [(f"{workload.SOURCE_LANG} {m['id']}", m["current"]) for m in db.messages(conn)]
	ref = db.reference_lang(conn)
	if ref and wanted(ref):
		out += [(f"{ref} {k}", v) for k, v in sorted(db.reference(conn).items())]
	for lang in db.translation_langs(conn):
		if wanted(lang):
			out += [(f"{lang} {k}", v) for k, v in sorted(db.translations(conn, lang).items())]
	return out


def strings_texts(path, langs=None):
	"""(label, text) from an event script string table (events/FORMAT.md)."""
	out = []
	language = None
	for number, line in _lines(path):
		stripped = line.strip()
		if stripped.startswith("language "):
			language = stripped.split(None, 1)[1]
			continue
		if "=" not in stripped:
			raise AssetError(f"{path}:{number}: expected '<id> = <text>'")
		key, value = (part.strip() for part in stripped.split("=", 1))
		out.append((f"{Path(path).name}:{number} {key}", value.replace("\\n", "\n").replace("\\\\", "\\")))
	if langs and language not in langs:
		return []
	return out


def text_table_texts(path):
	"""(label, text) from a {"text": {key: template}} table, as core and game use."""
	try:
		data = json.loads(Path(path).read_text(encoding="utf-8"))
	except (OSError, ValueError) as e:
		raise AssetError(f"cannot read {path}: {e}")
	table = data.get("text") if isinstance(data, dict) else None
	if not isinstance(table, dict):
		raise AssetError(f"{path}: expected an object with a 'text' object")
	# Template parameters ({0}, {1}) are runtime values, like {var:...}.
	return [(f"{Path(path).name} {k}", re.sub(r"\{\d+\}", "{var:p}", v)) for k, v in sorted(table.items()) if isinstance(v, str)]
