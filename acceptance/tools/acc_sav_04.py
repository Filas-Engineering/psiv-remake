"""ACC-SAV-04: no save chunk stores a table index."""

import re

from common import game_save_api, probe

ID = "ACC-SAV-04"
VERIFIES = "SAV-012"


def run(ctx):
	character = (ctx.root / "core" / "include" / "rules" / "character.h").read_text(encoding="utf-8")
	macro = (ctx.root / "core" / "include" / "rules" / "macro.h").read_text(encoding="utf-8")
	combo = (ctx.root / "core" / "include" / "rules" / "combo.h").read_text(encoding="utf-8")
	ctx.expect("std::string characterId" in character and "std::map<EquipSlot, std::string> equipment" in character, "PartyMember no longer refers to its character and equipment by string ID")
	ctx.expect("std::vector<ActiveStatus> statuses" in character and "std::string id;" in (ctx.root / "core" / "include" / "rules" / "status.h").read_text(encoding="utf-8"), "carried statuses are not stored by status ID")
	ctx.expect("std::string member" in macro, "macro steps no longer name members by character ID")
	ctx.expect("std::string recipe" in combo and "std::vector<std::string> characters" in combo, "the combo log does not store combinations and characters by ID")
	ctx.expect(probe(ctx, "roundtrip") is not None, "setup: save_probe failed")

	# The interpreter save: content by name; only program positions are numbers, guarded by the script fingerprint.
	header = (ctx.root / "events" / "include" / "events" / "save_state.h").read_text(encoding="utf-8")
	positions = sorted(set(re.findall(r"int (pc|event|return_pc) = -1;", header)))
	ctx.expect("std::string event;" in header and "std::map<std::string, std::int64_t> locals" in header, "the interpreter save does not name its event and locals by string")
	hits, probed = game_save_api(ctx)
	if not hits:
		ctx.gap(f"no game save format exists to audit: {probed} public headers and the text tool offer no SAV-001 save, so its chunks cannot be checked for table indexes (SAV-012)")
	ctx.note("the payloads a save would carry refer to content by string ID: party members, equipment, carried statuses, macros, the combo log, battle record setups, interpreter locals and presentation")
	if positions:
		ctx.note(f"the interpreter save stores program positions ({', '.join(positions)}), not content indexes; its script fingerprint restarts the event when the program changes (EVT-052)")
