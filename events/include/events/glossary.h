#pragma once

#include "events/diagnostics.h"
#include "events/program.h"
#include "events/strings.h"

#include <string>
#include <vector>

namespace events {

// A renamed name: what the shipped release called it and its restored form.
// Dialogue refers to it with the name token {name:<id>}. The file format is
// in FORMAT.md ("Name glossary").
struct GlossaryEntry {
	std::string id;
	std::string shipped;
	std::string restored;
	std::string reason;
	int line = 0;
};

struct Glossary {
	std::string file;
	std::vector<GlossaryEntry> entries;

	const GlossaryEntry* find(const std::string& id) const;
};

bool load_glossary(const std::string& file, const std::string& text, Glossary& glossary, std::vector<Diagnostic>& diagnostics);

enum class NameSet { restored, shipped };

// The "show shipped names" setting only exists for English text, so any
// other language always gets restored names.
NameSet name_set_for(bool show_shipped_names, const std::string& text_language);

// Replaces every {name:<id>} token with that entry's name from `names`.
// A token with an unknown ID, or a broken one, is left as written and its
// text is added to `problems` when given.
std::string render_names(const std::string& text, const Glossary& glossary, NameSet names, std::vector<std::string>* problems = nullptr);

struct NameToken {
	std::size_t begin = 0;
	std::size_t end = 0;
	// Empty for a broken token: "{name:" without a valid ID and closing brace.
	std::string id;
};

std::vector<NameToken> find_name_tokens(const std::string& text);

enum class MentionKind {
	token,
	// The name is typed out instead of using a token, so the setting
	// cannot switch it.
	literal_shipped,
	literal_restored,
	unknown_token,
	broken_token,
};

struct NameMention {
	std::string file;
	int line = 0;
	std::string event;
	MentionKind kind = MentionKind::token;
	// Glossary ID; for unknown_token the ID written; empty for broken_token.
	std::string id;
	// Where the text came from: empty for inline text, else "@id (file)".
	std::string via;
	// The table's language for text that came through one, else empty.
	std::string language;
};

// Every script line whose text mentions a glossary name, either through a
// token or typed out. Lines that use a string ID are checked in each table.
std::vector<NameMention> find_name_mentions(const Program& program, const Glossary& glossary, const std::vector<StringTable>& tables);

const char* mention_kind_name(MentionKind kind);

}
