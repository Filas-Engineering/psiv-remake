#include "check.h"

#include "events/glossary.h"

using events::Diagnostic;
using events::Glossary;
using events::MentionKind;
using events::NameMention;
using events::NameSet;

namespace {

const char* const glossary_text = R"(# id | shipped | restored | reason
tide_lamp | Tidlamp | Tide Lantern | cut by the item name limit
moth | Moth | Ember Moth | cut by the enemy name limit
reef_salt | Reefslt | Reef Salt
)";

Glossary glossary_ok() {
	Glossary g;
	std::vector<Diagnostic> d;
	CHECK(events::load_glossary("names.txt", glossary_text, g, d));
	CHECK(d.empty());
	return g;
}

}

TEST(glossary_loads_entries_and_reports_bad_lines) {
	Glossary g = glossary_ok();
	CHECK_EQ(g.entries.size(), 3u);
	CHECK_EQ(g.find("tide_lamp")->restored, std::string("Tide Lantern"));
	CHECK_EQ(g.find("tide_lamp")->reason, std::string("cut by the item name limit"));
	CHECK_EQ(g.find("reef_salt")->reason, std::string());
	CHECK_EQ(g.find("moth")->line, 3);
	CHECK(g.find("missing") == nullptr);

	Glossary bad;
	std::vector<Diagnostic> d;
	CHECK(!events::load_glossary("names.txt", "a | A\n2b | B | Bee\nc |  | See\nd | D | Dee\nd | D2 | Dee2\n", bad, d));
	CHECK(test::has_diagnostic(d, 1, "expected '<id> | <shipped name> | <restored name> [| <reason>]'"));
	CHECK(test::has_diagnostic(d, 2, "'2b' is not a valid glossary ID"));
	CHECK(test::has_diagnostic(d, 3, "cannot be empty"));
	CHECK(test::has_diagnostic(d, 5, "duplicate glossary ID 'd'; it is already defined at line 4"));
	CHECK_EQ(d.size(), 4u);
}

TEST(shipped_names_are_only_offered_for_english_text) {
	CHECK(events::name_set_for(true, "en") == NameSet::shipped);
	CHECK(events::name_set_for(false, "en") == NameSet::restored);
	CHECK(events::name_set_for(true, "ja") == NameSet::restored);
	CHECK(events::name_set_for(true, "fr") == NameSet::restored);
}

TEST(render_names_switches_every_token_and_keeps_bad_ones_visible) {
	Glossary g = glossary_ok();
	const std::string line = "Fill the {name:tide_lamp} before the {name:moth} wakes. {name:tide_lamp}!";
	CHECK_EQ(events::render_names(line, g, NameSet::restored), std::string("Fill the Tide Lantern before the Ember Moth wakes. Tide Lantern!"));
	CHECK_EQ(events::render_names(line, g, NameSet::shipped), std::string("Fill the Tidlamp before the Moth wakes. Tidlamp!"));

	std::vector<std::string> problems;
	std::string rendered = events::render_names("A {name:ghost} and {name: broken and {name:reef_salt}.", g, NameSet::shipped, &problems);
	CHECK_EQ(rendered, std::string("A {name:ghost} and {name: broken and Reefslt."));
	CHECK(problems == (std::vector<std::string>{"{name:ghost}", "{name:"}));
	CHECK_EQ(events::render_names("No names here.", g, NameSet::shipped), std::string("No names here."));
}

TEST(mention_report_finds_tokens_and_typed_names_through_tables) {
	Glossary g = glossary_ok();
	events::Program p = test::compile_ok(R"(event dock unskippable
	say guide "Your {name:tide_lamp} is low."
	say guide "An Ember Moth! Just a Moth, really."
	say guide "Mothballs are not moths."
	narrate @dock.salt
	choice "Take {name:ghost}?"
	option "Yes"
	option "No"
	end
end
)");
	events::StringTable en;
	events::StringTable ja;
	std::vector<Diagnostic> d;
	CHECK(events::load_string_table("en.strings", "language en\ndock.salt = The air tastes of Reef Salt.\n", en, d));
	CHECK(events::load_string_table("ja.strings", "language ja\ndock.salt = {name:reef_salt}の味がする。\n", ja, d));
	std::vector<NameMention> m = events::find_name_mentions(p, g, {en, ja});
	std::vector<std::string> got;
	for (const NameMention& n : m) {
		got.push_back(std::to_string(n.line) + " " + n.event + " " + events::mention_kind_name(n.kind) + " " + n.id + (n.via.empty() ? "" : " " + n.via) +
			(n.language.empty() ? "" : " " + n.language));
	}
	std::vector<std::string> expected = {
		"2 dock token tide_lamp",
		// The longer restored name wins over the shipped "Moth" inside it.
		"3 dock typed restored name moth",
		"3 dock typed shipped name moth",
		"5 dock typed restored name reef_salt @dock.salt (en.strings) en",
		"5 dock token reef_salt @dock.salt (ja.strings) ja",
		"6 dock unknown token ghost",
	};
	CHECK(got == expected);
	if (got != expected) {
		std::printf("%s", test::join(got).c_str());
	}
}

TEST(name_check_rejects_typed_names_and_bad_tokens_through_tables) {
	Glossary g = glossary_ok();
	events::Program p = test::compile_ok(R"(event dock unskippable
	say guide "Your {name:tide_lamp} is low."
	narrate @dock.salt
	say guide "A {name:ghost} and a {name: broken one."
end
)");
	events::StringTable en;
	std::vector<Diagnostic> d;
	CHECK(events::load_string_table("en.strings", "language en\ndock.salt = The air tastes of Reefslt.\n", en, d));
	std::vector<Diagnostic> found = events::check_names(p, g, {en});
	std::vector<std::string> got;
	for (const Diagnostic& diagnostic : found) {
		got.push_back(events::format_diagnostic(diagnostic));
	}
	std::vector<std::string> expected = {
		"test.ev:3: error: the name \"Reefslt\" is typed out (via @dock.salt (en.strings)); write {name:reef_salt} so the \"show shipped names\" setting can switch it",
		"test.ev:4: error: unknown name token {name:ghost}: it is not in names.txt",
		"test.ev:4: error: broken name token: write {name:<id>}",
	};
	CHECK(got == expected);
	if (got != expected) {
		std::printf("%s", test::join(got).c_str());
	}
}
