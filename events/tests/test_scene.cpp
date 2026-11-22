#include "check.h"

#include "events/language.h"
#include "events/registry.h"
#include "events/save_state.h"

#include <cstdio>
#include <string>
#include <vector>

using events::ActionType;
using events::Interpreter;
using events::Op;
using events::Program;
using events::RecordingHost;
using events::SaveState;
using events::Status;

namespace {

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

const char* const picture_scene = R"(event album skippable
	picture show harbor_dawn
	say guide "The harbor at dawn."
	picture show lighthouse
	picture show storm
	say guide "Then the storm."
	picture hide lighthouse
	picture hide
	picture show beacon
	say guide "And the beacon."
	picture clear
	picture show farewell
end
)";

}

TEST(picture_commands_compile_and_reach_the_host_in_order) {
	Program p = test::compile_ok(picture_scene);
	CHECK(p.code[0].op == Op::picture && p.code[0].name == "harbor_dawn" && p.code[0].number == 0);
	CHECK(p.code[6].op == Op::picture && p.code[6].name.empty() && p.code[6].number == 1);
	CHECK(p.code[9].op == Op::picture && p.code[9].number == 2);
	RecordingHost host;
	Interpreter it(p, host);
	CHECK(it.start("album"));
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	const std::vector<std::string> expected = {"picture show harbor_dawn", "picture show lighthouse", "picture show storm", "picture hide lighthouse",
		"picture hide", "picture show beacon", "picture clear", "picture show farewell"};
	CHECK(with_prefix(host.trace, "picture ") == expected);
}

TEST(a_skip_applies_picture_commands) {
	Program p = test::compile_ok(picture_scene);
	RecordingHost host;
	Interpreter it(p, host);
	CHECK(it.start("album"));
	CHECK(it.skip());
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	CHECK_EQ(with_prefix(host.trace, "picture ").size(), 8u);
	CHECK(with_prefix(host.trace, "line ").empty());
}

TEST(picture_mistakes_are_reported) {
	auto d = test::compile_errors(R"(event a unskippable
	picture
	picture show
	picture flip x
	together
		picture show x
		wait 1
	end
	picture clear x
end

event b background
	picture show x
end
)");
	CHECK(test::has_diagnostic(d, 2, "expected 'show', 'hide' or 'clear'"));
	CHECK(test::has_diagnostic(d, 3, "expected a picture name"));
	CHECK(test::has_diagnostic(d, 4, "expected 'show', 'hide' or 'clear'"));
	CHECK(test::has_diagnostic(d, 6, "'picture' is not allowed inside 'together'"));
	CHECK(test::has_diagnostic(d, 9, "unexpected name 'x'"));
	CHECK(test::has_diagnostic(d, 13, "'picture'"));
}

TEST(the_registry_checks_picture_names_only_when_it_lists_some) {
	events::Registry open;
	open.add(events::NameKind::actor, "guide");
	CHECK(compile_with(open, "event e unskippable\n\tpicture show anything\n\tpicture hide other\nend\n").empty());
	events::Registry listed = open;
	listed.add(events::NameKind::picture, "harbor_dawn");
	CHECK(compile_with(listed, "event e unskippable\n\tpicture show harbor_dawn\n\tpicture hide\n\tpicture hide harbor_dawn\nend\n").empty());
	auto bad = compile_with(listed, "event e unskippable\n\tpicture show storm\n\tpicture hide storm\nend\n");
	CHECK(test::has_diagnostic(bad, 2, "unknown picture 'storm'"));
	CHECK(test::has_diagnostic(bad, 3, "unknown picture 'storm'"));
	std::vector<events::Diagnostic> diagnostics;
	events::Registry loaded;
	CHECK(events::load_registry("r.txt", "picture harbor_dawn storm\n", loaded, diagnostics));
	CHECK(loaded.has(events::NameKind::picture, "storm"));
}

TEST(camera_to_and_reset_compile_with_markers_cells_and_times) {
	Program p = test::compile_ok(R"(event a unskippable
	camera to pier_end
	camera to 4 7 1.5
	camera reset
	camera reset 0.5
	together
		camera to pier_end 2
		wait 1
	end
end
)");
	CHECK(p.code[0].action.type == ActionType::camera_to && p.code[0].action.target == "pier_end" && !p.code[0].action.has_position &&
		p.code[0].action.seconds == 0);
	CHECK(p.code[1].action.has_position && p.code[1].action.position.x == 4 && p.code[1].action.position.y == 7 && p.code[1].action.seconds == 1.5);
	CHECK(p.code[2].action.type == ActionType::camera_reset && p.code[2].action.seconds == 0);
	CHECK(p.code[3].action.seconds == 0.5);
	CHECK_EQ(events::describe(p.code[0].action), std::string("camera to pier_end"));
	CHECK_EQ(events::describe(p.code[1].action), std::string("camera to 4 7 1.5"));
	CHECK_EQ(events::describe(p.code[3].action), std::string("camera reset 0.5"));
	ActionType type = ActionType::move;
	CHECK(events::parse_action_type("camera_to", type) && type == ActionType::camera_to);
	CHECK(events::parse_action_type("camera_reset", type) && type == ActionType::camera_reset);
	auto d = test::compile_errors("event a unskippable\n\tcamera to 1.5 2\n\tcamera to -1 2\n\tcamera to 3\n\tcamera spin\nend\n");
	CHECK(test::has_diagnostic(d, 2, "a camera cell is two whole numbers of 0 or more"));
	CHECK(test::has_diagnostic(d, 3, "a camera cell is two whole numbers of 0 or more"));
	CHECK(test::has_diagnostic(d, 4, "expected a cell's y"));
	CHECK(test::has_diagnostic(d, 5, "expected 'cut', 'move', 'to' or 'reset'"));
	events::Registry r;
	r.add(events::NameKind::map, "harbor");
	r.add_marker("harbor", "pier_end");
	r.add_marker("lighthouse", "lamp");
	r.add_event_map("harbor", "e");
	CHECK(compile_with(r, "event e unskippable\n\tcamera to pier_end\n\tcamera to 2 2\nend\n").empty());
	CHECK(test::has_diagnostic(compile_with(r, "event e unskippable\n\tcamera to lamp\nend\n"), 2, "marker 'lamp' is not on map 'harbor'"));
}

TEST(a_skip_and_a_restore_rebuild_the_last_scene_camera) {
	Program p = test::compile_ok(R"(event a skippable
	camera cut wide
	camera to pier_end 2
	say guide "Look."
	camera to 3 4
	camera reset 1
	say guide "Back."
end
)");
	RecordingHost host;
	Interpreter it(p, host);
	it.start("a");
	CHECK(it.skip());
	test::run(it, host);
	CHECK(it.status() == Status::finished);
	CHECK(!host.instant_calls.empty() && host.instant_calls.back() == "camera reset");

	RecordingHost watched;
	Interpreter run(p, watched);
	run.start("a");
	// Saved during the two-second move toward pier_end.
	for (int i = 0; i < 5; ++i) {
		watched.advance(0.1);
		run.step(0.1);
	}
	CHECK(run.position().line == 3);
	CHECK(run.presentation().camera_to && run.presentation().camera.empty() && run.presentation().camera_marker == "pier_end");
	const std::string text = events::save_state_to_text(run.save());
	CHECK(text.rfind("events-save 5 ", 0) == 0);
	SaveState loaded;
	CHECK(events::save_state_from_text(text, loaded));
	CHECK(loaded.presentation == run.presentation());
	RecordingHost resumed_host = watched.reloaded();
	Interpreter resumed(p, resumed_host);
	std::string error;
	CHECK(resumed.restore(loaded, error));
	resumed_host.advance(0.1);
	resumed.step(0.1);
	CHECK(!resumed_host.instant_calls.empty() && resumed_host.instant_calls.front() == "camera to pier_end");
}

TEST(music_play_takes_a_fade_or_a_cut) {
	Program p = test::compile_ok("event a unskippable\n\tmusic play tide\n\tmusic play tide fade 1200\n\tmusic play tide cut nowait\n\tsync\nend\n");
	CHECK(p.code[0].action.fade_ms == -1 && p.code[1].action.fade_ms == 1200 && p.code[2].action.fade_ms == 0 && !p.code[2].wait);
	CHECK(p.code[1].action.seconds == 0);
	CHECK_EQ(events::describe(p.code[1].action), std::string("music play tide fade 1200"));
	CHECK_EQ(events::describe(p.code[2].action), std::string("music play tide cut"));
	auto d = test::compile_errors("event a unskippable\n\tmusic play tide fade\n\tmusic play tide fade 0\n\tmusic play tide fade 1.5\nend\n");
	CHECK(test::has_diagnostic(d, 2, "expected a fade time in milliseconds"));
	CHECK(test::has_diagnostic(d, 3, "a music fade is a whole number of milliseconds from 1 to 60000"));
	CHECK(test::has_diagnostic(d, 4, "a music fade is a whole number of milliseconds from 1 to 60000"));
}

TEST(the_command_table_is_what_the_parser_knows) {
	for (const events::CommandInfo& c : events::script_commands()) {
		const events::CompileResult result = events::compile({{"t.ev", "event a unskippable\n\t" + c.name + "\nend\n"}});
		for (const events::Diagnostic& d : result.diagnostics) {
			CHECK(d.key != "script.unknown_command");
		}
		CHECK(!c.forms.empty());
	}
	CHECK(events::script_command("picture") && !events::script_command("picture")->background && !events::script_command("picture")->parallel);
	CHECK(events::script_command("camera") && events::script_command("camera")->parallel);
	CHECK(events::script_command("bogus_word") == nullptr);
	CHECK(test::has_diagnostic(test::compile_errors("event a unskippable\n\tbogus_word\nend\n"), 2, "unknown command 'bogus_word'"));
}

TEST(register_and_unregister_town_reach_the_host) {
	Program p = test::compile_ok("event a unskippable\n\tregister town saltgrass\n\tunregister town tidewater\nend\n");
	CHECK(p.code[0].op == Op::register_town && p.code[0].name == "saltgrass" && p.code[0].number == 0);
	CHECK(p.code[1].op == Op::register_town && p.code[1].name == "tidewater" && p.code[1].number == 1);
	RecordingHost host;
	Interpreter it(p, host);
	CHECK(it.start("a"));
	test::run(it, host);
	CHECK(with_prefix(host.trace, "register town ") == std::vector<std::string>{"register town saltgrass"});
	CHECK(with_prefix(host.trace, "unregister town ") == std::vector<std::string>{"unregister town tidewater"});
	auto d = test::compile_errors("event a unskippable\n\tregister saltgrass\n\tunregister town\nend\nevent b background\n\tregister town saltgrass\nend\n");
	CHECK(test::has_diagnostic(d, 2, "'town'"));
	CHECK(test::has_diagnostic(d, 3, "expected a map name"));
	CHECK(test::has_diagnostic(d, 6, "'register'"));
	events::Registry r;
	r.add(events::NameKind::map, "saltgrass");
	events::CompileOptions options;
	options.registry = &r;
	auto unknown = events::compile({{"t.ev", "event a unskippable\n\tregister town saltgrass\n\tregister town nowhere\nend\n"}}, options).diagnostics;
	CHECK(test::has_diagnostic(unknown, 3, "unknown map 'nowhere'"));
	CHECK_EQ(unknown.size(), 1u);
}

TEST(a_warp_carries_its_follower_placement_to_the_host) {
	Program p = test::compile_ok("event a unskippable\n\twarp shore landing followers left\n\twarp shore pier\nend\n");
	CHECK(p.code[0].action.followers == "left" && p.code[1].action.followers.empty());
	RecordingHost host;
	Interpreter it(p, host);
	CHECK(it.start("a"));
	test::run(it, host);
	CHECK(with_prefix(host.trace, "action warp ") == (std::vector<std::string>{"action warp shore landing followers left", "action warp shore pier"}));
	auto d = test::compile_errors("event a unskippable\n\twarp shore landing followers sideways\n\twarp shore landing followers\nend\n");
	CHECK(test::has_diagnostic(d, 2, "stacked, behind, up, down, left or right"));
	CHECK(test::has_diagnostic(d, 3, "stacked, behind, up, down, left or right"));
}

TEST(a_choice_header_reaches_the_host_with_the_options) {
	Program p = test::compile_ok("event a unskippable\n\tchoice \"Where to?\" header @trip.header into trip.pick\n\toption \"North\"\n\toption \"South\"\n\tend\n"
		"\tchoice header \"Pick one\"\n\toption \"A\"\n\toption \"B\"\n\tend\nend\n");
	CHECK(p.code[0].choice.header.is_id && p.code[0].choice.header.value == "trip.header" && p.code[0].choice.prompt.value == "Where to?");
	RecordingHost host;
	host.queue_choices({1, 0});
	Interpreter it(p, host);
	CHECK(it.start("a"));
	test::run(it, host);
	CHECK(with_prefix(host.trace, "choice ") ==
		(std::vector<std::string>{"choice \"Where to?\" header @trip.header [\"North\" | \"South\"] -> 1", "choice header \"Pick one\" [\"A\" | \"B\"] -> 0"}));
	auto d = test::compile_errors("event a unskippable\n\tchoice header\n\toption \"A\"\n\toption \"B\"\n\tend\nend\n");
	CHECK(test::has_diagnostic(d, 2, "expected header text"));
}
