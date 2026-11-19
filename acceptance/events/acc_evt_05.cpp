#include "support.h"

#include <algorithm>

using namespace events;

ACC_CHECK(acc_evt_05, "ACC-EVT-05", "EVT-041, EVT-042") {
	const Program program = support::compileSamples(t);
	const std::vector<support::Path> paths = support::allPaths(program, {{}, {"harbor.met_guide"}});
	int cases = 0;
	int matched = 0;
	int crossedDecision = 0;
	int insideBranch = 0;
	std::string example;
	std::string branchExample;
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
			support::run(it, host);
			++cases;
			t.expect(it.status() == Status::finished, support::describe(path) + " skip at step " + std::to_string(k) + ": did not finish");
			t.expect(host.live_handles() == 0 && host.errors.empty(), support::describe(path) + " skip at step " + std::to_string(k) + ": host handles leaked or misused");
			if (host.same_world(watched)) {
				++matched;
				continue;
			}
			// A decision the watched run answered that the skipped run was never asked.
			bool skippedPast = false;
			for (std::size_t i = 0; i < watched.labels.size(); ++i) {
				const bool asked = std::find(host.labels.begin(), host.labels.end(), watched.labels[i]) != host.labels.end();
				skippedPast = skippedPast || !asked;
			}
			if (skippedPast) {
				++crossedDecision;
				if (example.empty()) {
					example = support::describe(path) + " skipped at step " + std::to_string(k);
				}
			} else {
				++insideBranch;
				if (branchExample.empty()) {
					branchExample = support::describe(path) + " skipped at step " + std::to_string(k);
				}
			}
		}
	}
	t.expect(crossedDecision == 0, std::to_string(crossedDecision) + " of " + std::to_string(cases) +
		" skips end in a different state from watching the same path, because skip ran the on-skip block past a choice or battle the player had not answered yet instead of stopping there (EVT-041, EVT-042); first: " + example);
	t.expect(insideBranch == 0, std::to_string(insideBranch) + " of " + std::to_string(cases) +
		" skips taken after a choice was answered but before its branch wrote its result end in a different state: the interpreter does not record a choice's result before the branch runs (EVT-006), so the on-skip block cannot see it; first: " + branchExample);
	t.note(std::to_string(matched) + " of " + std::to_string(cases) + " skips (every path, every step where skip is allowed) match the watched state, excluding play time and text log");
}
