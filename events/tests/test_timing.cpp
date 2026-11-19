#include "check.h"

#include "events/timing.h"

#include <cmath>

using events::ActionType;
using events::Diagnostic;
using events::TimedPath;
using events::TimingConfig;

namespace {

bool near(double a, double b) {
	return std::fabs(a - b) < 0.01;
}

std::vector<TimedPath> time_ok(const std::string& script, const std::string& event, const TimingConfig& config) {
	events::Program p = test::compile_ok(script);
	std::vector<TimedPath> paths;
	bool truncated = true;
	std::string error;
	CHECK(events::time_event(p, event, config, paths, truncated, error));
	CHECK(!truncated);
	return paths;
}

}

TEST(timing_splits_dialogue_action_and_wait_and_counts_characters) {
	TimingConfig config;
	config.chars_per_second = 10;
	config.seconds_per_line = 0.5;
	config.action_seconds[ActionType::move] = 2;
	config.subject_seconds[{ActionType::move, "keeper"}] = 3;
	std::vector<TimedPath> paths = time_ok(R"(event scene unskippable
	say guide "0123456789"
	move guide to pier
	move keeper to pier
	wait 1.5
	fade out 0.75
	narrate "ねこがいる"
	move guide to dock nowait
	wait 0.25
	say guide "Hi"
	sync
end
)", "scene", config);
	CHECK_EQ(paths.size(), 1u);
	if (paths.empty()) {
		return;
	}
	const events::SceneTime& t = paths[0].time;
	// Lines: 10 chars -> 1.5 s, 5 code points -> 1.0 s, "Hi" -> 0.7 s.
	// The background move lasts 2 s: it covers the 0.25 s wait, then the
	// 0.7 s line counts as dialogue, then the move's last 1.05 s as action.
	CHECK(near(t.dialogue, 1.5 + 1.0 + 0.7));
	CHECK(near(t.action, 2 + 3 + 0.75 + 0.25 + 1.05));
	CHECK(near(t.wait, 1.5));
	CHECK(near(t.total, t.dialogue + t.action + t.wait));
	CHECK(near(t.total, 1.5 + 2 + 3 + 1.5 + 0.75 + 1.0 + 2));
	CHECK(paths[0].error.empty());
}

TEST(timing_runs_every_branch_of_every_choice_and_allowed_battle_result) {
	TimingConfig config;
	config.chars_per_second = 0;
	std::vector<TimedPath> paths = time_ok(R"(event scene unskippable
	ask "Go?"
	yes
		wait 1
	no
		wait 2
	end
	choice
	option "a"
		wait 10
	option "b"
	option "c"
		battle crabs
		win
			wait 100
		flee
		end
	end
end
)", "scene", config);
	// 2 answers x (a, b, c-win, c-flee); lose is not allowed without a section.
	CHECK_EQ(paths.size(), 8u);
	std::vector<double> totals;
	std::vector<std::string> labels;
	for (const TimedPath& p : paths) {
		totals.push_back(std::round(p.time.total * 100) / 100);
		std::string label;
		for (const events::Decision& d : p.decisions) {
			label += d.options[d.picked] + " ";
		}
		labels.push_back(label);
	}
	CHECK(totals == (std::vector<double>{11, 1, 101, 1, 12, 2, 102, 2}));
	CHECK_EQ(labels[0], std::string("yes \"a\" "));
	CHECK_EQ(labels[2], std::string("yes \"c\" win "));
	CHECK_EQ(labels[3], std::string("yes \"c\" flee "));
	CHECK_EQ(paths[3].decisions[2].label, std::string("battle crabs"));
	CHECK_EQ(labels[7], std::string("no \"c\" flee "));
}

TEST(timing_measures_string_ids_through_a_table_and_counts_unmeasured_ones) {
	events::StringTable table;
	std::vector<Diagnostic> d;
	CHECK(events::load_string_table("en.strings", "language en\nscene.hello = 12345\n", table, d));
	TimingConfig config;
	config.chars_per_second = 5;
	const char* script = "event scene unskippable\n\tsay guide @scene.hello\n\tsay guide @scene.missing\nend\n";
	std::vector<TimedPath> without = time_ok(script, "scene", config);
	if (without.empty()) {
		return;
	}
	CHECK_EQ(without[0].unmeasured_lines, 2);
	CHECK(near(without[0].time.total, 0));
	config.strings = &table;
	std::vector<TimedPath> with = time_ok(script, "scene", config);
	if (with.empty()) {
		return;
	}
	CHECK_EQ(with[0].unmeasured_lines, 1);
	CHECK(near(with[0].time.dialogue, 1));
}

TEST(timing_stops_at_the_path_limit_and_rejects_unknown_events) {
	events::Program p = test::compile_ok(R"(event scene unskippable
	ask "One?"
	end
	ask "Two?"
	end
	ask "Three?"
	end
end
)");
	std::vector<TimedPath> paths;
	bool truncated = false;
	std::string error;
	CHECK(events::time_event(p, "scene", TimingConfig(), paths, truncated, error, 0.01, 3));
	CHECK_EQ(paths.size(), 3u);
	CHECK(truncated);
	CHECK(!events::time_event(p, "nope", TimingConfig(), paths, truncated, error));
	CHECK(error.find("unknown event 'nope'") != std::string::npos);
}

TEST(timing_config_file_sets_speeds_and_durations) {
	TimingConfig config;
	std::vector<Diagnostic> d;
	CHECK(events::load_timing_config("t.cfg", "# speeds\ntext_speed 24   # per second\nline_pause 0.3\nchoice_time 1\nbattle_time 2\n"
		"duration move 1.5\nduration camera_move pier_cam 4\n", config, d));
	CHECK(near(config.chars_per_second, 24));
	CHECK(near(config.seconds_per_line, 0.3));
	CHECK(near(config.seconds_per_choice, 1));
	CHECK(near(config.seconds_per_battle, 2));
	CHECK(near(config.action_seconds[ActionType::move], 1.5));
	CHECK(near(config.subject_seconds[{ActionType::camera_move, "pier_cam"}], 4));

	TimingConfig untouched;
	CHECK(!events::load_timing_config("t.cfg", "text_speed fast\nduration teleport 1\nduration move -1\nvolume 3\nduration move\n", untouched, d));
	CHECK(test::has_diagnostic(d, 1, "text_speed: expected one non-negative number"));
	CHECK(test::has_diagnostic(d, 2, "unknown action 'teleport'"));
	CHECK(test::has_diagnostic(d, 3, "non-negative number of seconds"));
	CHECK(test::has_diagnostic(d, 4, "unknown setting 'volume'"));
	CHECK(test::has_diagnostic(d, 5, "expected '<action> [<subject>] <seconds>'"));
	CHECK(near(untouched.chars_per_second, 30));
}

TEST(timing_runs_start_from_the_configured_world) {
	TimingConfig config;
	config.start_flags["dock.open"] = true;
	config.start_vars["dock.crates"] = 2;
	std::vector<TimedPath> paths = time_ok(R"(event scene unskippable
	if dock.open
		wait 1
	end
	while dock.crates > 0
		wait 0.5
		var dock.crates -= 1
	end
end
)", "scene", config);
	CHECK_EQ(paths.size(), 1u);
	if (!paths.empty()) {
		CHECK(near(paths[0].time.wait, 2));
	}
}
