"""ACC-TXT-03: portrait codes resolve, and translations keep portrait codes and name tokens in order."""

import json

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

	# tools/text: every portrait code of the delivery, the reference and the current text resolves to an asset.
	portraits = ctx.examples / "portraits.txt"
	delivery = ctx.examples / "delivery"
	reference = ["--reference", ctx.examples / "delivery_ja", "--reference-lang", "ja"]
	code, output = ctx.text("import", "--delivery", delivery, *reference, "--portraits", portraits, "--db", ctx.temp / "assets.db")
	ctx.expect(code == 0, f"import with the placeholder portrait list failed: {output.strip()[:300]}")
	assets = [line.strip() for line in portraits.read_text(encoding="utf-8").splitlines() if line.strip() and not line.startswith("#")]
	used = sorted({c.split(":", 1)[1] for path in delivery.rglob("*.json") for record in json.loads(path.read_text(encoding="utf-8")) for c in record["codes"] if c.startswith("portrait:")})
	ctx.expect(bool(used), "setup: the placeholder delivery has no portrait codes")
	missing = used[-1]
	short = ctx.temp / "portraits_short.txt"
	short.write_text("\n".join(a for a in assets if a != missing) + "\n", encoding="utf-8")
	code, output = ctx.text("import", "--delivery", delivery, *reference, "--portraits", short, "--check")
	ctx.expect(code == 1 and f"{{portrait:{missing}}} names no portrait asset" in output, f"import accepted portrait codes that name no asset (exit {code}) (TXT-021)")
	ctx.expect("ja " in output, "import did not check the reference language's portrait codes (TXT-021)")
	glossary = ctx.temp / "empty.csv"
	ctx.text("glossary", "init", "--glossary", glossary)
	code, output = ctx.text("verify", "--db", ctx.temp / "assets.db", "--glossary", glossary, "--portraits", portraits)
	ctx.expect(code == 0, f"verify with the full portrait list failed: {output.strip()[:300]}")
	code, output = ctx.text("verify", "--db", ctx.temp / "assets.db", "--glossary", glossary, "--portraits", short)
	ctx.expect(code == 1 and f"{{portrait:{missing}}} names no portrait asset" in output, f"verify passed text whose portrait codes name no asset (exit {code}) (TXT-021)")
	ctx.note(f"import and verify resolve every portrait code of the placeholder delivery and its ja reference to tools/text/examples/placeholder/portraits.txt ({len(assets)} assets) and fail, naming the code, when {missing} is left out; tools/text rejects translations that move, drop or add a portrait code and accepts one that keeps every code; event_cli check --strict with both tables passes the samples and rejects a translation that drops or breaks a name token; the event registry rejects an unknown portrait code")
