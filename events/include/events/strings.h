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
	int line = 0;
	std::string event;
	// "say <speaker>", "narrate", "choice prompt", "option" or "ask".
	std::string role;
	Text text;
};

std::vector<TextUse> collect_texts(const Program& program);

// Errors: an ID a script uses that a table lacks. Warnings: an ID no script
// uses (only when the program has events), an ID one language has and
// another lacks, and an ID whose name tokens differ between languages.
std::vector<Diagnostic> check_strings(const Program& program, const std::vector<StringTable>& tables);

// A proposed table holding every inline text in the program, with IDs of
// the form <event>.<n>. Nothing is rewritten; writers move lines to IDs by hand.
std::string extract_strings(const Program& program, const std::string& language);

}
