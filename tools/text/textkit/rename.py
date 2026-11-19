import csv
from pathlib import Path

from . import db, mentions, sheet

MENTION_COLUMNS = ("message_id", "kind", "speaker", "map", "start", "end", "mention", "glossary_id", "shipped", "restored", "case", "split", "broken", "line")


def all_mentions(conn, entries):
	registry = db.registry(conn)
	compiled = mentions.compile_names(entries)
	for message in db.messages(conn):
		for m in mentions.find(message["current"], registry, compiled):
			yield message, m


def write_mentions(path, conn, entries):
	count = 0
	with Path(path).open("w", encoding="utf-8", newline="") as f:
		writer = csv.writer(f, lineterminator="\n")
		writer.writerow(MENTION_COLUMNS)
		for message, m in all_mentions(conn, entries):
			writer.writerow([
				message["id"], message["kind"], message["speaker"] or "", message["map"] or "",
				m.start, m.end, m.text, m.entry.id, m.entry.shipped, m.entry.restored,
				m.case, "yes" if m.split else "", "yes" if m.broken else "", message["current"],
			])
			count += 1
	return count


def propose(conn, entries):
	by_id = {e.id: e for e in entries}
	rows = []
	for message, m in all_mentions(conn, entries):
		if not m.entry.renamed:
			continue
		new, why = mentions.substitute(m)
		notes = [n for n in (m.note, why) if n]
		if not m.entry.signoff.strip():
			notes.append(f"glossary entry {m.entry.id} is not signed off; apply will refuse it")
		reason = f"glossary {m.entry.id}: {m.entry.shipped} -> {m.entry.restored}"
		if m.entry.reason:
			reason += f" ({m.entry.reason})"
		row = sheet.proposal(
			f"R{len(rows) + 1:04d}", "rename", message, m.start, m.text, new or "", reason, "; ".join(notes),
		)
		row.show_names(by_id)
		rows.append(row)
	return rows
