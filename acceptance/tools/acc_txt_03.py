"""ACC-TXT-03: portrait codes resolve, and translations keep portrait codes and name tokens in order."""

from common import imported, read_csv, write_csv

ID = "ACC-TXT-03"
VERIFIES = "TXT-021, TXT-034"


def translate(ctx, table, out, change=None):
	rows = read_csv(table)
	for row in rows:
		row["target"] = row["source"].replace("the", "la")
	if change:
		change(rows)
	write_csv(out, rows)
	return ctx.text("import-translation", "--db", ctx.temp / "script.db", "--lang", "fr", "--table", out)


def run(ctx):
	imported(ctx)
	table = ctx.temp / "table.csv"
	code, output = ctx.text("export", "--db", ctx.temp / "script.db", "--out", table)
	if not ctx.expect(code == 0, f"export failed: {output.strip()}"):
		return

	code, output = translate(ctx, table, ctx.temp / "fr_ok.csv")
	ctx.expect(code == 0, f"a translation that keeps every code was rejected: {output.strip()[:300]}")

	def reorder(rows):
		row = next(r for r in rows if r["id"] == "harbor_guide:0")
		row["target"] = "{page}Bienvenue!{br}Je suis Finch.{portrait:FINCH}Demandez."
	code, _ = translate(ctx, table, ctx.temp / "fr_reordered.csv", reorder)
	ctx.expect(code != 0, "tools/text accepted a translation that moves a portrait code")

	def drop(rows):
		row = next(r for r in rows if r["id"] == "harbor_guide:0")
		row["target"] = "Bienvenue!{br}Je suis Finch.{page}Demandez."
	code, _ = translate(ctx, table, ctx.temp / "fr_dropped.csv", drop)
	ctx.expect(code != 0, "tools/text accepted a translation that removes a portrait code")

	# Event string tables: a translation that drops a name token.
	ja = ctx.temp / "ja.strings"
	ja.write_text("language ja\n\nharbor.option_rest = 宿へ\nmarket.vendor_greeting = ランタン！油！\nmarket.closing_narration = 鐘が鳴る。\n", encoding="utf-8")
	samples = sorted(str(p) for p in (ctx.root / "events" / "samples").glob("*.ev"))
	code, output = ctx.event_cli("strings", "check", "--table", "sample_text/en.strings", "--table", ja, *samples)
	ctx.expect("name tokens" in output, "event_cli strings check did not notice a translation that drops a name token")
	ctx.expect(code != 0, "event_cli strings check reports a translation that drops a name token only as a warning and exits 0, so it is not rejected (TXT-034)")

	ctx.gap("no portrait asset registry exists in the libraries or tools, so no check resolves portrait codes to assets (TXT-021)")
	ctx.note("tools/text rejects translations that move or drop a portrait code and accepts one that keeps every code")
