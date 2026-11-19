#include "support.h"

#include <algorithm>

using namespace events;

namespace {

// Lines the player was shown, and lines a skip passed over (logged for the text log).
std::vector<std::string> linesOf(const std::vector<std::string>& trace, bool skipped) {
	std::vector<std::string> out;
	for (const std::string& line : trace) {
		const std::string text = skipped ? (line.rfind("skipped ", 0) == 0 ? line.substr(8) : "") : line;
		if (text.rfind("say ", 0) == 0 || text.rfind("narrate ", 0) == 0) {
			out.push_back(text);
		}
	}
	return out;
}

}

ACC_CHECK(acc_evt_05, "ACC-EVT-05", "EVT-041, EVT-042") {
	const Program program = support::compileSamples(t);
	const std::vector<support::Path> paths = support::allPaths(program, {{}, {"harbor.met_guide"}});
	int cases = 0;
	int stops = 0;
	int resumedSkips = 0;
	for (const support::Path& path : paths) {
		support::DecisionHost watched;
		support::configure(watched);
		support::seed(watched, path.world);
		watched.picks = path.picks;
		Interpreter full(program, watched);
		full.start(path.event);
		const int total = support::run(full, watched);
		std::map<std::string, int> answers;
		for (std::size_t i = 0; i < watched.labels.size(); ++i) {
			answers[watched.labels[i]] = watched.given[i];
		}
		const std::vector<std::string> watchedChanges = support::stateChanges(watched.trace);
		const support::Screen watchedScreen = support::screenOf(watched.trace);
		const std::vector<std::string> watchedLines = linesOf(watched.trace, false);

		// Skip once at step k; or skip at k and again every time skip is offered after a stop.
		for (const bool again : {false, true}) {
			for (int k = 0; k < total; ++k) {
				support::DecisionHost host;
				support::configure(host);
				support::seed(host, path.world);
				host.pickByLabel = true;
				host.byLabel = answers;
				Interpreter it(program, host);
				it.start(path.event);
				for (int i = 0; i < k; ++i) {
					host.advance(support::kStep);
					it.step(support::kStep);
				}
				if (!it.can_skip()) {
					continue;
				}
				it.skip();
				for (int i = 0; it.status() == Status::running && i < 20000; ++i) {
					if (again && it.can_skip()) {
						it.skip();
						++resumedSkips;
					}
					host.advance(support::kStep);
					it.step(support::kStep);
				}
				++cases;
				const std::string where = support::describe(path) + (again ? " skipping every time" : " skipped once") + " at step " + std::to_string(k);
				t.expect(it.status() == Status::finished, where + ": did not finish");
				t.expect(host.live_handles() == 0 && host.errors.empty(), where + ": host handles leaked or misused");
				t.expect(host.same_world(watched), where + ": the game state differs from watching the same choices and battles (EVT-042)");
				t.expect(host.labels == watched.labels, where + ": the skip did not stop at every choice and battle the watched run presented");
				t.expect(support::stateChanges(host.trace) == watchedChanges, where + ": the state-changing host calls differ from watching, or came in another order");
				t.expect(support::screenOf(host.trace) == watchedScreen, where + ": the screen after skipping [" + support::screenOf(host.trace).describe() + "] is not the watched end state [" + watchedScreen.describe() + "]");
				t.expect(it.presentation() == full.presentation(), where + ": the interpreter's presentation state differs from watching");
				std::vector<std::string> accounted = linesOf(host.trace, false);
				const std::vector<std::string> skipped = linesOf(host.trace, true);
				accounted.insert(accounted.end(), skipped.begin(), skipped.end());
				bool everyLine = true;
				for (const std::string& line : watchedLines) {
					everyLine = everyLine && std::find(accounted.begin(), accounted.end(), line) != accounted.end();
				}
				t.expect(everyLine, where + ": a line the watched run showed was neither shown nor logged as skipped");
				stops += static_cast<int>(host.labels.size());
			}
		}
	}
	t.note(std::to_string(cases) + " skips over " + std::to_string(paths.size()) + " paths of every sample script (each step where skip is offered, skipping once, and skipping again whenever skip was offered after a stop: " +
		std::to_string(resumedSkips) + " further skips) end with the watched game state, the same state-changing host calls in the same order, the watched end-state screen and presentation; every skip stopped at all " +
		std::to_string(stops) + " choices and battles on its path; every passed-over line went to the text log");
}
