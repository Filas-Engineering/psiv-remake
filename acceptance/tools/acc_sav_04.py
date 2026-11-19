"""ACC-SAV-04: no save chunk stores a table index."""

import re

ID = "ACC-SAV-04"
VERIFIES = "SAV-012"


def run(ctx):
	header = (ctx.root / "events" / "include" / "events" / "save_state.h").read_text(encoding="utf-8")
	indexed = [m.group(1) for m in re.finditer(r"int (pc|event) = -1;", header)]
	character = (ctx.root / "core" / "include" / "rules" / "character.h").read_text(encoding="utf-8")
	macro = (ctx.root / "core" / "include" / "rules" / "macro.h").read_text(encoding="utf-8")
	ctx.expect("std::string characterId" in character and "std::map<EquipSlot, std::string> equipment" in character, "PartyMember no longer refers to its character and equipment by string ID")
	ctx.expect("std::string member" in macro, "macro steps no longer name members by character ID")
	ctx.gap("no game save format exists to audit (SAV-012)")
	if indexed:
		ctx.gap(f"the interpreter state a mid-event save would carry stores instruction and event indexes ({', '.join(sorted(set(indexed)))}); the script hash makes a changed program restart the event rather than misread them (SAV-012)")
	ctx.note("party members, equipment and macros refer to content by string ID")
