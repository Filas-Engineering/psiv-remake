"""ACC-TXT-02: renamed names need a signed-off glossary entry and reach dialogue only through name tokens."""

import re
import sqlite3

from common import imported, read_csv, reviewed_renames, write_csv

ID = "ACC-TXT-02"
VERIFIES = "TXT-005, TXT-011, TXT-014"


def run(ctx):
	glossary = ctx.examples / "glossary.csv"
	code, output = ctx.text("glossary", "validate", "--glossary", glossary)
	ctx.expect(code == 0, f"the placeholder glossary does not validate: {output.strip()}")

	db = imported(ctx)
	sheet, rows = reviewed_renames(ctx, db)
	if sheet is None:
		return

	# A rename whose glossary entry is not signed off is refused.
	unsigned = [r for r in rows if "not signed off" in r["note"]]
	ctx.expect(bool(unsigned), "setup: the placeholder glossary has no unsigned entry to probe")
	forced = ctx.temp / "forced.csv"
	write_csv(forced, [dict(r, approve="yes") for r in rows])
	code, output = ctx.text("apply", "--db", db, "--sheet", forced, "--glossary", glossary, "--out", ctx.temp / "forced.db")
	ctx.expect(code != 0, "apply accepted a rename whose glossary entry has no sign-off")

	# A system text name changed by hand, with no glossary entry, fails verify.
	renamed = ctx.temp / "renamed.db"
	code, output = ctx.text("apply", "--db", db, "--sheet", sheet, "--glossary", glossary, "--out", renamed)
	ctx.expect(code == 0, f"setup: apply failed: {output.strip()}")
	with sqlite3.connect(renamed) as conn:
		conn.execute("UPDATE messages SET current = 'Throw Away' WHERE id = 'menu:0'")
	code, output = ctx.text("verify", "--db", renamed, "--glossary", glossary, "--sheet", sheet)
	ctx.expect(code == 1, "verify accepted a system text name that differs from shipped with no glossary entry")

	# Renamed names in dialogue: the text tool writes the new name as literal text.
	applied = ctx.temp / "clean.db"
	ctx.text("apply", "--db", db, "--sheet", sheet, "--glossary", glossary, "--out", applied)
	restored = {r["restored"] for r in read_csv(glossary) if r["restored"] != r["shipped"]}
	literal = []
	with sqlite3.connect(applied) as conn:
		for message_id, text in conn.execute("SELECT id, current FROM messages WHERE kind = 'dialogue'"):
			plain = re.sub(r"\{[^}]*\}", " ", text)
			literal += [f"{message_id}: {name}" for name in restored if re.search(rf"\b{re.escape(name)}\b", plain, re.IGNORECASE)]
	ctx.expect(not literal, f"tools/text rename writes renamed names into dialogue as literal text, not name tokens, so 'Show shipped names' cannot switch them ({len(literal)} mentions, first {literal[:1]}) (TXT-005)")

	# The event pipeline: sample dialogue uses tokens, and broken or unknown tokens fail the names check.
	code, output = ctx.event_cli("names", "sample_text/glossary.txt", "--table", "sample_text/en.strings", "--table", "sample_text/ja.strings", *sorted(str(p) for p in (ctx.root / "events" / "samples").glob("*.ev")))
	ctx.expect(code == 0, f"event_cli names fails on the sample scripts: {output.strip()[:300]}")
	probe = ctx.temp / "probe.ev"
	probe.write_text('event probe unskippable\n\tsay guide "Bring a {name:acc_unknown}."\nend\n', encoding="utf-8")
	code, output = ctx.event_cli("names", "sample_text/glossary.txt", probe)
	ctx.expect(code != 0, "event_cli names accepted an unknown name token")
	typed = ctx.temp / "typed.ev"
	typed.write_text('event probe unskippable\n\tsay guide "Bring a Tide Lantern."\nend\n', encoding="utf-8")
	code, output = ctx.event_cli("names", "sample_text/glossary.txt", typed)
	ctx.expect(code != 0, "event_cli names reports a renamed name typed out in dialogue but still exits 0, so no build check enforces name tokens (TXT-005)")
	ctx.note("renames need a signed-off entry; a system text name changed without one fails verify; event scripts' name tokens are checked")
