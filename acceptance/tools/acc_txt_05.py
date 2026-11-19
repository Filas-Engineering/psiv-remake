"""ACC-TXT-05: every character in every shipped string table has a glyph."""

import re

ID = "ACC-TXT-05"
VERIFIES = "TXT-033"


def without(listed, char):
	"""The glyph list text with one character taken out of whichever range or run holds it."""
	out = []
	for line in listed.splitlines():
		m = re.fullmatch(r"U\+([0-9A-F]{4,6})-U\+([0-9A-F]{4,6})", line.strip())
		if m and int(m.group(1), 16) <= ord(char) <= int(m.group(2), 16):
			low, high = int(m.group(1), 16), int(m.group(2), 16)
			out += [f"U+{a:04X}-U+{b:04X}" for a, b in ((low, ord(char) - 1), (ord(char) + 1, high)) if a <= b]
		elif not line.startswith("#") and not line.startswith("U+"):
			out.append(line.replace(char, ""))
		else:
			out.append(line)
	return "\n".join(out) + "\n"


def run(ctx):
	text = ctx.root / "game" / "data" / "text"
	core = ctx.root / "core" / "data" / "text_en.json"
	glossary = ctx.temp / "glossary.csv"
	code, output = ctx.text("glossary", "import-events", "--events", text / "glossary.txt", "--out", glossary, "--category", "enemy")
	if not ctx.expect(code == 0, f"the game glossary does not convert for the text tool: {output.strip()[:300]}"):
		return

	# The game's tables per language, with core's battle text, which every language shows, and the names behind
	# name tokens both ways.
	checked = []
	for lang in ("en", "ja"):
		glyphs = text / f"glyphs_{lang}.txt"
		args = ["glyphs", "--glyphs", glyphs, "--strings", text / f"{lang}.strings", "--text", text / f"{lang}.json", "--text", core, "--glossary", glossary, "--lang", lang]
		code, output = ctx.text(*args)
		ctx.expect(code == 0, f"the {lang} tables use characters game/data/text/glyphs_{lang}.txt lacks: {output.strip()[:300]}")
		checked.append(f"{lang} ({output.split('distinct characters ')[1].split()[0] if 'distinct characters ' in output else '?'} characters)")
		# Probe: a font without one character the tables use must be reported.
		probe_char = "k" if lang == "en" else "フ"
		ctx.expect(probe_char in (text / f"{lang}.strings").read_text(encoding="utf-8"), f"setup: the {lang} table does not use {probe_char!r}")
		reduced = ctx.temp / f"glyphs_{lang}_reduced.txt"
		reduced.write_text(without(glyphs.read_text(encoding="utf-8"), probe_char), encoding="utf-8")
		args[2] = reduced
		code, output = ctx.text(*args)
		ctx.expect(code == 1 and f"U+{ord(probe_char):04X}" in output, f"a {lang} font without {probe_char!r} was not reported (exit {code}): {output.strip()[:200]}")

	# The text tool's placeholder delivery and its Japanese reference against the example glyph lists.
	db = ctx.temp / "script.db"
	code, output = ctx.text("import", "--delivery", ctx.examples / "delivery", "--reference", ctx.examples / "delivery_ja", "--reference-lang", "ja", "--db", db)
	ctx.expect(code == 0, f"setup: import failed: {output.strip()[:200]}")
	for glyphs, lang in (("glyphs.txt", "en"), ("glyphs_ja.txt", "ja")):
		code, output = ctx.text("glyphs", "--glyphs", ctx.examples / glyphs, "--db", db, "--lang", lang)
		ctx.expect(code == 0, f"the example {lang} text uses characters {glyphs} lacks: {output.strip()[:200]}")
	code, output = ctx.text("glyphs", "--glyphs", ctx.examples / "glyphs.txt", "--db", db, "--lang", "ja")
	ctx.expect(code == 1 and "missing" in output, "Japanese text checked against the Latin glyph list was not reported")

	ctx.note(f"tools/text glyphs: every character of the game's string tables and text tables, with core's battle text and both forms of every glossary name, has a glyph in the placeholder glyph list of its language: {', '.join(checked)}; removing one used character from a list is reported with its code point; the text tool's example delivery and Japanese reference pass their example glyph lists, and Japanese against the Latin list fails. The glyph lists are placeholders until real fonts exist")
