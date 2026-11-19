#include "check.h"

#include "events/lint.h"

using events::Diagnostic;

namespace {

std::vector<std::string> lint_lines(const std::string& text, const std::vector<std::string>& entries) {
	events::LintOptions options;
	options.entry_events = entries;
	std::vector<std::string> out;
	for (const Diagnostic& d : events::lint({{"test.ev", text}}, options)) {
		out.push_back(std::to_string(d.line) + (d.severity == events::Severity::error ? " error: " : " ") + d.message);
	}
	return out;
}

void check_lines(const std::vector<std::string>& got, const std::vector<std::string>& expected) {
	CHECK(got == expected);
	if (got != expected) {
		std::printf("  got:\n%s", test::join(got).c_str());
	}
}

}

TEST(lint_reports_flags_and_variables_written_or_read_on_one_side_only) {
	check_lines(lint_lines(R"(event a skippable
	set dock.visited
	set dock.used
	clear dock.cleared_only
	var dock.count = 1
	if dock.used and not dock.never_set
		var dock.tally += 1
	end
	while dock.count < 3 and dock.ghost > 0
		var dock.count += 1
	end
	ask "Stay?" into dock.stay
	end
	choice into dock.way
	option "Left"
	option "Right"
	end
	if dock.way == 2
		wait 1
	end
end
)", {"a"}), {
		"2 flag 'dock.visited' is set but never read by any script",
		"4 flag 'dock.cleared_only' is set but never read by any script",
		"6 flag 'dock.never_set' is read but never set by any script",
		"7 variable 'dock.tally' is written but never read by any script",
		"9 variable 'dock.ghost' is read but never written by any script",
		"12 flag 'dock.stay' is set but never read by any script",
	});
}

TEST(lint_reports_events_nobody_calls_unless_they_are_entry_events) {
	const char* script = R"(event start unskippable
	call helper
end
event helper unskippable
	wait 1
end
event orphan unskippable
	wait 1
end
)";
	check_lines(lint_lines(script, {"start"}), {"7 event 'orphan' is never called by another event and is not listed as an entry event"});
	check_lines(lint_lines(script, {"start", "orphan"}), {});
}

TEST(lint_reports_choices_with_identical_branches) {
	check_lines(lint_lines(R"(event a unskippable
	choice "Which?"
	option "Left"
		say guide "Onward."
		wait 1
	option "Right"
		say guide "Onward."
		wait 1
	option "Back"
		say guide "Back we go."
	option "Stay"
		say guide "Onward."
		wait 1
	end
	ask "Sure?"
	yes
		wait 1
	no
		wait 1
	end
	ask "Really?"
	yes
		say guide "Onward." as smiling
	no
		say guide "Onward."
	end
end
)", {"a"}), {
		"6 option 2 of this choice has the same branch as option 1",
		"11 option 4 of this choice has the same branch as option 1",
		"15 the 'yes' and 'no' branches of this ask are identical, so the answer changes nothing",
	});
}

TEST(lint_reports_commands_after_return_or_goto_until_a_label) {
	check_lines(lint_lines(R"(event a unskippable
	if ready
		return
		say guide "Never."
		wait 1
	end
	goto after
	say guide "Skipped."
	label after
	say guide "Reached."
	goto done
	label done
end
)", {"a"}), {
		"2 flag 'ready' is read but never set by any script",
		"4 unreachable: nothing jumps here after the 'return' at line 3; add a label or remove it",
		"8 unreachable: nothing jumps here after the 'goto' at line 7; add a label or remove it",
	});
}

TEST(lint_returns_compile_errors_unchanged) {
	std::vector<std::string> got = lint_lines("event a unskippable\n\tbogus\nend\n", {});
	check_lines(got, {"2 error: unknown command 'bogus'"});
}
