"""ACC-SAV-01: load then save is byte-identical except write time."""

from common import game_save_api, probe

ID = "ACC-SAV-01"
VERIFIES = "SAV-010, SAV-011"


def run(ctx):
	values = probe(ctx, "roundtrip")
	if values is None:
		return
	for key, what in (("events_save_roundtrip", "interpreter saves"), ("battle_record_roundtrip", "battle records"), ("macros_roundtrip", "macro sets"), ("combo_log_roundtrip", "combo logs")):
		same, total = values[key][:2]
		ctx.expect(same == total, f"{total - same} of {total} {what} change on a load and save")
	hits, probed = game_save_api(ctx)
	if hits:
		ctx.gap(f"a game save API appears ({'; '.join(hits[:3])}) but no acceptance probe drives it yet (SAV-010, SAV-011)")
	else:
		ctx.gap(f"no game save format: {probed} public headers of core and events and the text tool's commands offer no writer or reader for the SAV-001 save (header, location, party, inventory, world, quests, battle-side, unlocks, randomness, difficulty), so there is no save to round-trip (SAV-010, SAV-011)")
	ctx.note(f"the payloads a save would carry round-trip byte for byte: {values['events_save_roundtrip'][1]} interpreter saves (every step of every sample event), {values['battle_record_roundtrip'][1]} battle records, the data macros and a combo log of {values['combo_log_roundtrip'][2]} entries")
