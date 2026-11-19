"""ACC-FLD-04: chest, shop and hook data counts, unique IDs, hooks to compiled scripts, party talk lines."""

ID = "ACC-FLD-04"
VERIFIES = "FLD-023, FLD-032, FLD-033, FLD-071"


def run(ctx):
	found = []
	for folder in ("core/data", "events/samples", "events/sample_text", "tools/text/examples"):
		for path in (ctx.root / folder).rglob("*"):
			if path.is_file() and any(word in path.read_text(encoding="utf-8", errors="ignore").lower() for word in ('"chests"', '"shops"', '"hooks"', "party_talk")):
				found.append(str(path.relative_to(ctx.root)))
	if found:
		ctx.gap(f"field data files exist ({', '.join(found)}) but no tool checks them (FLD-023, FLD-032, FLD-033, FLD-071)")
	else:
		ctx.gap("no field content data (chests, shops, event hooks, party talk) and no audited counts exist in core, events or tools, so there is nothing to check (FLD-023, FLD-032, FLD-033, FLD-071)")
