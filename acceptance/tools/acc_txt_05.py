"""ACC-TXT-05: every character in every shipped string table has a glyph."""

ID = "ACC-TXT-05"
VERIFIES = "TXT-033"


def run(ctx):
	tables = sorted((ctx.root / "events" / "sample_text").glob("*.strings"))
	characters = set()
	for table in tables:
		characters |= set(table.read_text(encoding="utf-8"))
	fonts = [p for folder in ("core", "events", "tools") for p in (ctx.root / folder).rglob("*") if p.suffix.lower() in (".ttf", ".otf", ".fnt", ".bdf")]
	ctx.expect(bool(tables), "no string tables found")
	if not fonts:
		ctx.gap(f"no font asset or glyph list exists, so the {len(characters)} distinct characters in {len(tables)} string tables cannot be checked for coverage (TXT-033)")
	else:
		ctx.gap(f"font files exist ({len(fonts)}) but no tool reads their glyph tables (TXT-033)")
