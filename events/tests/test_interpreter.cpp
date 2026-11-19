#include "check.h"

using events::ActionType;
using events::BattleResult;
using events::Interpreter;
using events::Program;
using events::RecordingHost;
using events::Status;

namespace {

std::vector<std::string> spoken(const RecordingHost& host) {
	std::vector<std::string> out;
	for (const std::string& line : host.trace) {
		if (line.rfind("say ", 0) == 0 || line.rfind("narrate ", 0) == 0) {
			out.push_back(line);
		}
	}
	return out;
}

// Step index (1-based) at which a trace line was first logged, or -1.
struct Timeline {
	std::vector<int> step_of_line;
	int steps = 0;

	int step_of(const RecordingHost& host, const std::string& line) const {
		for (std::size_t i = 0; i < host.trace.size(); ++i) {
			if (host.trace[i] == line) {
				return step_of_line[i];
			}
		}
		return -1;
	}
};

Timeline run_timed(Interpreter& interpreter, RecordingHost& host, double dt) {
	Timeline t;
	int step = 0;
	while (interpreter.status() == Status::running && step < 1000) {
		++step;
		host.advance(dt);
		interpreter.step(dt);
		while (t.step_of_line.size() < host.trace.size()) {
			t.step_of_line.push_back(step);
		}
	}
	t.steps = step;
	return t;
}

void check_clean(const RecordingHost& host) {
	CHECK_EQ(host.live_handles(), 0u);
	CHECK(host.errors.empty());
}

}

TEST(if_chain_branches_on_flags_variables_and_comparisons) {
	Program p = test::compile_ok(R"(event a unskippable
	if rested and coins >= 10
		say guide "rich and rested"
	else if not rested or coins < 0
		say guide "tired or in debt"
	else if coins == 5
		say guide "exactly five"
	else
		say guide "something else"
	end
end
)");
	struct Case {
		bool rested;
		long long coins;
		const char* expected;
	};
	const Case cases[] = {
		{true, 10, "say guide \"rich and rested\""},
		{false, 50, "say guide \"tired or in debt\""},
		{true, -2, "say guide \"tired or in debt\""},
		{true, 5, "say guide \"exactly five\""},
		{true, 7, "say guide \"something else\""},
	};
	for (const Case& c : cases) {
		RecordingHost host;
		host.flags["rested"] = c.rested;
		host.vars["coins"] = c.coins;
		Interpreter it(p, host);
		CHECK(it.start("a"));
		test::run(it, host);
		CHECK(it.status() == Status::finished);
		CHECK_EQ(spoken(host).size(), 1u);
		CHECK_EQ(spoken(host).at(0), std::string(c.expected));
		check_clean(host);
	}
}

TEST(variables_loops_and_goto) {
	Program p = test::compile_ok(R"(event a unskippable
	var n = 4
	var n += 3
	var n -= 10
	var laps = 0
	while laps < 4
		var laps += 1
		narrate "lap"
	end
	var k = 0
	label again
	var k += 1
	if k < 3
		goto again
	end
	goto done
	narrate "skipped by goto"
	label done
	narrate "after"
end
)");
	RecordingHost host;
	Interpreter it(p, host);
	it.start("a");
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	CHECK_EQ(host.vars["n"], -3);
	CHECK_EQ(host.vars["laps"], 4);
	CHECK_EQ(host.vars["k"], 3);
	std::vector<std::string> expected = {"narrate \"lap\"", "narrate \"lap\"", "narrate \"lap\"", "narrate \"lap\"", "narrate \"after\""};
	CHECK(spoken(host) == expected);
}

TEST(choices_take_the_picked_branch) {
	Program p = test::compile_ok(R"(event a unskippable
	choice "Which road?"
	option "coast"
		say guide "coast"
	option "hills"
		say guide "hills"
	option @road.forest
		say guide "forest"
	end
	ask "Rest here?"
	yes
		say guide "resting"
	no
		say guide "moving on"
	end
	ask "Sure?"
	no
		say guide "only no has a branch"
	end
	say guide "done"
end
)");
	RecordingHost host;
	host.queue_choices({2, 1, 0});
	Interpreter it(p, host);
	it.start("a");
	test::run(it, host);
	std::vector<std::string> expected = {
		"choice \"Which road?\" [\"coast\" | \"hills\" | @road.forest] -> 2",
		"say guide \"forest\"",
		"ask \"Rest here?\" -> 1",
		"say guide \"moving on\"",
		"ask \"Sure?\" -> 0",
		"say guide \"done\"",
	};
	CHECK(host.trace == expected);
	check_clean(host);
}

TEST(waits_and_blocking_actions_span_steps) {
	Program p = test::compile_ok(R"(event a unskippable
	sound start
	wait 1.0
	sound after_wait
	move guide to pier
	sound after_move
	say guide "read slowly"
	sound after_line
end
)");
	RecordingHost host;
	host.set_duration(ActionType::move, 0.5);
	host.set_line_duration(0.75);
	Interpreter it(p, host);
	it.start("a");
	Timeline t = run_timed(it, host, 0.25);
	CHECK_EQ(t.step_of(host, "action sound start"), 1);
	// The wait starts in step 1 and loses 0.25 s in each of steps 2 to 5.
	CHECK_EQ(t.step_of(host, "action sound after_wait"), 5);
	CHECK_EQ(t.step_of(host, "action move guide to pier"), 5);
	CHECK_EQ(t.step_of(host, "action sound after_move"), 7);
	CHECK_EQ(t.step_of(host, "action sound after_line"), 10);
	CHECK(it.status() == Status::finished);
	check_clean(host);
}

TEST(parallel_branches_join_whichever_finishes_last) {
	Program p = test::compile_ok(R"(event a unskippable
	together
		sequence
			move guide to pier
			sound guide_arrived
		end
		sequence
			anim traveler stretch
			sound traveler_done
		end
		wait 0.3
	end
	sound all_done
end
)");
	for (int order = 0; order < 2; ++order) {
		RecordingHost host;
		host.set_duration(ActionType::move, order == 0 ? 0.2 : 0.6);
		host.set_duration(ActionType::animate, order == 0 ? 0.6 : 0.2);
		Interpreter it(p, host);
		it.start("a");
		Timeline t = run_timed(it, host, 0.1);
		int guide = t.step_of(host, "action sound guide_arrived");
		int traveler = t.step_of(host, "action sound traveler_done");
		int done = t.step_of(host, "action sound all_done");
		CHECK_EQ(t.step_of(host, "action move guide to pier"), 1);
		CHECK_EQ(t.step_of(host, "action anim traveler stretch"), 1);
		CHECK_EQ(order == 0 ? guide : traveler, 3);
		CHECK_EQ(order == 0 ? traveler : guide, 7);
		CHECK_EQ(done, 7);
		CHECK(host.trace.back() == "action sound all_done");
		check_clean(host);
	}
}

TEST(nowait_actions_run_in_background_until_sync_or_event_end) {
	Program p = test::compile_ok(R"(event a unskippable
	move guide to pier nowait
	sound started
	sync
	sound synced
	move traveler to dock nowait
	sound last_command
end
)");
	RecordingHost host;
	host.set_duration(ActionType::move, 0.5);
	Interpreter it(p, host);
	it.start("a");
	Timeline t = run_timed(it, host, 0.1);
	CHECK_EQ(t.step_of(host, "action sound started"), 1);
	CHECK_EQ(t.step_of(host, "action sound synced"), 6);
	CHECK_EQ(t.step_of(host, "action sound last_command"), 6);
	// The event only ends once the traveler arrives.
	CHECK_EQ(t.steps, 11);
	CHECK(it.status() == Status::finished);
	check_clean(host);
}

TEST(nested_calls_return_to_their_callers) {
	Program p = test::compile_ok(R"(event outer unskippable
	say guide "outer start"
	call middle
	say guide "outer end"
end
event middle unskippable
	say guide "middle start"
	call inner
	say guide "middle end"
end
event inner unskippable
	say guide "inner"
	if early
		return
	end
	say guide "inner late"
end
)");
	for (bool early : {true, false}) {
		RecordingHost host;
		host.flags["early"] = early;
		Interpreter it(p, host);
		it.start("outer");
		test::run(it, host);
		std::vector<std::string> expected = {"say guide \"outer start\"", "say guide \"middle start\"", "say guide \"inner\""};
		if (!early) {
			expected.push_back("say guide \"inner late\"");
		}
		expected.push_back("say guide \"middle end\"");
		expected.push_back("say guide \"outer end\"");
		CHECK(spoken(host) == expected);
		CHECK(it.status() == Status::finished);
	}
}

TEST(battle_results_pick_branches) {
	Program p = test::compile_ok(R"(event a unskippable
	battle reef_crabs
	win
		set crabs.beaten
	lose
		warp shore landing
	flee
		var crabs.fled += 1
	end
	battle reef_crabs
	win
		say guide "again"
	end
	say guide "after"
end
)");
	const BattleResult results[] = {BattleResult::win, BattleResult::lose, BattleResult::flee};
	for (BattleResult r : results) {
		RecordingHost host;
		host.queue_battle_results({r, BattleResult::flee});
		Interpreter it(p, host);
		it.start("a");
		test::run(it, host);
		CHECK(it.status() == Status::finished);
		CHECK_EQ(host.flags.count("crabs.beaten"), r == BattleResult::win ? 1u : 0u);
		CHECK_EQ(host.vars.count("crabs.fled"), r == BattleResult::flee ? 1u : 0u);
		bool warped = false;
		for (const std::string& line : host.trace) {
			warped = warped || line == "action warp shore landing";
		}
		CHECK_EQ(warped, r == BattleResult::lose);
		// The second battle has no flee branch, so fleeing carries on after it.
		CHECK(spoken(host) == std::vector<std::string>{"say guide \"after\""});
		check_clean(host);
	}
}

TEST(runtime_errors_stop_the_event_and_release_handles) {
	Program p = test::compile_ok(R"(event bad_choice unskippable
	move guide to pier nowait
	choice
	option "a"
	option "b"
	end
end
event spin unskippable
	label top
	goto top
end
event recurse unskippable
	call recurse
end
)");
	{
		RecordingHost host;
		host.set_duration(ActionType::move, 5);
		host.queue_choices({5});
		Interpreter it(p, host);
		it.start("bad_choice");
		test::run(it, host);
		CHECK(it.status() == Status::error);
		CHECK(it.error().find("test.ev:3: the host returned result 5") == 0);
		check_clean(host);
	}
	{
		RecordingHost host;
		Interpreter it(p, host);
		it.start("spin");
		CHECK(it.step(0.1) == Status::error);
		CHECK(it.error().find("without waiting") != std::string::npos);
	}
	{
		RecordingHost host;
		Interpreter it(p, host);
		it.start("recurse");
		CHECK(it.step(0.1) == Status::error);
		CHECK(it.error().find("nested more than 32 deep") != std::string::npos);
	}
	{
		RecordingHost host;
		Interpreter it(p, host);
		CHECK(!it.start("missing"));
		CHECK(it.status() == Status::error);
	}
}
