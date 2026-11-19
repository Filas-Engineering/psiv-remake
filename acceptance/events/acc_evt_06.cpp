#include "support.h"

#include <algorithm>

using namespace events;

namespace {

// A placeholder scene: lines, a choice, more lines, a battle with a flee branch, a closing line.
const char* const kScene = R"(event acc_scene skippable
	say guide "The tide turns soon."
	say guide "Mind the nets."
	ask "Wait for the ferry?"
	yes
		set acc.waited
	no
		say guide "Then we walk."
	end
	say guide "Something stirs under the pier."
	say guide "Stay close."
	battle lamp_room_moths
	win
		set acc.won
	flee
		set acc.fled
	end
	say guide "Onward."
	say guide "The road is long."
	set acc.done
end
)";

bool has(const std::vector<std::string>& trace, std::size_t from, const std::string& prefix) {
	return std::any_of(trace.begin() + static_cast<long>(std::min(from, trace.size())), trace.end(), [&](const std::string& line) { return line.rfind(prefix, 0) == 0; });
}

// Steps until a trace line starting with `prefix` appears, or the event stops. Returns whether it appeared.
bool stepUntil(Interpreter& it, RecordingHost& host, const std::string& prefix) {
	for (int i = 0; i < 400 && it.status() == Status::running; ++i) {
		if (has(host.trace, 0, prefix)) {
			return true;
		}
		host.advance(support::kStep);
		it.step(support::kStep);
	}
	return has(host.trace, 0, prefix);
}

}

ACC_CHECK(acc_evt_06, "ACC-EVT-06", "EVT-041, EVT-044") {
	const CompileResult result = compile({{"acc_scene.ev", kScene}});
	if (!t.expect(result.ok(), "setup: the probe scene does not compile")) {
		return;
	}
	for (const BattleResult outcome : {BattleResult::win, BattleResult::flee}) {
		const std::string label = std::string("battle ") + battle_result_name(outcome) + ": ";
		RecordingHost host;
		support::configure(host);
		host.queue_choices({0});
		host.queue_battle_results({outcome});
		Interpreter it(result.program, host);
		it.start("acc_scene");
		host.advance(support::kStep);
		it.step(support::kStep);

		// Skip during the first line: the rest of the lines are passed over and the skip stops at the choice.
		t.expect(it.can_skip() && it.skip(), label + "setup: the scene cannot be skipped during its first line");
		t.expect(!it.can_skip(), label + "skip stayed available while the skip was running");
		std::size_t mark = host.trace.size();
		const bool choice = stepUntil(it, host, "ask ");
		t.expect(choice, label + "skip did not stop at the next choice: the choice was never presented (EVT-041)");
		t.expect(!has(host.trace, mark, "say guide \"Mind the nets.\""), label + "a line before the choice was shown while skipping");
		t.expect(has(host.trace, mark, "skipped say guide \"Mind the nets.\""), label + "a line passed over by the skip was not logged");
		t.expect(!it.skipping(), label + "skip mode did not end at the choice");

		// After the choice that stopped skip mode the player can skip again (EVT-044), and the skip stops at the battle.
		mark = host.trace.size();
		for (int i = 0; i < 400 && it.status() == Status::running && !it.can_skip(); ++i) {
			host.advance(support::kStep);
			it.step(support::kStep);
		}
		t.expect(it.status() == Status::running && it.can_skip(), label + "after the choice that stopped skip mode, skip was not offered again before the event ended (EVT-044)");
		t.expect(host.flags["acc.waited"], label + "the answer to the choice that stopped skip mode was not applied");
		t.expect(it.skip(), label + "skipping again after the choice was refused (EVT-044)");
		mark = host.trace.size();
		const bool battle = stepUntil(it, host, "battle ");
		t.expect(battle, label + "skip did not stop at the next battle: the battle never started (EVT-041)");
		t.expect(!has(host.trace, mark, "say guide \"Stay close.\""), label + "a line before the battle was shown while skipping");

		// After the battle, skip again: the skip runs to the end of the event and applies the rest.
		for (int i = 0; i < 400 && it.status() == Status::running && !it.can_skip(); ++i) {
			host.advance(support::kStep);
			it.step(support::kStep);
		}
		t.expect(it.status() == Status::running && it.can_skip() && it.skip(), label + "after the battle that stopped skip mode, skip was not offered again (EVT-044)");
		mark = host.trace.size();
		support::run(it, host);
		t.expect(it.status() == Status::finished, label + "the scene did not finish after the last skip");
		t.expect(!has(host.trace, mark, "say "), label + "a line after the battle was shown while skipping to the end");
		t.expect(host.flags["acc.done"] && host.flags[outcome == BattleResult::win ? "acc.won" : "acc.fled"], label + "the skip to the end did not apply the battle branch and the closing state change");
		t.expect(host.live_handles() == 0 && host.errors.empty(), label + "host handles leaked or misused");
	}

	// A skippable scene called from an unskippable one: the skip ends with the callee and the caller runs its battle.
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
		t.expect(has(lighthouse.trace, 0, "battle "), "skipping the alarm scene skipped the caller's battle");
	}
	t.note("skip passes over lines (logging them) and stops at the next choice, then at the next battle, for a won and a fled battle; after each stop skip is offered again and the last skip runs to the end of the event applying the rest; skipping a scene called from an unskippable event returns to the caller, which still runs its battle");
}
