#include "stress_data.h"
#include "test.h"

#include "game/runner.h"

#include <iostream>

using namespace game;

namespace {

// Work bounds, not times, so slow machines pass: a small copy of `make perf`'s world played by its script.
struct Played {
	SessionStats stats;
	int errors = 0;
	int busyActors = 0;
	int busyCells = 0;
};

Played playStress(const std::string& name, const std::string& timing) {
	const std::string root = test::freshDirectory(name);
	std::string summary;
	const std::string problem = stress_data::make("data", "../core/data", root,
		{"areas=12", "width=32", "height=24", "busy_width=64", "busy_height=48", "actors=140", "hooks=240", "busy_hooks=80", "events=400", "tour=6", "rounds=1",
			"timing=" + timing},
		summary);
	Played played;
	CHECK_EQ(problem, std::string());
	const rules::Result<GameData> data = loadGameData(root + "/game/data");
	CHECK(data.ok());
	if (!problem.empty() || !data.ok()) {
		std::cout << problem << data.error << "\n";
		return played;
	}
	std::string script;
	CHECK(readFile(root + "/stress.txt", script));
	SessionOptions options;
	options.seed = 6;
	options.saveDirectory = test::freshDirectory(name + "_saves");
	Session session(*data.value, options);
	std::ostringstream transcript;
	RunnerOptions runnerOptions;
	runnerOptions.echo = true;
	Runner runner(session, transcript, runnerOptions);
	runner.runScript(script);
	played.stats = session.stats();
	played.errors = runner.errors();
	for (const ActorDef& def : data.value->config.actorDefs) {
		played.busyActors += def.area == "stress_000" ? 1 : 0;
	}
	played.busyCells = data.value->area("stress_000")->width() * data.value->area("stress_000")->height();
	return played;
}

void checkBounds(const Played& p) {
	const SessionStats& s = p.stats;
	CHECK_EQ(p.errors, 0);
	CHECK(s.ticks > 1000 && s.walkPlans > 100 && s.autosaves > 10 && s.loads > 0);
	// Each path search reaches a cell once.
	CHECK(s.walkCells <= s.walkPlans * static_cast<std::uint64_t>(p.busyCells));
	// A tick builds each actor's view a few times at most (the leader's step, the wanderers and patrols, the visible enemies),
	// and a path search once, not once per wanderer or per cell searched.
	const std::uint64_t bound = 4 * static_cast<std::uint64_t>(p.busyActors) * (s.ticks + s.walkPlans) + 200000;
	if (s.actorViews > bound) {
		std::cout << "actor views " << s.actorViews << " over " << bound << " (" << s.ticks << " ticks, " << s.walkPlans << " path searches, " << p.busyActors
				  << " actors in the busy area)\n";
	}
	CHECK(s.actorViews <= bound);
	CHECK(s.wanderTurns <= s.ticks * static_cast<std::uint64_t>(p.busyActors));
}

}

TEST(stressWorldStaysWithinWorkBounds) {
	checkBounds(playStress("stress_ticks", "ticks"));
	checkBounds(playStress("stress_exact", "exact"));
}
