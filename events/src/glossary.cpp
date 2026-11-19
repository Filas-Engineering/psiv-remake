#include "events/glossary.h"

#include "text_util.h"

#include <algorithm>
#include <cctype>

namespace events {

namespace {

const std::string token_open = "{name:";

std::vector<std::string> split_fields(const std::string& line) {
	std::vector<std::string> fields;
	std::size_t start = 0;
	while (true) {
		std::size_t bar = line.find('|', start);
		fields.push_back(trim(line.substr(start, bar == std::string::npos ? std::string::npos : bar - start)));
		if (bar == std::string::npos) {
			return fields;
		}
		start = bar + 1;
	}
}

bool is_word_byte(char c) {
	return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

struct Candidate {
	std::string name;
	std::string id;
	MentionKind kind;
};

void scan(const std::string& text, const Glossary& glossary, const std::vector<Candidate>& candidates, std::vector<std::pair<MentionKind, std::string>>& found) {
	std::string masked = text;
	for (const NameToken& t : find_name_tokens(text)) {
		if (t.id.empty()) {
			found.push_back({MentionKind::broken_token, ""});
		} else if (glossary.find(t.id)) {
			found.push_back({MentionKind::token, t.id});
		} else {
			found.push_back({MentionKind::unknown_token, t.id});
		}
		std::fill(masked.begin() + static_cast<long>(t.begin), masked.begin() + static_cast<long>(t.end), '\0');
	}
	for (const Candidate& c : candidates) {
		std::size_t at = 0;
		while ((at = masked.find(c.name, at)) != std::string::npos) {
			std::size_t end = at + c.name.size();
			bool whole = (at == 0 || !is_word_byte(masked[at - 1])) && (end >= masked.size() || !is_word_byte(masked[end]));
			if (whole) {
				found.push_back({c.kind, c.id});
				std::fill(masked.begin() + static_cast<long>(at), masked.begin() + static_cast<long>(end), '\0');
			}
			at = end;
		}
	}
}

}

const GlossaryEntry* Glossary::find(const std::string& id) const {
	for (const GlossaryEntry& e : entries) {
		if (e.id == id) {
			return &e;
		}
	}
	return nullptr;
}

bool load_glossary(const std::string& file, const std::string& text, Glossary& glossary, std::vector<Diagnostic>& diagnostics) {
	Glossary result;
	result.file = file;
	bool ok = true;
	std::vector<std::string> lines = split_lines(text);
	for (std::size_t i = 0; i < lines.size(); ++i) {
		int number = static_cast<int>(i) + 1;
		std::string line = trim(lines[i]);
		if (line.empty() || line[0] == '#') {
			continue;
		}
		std::vector<std::string> fields = split_fields(line);
		std::string problem;
		if (fields.size() < 3 || fields.size() > 4) {
			problem = "expected '<id> | <shipped name> | <restored name> [| <reason>]'";
		} else if (!is_name(fields[0])) {
			problem = "'" + fields[0] + "' is not a valid glossary ID";
		} else if (fields[1].empty() || fields[2].empty()) {
			problem = "the shipped and restored names cannot be empty";
		} else if (const GlossaryEntry* earlier = result.find(fields[0])) {
			problem = "duplicate glossary ID '" + fields[0] + "'; it is already defined at line " + std::to_string(earlier->line);
		}
		if (!problem.empty()) {
			diagnostics.push_back({Severity::error, file, number, problem});
			ok = false;
			continue;
		}
		GlossaryEntry entry;
		entry.id = fields[0];
		entry.shipped = fields[1];
		entry.restored = fields[2];
		entry.reason = fields.size() > 3 ? fields[3] : std::string();
		entry.line = number;
		result.entries.push_back(entry);
	}
	if (ok) {
		glossary = result;
	}
	return ok;
}

NameSet name_set_for(bool show_shipped_names, const std::string& text_language) {
	return show_shipped_names && text_language == "en" ? NameSet::shipped : NameSet::restored;
}

std::vector<NameToken> find_name_tokens(const std::string& text) {
	std::vector<NameToken> tokens;
	std::size_t at = 0;
	while ((at = text.find(token_open, at)) != std::string::npos) {
		NameToken token;
		token.begin = at;
		std::size_t close = text.find('}', at + token_open.size());
		std::string id = close == std::string::npos ? std::string() : text.substr(at + token_open.size(), close - at - token_open.size());
		if (is_name(id)) {
			token.id = id;
			token.end = close + 1;
		} else {
			token.end = at + token_open.size();
		}
		tokens.push_back(token);
		at = token.end;
	}
	return tokens;
}

std::string render_names(const std::string& text, const Glossary& glossary, NameSet names, std::vector<std::string>* problems) {
	std::string out;
	std::size_t copied = 0;
	for (const NameToken& t : find_name_tokens(text)) {
		const GlossaryEntry* entry = t.id.empty() ? nullptr : glossary.find(t.id);
		if (!entry) {
			if (problems) {
				problems->push_back(text.substr(t.begin, t.end - t.begin));
			}
			continue;
		}
		out += text.substr(copied, t.begin - copied);
		out += names == NameSet::shipped ? entry->shipped : entry->restored;
		copied = t.end;
	}
	return out + text.substr(copied);
}

std::vector<NameMention> find_name_mentions(const Program& program, const Glossary& glossary, const std::vector<StringTable>& tables) {
	std::vector<Candidate> candidates;
	for (const GlossaryEntry& e : glossary.entries) {
		candidates.push_back({e.shipped, e.id, MentionKind::literal_shipped});
		if (e.restored != e.shipped) {
			candidates.push_back({e.restored, e.id, MentionKind::literal_restored});
		}
	}
	// Longest first, so a name inside a longer one is not counted twice.
	std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.name.size() > b.name.size(); });

	std::vector<NameMention> mentions;
	for (const TextUse& use : collect_texts(program)) {
		struct Source {
			std::string text;
			std::string via;
			std::string language;
		};
		std::vector<Source> texts;
		if (!use.text.is_id) {
			texts.push_back({use.text.value, "", ""});
		} else {
			for (const StringTable& table : tables) {
				if (const std::string* found = table.find(use.text.value)) {
					texts.push_back({*found, "@" + use.text.value + " (" + table.file + ")", table.language});
				}
			}
		}
		for (const Source& t : texts) {
			std::vector<std::pair<MentionKind, std::string>> found;
			scan(t.text, glossary, candidates, found);
			for (const auto& f : found) {
				NameMention m;
				m.file = program.files[use.file];
				m.line = use.line;
				m.event = use.event;
				m.kind = f.first;
				m.id = f.second;
				m.via = t.via;
				m.language = t.language;
				bool seen = false;
				for (const NameMention& earlier : mentions) {
					seen = seen || (earlier.file == m.file && earlier.line == m.line && earlier.kind == m.kind && earlier.id == m.id && earlier.via == m.via);
				}
				if (!seen) {
					mentions.push_back(m);
				}
			}
		}
	}
	return mentions;
}

const char* mention_kind_name(MentionKind kind) {
	switch (kind) {
	case MentionKind::token:
		return "token";
	case MentionKind::literal_shipped:
		return "typed shipped name";
	case MentionKind::literal_restored:
		return "typed restored name";
	case MentionKind::unknown_token:
		return "unknown token";
	case MentionKind::broken_token:
		return "broken token";
	}
	return "unknown";
}

}
