"""ACC-SAV-02: migration of older saves, unknown chunks kept, newer saves refused."""

from common import probe

ID = "ACC-SAV-02"
VERIFIES = "SAV-020, SAV-021"


def run(ctx):
	values = probe(ctx, "versions")
	if values is None:
		return
	ctx.expect(values["events_save_current_header"] == [1], "setup: interpreter saves no longer start with 'events-save 2'")
	ctx.expect(values["events_save_v3_loads"] == [0], "an interpreter save from a newer format version loaded")
	ctx.expect(values["battle_record_newer_format_loads"] == [0], "a battle record from a newer format version loaded")
	ctx.gap("no chunked save format and no migrations: the interpreter save rejects version 1 outright instead of migrating it, and a battle record drops or rejects lines it does not know instead of keeping them (SAV-020, SAV-021)")
	ctx.note("payloads from a newer format version are refused")
