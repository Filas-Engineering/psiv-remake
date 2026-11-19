"""ACC-SAV-02: migration of older saves, unknown chunks kept, newer saves refused."""

from common import game_save_api, probe

ID = "ACC-SAV-02"
VERIFIES = "SAV-020, SAV-021"


def run(ctx):
	values = probe(ctx, "versions")
	if values is None:
		return
	ctx.expect(values["events_save_resealed_loads"] == [1] and values["battle_record_resealed_loads"] == [1], "setup: a payload whose checksum the probe recomputes no longer loads, so the version probes mean nothing")
	ctx.expect(values["events_save_newer_loads"] == [0], f"an interpreter save from a newer format version ({values['events_save_version'][0] + 1}) loaded (SAV-021)")
	ctx.expect(values["battle_record_newer_loads"] == [0], f"a battle record from a newer format version ({values['battle_record_format'][0] + 1}) loaded (SAV-021)")
	hits, probed = game_save_api(ctx)
	older, versions = values["events_save_older_loads"]
	record_older, record_versions = values["battle_record_older_loads"]
	loads, kept = values["battle_record_unknown_line"]
	if not hits:
		ctx.gap(f"no chunked game save format and no migrations: {probed} public headers and the text tool offer no save with typed, versioned chunks (SAV-020, SAV-021)")
	ctx.note(f"newer payloads are refused: interpreter save {values['events_save_version'][0] + 1}, battle record {values['battle_record_format'][0] + 1}")
	ctx.note(f"the payloads are not chunked either: the interpreter save loads {older} of {versions} older versions and a battle record {record_older} of {record_versions}, and a record with an unknown line {'loads' if loads else 'is refused'}{', keeping it' if kept else ''}; no build has been released, so no migration is owed yet")
