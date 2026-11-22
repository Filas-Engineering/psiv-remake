#include "check.h"

#include <algorithm>

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

TEST(position_names_the_innermost_event_and_the_waiting_line) {
	Program p = test::compile_ok(R"(event outer unskippable
	call inner
	together
		wait 1.0
		sequence
			move guide to pier
		end
	end
end
event inner unskippable
	sound first
	wait 0.5
end
)");
	RecordingHost host;
	host.set_duration(ActionType::move, 0.5);
	Interpreter it(p, host);
	CHECK(it.position().event.empty());
	it.start("outer");
	it.step(0);
	CHECK_EQ(it.position().event, std::string("inner"));
	CHECK_EQ(it.position().file, std::string("test.ev"));
	CHECK_EQ(it.position().line, 12);
	host.advance(0.5);
	it.step(0.5);
	// Back in the caller, waiting on the together block: its first live branch, the timed wait.
	CHECK_EQ(it.position().event, std::string("outer"));
	CHECK_EQ(it.position().line, 4);
	test::run(it, host);
	CHECK(it.position().event.empty());
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
		host.queue_battle_results({r, BattleResult::win});
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
		CHECK(spoken(host) == (std::vector<std::string>{"say guide \"again\"", "say guide \"after\""}));
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

TEST(race_continues_when_its_first_branch_ends_and_stops_the_rest) {
	Program p = test::compile_ok(R"(event a unskippable
	race
		sequence
			move guide to pier
			sound guide_arrived
		end
		sequence
			anim traveler stretch
			sound traveler_done
		end
	end
	sound race_over
end
)");
	for (int order = 0; order < 2; ++order) {
		RecordingHost host;
		host.set_duration(ActionType::move, order == 0 ? 0.2 : 0.6);
		host.set_duration(ActionType::animate, order == 0 ? 0.6 : 0.2);
		Interpreter it(p, host);
		it.start("a");
		Timeline t = run_timed(it, host, 0.1);
		const std::string winner = order == 0 ? "action sound guide_arrived" : "action sound traveler_done";
		const std::string loser = order == 0 ? "action sound traveler_done" : "action sound guide_arrived";
		const std::string cancelled = order == 0 ? "cancel anim traveler stretch" : "cancel move guide to pier";
		CHECK_EQ(t.step_of(host, winner), 3);
		CHECK_EQ(t.step_of(host, cancelled), 3);
		CHECK_EQ(t.step_of(host, "action sound race_over"), 3);
		// The stopped branch never reaches its next command.
		CHECK_EQ(t.step_of(host, loser), -1);
		CHECK(it.status() == Status::finished);
		check_clean(host);
	}
}

TEST(a_race_won_by_a_wait_cancels_the_slower_action) {
	Program p = test::compile_ok(R"(event a unskippable
	race
		camera move far_cam 5.0
		wait 0.5
	end
	say guide "Moving on."
end
)");
	RecordingHost host;
	Interpreter it(p, host);
	it.start("a");
	Timeline t = run_timed(it, host, 0.1);
	CHECK_EQ(t.step_of(host, "cancel camera move far_cam 5"), 6);
	CHECK_EQ(t.step_of(host, "say guide \"Moving on.\""), 6);
	check_clean(host);
}

namespace {

// A host that runs no menus: the Host defaults answer with no handle.
class PlainHost : public RecordingHost {
public:
	events::Handle open_shop(const std::string& shop) override { return events::Host::open_shop(shop); }
};

}

TEST(menus_and_rests_are_blocking_host_calls) {
	Program p = test::compile_ok(R"(event a unskippable
	shop dock_stall
	rest
	rest dock_inn
	save prompt
	name entry traveler
	say guide "All done."
end
)");
	RecordingHost host;
	Interpreter it(p, host);
	it.start("a");
	test::run(it, host);
	std::vector<std::string> expected = {"shop dock_stall", "rest", "rest dock_inn", "save prompt", "name entry traveler", "say guide \"All done.\""};
	CHECK(host.trace == expected);
	CHECK(it.status() == Status::finished);
	check_clean(host);

	PlainHost plain;
	Interpreter refused(p, plain);
	refused.start("a");
	test::run(refused, plain);
	CHECK(refused.status() == Status::error);
	CHECK_EQ(refused.error(), std::string("test.ev:2: the host does not support 'shop': Host::open_shop returned no handle"));
}

TEST(a_skip_stops_at_a_menu_but_rests_while_skipping) {
	Program p = test::compile_ok(R"(event a skippable
	say guide "Long day."
	rest dock_inn
	say guide "Fresh again."
	shop dock_stall
	say guide "Bye."
end
)");
	RecordingHost host;
	host.set_line_duration(1);
	Interpreter it(p, host);
	it.start("a");
	it.step(0.1);
	CHECK(it.skip());
	it.step(0.1);
	CHECK(host.instant_calls == std::vector<std::string>{"rest dock_inn"});
	CHECK(!it.skipping());
	CHECK_EQ(host.trace.back(), std::string("say guide \"Bye.\""));
	std::vector<std::string> expected = {"say guide \"Long day.\"", "cancel say guide \"Long day.\"", "rest dock_inn", "skipped say guide \"Fresh again.\"",
		"shop dock_stall", "say guide \"Bye.\""};
	CHECK(host.trace == expected);
}

TEST(control_tags_and_name_tokens_reach_the_host_as_written) {
	Program p = test::compile_ok(R"(event a skippable
	say keeper "{portrait:keeper_tired}Again, {var:leader}?{wait:30}{page}The {name_upper:moth_swarm}." as cheerful
	narrate "Gone.{wait:10}"
end
)");
	RecordingHost host;
	Interpreter it(p, host);
	it.start("a");
	test::run(it, host);
	CHECK(spoken(host) == (std::vector<std::string>{
		"say keeper \"{portrait:keeper_tired}Again, {var:leader}?{wait:30}{page}The {name_upper:moth_swarm}.\" as cheerful",
		"narrate \"Gone.{wait:10}\"",
	}));
	check_clean(host);
}

TEST(vehicle_commands_reach_the_host_and_vehicle_tests_read_it) {
	Program p = test::compile_ok(R"(event a skippable
	if not vehicle skiff
		vehicle give skiff harbor dock
	end
	say guide "aboard?"
	if vehicle skiff and not aboard skiff
		vehicle place skiff harbor pier
	end
	if aboard skiff or vehicle raft
		say guide "never"
	end
	vehicle remove raft
end
)");
	RecordingHost host;
	Interpreter it(p, host);
	CHECK(it.start("a"));
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	CHECK_EQ(host.vehicles.size(), std::size_t(1));
	CHECK_EQ(host.vehicles.at("skiff"), std::string("harbor pier"));
	CHECK_EQ(spoken(host).size(), 1u);
	CHECK(std::find(host.trace.begin(), host.trace.end(), "vehicle give skiff harbor dock") != host.trace.end());
	CHECK(std::find(host.trace.begin(), host.trace.end(), "vehicle remove raft") != host.trace.end());

	// A skip applies the vehicle changes in order, as watching does.
	RecordingHost skipped;
	Interpreter again(p, skipped);
	CHECK(again.start("a"));
	CHECK(again.skip());
	test::run(again, skipped);
	CHECK(again.status() == Status::finished);
	CHECK(skipped.same_world(host));
	CHECK_EQ(events::describe(p.code[0].condition), std::string("not vehicle skiff"));
	check_clean(host);
}

TEST(party_and_item_tests_read_the_host) {
	Program p = test::compile_ok(R"(event a unskippable
	if party scout and not item rusty_key
		say guide "the scout, no key"
	end
	if item tonic or party medic
		say guide "a tonic"
	end
	if party medic
		say guide "never"
	end
end
)");
	RecordingHost host;
	host.party = {"fighter", "scout"};
	host.items["tonic"] = 2;
	Interpreter it(p, host);
	CHECK(it.start("a"));
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	CHECK_EQ(spoken(host).size(), 2u);
	CHECK_EQ(events::describe(p.code[0].condition), std::string("party scout and not item rusty_key"));
	check_clean(host);
}

TEST(learn_and_follower_reach_the_host_and_apply_while_skipping) {
	Program p = test::compile_ok(R"(event a skippable
	learn medic hallow
	follower peddler
	say guide "walk with us"
	follower none
end
)");
	RecordingHost host;
	Interpreter it(p, host);
	CHECK(it.start("a"));
	CHECK(it.skip());
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	const std::vector<std::string> expected = {"learn medic hallow", "follower peddler", "follower none"};
	std::vector<std::string> seen;
	for (const std::string& line : host.trace) {
		if (line.rfind("learn ", 0) == 0 || line.rfind("follower ", 0) == 0) {
			seen.push_back(line);
		}
	}
	CHECK(seen == expected);
}

TEST(money_equipment_and_area_music_reach_the_host_and_apply_while_skipping) {
	Program p = test::compile_ok(R"(event a skippable
	money give 30
	if money >= 25
		money take 25
	end
	equip fighter gill_knife
	unequip fighter tin_cap
	music area harbor_theme
	say guide "done"
	music area default
end
)");
	RecordingHost host;
	Interpreter it(p, host);
	CHECK(it.start("a"));
	CHECK(it.skip());
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	CHECK_EQ(host.purse, 5);
	const std::vector<std::string> expected = {"money 30", "money -> 30", "money -25", "equip fighter gill_knife", "unequip fighter tin_cap",
		"music area harbor_theme", "music area default"};
	std::vector<std::string> seen;
	for (const std::string& line : host.trace) {
		if (line.rfind("money", 0) == 0 || line.rfind("music area", 0) == 0 || line.rfind("equip ", 0) == 0 || line.rfind("unequip ", 0) == 0) {
			seen.push_back(line);
		}
	}
	CHECK_EQ(test::join(seen), test::join(expected));
}

namespace {

// Answers the leader, music, area and tile tests from fixed values.
struct StateHost : RecordingHost {
	bool game_state(const std::string& test, const std::string& value) override {
		trace.push_back("state " + test + " " + value);
		return (test == "leader" && value == "fighter") || (test == "music" && value == "harbor_theme") || (test == "area" && value == "cove") ||
			(test == "tile" && value == "water");
	}
};

}

TEST(leader_music_area_and_tile_conditions_ask_the_host) {
	Program p = test::compile_ok(R"(event a skippable
	if leader fighter and music harbor_theme and area cove and tile water
		say guide "all four"
	end
	if leader medic or not tile floor
		say guide "either"
	end
end
)");
	StateHost host;
	Interpreter it(p, host);
	CHECK(it.start("a"));
	CHECK(it.skip());
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	const std::string trace = test::join(host.trace);
	CHECK(trace.find("all four") != std::string::npos && trace.find("either") != std::string::npos);
	CHECK(trace.find("state tile water") != std::string::npos);
	CHECK(!test::compile_errors("event b skippable\n\tif tile lava\n\t\tsay guide \"x\"\n\tend\nend\n").empty());
}

TEST(next_output_looks_past_the_line_shown_without_running_anything) {
	Program p = test::compile_ok(R"(event a unskippable
	say guide "one"
	set seen
	say guide "two"
	call b
	if seen
		say guide "three"
	end
end

event b unskippable
	say guide "inner"
	var x = 2
end
)");
	RecordingHost host;
	host.set_line_duration(1.0);
	Interpreter it(p, host);
	CHECK(it.next_output() == Interpreter::NextOutput::none);
	it.start("a");
	it.step(0);
	CHECK(host.trace.size() == 1u && host.trace.back() == "say guide \"one\"");
	CHECK(it.next_output() == Interpreter::NextOutput::line);
	CHECK(host.trace.size() == 1u);
	host.advance(1.0);
	it.step(1.0);
	CHECK(host.trace.back() == "say guide \"two\"");
	CHECK(it.next_output() == Interpreter::NextOutput::other);
	host.advance(1.0);
	it.step(1.0);
	CHECK(host.trace.back() == "say guide \"inner\"");
	CHECK(it.next_output() == Interpreter::NextOutput::other);
	host.advance(1.0);
	it.step(1.0);
	CHECK(host.trace.back() == "say guide \"three\"");
	CHECK(it.next_output() == Interpreter::NextOutput::end);
	host.advance(1.0);
	it.step(1.0);
	CHECK(it.status() != Status::running);
	CHECK(it.next_output() == Interpreter::NextOutput::none);
}

TEST(next_output_names_a_choice_ahead) {
	Program p = test::compile_ok(R"(event a unskippable
	say guide "one"
	ask "Sure?"
	yes
		say guide "yes"
	end
end
)");
	RecordingHost host;
	host.set_line_duration(1.0);
	host.queue_choices({0});
	Interpreter it(p, host);
	it.start("a");
	it.step(0);
	CHECK(it.next_output() == Interpreter::NextOutput::choice);
}
