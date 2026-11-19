#include "support.h"

#include "events/save_state.h"

using namespace events;

namespace {

// The music the host was last asked to play before `count` trace lines, or empty.
std::string musicAt(const std::vector<std::string>& trace, std::size_t count) {
	std::string music;
	for (std::size_t i = 0; i < count && i < trace.size(); ++i) {
		const std::string& line = trace[i];
		if (line.rfind("action music play ", 0) == 0) {
			music = line.substr(18);
		} else if (line.rfind("action music stop", 0) == 0 || line.rfind("action music fade", 0) == 0) {
			music.clear();
		}
	}
	return music;
}

}

ACC_CHECK(acc_evt_04, "ACC-EVT-04", "EVT-022, EVT-051") {
	const Program program = support::compileSamples(t);
	const std::vector<support::Path> paths = support::allPaths(program, {{}, {"harbor.met_guide"}});
	int resumes = 0;
	int musicSaves = 0;
	int musicRebuilt = 0;
	for (const support::Path& path : paths) {
		support::DecisionHost played;
		support::configure(played);
		support::seed(played, path.world);
		played.picks = path.picks;
		Interpreter full(program, played);
		full.start(path.event);
		const int total = support::run(full, played);
		t.expect(full.status() == Status::finished, support::describe(path) + ": did not finish");

		for (int k = 1; k < total; ++k) {
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
			const std::string text = save_state_to_text(first.save());
			SaveState loaded;
			if (!t.expect(save_state_from_text(text, loaded), support::describe(path) + " step " + std::to_string(k) + ": the save text does not load")) {
				continue;
			}
			t.expect(save_state_to_text(loaded) == text, support::describe(path) + " step " + std::to_string(k) + ": the save text changes on a round trip");

			// A fresh interpreter over the loaded game: the world as saved, the remaining answers, no live handles.
			support::DecisionHost resumed;
			static_cast<RecordingHost&>(resumed) = host.reloaded();
			resumed.picks = std::vector<int>(path.picks.begin() + static_cast<long>(std::min(host.optionCounts.size(), path.picks.size())), path.picks.end());
			// A decision that was on screen when the save was taken is asked again.
			const SaveState state = first.save();
			const Op waitingOn = program.code[static_cast<std::size_t>(state.main.pc)].op;
			const std::size_t asked = host.optionCounts.size();
			if (asked > 0 && state.main.wait == WaitKind::handle && (waitingOn == Op::choice || waitingOn == Op::battle)) {
				resumed.picks.insert(resumed.picks.begin(), path.picks[asked - 1]);
			}
			Interpreter second(program, resumed);
			std::string error;
			if (!t.expect(second.restore(loaded, error), support::describe(path) + " step " + std::to_string(k) + ": restore failed: " + error)) {
				continue;
			}
			support::run(second, resumed);
			t.expect(second.status() == Status::finished, support::describe(path) + " step " + std::to_string(k) + ": the resumed run did not finish");
			t.expect(resumed.same_world(played), support::describe(path) + " step " + std::to_string(k) + ": final state differs from the uninterrupted run");
			++resumes;

			const std::string music = musicAt(host.trace, host.trace.size());
			if (!music.empty() && musicAt(played.trace, played.trace.size()) == music) {
				++musicSaves;
				musicRebuilt += !resumed.trace.empty() && resumed.trace.front() == "action music play " + music ? 1 : 0;
			}
		}
	}

	if (musicSaves > musicRebuilt) {
		t.gap("a restore does not rebuild presentation: in " + std::to_string(musicSaves - musicRebuilt) + " of " + std::to_string(musicSaves) +
			" saves taken while music played, the resumed interpreter never asked the host to restart it; SaveState holds no panel page, music, ambient or actor poses (EVT-022, EVT-051)");
	}
	t.note(std::to_string(resumes) + " saves at every step of every path, serialized to text and restored into a new interpreter, finish with the same game state as the uninterrupted run");
}
