#include "support.h"

#include <algorithm>

using namespace events;

namespace {

const char* const kParallel = R"(event acc_parallel skippable
	together
		move guide to pier_end
		sequence
			anim traveler wave
			face traveler guide
		end
	end
	say guide "Both done."
on skip
	set acc.skipped
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

std::vector<std::pair<int, std::string>> runLogged(RecordingHost& host, Interpreter& it, int skipAt) {
	std::vector<std::pair<int, std::string>> log;
	it.start("acc_parallel");
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

}

ACC_CHECK(acc_evt_07, "ACC-EVT-07", "EVT-008") {
	const CompileResult result = compile({{"acc_parallel.ev", kParallel}});
	if (!t.expect(result.ok(), "setup: the probe scene does not compile")) {
		return;
	}
	// Wait for all, with either branch finishing last.
	for (const auto& [move, wave] : std::vector<std::pair<double, double>>{{1.2, 0.3}, {0.3, 1.2}}) {
		RecordingHost host;
		host.set_duration(ActionType::move, move);
		host.set_duration(ActionType::animate, wave);
		host.set_duration(ActionType::face, 0.2);
		Interpreter it(result.program, host);
		const auto log = runLogged(host, it, -1);
		const int line = stepOf(log, "say guide");
		const int slowest = static_cast<int>(std::max(move, wave + 0.2) / support::kStep);
		t.expect(line >= slowest, "wait-all continued after " + std::to_string(line) + " steps, before its slowest branch finished (" + std::to_string(slowest) + " steps)");
		t.expect(stepOf(log, "action face") > stepOf(log, "action anim"), "a sequence branch did not run its steps in order");
		t.expect(it.status() == Status::finished, "the parallel scene did not finish");
	}

	// Stopped branches apply nothing after their last yield.
	{
		RecordingHost host;
		host.set_duration(ActionType::move, 1.0);
		host.set_duration(ActionType::animate, 1.0);
		Interpreter it(result.program, host);
		const auto log = runLogged(host, it, 3);
		t.expect(stepOf(log, "cancel move guide") >= 0 && stepOf(log, "cancel anim traveler") >= 0, "stopping the block did not cancel both running branches");
		t.expect(stepOf(log, "action face") < 0, "a stopped branch went on to its next step");
		t.expect(host.live_handles() == 0, "stopped branches left host handles live");
	}

	t.gap("there is no wait-for-any join: together always waits for every branch, and the format has no syntax to stop the rest when one finishes (EVT-008)");
	t.note("wait-all continues only after the slowest branch, whichever it is; branches stopped by a skip are cancelled and run nothing further");
}
