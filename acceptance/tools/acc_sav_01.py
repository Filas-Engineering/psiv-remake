"""ACC-SAV-01: load then save is byte-identical except write time."""

from common import probe

ID = "ACC-SAV-01"
VERIFIES = "SAV-010, SAV-011"


def run(ctx):
	values = probe(ctx, "roundtrip")
	if values is None:
		return
	same, total = values["events_save_roundtrip"]
	ctx.expect(same == total, f"{total - same} of {total} interpreter saves change on a load and save")
	same, total = values["battle_record_roundtrip"]
	ctx.expect(same == total, f"{total - same} of {total} battle records change on a load and save")
	ctx.gap("no game save format exists in core, events or tools: nothing writes the SAV-001 header, location, party, inventory, world, quests, battle-side, unlocks, randomness or difficulty groups, so there is no save to round-trip (SAV-010, SAV-011)")
	ctx.note(f"the two payloads a save would carry round-trip byte for byte: {values['events_save_roundtrip'][1]} interpreter saves and {values['battle_record_roundtrip'][1]} battle records")
