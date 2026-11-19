#pragma once

#include "events/diagnostics.h"
#include "events/program.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace events {

// The kinds of content a script names. The file keyword of each is in
// FORMAT.md ("Registry").
enum class NameKind {
	actor,
	marker,
	camera,
	layout,
	audio,
	animation,
	portrait,
	item,
	member,
	encounter,
	map,
	shop,
	rest_point,
};

// The game's content, so the compiler can reject names it does not know
// (EVT-003) and flags and variables nobody declared (EVT-004). Load one from
// a text file with load_registry, or build one in code with add() and
// declare_flag() / declare_var().
struct Registry {
	std::string file;
	std::map<NameKind, std::set<std::string>> names;
	std::vector<Declaration> declarations;

	void add(NameKind kind, const std::string& name);
	bool has(NameKind kind, const std::string& name) const;
	void declare_flag(const std::string& name, bool local = false, bool initial = false);
	void declare_var(const std::string& name, bool local = false, std::int64_t initial = 0);
	const Declaration* find(const std::string& name) const;
};

// Returns false and reports every problem when the text is not a valid registry.
bool load_registry(const std::string& file, const std::string& text, Registry& registry, std::vector<Diagnostic>& diagnostics);

// For messages: "actor", "audio cue", "rest point"...
const char* name_kind_name(NameKind kind);

}
