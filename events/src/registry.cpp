#include "events/registry.h"

#include "text_util.h"

#include <cstdlib>
#include <sstream>

namespace events {

namespace {

struct Keyword {
	const char* word;
	NameKind kind;
};

const Keyword keywords[] = {
	{"actor", NameKind::actor},
	{"marker", NameKind::marker},
	{"camera", NameKind::camera},
	{"layout", NameKind::layout},
	{"audio", NameKind::audio},
	{"animation", NameKind::animation},
	{"portrait", NameKind::portrait},
	{"item", NameKind::item},
	{"member", NameKind::member},
	{"encounter", NameKind::encounter},
	{"map", NameKind::map},
	{"shop", NameKind::shop},
	{"rest", NameKind::rest_point},
};

bool parse_integer(const std::string& word, std::int64_t& out) {
	char* end = nullptr;
	out = std::strtoll(word.c_str(), &end, 10);
	return !word.empty() && end && *end == '\0';
}

}

void Registry::add(NameKind kind, const std::string& name) {
	names[kind].insert(name);
}

bool Registry::has(NameKind kind, const std::string& name) const {
	auto found = names.find(kind);
	return found != names.end() && found->second.count(name) > 0;
}

void Registry::declare_flag(const std::string& name, bool local, bool initial) {
	declarations.push_back({name, true, local, initial ? 1 : 0, 0});
}

void Registry::declare_var(const std::string& name, bool local, std::int64_t initial) {
	declarations.push_back({name, false, local, initial, 0});
}

const Declaration* Registry::find(const std::string& name) const {
	for (const Declaration& d : declarations) {
		if (d.name == name) {
			return &d;
		}
	}
	return nullptr;
}

bool load_registry(const std::string& file, const std::string& text, Registry& registry, std::vector<Diagnostic>& diagnostics) {
	Registry result;
	result.file = file;
	bool ok = true;
	std::map<std::pair<NameKind, std::string>, int> seen;
	std::vector<std::string> lines = split_lines(text);
	for (std::size_t i = 0; i < lines.size(); ++i) {
		const int number = static_cast<int>(i) + 1;
		auto error = [&](const std::string& message) {
			diagnostics.push_back({Severity::error, file, number, message});
			ok = false;
		};
		std::istringstream in(trim(lines[i].substr(0, lines[i].find('#'))));
		std::vector<std::string> words;
		std::string w;
		while (in >> w) {
			words.push_back(w);
		}
		if (words.empty()) {
			continue;
		}
		const std::string& key = words[0];
		if (key == "flag" || key == "var") {
			Declaration d;
			d.is_flag = key == "flag";
			d.line = number;
			std::string problem;
			if (words.size() < 2 || !is_name(words[1])) {
				problem = key + ": expected a name";
			} else {
				d.name = words[1];
			}
			for (std::size_t j = 2; j < words.size() && problem.empty(); ++j) {
				if (words[j] == "local") {
					d.local = true;
				} else if (words[j] == "global") {
					d.local = false;
				} else if (words[j] == "default" && j + 1 < words.size()) {
					const std::string& value = words[++j];
					if (d.is_flag && (value == "true" || value == "false")) {
						d.initial = value == "true" ? 1 : 0;
					} else if (d.is_flag || !parse_integer(value, d.initial)) {
						problem = key + ": the default must be " + (d.is_flag ? "true or false" : "an integer") + ", found '" + value + "'";
					}
				} else {
					problem = key + ": expected 'global', 'local' or 'default <value>', found '" + words[j] + "'";
				}
			}
			if (problem.empty()) {
				if (const Declaration* earlier = result.find(d.name)) {
					problem = "'" + d.name + "' is already declared at line " + std::to_string(earlier->line);
				}
			}
			if (!problem.empty()) {
				error(problem);
				continue;
			}
			result.declarations.push_back(d);
			continue;
		}
		const Keyword* keyword = nullptr;
		for (const Keyword& k : keywords) {
			keyword = key == k.word ? &k : keyword;
		}
		if (!keyword) {
			error("unknown kind '" + key + "'");
			continue;
		}
		if (words.size() < 2) {
			error(key + ": expected one or more names");
			continue;
		}
		for (std::size_t j = 1; j < words.size(); ++j) {
			if (!is_name(words[j])) {
				error(key + ": '" + words[j] + "' is not a valid name");
				continue;
			}
			auto found = seen.find({keyword->kind, words[j]});
			if (found != seen.end()) {
				error(key + ": '" + words[j] + "' is already listed at line " + std::to_string(found->second));
				continue;
			}
			seen[{keyword->kind, words[j]}] = number;
			result.add(keyword->kind, words[j]);
		}
	}
	if (ok) {
		registry = result;
	}
	return ok;
}

const char* name_kind_name(NameKind kind) {
	switch (kind) {
	case NameKind::actor:
		return "actor";
	case NameKind::marker:
		return "marker";
	case NameKind::camera:
		return "camera";
	case NameKind::layout:
		return "layout";
	case NameKind::audio:
		return "audio cue";
	case NameKind::animation:
		return "animation";
	case NameKind::portrait:
		return "portrait";
	case NameKind::item:
		return "item";
	case NameKind::member:
		return "party member";
	case NameKind::encounter:
		return "encounter";
	case NameKind::map:
		return "map";
	case NameKind::shop:
		return "shop";
	case NameKind::rest_point:
		return "rest point";
	}
	return "name";
}

}
