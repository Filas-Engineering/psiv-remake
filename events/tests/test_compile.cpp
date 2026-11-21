#include "check.h"

using events::Diagnostic;
using events::Severity;

TEST(lexical_errors_report_their_line_and_keep_going) {
	auto d = test::compile_errors(R"(event a unskippable
	say guide "fine"
	say guide "never closed
	wait 1
	say guide "bad \q escape"
	narrate @
end
)");
	CHECK(test::has_diagnostic(d, 3, "unterminated string"));
	CHECK(test::has_diagnostic(d, 5, "unknown escape"));
	CHECK(test::has_diagnostic(d, 6, "expected a string ID after '@'"));
	CHECK_EQ(d.size(), 3u);
	CHECK_EQ(d[0].file, std::string("test.ev"));
}

TEST(unknown_command_is_reported_at_its_line) {
	auto d = test::compile_errors(R"(# a comment line
event a unskippable

	wlak guide to pier
	say guide "hello"  # trailing comment
	jump
end
)");
	CHECK(test::has_diagnostic(d, 4, "unknown command 'wlak'"));
	CHECK(test::has_diagnostic(d, 6, "unknown command 'jump'"));
	CHECK_EQ(d.size(), 2u);
}

TEST(wrong_argument_counts_and_types) {
	auto d = test::compile_errors(R"(event a unskippable
	say guide
	wait soon
	var coins = many
	move guide pier
	set first second
	give rope 0
	panel show p1 camera wide
	shake
	say guide "hi" as
	fade sideways
	var coins *= 2
	camera move high
	party invite guide
	anim guide wave sometimes
end
)");
	CHECK(test::has_diagnostic(d, 2, "say: expected text (a quoted string or @string.id), but the line ended"));
	CHECK(test::has_diagnostic(d, 3, "wait: expected a duration in seconds, found name 'soon'"));
	CHECK(test::has_diagnostic(d, 4, "var: expected an integer value, found name 'many'"));
	CHECK(test::has_diagnostic(d, 5, "move: expected 'to', found name 'pier'"));
	CHECK(test::has_diagnostic(d, 6, "set: unexpected name 'second'"));
	CHECK(test::has_diagnostic(d, 7, "the item count must be at least 1"));
	CHECK(test::has_diagnostic(d, 8, "panel: expected 'layout'"));
	CHECK(test::has_diagnostic(d, 9, "shake: expected a duration in seconds"));
	CHECK(test::has_diagnostic(d, 10, "expected a portrait name after 'as'"));
	CHECK(test::has_diagnostic(d, 11, "fade: expected 'out', found name 'sideways'"));
	CHECK(test::has_diagnostic(d, 12, "unexpected character '*'"));
	CHECK(test::has_diagnostic(d, 13, "camera: expected a blend time in seconds"));
	CHECK(test::has_diagnostic(d, 14, "party: expected 'add', found name 'invite'"));
	CHECK(test::has_diagnostic(d, 15, "anim: unexpected name 'sometimes'"));
	CHECK_EQ(d.size(), 14u);
}

TEST(unknown_and_duplicate_labels) {
	auto d = test::compile_errors(R"(event a unskippable
	label top
	goto nowhere
	label top
	goto top
end
event b unskippable
	goto top
end
)");
	CHECK(test::has_diagnostic(d, 3, "unknown label 'nowhere'"));
	CHECK(test::has_diagnostic(d, 4, "duplicate label 'top'; it is already defined at line 2"));
	// Labels are local to their event.
	CHECK(test::has_diagnostic(d, 8, "unknown label 'top' in event 'b'"));
	CHECK_EQ(d.size(), 3u);
}

TEST(unknown_and_duplicate_events_across_files) {
	events::CompileResult result = events::compile({
		{"first.ev", "event greet unskippable\n\tcall farewell\n\tcall missing\nend\n"},
		{"second.ev", "event farewell unskippable\nend\n\nevent greet unskippable\nend\n"},
	});
	CHECK(!result.ok());
	CHECK_EQ(result.diagnostics.size(), 2u);
	CHECK(test::has_diagnostic(result.diagnostics, 3, "call: unknown event 'missing'"));
	CHECK(test::has_diagnostic(result.diagnostics, 4, "duplicate event 'greet'; it is already defined at first.ev:1"));
	for (const Diagnostic& d : result.diagnostics) {
		if (d.line == 4) {
			CHECK_EQ(d.file, std::string("second.ev"));
		}
	}
}

TEST(unreachable_else_branches) {
	auto d = test::compile_errors(R"(event a unskippable
	if ready
		say guide "one"
	else
		say guide "two"
	else
		say guide "three"
	end
	if coins > 3
	else if coins > 3
	else if ready and rested
	else
	else if late
	end
end
)");
	CHECK(test::has_diagnostic(d, 6, "unreachable 'else': it follows the 'else' at line 4"));
	CHECK(test::has_diagnostic(d, 10, "unreachable 'else if': 'coins > 3' was already tested at line 9"));
	CHECK(test::has_diagnostic(d, 13, "unreachable 'else if': it follows the 'else' at line 12"));
	CHECK_EQ(d.size(), 3u);
}

TEST(unterminated_and_stray_blocks) {
	auto d = test::compile_errors(R"(event a unskippable
	else
	say guide "hi"
end
end
event b unskippable
	together
		move guide to pier
event c skippable
	if ready
		say guide "never closed"
)");
	CHECK(test::has_diagnostic(d, 2, "'else' without a matching 'if'"));
	CHECK(test::has_diagnostic(d, 5, "'end' without an open block"));
	CHECK(test::has_diagnostic(d, 9, "unterminated 'together' block opened at line 7"));
	CHECK(test::has_diagnostic(d, 9, "event 'b' opened at line 6 is missing 'end'"));
	CHECK(test::has_diagnostic(d, 11, "unterminated 'if' block opened at line 10"));
	CHECK(test::has_diagnostic(d, 11, "event 'c' opened at line 9 is missing 'end'"));
	CHECK_EQ(d.size(), 6u);
}

TEST(together_accepts_only_parallel_commands) {
	auto d = test::compile_errors(R"(event a unskippable
	together
		move guide to pier
		say guide "no talking while walking"
		sequence
			anim guide wave
			sequence
				wait 1
			end
		end
		face guide traveler nowait
		if ready
			wait 1
		end
	end
	sequence
		wait 1
	end
	together
	end
end
)");
	CHECK(test::has_diagnostic(d, 4, "'say' is not allowed inside 'together'"));
	CHECK(test::has_diagnostic(d, 7, "'sequence' is only allowed directly inside 'together'"));
	CHECK(test::has_diagnostic(d, 11, "'nowait' has no effect inside 'together'"));
	CHECK(test::has_diagnostic(d, 12, "'if' is not allowed inside 'together'"));
	CHECK(test::has_diagnostic(d, 16, "'sequence' is only allowed directly inside 'together'"));
	CHECK(test::has_diagnostic(d, 19, "together: the block is empty"));
	CHECK_EQ(d.size(), 6u);
}

TEST(skip_markers_are_required_and_on_skip_blocks_are_ignored_with_a_warning) {
	auto d = test::compile_errors(R"(event a
	set seen
end
)");
	CHECK(test::has_diagnostic(d, 1, "event 'a' must be marked 'skippable' or 'unskippable'"));
	CHECK_EQ(d.size(), 1u);

	// Older scripts still compile; the block's contents never run.
	events::CompileResult result = events::compile({{"test.ev", R"(event c skippable
	set seen
on skip
	say guide "old style"
	set skipped
end
)"}});
	CHECK(result.ok());
	CHECK_EQ(result.diagnostics.size(), 1u);
	CHECK(result.diagnostics[0].severity == Severity::warning);
	CHECK(test::has_diagnostic(result.diagnostics, 3, "'on skip' is ignored"));
	for (const events::Instruction& ins : result.program.code) {
		CHECK(ins.name != "skipped");
		CHECK(ins.op != events::Op::say);
	}
}

TEST(race_into_opening_ambient_and_menu_commands_compile_to_their_instructions) {
	events::Program p = test::compile_ok(R"(event a unskippable
	race
		move guide to pier
		wait 2
	end
	choice "Which?" into pick.way
	option "One"
	option "Two"
	end
	ask "Sure?" into pick.sure
	end
	battle reef_crabs opening party_first
	win
	end
	ambient play surf nowait
	ambient stop
	shop dock_stall
	rest
	rest harbor_inn
	save prompt
	name entry traveler
end
)");
	std::vector<std::string> ops;
	for (const events::Instruction& ins : p.code) {
		ops.push_back(events::op_name(ins.op));
	}
	CHECK_EQ(ops[0], std::string("fork"));
	CHECK_EQ(p.code[0].number, 1);
	const events::Instruction* choice = nullptr;
	const events::Instruction* ask = nullptr;
	const events::Instruction* battle = nullptr;
	for (const events::Instruction& ins : p.code) {
		if (ins.op == events::Op::choice) {
			(ins.choice.yes_no ? ask : choice) = &ins;
		}
		if (ins.op == events::Op::battle) {
			battle = &ins;
		}
	}
	CHECK(choice && choice->name == "pick.way");
	CHECK(choice && choice->option_lines == (std::vector<int>{7, 8}));
	CHECK(ask && ask->name == "pick.sure");
	CHECK(battle && battle->battle.opening == events::BattleOpening::party_first);
	std::vector<std::string> tail(ops.end() - 8, ops.end());
	std::vector<std::string> expected = {"action", "action", "shop", "rest", "rest", "save prompt", "name entry", "return"};
	CHECK(tail == expected);
}

TEST(new_command_syntax_errors) {
	auto d = test::compile_errors(R"(event a unskippable
	battle reef_crabs opening sideways
	win
	end
	choice into
	option "a"
	option "b"
	end
	ambient loud
	save game
	name traveler
	shop
	race
		say guide "racing"
		wait 1
	end
	race
		move guide to pier nowait
		wait 1
	end
end
)");
	CHECK(test::has_diagnostic(d, 2, "expected 'normal', 'party_first' or 'enemy_first' after 'opening', found 'sideways'"));
	CHECK(test::has_diagnostic(d, 5, "choice: expected a variable name after 'into'"));
	CHECK(test::has_diagnostic(d, 9, "ambient: expected 'play' or 'stop'"));
	CHECK(test::has_diagnostic(d, 10, "save: expected 'prompt'"));
	CHECK(test::has_diagnostic(d, 11, "name: expected 'entry'"));
	CHECK(test::has_diagnostic(d, 12, "shop: expected a shop name"));
	CHECK(test::has_diagnostic(d, 14, "'say' is not allowed inside 'race'"));
	CHECK(test::has_diagnostic(d, 18, "'nowait' has no effect inside 'race'"));
	CHECK_EQ(d.size(), 8u);
}

TEST(choice_ask_and_battle_structure) {
	auto d = test::compile_errors(R"(event a unskippable
	choice "Only one way?"
	option "Yes"
		wait 1
	end
	choice
		say guide "before any option"
	option "a"
	option "b"
	end
	ask "Again?"
	yes
	yes
	end
	battle reef_crabs
	win
	draw
	end
	option "loose"
	flee
end
)");
	CHECK(test::has_diagnostic(d, 2, "choice: needs at least two options"));
	CHECK(test::has_diagnostic(d, 7, "commands must come after the first 'option'"));
	CHECK(test::has_diagnostic(d, 13, "duplicate 'yes' section; the first one is at line 12"));
	CHECK(test::has_diagnostic(d, 17, "unknown command 'draw'"));
	CHECK(test::has_diagnostic(d, 19, "'option' outside of a 'choice' block"));
	CHECK(test::has_diagnostic(d, 20, "'flee' outside of a 'battle' block"));
	CHECK_EQ(d.size(), 6u);
}

TEST(condition_syntax_errors) {
	auto d = test::compile_errors(R"(event a unskippable
	if ready and rested or late
	end
	if coins = 3
	end
	if coins >
	end
	while
	end
	if ready rested
	end
end
)");
	CHECK(test::has_diagnostic(d, 2, "mixing 'and' and 'or'"));
	CHECK(test::has_diagnostic(d, 4, "'=' is not a comparison"));
	CHECK(test::has_diagnostic(d, 6, "expected an integer to compare with"));
	CHECK(test::has_diagnostic(d, 8, "expected a flag or variable name"));
	CHECK(test::has_diagnostic(d, 10, "expected 'and', 'or' or the end of the line"));
	CHECK_EQ(d.size(), 5u);
}

TEST(money_equipment_and_area_music_commands_are_checked) {
	const std::vector<Diagnostic> d = test::compile_errors(R"(event a skippable
	money take 0
	money spend 4
	equip fighter
	together
		music area harbor_theme
	end
end
)");
	CHECK(test::has_diagnostic(d, 2, "money must be at least 1"));
	CHECK(test::has_diagnostic(d, 3, "'give' or 'take'"));
	CHECK(test::has_diagnostic(d, 4, "an equipment name"));
	CHECK(test::has_diagnostic(d, 6, "music"));
}
