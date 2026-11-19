#include "events/compiler.h"
#include "events/glossary.h"
#include "events/interpreter.h"
#include "events/lint.h"
#include "events/recording_host.h"
#include "events/strings.h"
#include "events/timing.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace events;

namespace {

const int max_steps = 100000;

int usage() {
	std::fprintf(stderr,
		"usage:\n"
		"  event_cli check [--strict] [--registry FILE] [--glossary FILE] [--table FILE]... <file>...\n"
		"  event_cli run <file>... <event> [--choose 1,0] [--battle win,lose,flee] [--skip-at STEP] [--dt SECONDS]\n"
		"  event_cli lint [--entry EVENT,...] [--entries FILE] [--strict] <file>...\n"
		"  event_cli time <file>... <event> [--config FILE] [--text-speed CPS] [--table FILE] [--set FLAG,...] [--var NAME=N]...\n"
		"                [--dt SECONDS] [--max-paths N]\n"
		"  event_cli strings extract [--language CODE] <file>...\n"
		"  event_cli strings check --table FILE [--table FILE]... [<file>...]\n"
		"  event_cli names <glossary> [--table FILE]... [--show-shipped] [--language CODE] <file>...\n");
	return 2;
}

bool read_text(const std::string& path, std::string& text) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		std::fprintf(stderr, "%s: cannot read file\n", path.c_str());
		return false;
	}
	std::ostringstream out;
	out << in.rdbuf();
	text = out.str();
	return true;
}

std::vector<std::string> split(const std::string& list) {
	std::vector<std::string> parts;
	std::stringstream in(list);
	std::string part;
	while (std::getline(in, part, ',')) {
		parts.push_back(part);
	}
	return parts;
}

void print_diagnostics(const std::vector<Diagnostic>& diagnostics) {
	for (const Diagnostic& d : diagnostics) {
		std::fprintf(stderr, "%s\n", format_diagnostic(d).c_str());
	}
}

bool read_sources(const std::vector<std::string>& paths, std::vector<Source>& sources) {
	for (const std::string& path : paths) {
		Source source;
		source.file = path;
		if (!read_text(path, source.text)) {
			return false;
		}
		sources.push_back(source);
	}
	return true;
}

bool compile_files(const std::vector<std::string>& paths, CompileResult& result, const Registry* registry = nullptr) {
	std::vector<Source> sources;
	if (!read_sources(paths, sources)) {
		return false;
	}
	CompileOptions options;
	options.registry = registry;
	result = compile(sources, options);
	print_diagnostics(result.diagnostics);
	return result.ok();
}

bool load_table(const std::string& path, StringTable& table) {
	std::string text;
	std::vector<Diagnostic> diagnostics;
	bool ok = read_text(path, text) && load_string_table(path, text, table, diagnostics);
	print_diagnostics(diagnostics);
	return ok;
}

bool load_tables(const std::vector<std::string>& paths, std::vector<StringTable>& tables) {
	for (const std::string& path : paths) {
		StringTable table;
		if (!load_table(path, table)) {
			return false;
		}
		tables.push_back(table);
	}
	return true;
}

bool load_glossary_file(const std::string& path, Glossary& glossary) {
	std::string text;
	std::vector<Diagnostic> diagnostics;
	bool ok = read_text(path, text) && load_glossary(path, text, glossary, diagnostics);
	print_diagnostics(diagnostics);
	return ok;
}

// Splits arguments into positionals and `--name value` options. Names in
// `flags` take no value.
struct Args {
	std::vector<std::string> positional;
	std::multimap<std::string, std::string> options;
	bool ok = true;

	Args(int argc, char** argv, int first, const std::vector<std::string>& flags) {
		for (int i = first; i < argc; ++i) {
			std::string arg = argv[i];
			if (arg.compare(0, 2, "--") != 0) {
				positional.push_back(arg);
				continue;
			}
			bool is_flag = false;
			for (const std::string& f : flags) {
				is_flag = is_flag || f == arg;
			}
			if (is_flag) {
				options.insert({arg, ""});
			} else if (i + 1 < argc) {
				options.insert({arg, argv[++i]});
			} else {
				std::fprintf(stderr, "%s needs a value\n", arg.c_str());
				ok = false;
			}
		}
	}

	bool has(const std::string& name) const { return options.count(name) > 0; }

	std::string get(const std::string& name, const std::string& fallback = "") const {
		auto found = options.find(name);
		return found == options.end() ? fallback : found->second;
	}

	std::vector<std::string> all(const std::string& name) const {
		std::vector<std::string> values;
		for (auto range = options.equal_range(name); range.first != range.second; ++range.first) {
			values.push_back(range.first->second);
		}
		return values;
	}

	bool only(const std::vector<std::string>& known) const {
		for (const auto& option : options) {
			bool found = false;
			for (const std::string& k : known) {
				found = found || k == option.first;
			}
			if (!found) {
				std::fprintf(stderr, "unknown option %s\n", option.first.c_str());
				return false;
			}
		}
		return true;
	}
};

int check(int argc, char** argv) {
	Args args(argc, argv, 2, {"--strict"});
	if (!args.ok || !args.only({"--strict", "--registry", "--glossary", "--table"}) || args.positional.empty()) {
		return usage();
	}
	const bool strict = args.has("--strict");
	Registry registry;
	if (args.has("--registry")) {
		std::string text;
		std::vector<Diagnostic> diagnostics;
		bool ok = read_text(args.get("--registry"), text) && load_registry(args.get("--registry"), text, registry, diagnostics);
		print_diagnostics(diagnostics);
		if (!ok) {
			return 1;
		}
	}
	std::vector<StringTable> tables;
	Glossary glossary;
	if (!load_tables(args.all("--table"), tables) || (args.has("--glossary") && !load_glossary_file(args.get("--glossary"), glossary))) {
		return 1;
	}
	CompileResult result;
	if (!compile_files(args.positional, result, args.has("--registry") ? &registry : nullptr)) {
		return 1;
	}
	std::vector<Diagnostic> findings = result.diagnostics;
	std::vector<Diagnostic> more;
	if (strict) {
		more = check_inline_text(result.program);
		findings.insert(findings.end(), more.begin(), more.end());
	}
	if (!tables.empty()) {
		more = check_strings(result.program, tables);
		findings.insert(findings.end(), more.begin(), more.end());
	}
	if (args.has("--glossary")) {
		more = check_names(result.program, glossary, tables);
		findings.insert(findings.end(), more.begin(), more.end());
	}
	print_diagnostics(std::vector<Diagnostic>(findings.begin() + static_cast<long>(result.diagnostics.size()), findings.end()));
	if (has_errors(findings) || (strict && !findings.empty())) {
		std::fprintf(stderr, "check failed: %zu finding(s)%s\n", findings.size(), strict ? "; --strict fails on warnings too" : "");
		return 1;
	}
	std::printf("ok: %zu file(s), %zu event(s)%s\n", args.positional.size(), result.program.events.size(), strict ? ", strict" : "");
	return 0;
}

int run(int argc, char** argv) {
	Args args(argc, argv, 2, {});
	if (!args.ok || !args.only({"--choose", "--battle", "--skip-at", "--dt"}) || args.positional.size() < 2) {
		return usage();
	}
	std::vector<std::string> paths(args.positional.begin(), args.positional.end() - 1);
	std::string event = args.positional.back();
	std::vector<int> choices;
	for (const std::string& p : split(args.get("--choose"))) {
		choices.push_back(std::atoi(p.c_str()));
	}
	std::vector<BattleResult> battles;
	for (const std::string& p : split(args.get("--battle"))) {
		if (p == "win") {
			battles.push_back(BattleResult::win);
		} else if (p == "lose") {
			battles.push_back(BattleResult::lose);
		} else if (p == "flee") {
			battles.push_back(BattleResult::flee);
		} else {
			std::fprintf(stderr, "--battle: unknown result '%s'\n", p.c_str());
			return 2;
		}
	}
	int skip_at = args.has("--skip-at") ? std::atoi(args.get("--skip-at").c_str()) : -1;
	double dt = std::atof(args.get("--dt", "0.1").c_str());
	if (dt <= 0) {
		std::fprintf(stderr, "--dt must be positive\n");
		return 2;
	}

	CompileResult result;
	if (!compile_files(paths, result)) {
		return 1;
	}
	RecordingHost host;
	for (ActionType type : {ActionType::move, ActionType::face, ActionType::animate, ActionType::warp, ActionType::panel_show, ActionType::panel_hide}) {
		host.set_duration(type, 0.5);
	}
	host.set_battle_duration(0.5);
	host.queue_choices(choices);
	host.queue_battle_results(battles);

	Interpreter interpreter(result.program, host);
	if (!interpreter.start(event)) {
		std::fprintf(stderr, "%s\n", interpreter.error().c_str());
		return 1;
	}
	std::size_t printed = 0;
	int step = 0;
	for (; step < max_steps && interpreter.status() == Status::running; ++step) {
		if (step == skip_at) {
			bool accepted = interpreter.skip();
			std::printf("[step %d] skip %s\n", step, accepted ? "accepted" : "refused");
		}
		host.advance(dt);
		interpreter.step(dt);
		for (; printed < host.trace.size(); ++printed) {
			std::printf("[step %d] %s\n", step, host.trace[printed].c_str());
		}
	}
	if (interpreter.status() == Status::error) {
		std::fprintf(stderr, "runtime error: %s\n", interpreter.error().c_str());
		return 1;
	}
	if (interpreter.status() != Status::finished) {
		std::fprintf(stderr, "the event did not finish within %d steps\n", max_steps);
		return 1;
	}
	std::printf("finished after %d step(s)\n", step);
	for (const auto& f : host.flags) {
		std::printf("flag %s = %s\n", f.first.c_str(), f.second ? "true" : "false");
	}
	for (const auto& v : host.vars) {
		std::printf("var %s = %lld\n", v.first.c_str(), static_cast<long long>(v.second));
	}
	for (const auto& it : host.items) {
		std::printf("item %s x%d\n", it.first.c_str(), it.second);
	}
	for (const std::string& member : host.party) {
		std::printf("party %s\n", member.c_str());
	}
	std::printf("player control %s\n", host.player_control ? "on" : "off");
	return 0;
}

int lint_command(int argc, char** argv) {
	Args args(argc, argv, 2, {"--strict"});
	if (!args.ok || !args.only({"--entry", "--entries", "--strict"}) || args.positional.empty()) {
		return usage();
	}
	LintOptions options;
	for (const std::string& list : args.all("--entry")) {
		for (const std::string& name : split(list)) {
			options.entry_events.push_back(name);
		}
	}
	for (const std::string& path : args.all("--entries")) {
		std::string text;
		if (!read_text(path, text)) {
			return 1;
		}
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line)) {
			std::istringstream words(line);
			std::string name;
			if (words >> name && name[0] != '#') {
				options.entry_events.push_back(name);
			}
		}
	}
	std::vector<Source> sources;
	if (!read_sources(args.positional, sources)) {
		return 1;
	}
	std::vector<Diagnostic> findings = lint(sources, options);
	for (const Diagnostic& d : findings) {
		std::printf("%s\n", format_diagnostic(d).c_str());
	}
	if (has_errors(findings)) {
		return 1;
	}
	std::printf("%zu finding(s)\n", findings.size());
	return args.has("--strict") && !findings.empty() ? 1 : 0;
}

std::string seconds(double value) {
	char buffer[32];
	std::snprintf(buffer, sizeof buffer, "%.2f s", value);
	return buffer;
}

int time_command(int argc, char** argv) {
	Args args(argc, argv, 2, {});
	if (!args.ok || !args.only({"--config", "--text-speed", "--table", "--set", "--var", "--dt", "--max-paths"}) || args.positional.size() < 2) {
		return usage();
	}
	std::vector<std::string> paths(args.positional.begin(), args.positional.end() - 1);
	std::string event = args.positional.back();
	TimingConfig config;
	if (args.has("--config")) {
		std::string text;
		std::vector<Diagnostic> diagnostics;
		bool ok = read_text(args.get("--config"), text) && load_timing_config(args.get("--config"), text, config, diagnostics);
		print_diagnostics(diagnostics);
		if (!ok) {
			return 1;
		}
	}
	if (args.has("--text-speed")) {
		config.chars_per_second = std::atof(args.get("--text-speed").c_str());
	}
	for (const std::string& list : args.all("--set")) {
		for (const std::string& flag : split(list)) {
			config.start_flags[flag] = true;
		}
	}
	for (const std::string& assignment : args.all("--var")) {
		std::size_t eq = assignment.find('=');
		if (eq == std::string::npos) {
			std::fprintf(stderr, "--var: expected NAME=N, found '%s'\n", assignment.c_str());
			return 2;
		}
		config.start_vars[assignment.substr(0, eq)] = std::atoll(assignment.c_str() + eq + 1);
	}
	StringTable table;
	if (args.has("--table")) {
		if (!load_table(args.get("--table"), table)) {
			return 1;
		}
		config.strings = &table;
	}
	double dt = std::atof(args.get("--dt", "0.001").c_str());
	long max_paths = std::atol(args.get("--max-paths", "256").c_str());
	if (dt <= 0 || max_paths < 1) {
		std::fprintf(stderr, "--dt and --max-paths must be positive\n");
		return 2;
	}

	CompileResult result;
	if (!compile_files(paths, result)) {
		return 1;
	}
	std::vector<TimedPath> timed;
	bool truncated = false;
	std::string error;
	if (!time_event(result.program, event, config, timed, truncated, error, dt, static_cast<std::size_t>(max_paths))) {
		std::fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	std::printf("%s: %zu path(s)%s, text speed %g chars/s, line pause %g s\n", event.c_str(), timed.size(), truncated ? " (truncated)" : "",
		config.chars_per_second, config.seconds_per_line);
	bool failed = false;
	double shortest = 0;
	double longest = 0;
	for (std::size_t i = 0; i < timed.size(); ++i) {
		const TimedPath& path = timed[i];
		std::printf("\npath %zu:", i + 1);
		if (path.decisions.empty()) {
			std::printf(" (no choices)");
		}
		std::printf("\n");
		for (const Decision& d : path.decisions) {
			std::printf("\t%s -> %s\n", d.label.c_str(), d.options[d.picked].c_str());
		}
		const SceneTime& t = path.time;
		std::printf("\ttotal %s = dialogue %s + action %s + wait %s\n", seconds(t.total).c_str(), seconds(t.dialogue).c_str(), seconds(t.action).c_str(),
			seconds(t.wait).c_str());
		if (path.unmeasured_lines > 0) {
			std::printf("\tnote: %d line(s) use a string ID with no text to measure; pass --table\n", path.unmeasured_lines);
		}
		if (!path.error.empty()) {
			std::printf("\terror: %s\n", path.error.c_str());
			failed = true;
		}
		shortest = i == 0 ? t.total : std::min(shortest, t.total);
		longest = std::max(longest, t.total);
	}
	std::printf("\nshortest %s, longest %s\n", seconds(shortest).c_str(), seconds(longest).c_str());
	return failed ? 1 : 0;
}

int strings_command(int argc, char** argv) {
	if (argc < 3) {
		return usage();
	}
	std::string sub = argv[2];
	Args args(argc, argv, 3, {});
	if (!args.ok) {
		return usage();
	}
	if (sub == "extract") {
		if (!args.only({"--language"}) || args.positional.empty()) {
			return usage();
		}
		CompileResult result;
		if (!compile_files(args.positional, result)) {
			return 1;
		}
		std::printf("%s", extract_strings(result.program, args.get("--language", "en")).c_str());
		return 0;
	}
	if (sub == "check") {
		if (!args.only({"--table"}) || !args.has("--table")) {
			return usage();
		}
		std::vector<StringTable> tables;
		if (!load_tables(args.all("--table"), tables)) {
			return 1;
		}
		CompileResult result;
		if (!args.positional.empty() && !compile_files(args.positional, result)) {
			return 1;
		}
		std::vector<Diagnostic> findings = check_strings(result.program, tables);
		for (const Diagnostic& d : findings) {
			std::printf("%s\n", format_diagnostic(d).c_str());
		}
		std::printf("%zu table(s), %zu finding(s)\n", tables.size(), findings.size());
		return has_errors(findings) ? 1 : 0;
	}
	return usage();
}

int names_command(int argc, char** argv) {
	Args args(argc, argv, 2, {"--show-shipped"});
	if (!args.ok || !args.only({"--table", "--show-shipped", "--language"}) || args.positional.size() < 2) {
		return usage();
	}
	std::string glossary_path = args.positional.front();
	Glossary glossary;
	if (!load_glossary_file(glossary_path, glossary)) {
		return 1;
	}
	std::vector<StringTable> tables;
	if (!load_tables(args.all("--table"), tables)) {
		return 1;
	}
	CompileResult result;
	if (!compile_files(std::vector<std::string>(args.positional.begin() + 1, args.positional.end()), result)) {
		return 1;
	}
	bool show_shipped = args.has("--show-shipped");
	std::string language = args.get("--language", "en");
	bool broken = false;
	std::vector<NameMention> mentions = find_name_mentions(result.program, glossary, tables);
	for (const NameMention& m : mentions) {
		std::string where = m.file + ":" + std::to_string(m.line) + ": " + m.event + ": ";
		std::string via = m.via.empty() ? "" : " via " + m.via;
		const GlossaryEntry* entry = glossary.find(m.id);
		NameSet names = name_set_for(show_shipped, m.language.empty() ? language : m.language);
		switch (m.kind) {
		case MentionKind::token:
			std::printf("%stoken {%s:%s}%s shows \"%s\" (shipped \"%s\", restored \"%s\")\n", where.c_str(), name_case_word(m.name_case), m.id.c_str(), via.c_str(),
				shown_name(*entry, names, m.name_case).c_str(), shown_name(*entry, NameSet::shipped, m.name_case).c_str(),
				shown_name(*entry, NameSet::restored, m.name_case).c_str());
			break;
		case MentionKind::literal_shipped:
		case MentionKind::literal_restored:
			std::printf("%s%s \"%s\"%s; use {name:%s} so the setting can switch it\n", where.c_str(), mention_kind_name(m.kind),
				(m.kind == MentionKind::literal_shipped ? entry->shipped : entry->restored).c_str(), via.c_str(), m.id.c_str());
			broken = true;
			break;
		case MentionKind::unknown_token:
			std::printf("%sunknown token {%s:%s}%s: not in %s\n", where.c_str(), name_case_word(m.name_case), m.id.c_str(), via.c_str(), glossary_path.c_str());
			broken = true;
			break;
		case MentionKind::broken_token:
			std::printf("%sbroken name token%s: write {%s:<id>}\n", where.c_str(), via.c_str(), name_case_word(m.name_case));
			broken = true;
			break;
		}
	}
	std::printf("%zu mention(s) of %zu glossary name(s); shipped names %s\n", mentions.size(), glossary.entries.size(),
		show_shipped ? "on (English text only)" : "off");
	return broken ? 1 : 0;
}

}

int main(int argc, char** argv) {
	if (argc < 2) {
		return usage();
	}
	std::string command = argv[1];
	if (command == "check") {
		return check(argc, argv);
	}
	if (command == "run") {
		return run(argc, argv);
	}
	if (command == "lint") {
		return lint_command(argc, argv);
	}
	if (command == "time") {
		return time_command(argc, argv);
	}
	if (command == "strings") {
		return strings_command(argc, argv);
	}
	if (command == "names") {
		return names_command(argc, argv);
	}
	return usage();
}
