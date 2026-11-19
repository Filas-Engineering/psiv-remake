#pragma once

#include "events/diagnostics.h"
#include "events/host.h"
#include "events/program.h"

#include <map>
#include <string>
#include <vector>

namespace events {

// One localization table: one file per language. The text format is in
// FORMAT.md ("String tables").
struct StringEntry {
	std::string text;
	int line = 0;
};

struct StringTable {
	std::string file;
	std::string language;
	std::map<std::string, StringEntry> entries;

	// nullptr when the ID is not in the table.
	const std::string* find(const std::string& id) const;
};

// Returns false and reports every problem when the text is not a valid table.
bool load_string_table(const std::string& file, const std::string& text, StringTable& table, std::vector<Diagnostic>& diagnostics);

// Inline text comes back as written. An ID is looked up; when it is missing
// the result is false and `out` is "@id", so a missing string is visible.
bool resolve_text(const Text& text, const StringTable& table, std::string& out);

// Every piece of dialogue, prompt and option text in the program, in code
// order, with where it came from.
struct TextUse {
	int file = 0;
	// For an option, the line of the option itself.
	int line = 0;
	std::string event;
	// "say <speaker>", "narrate", "choice prompt", "option" or "ask".
	std::string role;
	Text text;
};

std::vector<TextUse> collect_texts(const Program& program);

// Every control tag in the text, in order: `{word}` or `{word:value}`, other
// than the name tokens `{name:<id>}`. The host interprets them, for example
// `{page}` for a page pause.
std::vector<std::string> find_control_tags(const std::string& text);

// Errors: an ID a script uses that a table lacks; an ID whose name tokens
// (in any order) or control tags (in order) differ between languages, as
// when a translation drops or changes one (TXT-034). Warnings: an ID no
// script uses (only when the program has events), and an ID one language
// has and another lacks.
std::vector<Diagnostic> check_strings(const Program& program, const std::vector<StringTable>& tables);

// An error for every quoted text in the program: player-facing text must
// come from string tables (TXT-030).
std::vector<Diagnostic> check_inline_text(const Program& program);

// A proposed table holding every inline text in the program, with IDs of
// the form <event>.<n>. Nothing is rewritten; writers move lines to IDs by hand.
std::string extract_strings(const Program& program, const std::string& language);

}
