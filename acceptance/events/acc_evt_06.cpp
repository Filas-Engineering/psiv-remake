#include "support.h"

#include <algorithm>

using namespace events;

namespace {

// A placeholder scene: a line, a choice, a line, a battle with a flee branch, a closing line.
const char* const kScene = R"(event acc_scene skippable
	say guide "The tide turns soon."
	ask "Wait for the ferry?"
	yes
		set acc.waited
	no
		say guide "Then we walk."
	end
	say guide "Something stirs under the pier."
	battle lamp_room_moths
	win
		set acc.won
	flee
		set acc.fled
	end
	say guide "Onward."
on skip
	set acc.skipped
end
)";

bool presented(const std::vector<std::string>& trace, const std::string& prefix) {
	return std::any_of(trace.begin(), trace.end(), [&](const std::string& line) { return line.rfind(prefix, 0) == 0; });
}

}

ACC_CHECK(acc_evt_06, "ACC-EVT-06", "EVT-041, EVT-044") {
	const CompileResult result = compile({{"acc_scene.ev", kScene}});
	if (!t.expect(result.ok(), "setup: the probe scene does not compile")) {
		return;
	}
	RecordingHost host;
	support::configure(host);
	Interpreter it(result.program, host);
	it.start("acc_scene");
	host.advance(support::kStep);
	it.step(support::kStep);
	t.expect(it.can_skip(), "setup: the scene cannot be skipped during its first line");
	it.skip();
	t.expect(!it.can_skip(), "skip stayed available while the skip was running");
	support::run(it, host);

	const bool stoppedAtChoice = presented(host.trace, "ask ");
	const bool stoppedAtBattle = presented(host.trace, "battle ");
	t.expect(stoppedAtChoice, "skip did not stop at the next choice: the choice was never presented and the on-skip block decided it (EVT-041)");
	t.expect(stoppedAtBattle, "skip did not stop at the next battle: the battle never started (EVT-041)");
	if (stoppedAtChoice) {
		t.expect(it.can_skip(), "after the choice that stopped skip mode, skip was not available again (EVT-044)");
	}

	// Within the library's model, a skippable scene called from an unskippable one returns to the caller,
	// which then runs its battle: the one case where a skip ends before a battle.
	{
		const Program program = support::compileSamples(t);
		RecordingHost lighthouse;
		support::configure(lighthouse);
		Interpreter run(program, lighthouse);
		run.start("lighthouse_ambush");
		for (int i = 0; i < 3 && !run.can_skip(); ++i) {
			lighthouse.advance(support::kStep);
			run.step(support::kStep);
		}
		t.expect(run.skip(), "setup: the lighthouse alarm scene could not be skipped");
		support::run(run, lighthouse);
		t.expect(presented(lighthouse.trace, "battle "), "skipping the alarm scene skipped the caller's battle");
	}
	t.note("skipping a scene called from an unskippable event returns to the caller, which still runs its battle");
}
