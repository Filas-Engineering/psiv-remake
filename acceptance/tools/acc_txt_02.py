"""ACC-TXT-02: renamed names need a signed-off glossary entry and reach dialogue only through name tokens."""

import re
import sqlite3

from common import imported, read_csv, reviewed_renames, samples, write_csv

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

	# The rename pass writes name tokens, which render as either name set.
	tokens = 0
	with sqlite3.connect(applied) as conn:
		for (text,) in conn.execute("SELECT current FROM messages WHERE kind = 'dialogue'"):
			tokens += len(re.findall(r"\{name(?:_upper|_lower)?:[\w.]+\}", text))
	ctx.expect(tokens > 0, "the approved rename pass wrote no name tokens into dialogue (TXT-005)")
	code, shipped = ctx.text("render", "--db", applied, "--glossary", glossary, "--names", "shipped")
	code2, restored = ctx.text("render", "--db", applied, "--glossary", glossary, "--names", "restored")
	ctx.expect(code == 0 and code2 == 0 and shipped != restored, "render does not switch the renamed names between the shipped and the restored set")

	# The event build gate: check --strict with the glossary rejects a typed-out renamed name and a broken token.
	gate = ["check", "--strict", "--registry", "sample_text/registry.txt", "--glossary", "sample_text/glossary.txt", "--table", "sample_text/en.strings", "--table", "sample_text/ja.strings"]
	code, output = ctx.event_cli(*gate, *samples(ctx))
	ctx.expect(code == 0, f"event_cli check --strict with the glossary rejects the sample scripts: {output.strip()[:300]}")
	typed = ctx.temp / "typed.ev"
	typed.write_text("event probe unskippable\n\tsay guide @acc.typed\nend\n", encoding="utf-8")
	table = ctx.temp / "typed.strings"
	table.write_text("language en\n\nacc.typed = Bring a Tide Lantern to the keeper.\n", encoding="utf-8")
	code, output = ctx.event_cli("check", "--strict", "--registry", "sample_text/registry.txt", "--glossary", "sample_text/glossary.txt", "--table", table, typed)
	ctx.expect(code == 1, f"event_cli check --strict accepted a renamed name typed out in dialogue instead of a name token (exit {code}) (TXT-005)")
	unknown = ctx.temp / "unknown.strings"
	unknown.write_text("language en\n\nacc.typed = Bring a {name:acc_unknown} to the keeper.\n", encoding="utf-8")
	code, output = ctx.event_cli("check", "--strict", "--registry", "sample_text/registry.txt", "--glossary", "sample_text/glossary.txt", "--table", unknown, typed)
	ctx.expect(code == 1, f"event_cli check --strict accepted a name token that is not in the glossary (exit {code}) (TXT-005)")
	ctx.note(f"renames need a signed-off entry; a system text name changed without one fails verify; the rename pass writes {tokens} name tokens that render as shipped or restored names; event_cli check --strict with the glossary passes the samples and rejects a typed-out renamed name and an unknown token")
