"""ACC-SAV-03: truncated, bit-flipped and interrupted writes never crash and are reported damaged."""

from common import probe

ID = "ACC-SAV-03"
VERIFIES = "SAV-013, SAV-014"


def run(ctx):
	values = probe(ctx, "corrupt")
	if values is None:
		return
	notes = []
	for name in ("events_save", "battle_record"):
		loaded, total = values[f"{name}_truncated"]
		flipped, undetected, flips = values[f"{name}_flipped"]
		notes.append(f"{name.replace('_', ' ')}: {loaded} of {total} truncations and {flipped} of {flips} bit flips still load, {undetected} as different data")
	ctx.gap("no checksum in either save payload, so damage is not detected: " + "; ".join(notes) + " (SAV-014)")
	ctx.gap("no save writer exists, so atomic writes cannot be tested (SAV-013)")
	ctx.note("no truncation or bit flip crashed either parser")
