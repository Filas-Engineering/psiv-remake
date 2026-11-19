#include "rules/combo.h"
#include "rules/macro.h"
#include "rules/names.h"
#include "text_encoding.h"

#include <sstream>

namespace rules {

namespace {

constexpr int kMacrosVersion = 1;
constexpr int kComboLogVersion = 1;

// Reads "<kind> <version>" ... "end", calling `line` for every line between. Returns an error or "".
template <class LineReader>
std::string readVersioned(const std::string& text, const char* kind, int version, LineReader line) {
	std::istringstream in(text);
	std::string raw;
	int number = 0;
	bool header = false;
	bool ended = false;
	while (std::getline(in, raw)) {
		++number;
		const std::vector<std::string> t = text::split(raw);
		if (t.empty()) {
			continue;
		}
		const std::string where = std::string(kind) + " line " + std::to_string(number) + ": ";
		if (ended) {
			return where + "content after 'end'";
		}
		if (!header) {
			int found = 0;
			if (t.size() != 2 || t[0] != kind || !text::parseInt(t[1], found)) {
				return where + "expected '" + kind + " <version>'";
			}
			if (found != version) {
				return where + "version " + t[1] + " is not supported (this build reads " + std::to_string(version) + ")";
			}
			header = true;
			continue;
		}
		if (t.size() == 1 && t[0] == "end") {
			ended = true;
			continue;
		}
		const std::string problem = line(t);
		if (!problem.empty()) {
			return where + problem;
		}
	}
	if (!header) {
		return std::string(kind) + " line 1: expected '" + kind + " <version>'";
	}
	return ended ? "" : std::string(kind) + " ends without 'end'";
}

}

std::string serializeMacros(const std::vector<Macro>& macros) {
	std::ostringstream s;
	s << "macros " << kMacrosVersion << '\n';
	for (const Macro& macro : macros) {
		s << "macro " << text::encode(macro.name) << '\n';
		for (const MacroStep& step : macro.steps) {
			s << "step " << text::encode(step.member) << ' ' << toString(step.kind) << ' ' << text::encode(step.id) << ' '
				<< toString(step.target.kind) << ' ' << text::encode(step.target.member) << ' ' << step.target.enemySlot << '\n';
		}
	}
	s << "end\n";
	return s.str();
}

Result<std::vector<Macro>> parseMacros(const std::string& text) {
	std::vector<Macro> macros;
	const std::string error = readVersioned(text, "macros", kMacrosVersion, [&macros](const std::vector<std::string>& t) -> std::string {
		if (t[0] == "macro" && t.size() == 2) {
			Macro macro;
			if (!text::decode(t[1], macro.name) || macro.name.empty()) {
				return "bad macro name";
			}
			macros.push_back(macro);
			return "";
		}
		if (t[0] == "step" && t.size() == 7) {
			if (macros.empty()) {
				return "'step' before any 'macro'";
			}
			MacroStep step;
			const bool ok = text::decode(t[1], step.member) && !step.member.empty() && parseCommandKind(t[2], step.kind) && text::decode(t[3], step.id)
				&& parseMacroTargetKind(t[4], step.target.kind) && text::decode(t[5], step.target.member) && text::parseInt(t[6], step.target.enemySlot);
			if (!ok) {
				return "bad step";
			}
			macros.back().steps.push_back(step);
			return "";
		}
		return "unknown or malformed line '" + t[0] + "'";
	});
	if (!error.empty()) {
		return Result<std::vector<Macro>>::failure(error);
	}
	return Result<std::vector<Macro>>::success(std::move(macros));
}

std::string serializeComboLog(const ComboLog& log) {
	std::ostringstream s;
	s << "combo_log " << kComboLogVersion << '\n';
	for (const ComboLogEntry& entry : log.entries()) {
		s << "entry " << text::encode(entry.recipe) << ' ' << entry.playTimeMs << ' ' << text::encodeList(entry.characters) << '\n';
	}
	s << "end\n";
	return s.str();
}

Result<ComboLog> parseComboLog(const std::string& text) {
	ComboLog log;
	const std::string error = readVersioned(text, "combo_log", kComboLogVersion, [&log](const std::vector<std::string>& t) -> std::string {
		ComboLogEntry entry;
		if (t[0] != "entry" || t.size() != 4) {
			return "unknown or malformed line '" + t[0] + "'";
		}
		if (!text::decode(t[1], entry.recipe) || entry.recipe.empty() || !text::parseInt64(t[2], entry.playTimeMs) || !text::decodeList(t[3], entry.characters)) {
			return "bad entry";
		}
		if (!log.discover(entry.recipe, entry.playTimeMs, entry.characters)) {
			return "recipe '" + entry.recipe + "' is listed twice";
		}
		return "";
	});
	if (!error.empty()) {
		return Result<ComboLog>::failure(error);
	}
	return Result<ComboLog>::success(std::move(log));
}

}
