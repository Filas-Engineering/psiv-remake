#include "support.h"

#include "events/save_state.h"

using namespace events;

namespace {

const char* const kVersion1 = R"(event acc_cove skippable
	set acc.cove_entered
	say keeper "Mind the rocks."
	give acc_rope_coil
	say keeper "Take this, the path is steep."
	var acc.cove_visits += 1
	say keeper "Off you go."
on skip
	set acc.cove_entered
	give acc_rope_coil
	var acc.cove_visits += 1
end
)";

// The same scene with one line reworded: a different script, so a different fingerprint.
const char* const kVersion2 = R"(event acc_cove skippable
	set acc.cove_entered
	say keeper "Mind the slippery rocks."
	give acc_rope_coil
	say keeper "Take this, the path is steep."
	var acc.cove_visits += 1
	say keeper "Off you go."
on skip
	set acc.cove_entered
	give acc_rope_coil
	var acc.cove_visits += 1
end
)";

// The first version with only comments, blank lines and indentation changed.
const char* const kVersion1Reformatted = R"(# The cove keeper hands over a rope.
event acc_cove skippable
set acc.cove_entered

say keeper "Mind the rocks."
		give acc_rope_coil
	say keeper "Take this, the path is steep."
	var acc.cove_visits += 1   # counted once per visit
	say keeper "Off you go."
on skip
	set acc.cove_entered
	give acc_rope_coil
	var acc.cove_visits += 1
end
)";

}

ACC_CHECK(acc_evt_08, "ACC-EVT-08", "EVT-052") {
	const Program v1 = compile({{"acc_cove.ev", kVersion1}}).program;
	const Program v2 = compile({{"acc_cove.ev", kVersion2}}).program;
	const Program v1b = compile({{"acc_cove.ev", kVersion1Reformatted}}).program;
	t.expect(v1.fingerprint != v2.fingerprint, "changing a line did not change the script hash");
	t.expect(v1.fingerprint == v1b.fingerprint, "comments, blank lines or indentation changed the script hash");

	// The uninterrupted run of the new script from the event-start world.
	RecordingHost reference;
	support::configure(reference);
	reference.vars["acc.cove_visits"] = 4;
	Interpreter fresh(v2, reference);
	fresh.start("acc_cove");
	support::run(fresh, reference);

	int restarts = 0;
	for (int k = 1; k < 12; ++k) {
		RecordingHost host;
		support::configure(host);
		host.vars["acc.cove_visits"] = 4;
		Interpreter old(v1, host);
		old.start("acc_cove");
		for (int i = 0; i < k; ++i) {
			host.advance(support::kStep);
			old.step(support::kStep);
		}
		if (old.status() != Status::running) {
			break;
		}
		SaveState state;
		t.expect(save_state_from_text(save_state_to_text(old.save()), state), "setup: save text does not load");

		RecordingHost loaded = host.reloaded();
		Interpreter installed(v2, loaded);
		std::string error;
		t.expect(installed.restore(state, error), "restore with a changed script failed: " + error);
		t.expect(installed.restarted(), "a save with a mismatched script hash resumed instead of restarting");
		t.expect(!loaded.trace.empty() && loaded.trace.front() == "restore_world", "the event-start state was not restored before restarting");
		support::run(installed, loaded);
		t.expect(loaded.trace.size() > 1 && loaded.trace[1] == "set_flag acc.cove_entered true", "the restarted event did not begin at its first command");
		t.expect(loaded.same_world(reference), "step " + std::to_string(k) + ": after restarting, the state differs from playing the new script from the start");
		++restarts;

		// The same save under the reformatted script resumes in place.
		RecordingHost same = host.reloaded();
		Interpreter reformatted(v1b, same);
		t.expect(reformatted.restore(state, error) && !reformatted.restarted(), "a save resumed under a reformatted but identical script restarted");
	}
	t.note(std::to_string(restarts) + " mid-event saves loaded under a changed script restore the event-start state and restart from the first command, ending as a fresh run does; formatting-only changes keep the hash");
}
