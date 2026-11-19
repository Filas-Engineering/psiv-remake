"""ACC-TXT-04: no player-facing literal text in code, scripts or art manifests."""

import re

ID = "ACC-TXT-04"
VERIFIES = "TXT-030"


def run(ctx):
	samples = sorted(str(p) for p in (ctx.root / "events" / "samples").glob("*.ev"))
	code, extracted = ctx.event_cli("strings", "extract", *samples)
	ctx.expect(code == 0, "event_cli strings extract failed on the sample scripts")
	inline = [line for line in extracted.splitlines() if re.match(r"^[\w.]+ = ", line)]

	# Does any build gate reject quoted text? Run the checks a pipeline would run on a script with only literal dialogue.
	probe = ctx.temp / "probe.ev"
	probe.write_text('event probe unskippable\n\tsay guide "Typed into the script."\nend\n', encoding="utf-8")
	gates = {
		"check": ctx.event_cli("check", probe)[0],
		"lint": ctx.event_cli("lint", "--strict", "--entry", "probe", probe)[0],
		"strings check": ctx.event_cli("strings", "check", "--table", "sample_text/en.strings", probe)[0],
	}
	passing = [name for name, rc in gates.items() if rc == 0]

	# Text in core: player-facing messages built in C++.
	core_text = []
	for source in sorted((ctx.root / "core" / "src").glob("*.cpp")):
		text = source.read_text(encoding="utf-8")
		for m in re.finditer(r'return\s+[^;]*"( (?:is|does|cannot|is not)[^"]*)"', text):
			core_text.append(f"{source.name}: \"{m.group(1).strip()}\"")

	if passing:
		ctx.gap(f"no gate rejects literal dialogue in scripts: event_cli {', '.join(passing)} all pass a script with quoted text; the {len(samples)} sample scripts hold {len(inline)} inline texts (TXT-030)")
	if core_text:
		ctx.gap(f"core returns English sentences as command rejection reasons (for example {core_text[0]}) where a menu would show them; no message key is offered (TXT-030)")
	ctx.gap("there are no art manifests to scan yet")
	ctx.note("event_cli strings extract finds every inline text, so a gate can be built on it")
