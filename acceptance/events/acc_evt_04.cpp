#include "support.h"

#include "events/save_state.h"

#include <algorithm>

using namespace events;

namespace {

// EVT-051's rebuild order, by the kind of each instant call: music, ambient, camera, actors, panels, fade.
int rebuildRank(const std::string& line) {
	const std::string call = line.substr(7);
	if (call.rfind("music", 0) == 0) {
		return 0;
	}
	if (call.rfind("ambient", 0) == 0) {
		return 1;
	}
	if (call.rfind("camera", 0) == 0) {
		return 2;
	}
	if (call.rfind("panel", 0) == 0) {
		return 4;
	}
	if (call.rfind("fade", 0) == 0) {
		return 5;
	}
	return 3;
}

}

ACC_CHECK(acc_evt_04, "ACC-EVT-04", "EVT-022, EVT-051") {
	const Program program = support::compileSamples(t);
	const std::vector<support::Path> paths = support::allPaths(program, {{}, {"harbor.met_guide"}});
	int resumes = 0;
	int rebuilt = 0;
	for (const support::Path& path : paths) {
		support::DecisionHost played;
		support::configure(played);
		support::seed(played, path.world);
		played.picks = path.picks;
		Interpreter full(program, played);
		full.start(path.event);
		const int total = support::run(full, played);
		t.expect(full.status() == Status::finished, support::describe(path) + ": did not finish");
		const support::Screen finalScreen = support::screenOf(played.trace);

		for (int k = 1; k < total; ++k) {
			const std::string where = support::describe(path) + " step " + std::to_string(k);
			support::DecisionHost host;
			support::configure(host);
			support::seed(host, path.world);
			host.picks = path.picks;
			Interpreter first(program, host);
			first.start(path.event);
			for (int i = 0; i < k; ++i) {
				host.advance(support::kStep);
				first.step(support::kStep);
			}
			if (first.status() != Status::running) {
				break;
			}
			const SaveState state = first.save();
			const std::string text = save_state_to_text(state);
			SaveState loaded;
			if (!t.expect(save_state_from_text(text, loaded), where + ": the save text does not load")) {
				continue;
			}
			t.expect(save_state_to_text(loaded) == text, where + ": the save text changes on a round trip");
			t.expect(loaded.presentation == first.presentation(), where + ": the save does not hold the presentation that was active");

			// A fresh interpreter over the loaded game: the world as saved, the remaining answers, no live handles.
			support::DecisionHost resumed;
			static_cast<RecordingHost&>(resumed) = host.reloaded();
			resumed.picks = std::vector<int>(path.picks.begin() + static_cast<long>(std::min(host.optionCounts.size(), path.picks.size())), path.picks.end());
			// A decision that was on screen when the save was taken is asked again.
			const Op waitingOn = program.code[static_cast<std::size_t>(state.main.pc)].op;
			const std::size_t asked = host.optionCounts.size();
			if (asked > 0 && state.main.wait == WaitKind::handle && (waitingOn == Op::choice || waitingOn == Op::battle)) {
				resumed.picks.insert(resumed.picks.begin(), path.picks[asked - 1]);
			}
			Interpreter second(program, resumed);
			std::string error;
			if (!t.expect(second.restore(loaded, error), where + ": restore failed: " + error)) {
				continue;
			}
			resumed.advance(support::kStep);
			second.step(support::kStep);

			// The first step rebuilds the screen the player had, with instant calls in EVT-051's order, before anything else.
			const std::vector<std::string> rebuild = support::instantLines(resumed);
			const support::Screen atSave = support::screenOf(host.trace);
			t.expect(support::screenOf(rebuild) == atSave, where + ": the rebuilt screen [" + support::screenOf(rebuild).describe() + "] differs from the screen at save time [" + atSave.describe() + "]");
			t.expect(std::equal(rebuild.begin(), rebuild.end(), resumed.trace.begin(), resumed.trace.begin() + static_cast<long>(std::min(rebuild.size(), resumed.trace.size()))),
				where + ": the rebuild calls are not the first host calls after the restore");
			bool ordered = true;
			for (std::size_t i = 1; i < rebuild.size(); ++i) {
				ordered = ordered && rebuildRank(rebuild[i - 1]) <= rebuildRank(rebuild[i]);
			}
			t.expect(ordered, where + ": the rebuild is not in the order music, ambient, camera, actors, panels, fade");
			rebuilt += rebuild.empty() ? 0 : 1;

			support::run(second, resumed);
			t.expect(second.status() == Status::finished, where + ": the resumed run did not finish");
			t.expect(resumed.same_world(played), where + ": final state differs from the uninterrupted run");
			t.expect(second.presentation() == full.presentation(), where + ": final presentation state differs from the uninterrupted run");
			t.expect(support::screenOf(resumed.trace) == finalScreen, where + ": the screen at the end differs from the uninterrupted run");
			t.expect(resumed.live_handles() == 0 && resumed.errors.empty(), where + ": host handles leaked or misused after the restore");
			++resumes;
		}
	}
	t.note(std::to_string(resumes) + " saves at every step of every path of every sample script, serialized to text, restored into a new interpreter and finished: same game state, presentation state and final screen as the uninterrupted run");
	t.note(std::to_string(rebuilt) + " restores rebuilt the screen at save time (music, ambient, camera, actor poses, panels, fade) with instant calls in that order before any other call; the rest started from an empty screen");
}
