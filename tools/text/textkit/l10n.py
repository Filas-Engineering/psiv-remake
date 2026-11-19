import csv
import re
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

from . import codes, db

TABLE_COLUMNS = ("id", "kind", "speaker", "map", "trigger", "context", "branches", "char_limit", "codes", "source", "target")
LANG_RE = re.compile(r"[a-z]{2,3}(?:-[A-Za-z0-9]{2,8})*\Z")


def export(conn, path, kind="all"):
	branches = db.branches(conn)
	registry = db.registry(conn)
	count = 0
	with Path(path).open("w", encoding="utf-8", newline="") as f:
		writer = csv.writer(f, lineterminator="\n")
		writer.writerow(TABLE_COLUMNS)
		for m in db.messages(conn):
			if kind != "all" and m["kind"] != kind:
				continue
			writer.writerow((
				m["id"], m["kind"], m["speaker"] or "", m["map"] or "", m["trigger"] or "",
				m["context"] or "", " ".join(branches.get(m["id"], [])),
				m["char_limit"] if m["char_limit"] is not None else "",
				" ".join(codes.strict_sequence(m["current"], registry)),
				m["current"], "",
			))
			count += 1
	return count


@dataclass
class ImportReport:
	lang: str
	errors: list = field(default_factory=list)
	warnings: list = field(default_factory=list)
	missing: list = field(default_factory=list)
	extra: list = field(default_factory=list)
	untranslated: list = field(default_factory=list)
	translations: dict = field(default_factory=dict)
	expected: int = 0

	def ok(self, allow_missing=False):
		if self.errors or self.extra:
			return False
		return allow_missing or not (self.missing or self.untranslated)

	def render(self, allow_missing=False):
		out = [
			f"Translation import check: {self.lang}",
			f"  expected IDs {self.expected}  translated {len(self.translations)}"
			f"  missing {len(self.missing)}  untranslated {len(self.untranslated)}  extra {len(self.extra)}",
			f"  result: {'PASS' if self.ok(allow_missing) else 'FAIL'}",
		]
		for title, items in (
			("Errors", self.errors),
			("Missing IDs", self.missing),
			("Untranslated IDs (empty target)", self.untranslated),
			("Extra IDs (not in the database)", self.extra),
			("Warnings", self.warnings),
		):
			if items:
				out.append("")
				out.append(title)
				out.extend(f"  {i}" for i in items)
		return "\n".join(out) + "\n"


def check_translation(conn, lang, path, kind="all"):
	report = ImportReport(lang)
	if not LANG_RE.match(lang):
		report.errors.append(f"language code {lang!r} is not a BCP 47 style code such as fr or pt-BR")
		return report
	registry = db.registry(conn)
	messages = {m["id"]: m for m in db.messages(conn) if kind == "all" or m["kind"] == kind}
	report.expected = len(messages)
	seen = set()
	with Path(path).open(encoding="utf-8", newline="") as f:
		reader = csv.DictReader(f)
		header = reader.fieldnames or []
		if "id" not in header or "target" not in header:
			report.errors.append(f"{path}: needs at least the id and target columns")
			return report
		for row in reader:
			where = f"line {reader.line_num}"
			message_id = row["id"]
			if message_id in seen:
				report.errors.append(f"{where}: duplicate ID {message_id}")
				continue
			seen.add(message_id)
			if message_id not in messages:
				report.extra.append(message_id)
				continue
			m = messages[message_id]
			if "source" in header and row["source"] != m["current"]:
				report.warnings.append(f"{message_id}: source text changed since export; check the translation")
			target = row["target"] or ""
			if not target.strip():
				report.untranslated.append(message_id)
				continue
			problems = _target_problems(m, target, registry)
			report.errors.extend(f"{message_id}: {p}" for p in problems)
			if not problems:
				report.translations[message_id] = target
	report.missing = [i for i in messages if i not in seen]
	return report


def _target_problems(message, target, registry):
	_, problems = codes.problems_in(target, registry)
	if problems:
		return problems
	expected = codes.strict_sequence(message["current"], registry)
	found = codes.strict_sequence(target, registry)
	if found != expected:
		removed = list((Counter(expected) - Counter(found)).elements())
		added = list((Counter(found) - Counter(expected)).elements())
		detail = []
		if removed:
			detail.append(f"removed {' '.join('{' + c + '}' for c in removed)}")
		if added:
			detail.append(f"added {' '.join('{' + c + '}' for c in added)}")
		if not detail:
			detail.append("reordered")
		problems.append(
			f"control codes and placeholders must be kept in order ({', '.join(detail)}):"
			f" expected {expected}, found {found}"
		)
	limit = message["char_limit"]
	if limit is not None:
		for line in codes.lines(target, registry):
			length = len(line.replace(codes.PLACEHOLDER, ""))
			if length > limit:
				problems.append(f"line {line!r} is {length} characters, over the limit of {limit}")
	return problems


def store(conn, report, out_path):
	out = db.copy(conn, out_path)
	with out:
		out.execute("DELETE FROM translations WHERE lang = ?", (report.lang,))
		out.executemany(
			"INSERT INTO translations (lang, message_id, text) VALUES (?, ?, ?)",
			[(report.lang, i, t) for i, t in report.translations.items()],
		)
	return out
