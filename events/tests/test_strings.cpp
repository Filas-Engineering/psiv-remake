#include "check.h"

#include "events/strings.h"

using events::Diagnostic;
using events::Severity;
using events::StringTable;

namespace {

StringTable table_ok(const std::string& file, const std::string& text) {
	StringTable table;
	std::vector<Diagnostic> d;
	CHECK(events::load_string_table(file, text, table, d));
	CHECK(d.empty());
	return table;
}

const char* const script = R"(event dock unskippable
	say guide @dock.hello as cheerful
	narrate "The planks creak."
	choice @dock.where
	option "North"
		say guide @dock.north
	option @dock.south
	end
	ask "Stay a while?"
	yes
	end
end
)";

std::vector<std::string> messages(const std::vector<Diagnostic>& diagnostics) {
	std::vector<std::string> out;
	for (const Diagnostic& d : diagnostics) {
		out.push_back(events::format_diagnostic(d));
	}
	return out;
}

bool any_contains(const std::vector<Diagnostic>& diagnostics, Severity severity, const std::string& where, const std::string& fragment) {
	for (const Diagnostic& d : diagnostics) {
		std::string text = events::format_diagnostic(d);
		if (d.severity == severity && text.find(where) == 0 && text.find(fragment) != std::string::npos) {
			return true;
		}
	}
	std::printf("  missing \"%s\" ... \"%s\" in:\n%s", where.c_str(), fragment.c_str(), test::join(messages(diagnostics)).c_str());
	return false;
}

}

TEST(string_tables_load_ids_escapes_and_comments) {
	StringTable t = table_ok("en.strings", "# English\n\nlanguage en\ndock.hello = Welcome aboard.  \n  dock.two =First\\nSecond \\\\ end\n# dock.hidden = no\nroom.sign = Room #3\n");
	CHECK_EQ(t.language, std::string("en"));
	CHECK_EQ(t.entries.size(), 3u);
	CHECK_EQ(*t.find("dock.hello"), std::string("Welcome aboard."));
	CHECK_EQ(*t.find("dock.two"), std::string("First\nSecond \\ end"));
	CHECK_EQ(*t.find("room.sign"), std::string("Room #3"));
	CHECK_EQ(t.entries.at("dock.two").line, 5);
	CHECK(t.find("dock.hidden") == nullptr);
}

TEST(string_table_errors_are_reported_with_lines) {
	StringTable t;
	std::vector<Diagnostic> d;
	CHECK(!events::load_string_table("bad.strings", "language en\nno equals sign\n9lives = Cats\na = one\na = two\nb = trailing \\\nc = \\q\n", t, d));
	CHECK(test::has_diagnostic(d, 2, "expected '<id> = <text>'"));
	CHECK(test::has_diagnostic(d, 3, "'9lives' is not a valid string ID"));
	CHECK(test::has_diagnostic(d, 5, "duplicate string ID 'a'; it is already defined at line 4"));
	CHECK(test::has_diagnostic(d, 6, "a '\\' at the end of the line"));
	CHECK(test::has_diagnostic(d, 7, "unknown escape '\\q'"));
	CHECK_EQ(d.size(), 5u);

	d.clear();
	CHECK(!events::load_string_table("bad.strings", "# only a comment\nhello = there\n", t, d));
	CHECK(test::has_diagnostic(d, 2, "must start with 'language <code>'"));
}

TEST(resolve_text_returns_inline_text_and_looks_up_ids) {
	StringTable t = table_ok("en.strings", "language en\ndock.hello = Welcome aboard.\n");
	std::string out;
	CHECK(events::resolve_text({false, "Plain."}, t, out));
	CHECK_EQ(out, std::string("Plain."));
	CHECK(events::resolve_text({true, "dock.hello"}, t, out));
	CHECK_EQ(out, std::string("Welcome aboard."));
	CHECK(!events::resolve_text({true, "dock.gone"}, t, out));
	CHECK_EQ(out, std::string("@dock.gone"));
}

TEST(string_check_reports_missing_unused_and_untranslated_ids) {
	events::Program p = test::compile_ok(script);
	StringTable en = table_ok("en.strings", "language en\ndock.hello = Hello.\ndock.where = Where?\ndock.north = North it is.\ndock.south = South\n"
		"dock.old = Unused line.\ndock.draft = Only in English.\n");
	StringTable ja = table_ok("ja.strings", "language ja\ndock.hello = こんにちは。\ndock.where = どこへ？\ndock.south = 南\ndock.old = 古い\n");
	std::vector<Diagnostic> d = events::check_strings(p, {en, ja});
	// Used but missing: an error at the script line, naming the language.
	CHECK(any_contains(d, Severity::error, "test.ev:6:", "'dock.north' is missing from the 'ja' table (ja.strings)"));
	// Unused, per table.
	CHECK(any_contains(d, Severity::warning, "en.strings:6:", "'dock.old' is not used by any script"));
	CHECK(any_contains(d, Severity::warning, "ja.strings:5:", "'dock.old' is not used by any script"));
	CHECK(any_contains(d, Severity::warning, "en.strings:7:", "'dock.draft' is not used by any script"));
	// Untranslated and unused: reported against the language that has it.
	CHECK(any_contains(d, Severity::warning, "en.strings:7:", "'dock.draft' is in the 'en' table but missing from 'ja' (ja.strings)"));
	CHECK_EQ(d.size(), 5u);
}

TEST(string_check_without_scripts_only_compares_languages) {
	StringTable en = table_ok("en.strings", "language en\na = A\nb = Take the {name:tide_lamp}.\n");
	StringTable ja = table_ok("ja.strings", "language ja\nb = ランプ\n");
	std::vector<Diagnostic> d = events::check_strings(events::Program(), {en, ja});
	CHECK(any_contains(d, Severity::warning, "en.strings:2:", "'a' is in the 'en' table but missing from 'ja'"));
	CHECK(any_contains(d, Severity::warning, "ja.strings:2:", "name tokens in 'b' (none) differ from the 'en' table (tide_lamp)"));
	CHECK_EQ(d.size(), 2u);
}

TEST(extract_lists_every_inline_text_in_order_and_skips_ids) {
	events::Program p = test::compile_ok(script);
	std::string table = events::extract_strings(p, "en");
	CHECK(table.find("language en\n") != std::string::npos);
	CHECK(table.find("# test.ev:3 dock: narrate\ndock.1 = The planks creak.\n") != std::string::npos);
	CHECK(table.find("# test.ev:4 dock: option\ndock.2 = North\n") != std::string::npos);
	CHECK(table.find("# test.ev:9 dock: ask\ndock.3 = Stay a while?\n") != std::string::npos);
	CHECK(table.find("dock.4") == std::string::npos);
	CHECK(table.find("@dock") == std::string::npos);

	// The proposal loads as a table and holds exactly the inline texts.
	StringTable loaded = table_ok("proposed.strings", table);
	CHECK_EQ(loaded.entries.size(), 3u);

	events::Program escaped = test::compile_ok("event a unskippable\n\tsay guide \"Line one\\nline two \\\\ done\"\nend\n");
	StringTable round = table_ok("proposed.strings", events::extract_strings(escaped, "en"));
	CHECK_EQ(*round.find("a.1"), std::string("Line one\nline two \\ done"));
}

TEST(collect_texts_names_event_role_and_line) {
	events::Program p = test::compile_ok(script + std::string("event second unskippable\n\tsay keeper \"Late.\"\nend\n"));
	std::vector<events::TextUse> uses = events::collect_texts(p);
	CHECK_EQ(uses.size(), 8u);
	CHECK_EQ(uses[0].role, std::string("say guide"));
	CHECK_EQ(uses[2].role, std::string("choice prompt"));
	CHECK_EQ(uses[6].role, std::string("ask"));
	CHECK_EQ(uses[7].event, std::string("second"));
	CHECK_EQ(uses[7].line, 14);
}
