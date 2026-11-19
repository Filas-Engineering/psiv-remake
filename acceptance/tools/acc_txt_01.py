"""ACC-TXT-01: locked dialogue diff through the text tool's verify command."""

import shutil
import sqlite3

from common import imported, read_csv, reviewed_renames

ID = "ACC-TXT-01"
VERIFIES = "TXT-002, TXT-003, TXT-004"


def run(ctx):
	db = imported(ctx)
	sheet, rows = reviewed_renames(ctx, db)
	if sheet is None:
		return
	renamed = ctx.temp / "renamed.db"
	glossary = ctx.examples / "glossary.csv"
	code, output = ctx.text("apply", "--db", db, "--sheet", sheet, "--glossary", glossary, "--out", renamed)
	if not ctx.expect(code == 0, f"apply of the approved rename sheet failed: {output.strip()}"):
		return

	changes = ctx.temp / "changes.csv"
	code, output = ctx.text("verify", "--db", renamed, "--glossary", glossary, "--sheet", sheet, "--delivery", ctx.examples / "delivery", "--change-list", changes)
	ctx.expect(code == 0, f"verify rejected text that is the shipped text plus approved renames: {output.strip()}")
	approved = [r for r in rows if r["approve"] == "yes"]
	listed = read_csv(changes) if changes.exists() else []
	ctx.expect(len(listed) == len(approved), f"the change list has {len(listed)} entries for {len(approved)} approved changes")
	for entry in listed:
		ctx.expect(all(entry.get(k) for k in ("old", "new", "reason")) and any(entry.get(k) for k in ("message_id", "id")), f"change list entry without line ID, old, new and reason: {entry}")

	# Any edit that is not in the change list fails the check.
	tampered = ctx.temp / "tampered.db"
	shutil.copy(renamed, tampered)
	with sqlite3.connect(tampered) as conn:
		conn.execute("UPDATE messages SET current = current || ' Extra words.' WHERE id = (SELECT id FROM messages WHERE kind = 'dialogue' ORDER BY seq LIMIT 1)")
	code, output = ctx.text("verify", "--db", tampered, "--glossary", glossary, "--sheet", sheet, "--delivery", ctx.examples / "delivery")
	ctx.expect(code == 1, f"verify accepted a dialogue edit that no change list entry covers (exit {code})")

	# The locked original cannot be edited behind the check's back.
	with sqlite3.connect(tampered) as conn:
		try:
			conn.execute("UPDATE messages SET original = 'Rewritten.' WHERE seq = 1")
			locked = False
		except sqlite3.DatabaseError:
			locked = True
	ctx.expect(locked, "the shipped text in the line database can be overwritten")

	ctx.note(f"verify passes the placeholder delivery plus {len(approved)} approved renames, writes a change list entry for each, and fails on an unlisted edit; the shipped text is locked")
	ctx.note("run on tools/text/examples/placeholder: no real delivery is in the repository")
