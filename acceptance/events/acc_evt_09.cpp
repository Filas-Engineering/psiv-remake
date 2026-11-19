#include "support.h"

#include "events/timing.h"

#include <cmath>

using namespace events;

ACC_CHECK(acc_evt_09, "ACC-EVT-09", "EVT-060, EVT-062") {
	const Program program = support::compileSamples(t);
	TimingConfig config;
	std::vector<Diagnostic> diagnostics;
	t.expect(load_timing_config("timing.cfg", support::readFile(t.path("events/sample_text/timing.cfg")), config, diagnostics), "setup: the sample timing config does not load");
	StringTable english;
	t.expect(load_string_table("en.strings", support::readFile(t.path("events/sample_text/en.strings")), english, diagnostics), "setup: the English table does not load");
	config.strings = &english;

	int timed = 0;
	for (const std::string& event : support::entryEvents(program)) {
		std::vector<TimedPath> paths;
		bool truncated = false;
		std::string error;
		if (!t.expect(time_event(program, event, config, paths, truncated, error), event + ": " + error)) {
			continue;
		}
		t.expect(!truncated && !paths.empty(), event + ": timing did not cover every path");
		for (const TimedPath& path : paths) {
			t.expect(path.status == Status::finished && path.error.empty(), event + ": a timed path did not finish: " + path.error);
			t.expect(std::fabs(path.time.dialogue + path.time.action + path.time.wait - path.time.total) < 1e-6, event + ": the time split does not add up to the total");
			t.expect(path.time.total > 0, event + ": a path took no time");
			t.expect(path.unmeasured_lines == 0, event + ": a line had no text to measure");
			++timed;
		}
		// Re-timing gives the same numbers, so the CI comparison is stable.
		std::vector<TimedPath> again;
		time_event(program, event, config, again, truncated, error);
		for (std::size_t i = 0; i < paths.size() && i < again.size(); ++i) {
			t.expect(again[i].time.total == paths[i].time.total, event + ": timing the same scene twice gave different totals");
		}
	}
	t.note(std::to_string(timed) + " paths of the sample scenes timed without rendering by the timing host, each split into dialogue, action and wait that sum to the total");
	t.note("the time is reported as overhead with text advanced as soon as each page completes: the timing host never waits on the player");
}
