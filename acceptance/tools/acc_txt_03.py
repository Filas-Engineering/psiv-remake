"""ACC-TXT-03: portrait codes resolve, and translations keep portrait codes and name tokens in order."""

from common import imported, read_csv, samples, write_csv

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

	# tools/text: a translation must keep its portrait codes in place.
	code, output = translate(ctx, table, ctx.temp / "fr_ok.csv")
	ctx.expect(code == 0, f"a translation that keeps every code was rejected: {output.strip()[:300]}")

	def reorder(rows):
		row = next(r for r in rows if r["id"] == "harbor_guide:0")
		row["target"] = "{page}Bienvenue!{br}Je suis Finch.{portrait:FINCH}Demandez."
	code, _ = translate(ctx, table, ctx.temp / "fr_reordered.csv", reorder)
	ctx.expect(code != 0, "tools/text accepted a translation that moves a portrait code (TXT-034)")

	def drop(rows):
		row = next(r for r in rows if r["id"] == "harbor_guide:0")
		row["target"] = "Bienvenue!{br}Je suis Finch.{page}Demandez."
	code, _ = translate(ctx, table, ctx.temp / "fr_dropped.csv", drop)
	ctx.expect(code != 0, "tools/text accepted a translation that removes a portrait code (TXT-034)")

	def add(rows):
		row = next(r for r in rows if r["id"] == "harbor_guide:0")
		row["target"] = row["target"] + "{portrait:FINCH}"
	code, _ = translate(ctx, table, ctx.temp / "fr_added.csv", add)
	ctx.expect(code != 0, "tools/text accepted a translation that adds a portrait code (TXT-034)")

	# Event scripts: the strict gate with both tables rejects a translation that drops or breaks a name token.
	en = ctx.root / "events" / "sample_text" / "en.strings"
	ja_source = (ctx.root / "events" / "sample_text" / "ja.strings").read_text(encoding="utf-8")
	gate = ["check", "--strict", "--registry", "sample_text/registry.txt", "--glossary", "sample_text/glossary.txt", "--table", en]
	code, output = ctx.event_cli(*gate, "--table", "sample_text/ja.strings", *samples(ctx))
	ctx.expect(code == 0, f"setup: the strict gate rejects the sample tables: {output.strip()[:300]}")
	for label, broken in (("drops", ""), ("breaks", "{name:tide_lamp")):
		ja = ctx.temp / f"ja_{label}.strings"
		lines = []
		for line in ja_source.splitlines():
			if line.startswith("market.vendor_greeting"):
				line = "market.vendor_greeting = ランタン！油！" + broken
			lines.append(line)
		ja.write_text("\n".join(lines) + "\n", encoding="utf-8")
		code, output = ctx.event_cli(*gate, "--table", ja, *samples(ctx))
		ctx.expect(code == 1, f"event_cli check --strict accepted a translation that {label} a name token (exit {code}) (TXT-034)")

	# Portrait codes: the event registry lists the portraits, so an unknown one fails the build check.
	probe = ctx.temp / "portrait.ev"
	probe.write_text("event probe unskippable\n\tsay guide @harbor.welcome as acc_no_portrait\nend\n", encoding="utf-8")
	code, _ = ctx.event_cli("check", "--registry", "sample_text/registry.txt", probe)
	ctx.expect(code == 1, f"event_cli check with the registry accepted an unknown portrait code (exit {code}) (TXT-021)")

	# tools/text: does anything resolve a delivery's portrait codes to assets?
	options = " ".join(ctx.text(command, "--help")[1] for command in ("import", "verify", "stats"))
	codes = (ctx.examples / "codes.json").read_text(encoding="utf-8")
	if "portrait" not in options.lower():
		ctx.gap("tools/text has no portrait asset list: import, verify and stats take no portrait registry and the code registry types the portrait argument only as an id, so a delivery's {portrait:...} codes are never resolved to assets (TXT-021)" if '"portrait"' in codes else "tools/text does not know the portrait code (TXT-021)")
	ctx.note("tools/text rejects translations that move, drop or add a portrait code and accepts one that keeps every code; event_cli check --strict with both tables passes the samples and rejects a translation that drops or breaks a name token; the event registry rejects an unknown portrait code")
