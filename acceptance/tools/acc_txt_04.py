"""ACC-TXT-04: no player-facing literal text in code, scripts or art manifests."""

import json
import re

from common import samples

ID = "ACC-TXT-04"
VERIFIES = "TXT-030"


def run(ctx):
	# Scripts: the strict gate rejects quoted text, and the shipped scripts pass it with none.
	probe = ctx.temp / "probe.ev"
	probe.write_text('event probe unskippable\n\tsay guide "Typed into the script."\nend\n', encoding="utf-8")
	code, output = ctx.event_cli("check", "--strict", probe)
	ctx.expect(code == 1, f"event_cli check --strict accepted a script with quoted dialogue (exit {code}) (TXT-030)")
	for body in ('\tnarrate "Typed narration."\n', '\task "Typed prompt?"\n\tyes\n\tno\n\tend\n', '\tchoice @harbor.first_stop_prompt\n\toption "Typed option"\n\toption @harbor.option_rest\n\tend\n'):
		probe.write_text(f"event probe unskippable\n{body}end\n", encoding="utf-8")
		code, _ = ctx.event_cli("check", "--strict", probe)
		ctx.expect(code == 1, f"event_cli check --strict accepted quoted player-facing text: {body.strip().splitlines()[0]} (TXT-030)")
	code, output = ctx.event_cli("check", "--strict", "--registry", "sample_text/registry.txt", *samples(ctx))
	ctx.expect(code == 0, f"the sample scripts fail the strict gate: {output.strip()[:300]}")
	code, extracted = ctx.event_cli("strings", "extract", *samples(ctx))
	inline = [line for line in extracted.splitlines() if re.match(r"^[\w.]+ = ", line)]
	ctx.expect(code == 0 and not inline, f"the sample scripts hold {len(inline)} quoted texts")

	# Code: core's player-facing results are message keys, and every key core uses is in its string table.
	headers = {name: (ctx.root / "core" / "include" / "rules" / name).read_text(encoding="utf-8") for name in ("battle.h", "progression.h", "field.h")}
	for name, signature in (("battle.h", "Message setCommand("), ("progression.h", "Message canEquip("), ("progression.h", "Message equip("), ("field.h", "Message rejection;")):
		ctx.expect(signature in headers[name], f"core/include/rules/{name} no longer returns a message key from {signature.split('(')[0].split()[-1]}: player-facing text would come from code (TXT-030)")
	table = json.loads((ctx.root / "core" / "data" / "text_en.json").read_text(encoding="utf-8"))["text"]
	used = set()
	for source in sorted((ctx.root / "core" / "src").glob("*.cpp")):
		used |= set(re.findall(r'"((?:command|equip|field|battle)\.[a-z_]+(?:\.[a-z_]+)*)"', source.read_text(encoding="utf-8")))
	missing = sorted(used - set(table))
	ctx.expect(not missing, f"core uses message keys its string table lacks: {', '.join(missing[:5])}")

	# Art: there are no art manifests in the repository yet.
	manifests = [p for folder in ("core", "events", "tools") for p in (ctx.root / folder).rglob("*") if p.is_file() and "manifest" in p.name.lower()]
	if manifests:
		ctx.gap(f"art manifests exist ({len(manifests)}) but no check reads them for literal text (TXT-030)")
	ctx.note(f"event_cli check --strict rejects quoted lines, narration, prompts and options, and passes the {len(samples(ctx))} sample scripts, which hold no quoted text; core's player-facing results (commands, equipment, field use) are message keys, and its {len(used)} keys are all in text_en.json")
	if not manifests:
		ctx.note("no art manifests exist in core, events or tools, so there is no art text to check")
