#include "check.h"

#include "events/registry.h"

using events::CompileOptions;
using events::Diagnostic;
using events::NameKind;
using events::Registry;

namespace {

const char* const registry_text = R"(# Content for the dock scenes.
actor guide traveler
marker pier dock
camera pier_wide
layout full
audio surf_theme gull_cry
animation wave
portrait smiling
item rope
member guide
encounter reef_crabs
map lighthouse
shop dock_stall
rest dock_inn

flag dock.met
flag dock.temp local default true
var dock.visits
var dock.offer local default 30   # haggling starts here
)";

Registry registry_ok() {
	Registry r;
	std::vector<Diagnostic> d;
	CHECK(events::load_registry("dock.registry", registry_text, r, d));
	CHECK(d.empty());
	return r;
}

std::vector<Diagnostic> compile_with(const Registry& registry, const std::string& text) {
	CompileOptions options;
	options.registry = &registry;
	return events::compile({{"test.ev", text}}, options).diagnostics;
}

}

TEST(registry_loads_names_and_declarations_and_reports_bad_lines) {
	Registry r = registry_ok();
	CHECK_EQ(r.file, std::string("dock.registry"));
	CHECK(r.has(NameKind::actor, "traveler"));
	CHECK(r.has(NameKind::rest_point, "dock_inn"));
	CHECK(!r.has(NameKind::actor, "pier"));
	const events::Declaration* temp = r.find("dock.temp");
	CHECK(temp && temp->is_flag && temp->local && temp->initial == 1 && temp->line == 17);
	const events::Declaration* offer = r.find("dock.offer");
	CHECK(offer && !offer->is_flag && offer->local && offer->initial == 30);
	CHECK(r.find("dock.visits") && !r.find("dock.visits")->local);

	Registry bad;
	std::vector<Diagnostic> d;
	CHECK(!events::load_registry("bad.registry", "actor\nprop lamp\nactor guide 9lives\nactor guide\nflag a\nflag x default 3\nvar b default yes\nvar a\nvar c sometimes\n", bad, d));
	CHECK(test::has_diagnostic(d, 1, "actor: expected one or more names"));
	CHECK(test::has_diagnostic(d, 2, "unknown kind 'prop'"));
	CHECK(test::has_diagnostic(d, 3, "'9lives' is not a valid name"));
	CHECK(test::has_diagnostic(d, 4, "'guide' is already listed at line 3"));
	CHECK(test::has_diagnostic(d, 6, "the default must be true or false, found '3'"));
	CHECK(test::has_diagnostic(d, 7, "the default must be an integer, found 'yes'"));
	CHECK(test::has_diagnostic(d, 8, "'a' is already declared at line 5"));
	CHECK(test::has_diagnostic(d, 9, "expected 'global', 'local' or 'default <value>', found 'sometimes'"));
	CHECK_EQ(d.size(), 8u);
}

TEST(registry_rejects_every_unknown_name_with_its_line) {
	Registry r = registry_ok();
	std::vector<Diagnostic> d = compile_with(r, R"(event a unskippable
	say stranger "Hi." as grinning
	move guide to nowhere
	move guide to 1 2 3
	face guide lamp_post
	face guide dock
	anim guide dance
	panel show p1 camera sky_cam layout corner
	camera cut pier_wide
	music play no_track
	sound gull_cry
	warp atlantis pier
	give anchor
	party add stranger
	battle sea_serpent
	win
	end
	shop black_market
	rest
	rest palace_inn
	name entry stranger
	set dock.unknown
	var dock.visits += 1
	if dock.met and dock.missing > 2
		wait 1
	end
end
)");
	CHECK(test::has_diagnostic(d, 2, "unknown actor 'stranger': it is not listed in dock.registry"));
	CHECK(test::has_diagnostic(d, 2, "unknown portrait 'grinning'"));
	CHECK(test::has_diagnostic(d, 3, "unknown marker 'nowhere'"));
	CHECK(test::has_diagnostic(d, 5, "unknown actor or marker 'lamp_post'"));
	CHECK(test::has_diagnostic(d, 7, "unknown animation 'dance'"));
	CHECK(test::has_diagnostic(d, 8, "unknown camera 'sky_cam'"));
	CHECK(test::has_diagnostic(d, 8, "unknown layout 'corner'"));
	CHECK(test::has_diagnostic(d, 10, "unknown audio cue 'no_track'"));
	CHECK(test::has_diagnostic(d, 12, "unknown map 'atlantis'"));
	CHECK(test::has_diagnostic(d, 13, "unknown item 'anchor'"));
	CHECK(test::has_diagnostic(d, 14, "unknown party member 'stranger'"));
	CHECK(test::has_diagnostic(d, 15, "unknown encounter 'sea_serpent'"));
	CHECK(test::has_diagnostic(d, 18, "unknown shop 'black_market'"));
	CHECK(test::has_diagnostic(d, 20, "unknown rest point 'palace_inn'"));
	CHECK(test::has_diagnostic(d, 21, "unknown party member 'stranger'"));
	CHECK(test::has_diagnostic(d, 22, "flag 'dock.unknown' is not declared in dock.registry"));
	CHECK(test::has_diagnostic(d, 24, "variable 'dock.missing' is not declared"));
	CHECK_EQ(d.size(), 17u);
}

TEST(registry_checks_that_flags_and_variables_are_used_as_declared) {
	Registry r = registry_ok();
	std::vector<Diagnostic> d = compile_with(r, R"(event a unskippable
	set dock.visits
	var dock.met = 1
	if dock.visits
	end
	while dock.met > 1
		wait 1
	end
	ask "Again?" into dock.visits
	end
	choice into dock.met
	option "a"
	option "b"
	end
end
)");
	CHECK(test::has_diagnostic(d, 2, "'dock.visits' is declared as a variable, not a flag"));
	CHECK(test::has_diagnostic(d, 3, "'dock.met' is declared as a flag, not a variable"));
	CHECK(test::has_diagnostic(d, 4, "'dock.visits' is declared as a variable, not a flag"));
	CHECK(test::has_diagnostic(d, 6, "'dock.met' is declared as a flag, not a variable"));
	CHECK(test::has_diagnostic(d, 9, "'dock.visits' is declared as a variable, not a flag"));
	CHECK(test::has_diagnostic(d, 11, "'dock.met' is declared as a flag, not a variable"));
	CHECK_EQ(d.size(), 6u);
}

TEST(a_registry_built_in_code_works_like_a_loaded_one_and_none_checks_nothing) {
	const std::string script = "event a unskippable\n\tmove guide to pier\n\tset dock.met\nend\n";
	Registry r;
	r.add(NameKind::actor, "guide");
	r.add(NameKind::marker, "pier");
	std::vector<Diagnostic> d = compile_with(r, script);
	CHECK(test::has_diagnostic(d, 3, "flag 'dock.met' is not declared in the registry"));
	CHECK_EQ(d.size(), 1u);
	r.declare_flag("dock.met");
	CHECK(compile_with(r, script).empty());
	CHECK(events::compile({{"test.ev", script}}).diagnostics.empty());
}

TEST(local_flags_and_variables_live_in_the_interpreter_and_reset_each_run) {
	Registry r = registry_ok();
	CompileOptions options;
	options.registry = &r;
	events::CompileResult compiled = events::compile({{"test.ev", R"(event a unskippable
	var dock.offer -= 5
	clear dock.temp
	say guide "Haggling."
	if dock.offer == 25 and not dock.temp
		var dock.visits += 1
	end
end
)"}}, options);
	CHECK(compiled.ok());
	events::RecordingHost host;
	host.set_line_duration(1);
	events::Interpreter it(compiled.program, host);
	for (int run = 0; run < 2; ++run) {
		it.start("a");
		test::run(it, host);
	}
	// Each run starts from the declared values, so both reach 25.
	CHECK_EQ(host.vars["dock.visits"], 2);
	CHECK(host.vars.count("dock.offer") == 0);
	CHECK(host.flags.count("dock.temp") == 0);

	// A save in the middle carries the local values.
	events::RecordingHost first;
	first.set_line_duration(1);
	events::Interpreter saver(compiled.program, first);
	saver.start("a");
	saver.step(0.1);
	events::SaveState loaded;
	CHECK(events::save_state_from_text(events::save_state_to_text(saver.save()), loaded));
	CHECK_EQ(loaded.locals["dock.offer"], 25);
	CHECK_EQ(loaded.locals["dock.temp"], 0);
	events::RecordingHost resumed_host = first.reloaded();
	events::Interpreter resumed(compiled.program, resumed_host);
	std::string error;
	CHECK(resumed.restore(loaded, error));
	test::run(resumed, resumed_host);
	CHECK_EQ(resumed_host.vars["dock.visits"], 1);

	// Changing a local's declaration changes what a save means.
	Registry changed = r;
	for (events::Declaration& d : changed.declarations) {
		if (d.name == "dock.offer") {
			d.initial = 40;
		}
	}
	CompileOptions other;
	other.registry = &changed;
	CHECK(events::compile({{"test.ev", "event a unskippable\nend\n"}}, other).program.fingerprint !=
		events::compile({{"test.ev", "event a unskippable\nend\n"}}, options).program.fingerprint);
}

TEST(registry_knows_which_map_holds_each_marker_and_where_events_start) {
	Registry r;
	std::vector<Diagnostic> d;
	CHECK(events::load_registry("maps.registry", R"(actor guide
map shore cliff
map_markers shore landing pier
map_markers cliff ledge pier
marker loose_end
event_map shore beach_walk
)", r, d));
	CHECK(d.empty());
	CHECK(r.has(NameKind::marker, "landing") && r.has(NameKind::marker, "loose_end"));
	CHECK(r.marker_on("landing", "shore") && !r.marker_on("landing", "cliff"));
	CHECK(r.marker_on("pier", "shore") && r.marker_on("pier", "cliff"));
	CHECK(r.marker_on("loose_end", "cliff"));
	CHECK(r.event_maps.at("beach_walk") == std::set<std::string>{"shore"});

	Registry bad;
	d.clear();
	CHECK(!events::load_registry("bad.registry", "map shore\nmap_markers shore\nmap_markers sea rock\nmap_markers shore a a\nevent_map nowhere walk\n", bad, d));
	CHECK(test::has_diagnostic(d, 2, "map_markers: expected a map and one or more markers"));
	CHECK(test::has_diagnostic(d, 3, "map 'sea' is not listed on a 'map' line"));
	CHECK(test::has_diagnostic(d, 4, "'a' is already listed for map 'shore'"));
	CHECK(test::has_diagnostic(d, 5, "map 'nowhere' is not listed on a 'map' line"));
}

TEST(a_warp_or_move_to_a_marker_on_another_map_does_not_compile) {
	Registry r;
	std::vector<Diagnostic> load;
	CHECK(events::load_registry("maps.registry", R"(actor guide
map shore cliff
map_markers shore landing pier
map_markers cliff ledge pier
marker anywhere
event_map shore beach_walk
flag seen
)", r, load));
	std::vector<Diagnostic> d = compile_with(r, R"(event beach_walk unskippable
	move guide to landing
	move guide to ledge
	warp cliff landing
	warp cliff ledge
	move guide to landing
	move guide to pier
	move guide to anywhere
	if seen
		warp shore pier
	end
	move guide to ledge
	move guide to landing
end
event elsewhere unskippable
	move guide to ledge
	warp shore landing
	call beach_walk
	move guide to ledge
end
)");
	CHECK(test::has_diagnostic(d, 3, "marker 'ledge' is not on map 'shore', where the party is at this point; it is on cliff"));
	CHECK(test::has_diagnostic(d, 4, "marker 'landing' is not on map 'cliff'; it is on shore"));
	CHECK(test::has_diagnostic(d, 6, "marker 'landing' is not on map 'cliff', where the party is at this point; it is on shore"));
	// After a branch that may warp, and after a call, the map is not known, so nothing more is reported.
	CHECK_EQ(d.size(), 3u);
}

TEST(battle_scripted_ends_name_an_enemy_the_registry_knows) {
	Registry r;
	std::vector<Diagnostic> load;
	CHECK(events::load_registry("battle.registry", "encounter reef_crabs\nenemy crab_king\n", r, load));
	std::vector<Diagnostic> d = compile_with(r, R"(event a unskippable
	battle reef_crabs until defeat crab_king
	win
	end
	battle reef_crabs until defeat crab_queen
	win
	end
end
)");
	CHECK(test::has_diagnostic(d, 5, "unknown enemy 'crab_queen'"));
	CHECK_EQ(d.size(), 1u);
}

TEST(a_vehicle_battle_names_a_listed_vehicle) {
	Registry r = registry_ok();
	r.add(NameKind::vehicle, "skiff");
	CompileOptions options;
	options.registry = &r;
	auto ok = events::compile({{"dock.ev", "event e unskippable\n\tbattle reef_crabs aboard skiff\n\twin\n\tend\nend\n"}}, options);
	CHECK(ok.ok());
	if (ok.ok()) {
		const events::Instruction* battle = nullptr;
		for (const events::Instruction& ins : ok.program.code) {
			if (ins.op == events::Op::battle) {
				battle = &ins;
			}
		}
		CHECK(battle && battle->battle.vehicle == "skiff");
		CHECK(battle && events::describe(battle->battle) == "battle reef_crabs aboard skiff no-escape defeat-game-over");
	}
	auto unknown = events::compile({{"dock.ev", "event e unskippable\n\tbattle reef_crabs aboard raft\n\twin\n\tend\nend\n"}}, options);
	CHECK(!unknown.ok());
	CHECK(!unknown.diagnostics.empty() && unknown.diagnostics[0].key == "script.unknown_name");
	CHECK(!unknown.diagnostics.empty() && unknown.diagnostics[0].params[0] == "vehicle" && unknown.diagnostics[0].params[1] == "raft");
}

TEST(registry_problems_carry_keys) {
	Registry r;
	std::vector<Diagnostic> d;
	CHECK(!events::load_registry("bad.registry", "flag dock.met\nflag dock.met\nwidget spanner\n", r, d));
	CHECK_EQ(d.size(), std::size_t(2));
	if (d.size() == 2) {
		CHECK_EQ(d[0].key, std::string("registry.already_declared"));
		CHECK(d[0].params == (std::vector<std::string>{"dock.met", "1"}));
		CHECK_EQ(d[0].message, std::string("'dock.met' is already declared at line 1"));
		CHECK_EQ(d[1].key, std::string("registry.unknown_kind"));
		CHECK_EQ(d[1].line, 3);
	}
}
