#include "check.h"

#include "events/registry.h"
#include "events/save_state.h"

#include <algorithm>
#include <string>
#include <vector>

using events::ActionType;
using events::Interpreter;
using events::Op;
using events::Program;
using events::RecordingHost;
using events::Status;

namespace {

const char* const yard_scene = R"(event yard_arrival skippable
	say keeper "The hen is loose."
	spawn hen_round
	look keeper enemy brine_crab
	look keeper lantern_bearer
	look keeper default
	hide hen with dissolve
	show hen with fade_in nowait
	sync
end

event hen_round background
	set yard.hen_out
	while yard.hen_out
		move hen via coop to gate pathfind
		face hen west
		anim hen hop
		sound cluck
		wait 0.5
		var yard.laps += 1
		if yard.laps >= 2
			clear yard.hen_out
		end
		call hen_rest
	end
	hide hen with dissolve
	remove hen until reload
end

event hen_rest background
	together
		anim hen peck
		wait 0.2
	end
	spawn hen_round
end
)";

}

TEST(background_events_spawn_and_look_compile) {
	Program p = test::compile_ok(yard_scene);
	const int round = p.find_event("hen_round");
	CHECK(round >= 0 && p.events[round].background && !p.events[round].skippable);
	CHECK(!p.events[p.find_event("yard_arrival")].background);
	CHECK(p.code[1].op == Op::spawn && p.code[1].name == "hen_round");
	CHECK(p.code[2].op == Op::actor_look && p.code[2].name == "keeper" && p.code[2].action.subject == "enemy" && p.code[2].action.target == "brine_crab");
	CHECK(p.code[3].op == Op::actor_look && p.code[3].action.subject.empty() && p.code[3].action.target == "lantern_bearer");
	CHECK(p.code[4].op == Op::actor_look && p.code[4].action.subject.empty() && p.code[4].action.target.empty());
	const events::Action& hide = p.code[5].action;
	CHECK(hide.type == ActionType::hide_actor && hide.target == "dissolve" && hide.seconds == 0.8);
	CHECK_EQ(events::describe(hide), std::string("hide hen with dissolve"));
	CHECK(p.code[6].action.type == ActionType::show_actor && p.code[6].action.target == "fade_in" && !p.code[6].wait);
	// A plain show or hide is written as before, so scripts without the new forms keep their text and fingerprint.
	CHECK_EQ(events::describe(test::compile_ok("event a unskippable\n\thide hen\nend\n").code[0].action), std::string("hide hen"));
}

TEST(background_events_reject_what_takes_control) {
	auto d = test::compile_errors(R"(event drift background
	say keeper "Hello."
	give lamp
	camera cut yard_wide
	together
		camera cut yard_wide
		wait 1
	end
	warp yard gate
	lock player
	music area yard_theme
	call scene
	choice
	option "A"
	option "B"
	end
	battle crabs
	win
	end
	party add guide
	shop stall
	rest
	fade out
	move hen to gate
end

event scene unskippable
	spawn scene
	spawn nowhere
	together
		spawn drift
		wait 1
	end
	show hen with fade_out
	hide hen with fade_in
end
)");
	CHECK(test::has_diagnostic(d, 2, "'say' is not allowed in a background event"));
	CHECK(test::has_diagnostic(d, 3, "'give' is not allowed in a background event"));
	CHECK(test::has_diagnostic(d, 4, "'camera' is not allowed in a background event"));
	CHECK(test::has_diagnostic(d, 6, "'camera' is not allowed in a background event"));
	CHECK(test::has_diagnostic(d, 9, "'warp' is not allowed in a background event"));
	CHECK(test::has_diagnostic(d, 10, "'lock' is not allowed in a background event"));
	CHECK(test::has_diagnostic(d, 11, "'music' is not allowed in a background event"));
	CHECK(test::has_diagnostic(d, 12, "call: event 'scene' is not marked 'background'"));
	CHECK(test::has_diagnostic(d, 13, "'choice' is not allowed in a background event"));
	CHECK(test::has_diagnostic(d, 17, "'battle' is not allowed in a background event"));
	CHECK(test::has_diagnostic(d, 20, "'party' is not allowed in a background event"));
	CHECK(test::has_diagnostic(d, 21, "'shop' is not allowed in a background event"));
	CHECK(test::has_diagnostic(d, 22, "'rest' is not allowed in a background event"));
	CHECK(test::has_diagnostic(d, 23, "'fade' is not allowed in a background event"));
	CHECK(test::has_diagnostic(d, 28, "spawn: event 'scene' is not marked 'background'"));
	CHECK(test::has_diagnostic(d, 29, "spawn: unknown event 'nowhere'"));
	CHECK(test::has_diagnostic(d, 31, "'spawn' is not allowed inside 'together'"));
	CHECK(test::has_diagnostic(d, 34, "'fade_out' leaves the actor the other way, so it cannot be the transition of 'show'"));
	CHECK(test::has_diagnostic(d, 35, "'fade_in' leaves the actor the other way, so it cannot be the transition of 'hide'"));
	CHECK_EQ(d.size(), 19u);

	events::Registry r;
	r.add(events::NameKind::actor, "hen");
	r.add(events::NameKind::enemy, "brine_crab");
	events::CompileOptions options;
	options.registry = &r;
	auto unknown = events::compile({{"t.ev", "event a unskippable\n\tlook hen enemy sand_crab\n\tlook ghost lantern\n\thide hen with sparkle\n\tlook hen enemy brine_crab\n"
										  "\thide hen with blink_out\nend\n"}},
		options)
						   .diagnostics;
	CHECK(test::has_diagnostic(unknown, 2, "unknown enemy 'sand_crab'"));
	CHECK(test::has_diagnostic(unknown, 3, "unknown actor 'ghost'"));
	CHECK(test::has_diagnostic(unknown, 4, "unknown animation 'sparkle'"));
	CHECK_EQ(unknown.size(), 3u);
}

TEST(spawn_and_look_reach_the_host_also_while_skipping) {
	Program p = test::compile_ok(yard_scene);
	for (const bool skipping : {false, true}) {
		RecordingHost host;
		Interpreter it(p, host);
		it.start("yard_arrival");
		if (skipping) {
			CHECK(it.skip());
		}
		test::run(it, host);
		CHECK(it.status() == Status::finished);
		CHECK(host.spawned == std::vector<std::string>({"hen_round"}));
		std::vector<std::string> looks;
		for (const std::string& line : host.trace) {
			if (line.rfind("look ", 0) == 0) {
				looks.push_back(line);
			}
		}
		CHECK(looks == std::vector<std::string>({"look keeper enemy brine_crab", "look keeper lantern_bearer", "look keeper default"}));
	}
}

TEST(a_background_event_runs_its_loop_and_ends_through_the_same_interpreter) {
	Program p = test::compile_ok(yard_scene);
	RecordingHost host;
	Interpreter it(p, host);
	it.start("hen_round");
	CHECK(!it.can_skip());
	test::run(it, host, 0.1);
	CHECK(it.status() == Status::finished);
	CHECK_EQ(host.vars["yard.laps"], 2);
	CHECK(!host.flags["yard.hen_out"]);
	CHECK(host.spawned == std::vector<std::string>({"hen_round", "hen_round"}));
	CHECK(host.removed.count("hen") && host.removed["hen"]);
}

TEST(cancel_stops_a_background_event_and_gives_back_its_handles) {
	Program p = test::compile_ok("event drift background\n\tmove hen to gate nowait\n\twait 5\n\tset late\nend\n");
	RecordingHost host;
	host.set_duration(ActionType::move, 3);
	Interpreter it(p, host);
	it.start("drift");
	it.step(0.1);
	CHECK(host.live_handles() == 1);
	it.cancel();
	CHECK(it.status() == Status::idle);
	CHECK(host.live_handles() == 0);
	host.advance(10);
	it.step(10);
	CHECK(!host.flags.count("late"));
	CHECK(host.errors.empty());
}

TEST(a_hide_with_a_vanishing_transition_is_rebuilt_as_that_animation) {
	Program p = test::compile_ok("event a unskippable\n\thide hen with blink_out\n\twait 1\nend\n");
	RecordingHost host;
	Interpreter it(p, host);
	it.start("a");
	it.step(0.1);
	const events::SaveState state = it.save();
	CHECK(state.presentation.actors.size() == 1 && state.presentation.actors[0].visible == 0 && state.presentation.actors[0].pose == "blink_out");
	RecordingHost after;
	Interpreter resumed(p, after);
	std::string error;
	CHECK(resumed.restore(state, error));
	resumed.step(0);
	CHECK(after.instant_calls == std::vector<std::string>({"anim hen blink_out"}));
}
