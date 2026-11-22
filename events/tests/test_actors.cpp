#include "check.h"

#include "events/registry.h"
#include "events/save_state.h"

#include <algorithm>
#include <cstdio>
#include <map>

using events::ActionType;
using events::Interpreter;
using events::Op;
using events::Program;
using events::RecordingHost;
using events::SaveState;
using events::Status;

namespace {

const events::Instruction* first_action(const Program& p, ActionType type) {
	for (const events::Instruction& ins : p.code) {
		if (ins.op == Op::action && ins.action.type == type) {
			return &ins;
		}
	}
	return nullptr;
}

std::vector<std::string> with_prefix(const std::vector<std::string>& trace, const std::string& prefix) {
	std::vector<std::string> out;
	for (const std::string& line : trace) {
		if (line.rfind(prefix, 0) == 0) {
			out.push_back(line);
		}
	}
	return out;
}

std::vector<events::Diagnostic> compile_with(const events::Registry& r, const std::string& text) {
	events::CompileOptions options;
	options.registry = &r;
	return events::compile({{"t.ev", text}}, options).diagnostics;
}

// Every new actor command, with lines between them so a skip and a save can land anywhere.
const char* const actor_scene = R"(event scene skippable
	place keeper counter facing south
	anim keeper fade_in
	say keeper "Welcome."
	move keeper via aisle shelf to door speed 2
	move drifter to door pathfind nowait
	face keeper west
	anim keeper hop
	say keeper "Mind the step."
	anim drifter dissolve
	anim keeper blink_out
	place drifter 2 0 3
	remove drifter until reload
	say keeper "Gone."
	remove keeper
end
)";

}

TEST(actor_commands_compile_to_actions_with_routes_facing_and_standard_lengths) {
	Program p = test::compile_ok(actor_scene);
	const events::Instruction* place = first_action(p, ActionType::place_actor);
	CHECK(place && place->action.subject == "keeper" && place->action.target == "counter" && place->action.facing == "south");
	CHECK(place && events::describe(place->action) == "place keeper counter facing south");
	const events::Instruction* move = first_action(p, ActionType::move);
	CHECK(move && move->action.waypoints == std::vector<std::string>({"aisle", "shelf"}) && move->action.speed == 2 && !move->action.pathfind);
	CHECK(move && events::describe(move->action) == "move keeper via aisle shelf to door speed 2");
	CHECK(events::describe(p.code[4].action) == "move drifter to door pathfind");
	CHECK(!p.code[4].wait);
	CHECK(events::describe(p.code[5].action) == "face keeper west");
	// Standard animations carry their default length; a free-form name has none.
	CHECK_EQ(p.code[1].action.seconds, 0.5);
	CHECK_EQ(p.code[6].action.seconds, 0.3);
	CHECK_EQ(p.code[8].action.seconds, 0.8);
	CHECK_EQ(p.code[9].action.seconds, 0.6);
	Program free_form = test::compile_ok("event a unskippable\n\tanim keeper bow\nend\n");
	CHECK_EQ(free_form.code[0].action.seconds, 0.0);
	CHECK(events::describe(p.code[10].action) == "place drifter 2 0 3");
	CHECK(p.code[11].op == Op::remove_actor && p.code[11].name == "drifter" && p.code[11].number == 1);
	CHECK(p.code[13].op == Op::remove_actor && p.code[13].name == "keeper" && p.code[13].number == 0);
	CHECK(events::standard_animation("dissolve") && events::standard_animation("dissolve")->end == events::AnimationEnd::hidden);
	CHECK(!events::standard_animation("bow"));
	ActionType type = ActionType::move;
	CHECK(events::parse_action_type("place", type) && type == ActionType::place_actor);
}

TEST(actor_command_mistakes_are_reported) {
	auto d = test::compile_errors(R"(event a unskippable
	place keeper counter facing up
	place keeper counter speed 2
	remove keeper until
	move keeper via to
	together
		remove keeper
		place keeper counter
	end
	face keeper counter pathfind
end
)");
	CHECK(test::has_diagnostic(d, 2, "expected north, east, south or west"));
	CHECK(test::has_diagnostic(d, 3, "unexpected name 'speed'"));
	CHECK(test::has_diagnostic(d, 4, "expected 'reload'"));
	CHECK(test::has_diagnostic(d, 5, "expected a marker name"));
	CHECK(test::has_diagnostic(d, 7, "'remove' is not allowed inside 'together'"));
	CHECK_EQ(d.size(), 6u);
	CHECK(test::has_diagnostic(d, 10, "unexpected name 'pathfind'"));
}

TEST(the_registry_accepts_standard_animations_and_directions_and_checks_routes) {
	events::Registry r;
	r.add(events::NameKind::actor, "keeper");
	r.add(events::NameKind::animation, "bow");
	r.add(events::NameKind::map, "shop");
	r.add_marker("shop", "counter");
	r.add_marker("shop", "door");
	r.add_marker("yard", "gate");
	r.add_event_map("shop", "e");
	CHECK(compile_with(r, R"(event e unskippable
	anim keeper dissolve
	anim keeper bow
	face keeper north
	move keeper via counter to door pathfind
	place keeper counter facing east
	remove keeper until reload
end
)").empty());
	auto bad = compile_with(r, R"(event e unskippable
	anim keeper wave
	face keeper up
	move keeper via gate to door
	place keeper gate
	remove stranger
end
)");
	CHECK(test::has_diagnostic(bad, 2, "unknown animation 'wave'"));
	CHECK(test::has_diagnostic(bad, 3, "'up'"));
	CHECK(test::has_diagnostic(bad, 4, "marker 'gate' is not on map 'shop'"));
	CHECK(test::has_diagnostic(bad, 5, "marker 'gate' is not on map 'shop'"));
	CHECK(test::has_diagnostic(bad, 6, "unknown actor 'stranger'"));
}

TEST(removal_reaches_the_host_and_applies_while_skipping) {
	Program p = test::compile_ok(actor_scene);
	RecordingHost host;
	Interpreter it(p, host);
	CHECK(it.start("scene"));
	CHECK(it.skip());
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	CHECK(with_prefix(host.trace, "remove ") == std::vector<std::string>({"remove drifter until reload", "remove keeper"}));
	CHECK(host.removed == (std::map<std::string, bool>{{"drifter", true}, {"keeper", false}}));
	CHECK(host.snapshot_world().find("removed drifter 1\n") != std::string::npos);
}

TEST(a_skip_puts_actor_animations_at_their_end_state) {
	Program p = test::compile_ok(actor_scene);
	RecordingHost watched;
	Interpreter full(p, watched);
	full.start("scene");
	const int total = test::run(full, watched);
	CHECK(full.status() == Status::finished);
	const events::ActorState* keeper = nullptr;
	const events::ActorState* drifter = nullptr;
	for (const events::ActorState& a : full.presentation().actors) {
		keeper = a.actor == "keeper" ? &a : keeper;
		drifter = a.actor == "drifter" ? &a : drifter;
	}
	// blink_out hid the keeper after its route; the drifter, placed after it dissolved, stays hidden with that pose.
	CHECK(keeper && keeper->visible == 0 && keeper->pose == "blink_out" && keeper->facing == "west" && keeper->marker == "door");
	CHECK(keeper && keeper->via == std::vector<std::string>({"aisle", "shelf"}) && !keeper->placed);
	CHECK(drifter && drifter->visible == 0 && drifter->pose == "dissolve" && drifter->placed && drifter->has_position && !drifter->pathfind);

	for (int k = 0; k < total; ++k) {
		RecordingHost host;
		Interpreter it(p, host);
		it.start("scene");
		for (int i = 0; i < k; ++i) {
			host.advance(0.1);
			it.step(0.1);
		}
		for (int guard = 0; it.status() == Status::running && guard < 1000; ++guard) {
			if (it.can_skip()) {
				CHECK(it.skip());
			}
			host.advance(0.1);
			it.step(0.1);
		}
		CHECK(it.status() == Status::finished);
		CHECK(host.same_world(watched));
		CHECK(it.presentation() == full.presentation());
		CHECK_EQ(host.live_handles(), 0u);
	}

	// Skipped from the start, the rebuild plays the hiding animations at once and shows nothing it hid.
	RecordingHost host;
	Interpreter it(p, host);
	it.start("scene");
	it.skip();
	test::run(it, host);
	CHECK(std::find(host.instant_calls.begin(), host.instant_calls.end(), "anim keeper blink_out") != host.instant_calls.end());
	CHECK(std::find(host.instant_calls.begin(), host.instant_calls.end(), "anim drifter dissolve") != host.instant_calls.end());
	CHECK(std::find(host.instant_calls.begin(), host.instant_calls.end(), "move keeper via aisle shelf to door") != host.instant_calls.end());
	CHECK(std::find(host.instant_calls.begin(), host.instant_calls.end(), "place drifter 2 0 3") != host.instant_calls.end());
	CHECK(std::find(host.instant_calls.begin(), host.instant_calls.end(), "hide keeper") == host.instant_calls.end());
	CHECK(std::find(host.instant_calls.begin(), host.instant_calls.end(), "show keeper") == host.instant_calls.end());
}

TEST(a_save_mid_animation_restores_the_route_and_replays_the_animation) {
	Program p = test::compile_ok(R"(event a unskippable
	place keeper counter facing south
	move keeper via aisle to door pathfind
	anim keeper dissolve
	say keeper "Gone."
end
)");
	RecordingHost host;
	host.set_duration(ActionType::animate, 1.0);
	Interpreter it(p, host);
	it.start("a");
	for (int i = 0; i < 3; ++i) {
		host.advance(0.1);
		it.step(0.1);
	}
	CHECK(it.position().line == 4);
	const std::string text = events::save_state_to_text(it.save());
	CHECK(text.rfind("events-save 4 ", 0) == 0);
	SaveState loaded;
	CHECK(events::save_state_from_text(text, loaded));
	CHECK(loaded.presentation == it.presentation());

	RecordingHost resumed_host = host.reloaded();
	Interpreter resumed(p, resumed_host);
	std::string error;
	CHECK(resumed.restore(loaded, error));
	resumed_host.advance(0.1);
	resumed.step(0.1);
	// The rebuild puts the keeper where its route ended, hidden by the dissolve; then the dissolve plays again.
	const std::vector<std::string> expected = {
		"action move keeper via aisle to door pathfind",
		"action anim keeper dissolve",
		"action anim keeper dissolve",
	};
	CHECK(resumed_host.trace == expected);
	if (resumed_host.trace != expected) {
		std::printf("%s", test::join(resumed_host.trace).c_str());
	}
	CHECK_EQ(resumed_host.instant_calls.size(), 2u);
	test::run(resumed, resumed_host);
	CHECK(resumed.status() == Status::finished);
	CHECK(resumed.presentation() == [&] {
		test::run(it, host);
		return it.presentation();
	}());
}

TEST(a_state_without_routes_is_written_as_version_3_and_loads) {
	Program p = test::compile_ok("event a unskippable\n\tmove keeper to door\n\tanim keeper fade_out\n\tsay keeper \"Bye.\"\nend\n");
	RecordingHost host;
	Interpreter it(p, host);
	it.start("a");
	it.step(0.1);
	const std::string text = events::save_state_to_text(it.save());
	CHECK(text.rfind("events-save 3 ", 0) == 0);
	SaveState loaded;
	CHECK(events::save_state_from_text(text, loaded));
	CHECK(loaded.presentation == it.presentation());
	CHECK(loaded.presentation.actors.size() == 1 && loaded.presentation.actors[0].pose == "fade_out" && loaded.presentation.actors[0].visible == 0);
}
