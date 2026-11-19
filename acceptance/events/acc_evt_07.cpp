#include "support.h"

#include <algorithm>

using namespace events;

namespace {

// Wait for all: the line comes after both branches.
const char* const kTogether = R"(event acc_together skippable
	together
		move guide to pier_end
		sequence
			anim traveler wave
			face traveler guide
		end
	end
	say guide "Both done."
end
)";

// Wait for any: the line comes after the first branch to finish; the other stops where it is.
const char* const kRace = R"(event acc_race skippable
	race
		move guide to pier_end
		sequence
			anim traveler wave
			face traveler guide
			show keeper
		end
	end
	say guide "One done."
end
)";

int stepOf(const std::vector<std::pair<int, std::string>>& log, const std::string& prefix) {
	for (const auto& [step, line] : log) {
		if (line.rfind(prefix, 0) == 0) {
			return step;
		}
	}
	return -1;
}

std::vector<std::pair<int, std::string>> runLogged(RecordingHost& host, Interpreter& it, const std::string& event, int skipAt = -1) {
	std::vector<std::pair<int, std::string>> log;
	it.start(event);
	for (int step = 0; it.status() == Status::running && step < 200; ++step) {
		const std::size_t before = host.trace.size();
		if (step == skipAt) {
			it.skip();
		}
		host.advance(support::kStep);
		it.step(support::kStep);
		for (std::size_t i = before; i < host.trace.size(); ++i) {
			log.push_back({step, host.trace[i]});
		}
	}
	return log;
}

RecordingHost timed(double move, double wave) {
	RecordingHost host;
	host.set_duration(ActionType::move, move);
	host.set_duration(ActionType::animate, wave);
	host.set_duration(ActionType::face, 0.2);
	host.set_duration(ActionType::show_actor, 0.2);
	return host;
}

}

ACC_CHECK(acc_evt_07, "ACC-EVT-07", "EVT-008") {
	const CompileResult together = compile({{"acc_together.ev", kTogether}});
	const CompileResult race = compile({{"acc_race.ev", kRace}});
	if (!t.expect(together.ok() && race.ok(), "setup: the probe scenes do not compile")) {
		return;
	}

	// Wait for all, with either branch finishing last.
	for (const auto& [move, wave] : std::vector<std::pair<double, double>>{{1.2, 0.3}, {0.3, 1.2}}) {
		RecordingHost host = timed(move, wave);
		Interpreter it(together.program, host);
		const auto log = runLogged(host, it, "acc_together");
		const int line = stepOf(log, "say guide");
		const int slowest = static_cast<int>(std::max(move, wave + 0.2) / support::kStep);
		t.expect(line >= slowest, "wait-all continued after " + std::to_string(line) + " steps, before its slowest branch finished (" + std::to_string(slowest) + " steps)");
		t.expect(stepOf(log, "action face") > stepOf(log, "action anim"), "a sequence branch did not run its steps in order");
		t.expect(stepOf(log, "cancel ") < 0, "wait-all cancelled a branch");
		t.expect(it.status() == Status::finished && host.live_handles() == 0, "the wait-all scene did not finish cleanly");
	}

	// Wait for any: whichever branch finishes first ends the block; the other stops at its yield point.
	{
		// The move wins: the sequence is cut during its first step and never reaches its later steps.
		RecordingHost host = timed(0.3, 1.5);
		Interpreter it(race.program, host);
		const auto log = runLogged(host, it, "acc_race");
		const int line = stepOf(log, "say guide");
		t.expect(line >= 0 && line <= 5, "wait-any did not continue when its first branch finished (line at step " + std::to_string(line) + ")");
		t.expect(stepOf(log, "cancel anim traveler wave") >= 0, "the losing branch's command in flight was not cancelled");
		t.expect(stepOf(log, "action face") < 0 && stepOf(log, "action show") < 0, "a branch stopped by wait-any went on to its next step");
		t.expect(it.status() == Status::finished && host.live_handles() == 0 && host.errors.empty(), "the wait-any scene left host handles live or misused them");
	}
	{
		// The sequence wins: the move is cancelled and nothing after it applies.
		RecordingHost host = timed(3.0, 0.2);
		Interpreter it(race.program, host);
		const auto log = runLogged(host, it, "acc_race");
		const int line = stepOf(log, "say guide");
		t.expect(line >= 0 && line < 30, "wait-any waited for its slowest branch");
		t.expect(stepOf(log, "action show keeper") >= 0 && stepOf(log, "action show keeper") <= line, "the winning sequence did not run to its end before the block ended");
		t.expect(stepOf(log, "cancel move guide") >= 0, "the slower branch's move was not cancelled when the first branch finished");
		t.expect(it.status() == Status::finished && host.live_handles() == 0, "the wait-any scene did not finish cleanly");
	}

	// A skip during wait-all fast-forwards every branch to its end state instead of stopping them.
	{
		RecordingHost watchedHost = timed(1.0, 1.0);
		Interpreter watched(together.program, watchedHost);
		runLogged(watchedHost, watched, "acc_together");
		RecordingHost host = timed(1.0, 1.0);
		Interpreter it(together.program, host);
		runLogged(host, it, "acc_together", 3);
		t.expect(support::screenOf(host.trace) == support::screenOf(watchedHost.trace), "a skip during wait-all did not leave every branch at its end state: [" + support::screenOf(host.trace).describe() + "] vs watched [" +
			support::screenOf(watchedHost.trace).describe() + "]");
		t.expect(it.status() == Status::finished && host.live_handles() == 0, "the skipped wait-all scene did not finish cleanly");
	}

	t.note("together waits for the slowest branch, whichever it is; race continues when its first branch finishes, whichever it is, cancels the other's command in flight and runs nothing more of it; a skip during together fast-forwards every branch to its end state");
}
