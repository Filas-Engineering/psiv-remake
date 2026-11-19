"""ACC-SAV-03: truncated, bit-flipped and interrupted writes never crash and are reported damaged."""

from common import game_save_api, probe

ID = "ACC-SAV-03"
VERIFIES = "SAV-013, SAV-014"


def run(ctx):
	values = probe(ctx, "corrupt")
	if values is None:
		return
	notes = []
	for name in ("events_save", "battle_record"):
		label = name.replace("_", " ")
		loaded, different, total = values[f"{name}_truncated"]
		ctx.expect(different == 0, f"{label}: {different} of {total} truncations load as different data instead of being reported damaged (SAV-014)")
		flipped, flipped_different, flips = values[f"{name}_flipped"]
		ctx.expect(flipped_different == 0, f"{label}: {flipped_different} of {flips} byte flips load as different data instead of being reported damaged (SAV-014)")
		unchanged = f" ({loaded} truncations and {flipped} flips load unchanged: they touch only the line break after the checksum)" if loaded or flipped else ""
		notes.append(f"{label}: all {total} truncations and all {flips} flips of every byte with each single-bit mask and 0xff are refused{unchanged}")
	hits, probed = game_save_api(ctx)
	if not hits:
		ctx.gap(f"no save writer: {probed} public headers and the text tool offer no function that writes a save file, so an interrupted write cannot be tested for atomicity (SAV-013)")
	ctx.note("; ".join(notes) + f"; {values['events_save_samples'][0]} interpreter saves from every sample event; no damaged payload crashed either parser")
