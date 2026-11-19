"""Apply approved review sheets and verify the result against the locked text."""

import csv
from dataclasses import dataclass, field
from pathlib import Path

from . import db, delivery, sheet


class ApplyError(Exception):
	def __init__(self, problems):
		super().__init__(f"{len(problems)} problem(s); nothing was written")
		self.problems = problems


def apply(conn, sheet_paths, entries, out_path):
	"""Write a copy of the database with the approved rows applied, in sheet order."""
	registry = db.registry(conn)
	check = sheet.RenameCheck(entries, registry)
	texts = {m["id"]: m["current"] for m in db.messages(conn)}
	staged = []
	problems = []
	for path in sheet_paths:
		try:
			rows = sheet.read(path)
		except sheet.SheetError as e:
			problems.extend(e.problems)
			continue
		texts, applied, sheet_problems = sheet.replay(texts, rows, check, registry)
		problems.extend(f"{Path(path).name}: {p}" for p in sheet_problems)
		staged.append((path, applied))
	if problems:
		raise ApplyError(problems)

	out = db.copy(conn, out_path)
	with out:
		for path, applied in staged:
			cursor = out.execute(
				"INSERT INTO applied_sheets (name, sha256, approved_rows, applied_at) VALUES (?, ?, ?, ?)",
				(Path(path).name, sheet.file_digest(path), len(applied), db.now()),
			)
			seq = cursor.lastrowid
			out.executemany(
				"INSERT INTO changes (sheet_seq, change_id, message_id, kind, start, old, new, reason)"
				" VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
				[(seq, r.change_id, r.message_id, r.kind, r.start, r.old, r.new, r.reason) for r in applied],
			)
		out.executemany(
			"UPDATE messages SET current = ? WHERE id = ? AND current != ?",
			[(text, message_id, text) for message_id, text in texts.items()],
		)
	return out, sum(len(a) for _, a in staged)


@dataclass
class Verdict:
	problems: list = field(default_factory=list)
	changes: list = field(default_factory=list)
	messages: int = 0
	changed: int = 0

	@property
	def ok(self):
		return not self.problems


def verify(conn, sheet_paths, entries, delivery_path=None):
	"""Prove current text = locked original + the approved rows of the given sheets."""
	registry = db.registry(conn)
	verdict = Verdict()
	rows = db.messages(conn)
	verdict.messages = len(rows)

	recorded = db.applied_sheets(conn)
	if len(recorded) != len(sheet_paths):
		verdict.problems.append(
			f"the database records {len(recorded)} applied sheet(s) but {len(sheet_paths)} were given"
		)
	for i, (record, path) in enumerate(zip(recorded, sheet_paths), 1):
		if not Path(path).is_file():
			verdict.problems.append(f"sheet {i}: {path} does not exist")
		elif sheet.file_digest(path) != record["sha256"]:
			verdict.problems.append(
				f"sheet {i}: {Path(path).name} is not the sheet that was applied as {record['name']}"
				" (its contents changed since apply)"
			)

	check = sheet.RenameCheck(entries, registry)
	texts = {m["id"]: m["original"] for m in rows}
	for path in sheet_paths:
		if not Path(path).is_file():
			continue
		try:
			sheet_rows = sheet.read(path)
		except sheet.SheetError as e:
			verdict.problems.extend(e.problems)
			continue
		texts, applied, problems = sheet.replay(texts, sheet_rows, check, registry)
		verdict.problems.extend(f"{Path(path).name}: {p}" for p in problems)
		verdict.changes.extend(applied)

	for m in rows:
		if m["current"] != m["original"]:
			verdict.changed += 1
		if texts[m["id"]] != m["current"]:
			verdict.problems.append(
				f"{m['id']}: unapproved change. Original plus approved changes gives {texts[m['id']]!r},"
				f" the database holds {m['current']!r}"
			)

	if delivery_path is not None:
		try:
			delivered = delivery.load(delivery_path, registry)
		except delivery.DeliveryError as e:
			verdict.problems.extend(f"delivery: {p}" for p in e.problems)
		else:
			_compare_delivery(rows, delivered, verdict)
	return verdict


def _compare_delivery(rows, delivered, verdict):
	locked = {m["id"]: m["original"] for m in rows}
	shipped = {m.id: m.text for m in delivered}
	for message_id in sorted(set(locked) - set(shipped)):
		verdict.problems.append(f"{message_id}: in the database but not in the delivery")
	for message_id in sorted(set(shipped) - set(locked)):
		verdict.problems.append(f"{message_id}: in the delivery but not in the database")
	for message_id in sorted(set(locked) & set(shipped)):
		if locked[message_id] != shipped[message_id]:
			verdict.problems.append(f"{message_id}: locked original text differs from the delivery")


def write_change_list(path, conn, verdict):
	"""The dialogue change list: every instance with line ID, old, new and reason."""
	originals = {m["id"]: m for m in db.messages(conn)}
	with Path(path).open("w", encoding="utf-8", newline="") as f:
		writer = csv.writer(f, lineterminator="\n")
		writer.writerow(("message_id", "change_id", "kind", "old", "new", "reason", "original_line", "current_line"))
		for r in verdict.changes:
			m = originals[r.message_id]
			writer.writerow((r.message_id, r.change_id, r.kind, r.old, r.new, r.reason, m["original"], m["current"]))
