#include "support.h"

#include "events/compiler.h"
#include "events/interpreter.h"
#include "events/recording_host.h"
#include "events/registry.h"
#include "events/save_state.h"
#include "rules/combo.h"
#include "rules/macro.h"
#include "rules/record.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Exercises the save payloads the libraries offer (a battle record, macros, the combo log and an interpreter
// SaveState) for the ACC-SAV tools checks. Prints "key value..." lines.

namespace {

std::string read(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

std::string fnvHex(const std::string& bytes) {
	std::uint64_t hash = 14695981039346656037ULL;
	for (unsigned char c : bytes) {
		hash ^= c;
		hash *= 1099511628211ULL;
	}
	char out[17];
	std::snprintf(out, sizeof out, "%016llx", static_cast<unsigned long long>(hash));
	return out;
}

// Interpreter saves at every step of every entry event of every sample script, compiled against the registry.
std::vector<std::string> eventSaves(const std::string& root, int stride) {
	std::vector<events::Source> sources;
	for (const std::string& file : support::listFiles(root + "/events/samples", ".ev")) {
		sources.push_back({file.substr(file.rfind('/') + 1), read(file)});
	}
	events::Registry registry;
	std::vector<events::Diagnostic> diagnostics;
	events::load_registry("registry.txt", read(root + "/events/sample_text/registry.txt"), registry, diagnostics);
	events::CompileOptions options;
	options.registry = &registry;
	const events::Program program = events::compile(sources, options).program;
	std::vector<std::string> saves;
	for (const events::EventInfo& event : program.events) {
		for (int k = 1; k < 400; k += stride) {
			events::RecordingHost host;
			host.set_line_duration(0.3);
			host.set_duration(events::ActionType::move, 0.4);
			host.set_battle_duration(0.5);
			events::Interpreter it(program, host);
			it.start(event.name);
			for (int i = 0; i < k && it.status() == events::Status::running; ++i) {
				host.advance(0.1);
				it.step(0.1);
			}
			if (it.status() != events::Status::running) {
				break;
			}
			saves.push_back(events::save_state_to_text(it.save()));
		}
	}
	return saves;
}

std::vector<std::string> records(const std::string& root, int count) {
	const acc::Context context(root);
	const rules::Database& db = support::data(context);
	rules::ComboLog log;
	std::vector<std::string> out;
	for (int seed = 1; seed <= count; ++seed) {
		out.push_back(rules::serializeRecord(support::playRandomBattle(db, static_cast<std::uint64_t>(seed), log).record));
	}
	return out;
}

bool eventLoads(const std::string& text, std::string& rewritten) {
	events::SaveState state;
	if (!events::save_state_from_text(text, state)) {
		return false;
	}
	rewritten = events::save_state_to_text(state);
	return true;
}

bool recordLoads(const std::string& text, std::string& rewritten) {
	rules::Result<rules::BattleRecord> parsed = rules::parseRecord(text);
	if (!parsed.ok()) {
		return false;
	}
	rewritten = rules::serializeRecord(*parsed.value);
	return true;
}

template <class Load>
void roundTrip(const char* name, const std::vector<std::string>& texts, Load load) {
	int identical = 0;
	for (const std::string& text : texts) {
		std::string rewritten;
		identical += load(text, rewritten) && rewritten == text ? 1 : 0;
	}
	std::printf("%s_roundtrip %d %zu\n", name, identical, texts.size());
}

// Every truncation, and every byte flipped with each mask: how many load, and how many of those load as
// something other than the original (damage the format did not detect).
template <class Load>
void corrupt(const char* name, const std::vector<std::string>& texts, const std::vector<unsigned char>& masks, Load load) {
	long truncated = 0;
	long truncatedLoaded = 0;
	long truncatedDifferent = 0;
	long flipped = 0;
	long flippedLoaded = 0;
	long flippedDifferent = 0;
	for (const std::string& text : texts) {
		for (std::size_t length = 0; length < text.size(); ++length) {
			std::string rewritten;
			++truncated;
			if (load(text.substr(0, length), rewritten)) {
				++truncatedLoaded;
				truncatedDifferent += rewritten != text ? 1 : 0;
			}
		}
		for (std::size_t at = 0; at < text.size(); ++at) {
			for (unsigned char mask : masks) {
				std::string damaged = text;
				damaged[at] = static_cast<char>(static_cast<unsigned char>(damaged[at]) ^ mask);
				std::string rewritten;
				++flipped;
				if (load(damaged, rewritten)) {
					++flippedLoaded;
					flippedDifferent += rewritten != text ? 1 : 0;
				}
			}
		}
	}
	std::printf("%s_truncated %ld %ld %ld\n%s_flipped %ld %ld %ld\n", name, truncatedLoaded, truncatedDifferent, truncated, name, flippedLoaded, flippedDifferent, flipped);
}

// The events save with its header version replaced and its checksum recomputed, so only the version differs.
std::string eventsWithVersion(const std::string& save, int version) {
	const std::size_t mark = save.rfind(" sum ");
	std::string body = save.substr(0, mark);
	body = "events-save " + std::to_string(version) + body.substr(body.find(' ', std::string("events-save ").size()));
	return body + " sum " + fnvHex(body);
}

// A battle record with its body edited and the checksum line recomputed.
std::string recordWithBody(const std::string& body) {
	return body + "checksum " + fnvHex(body) + "\n";
}

std::string recordBody(const std::string& record) {
	return record.substr(0, record.rfind("checksum "));
}

}

int main(int argc, char** argv) {
	if (argc != 3) {
		std::fprintf(stderr, "usage: %s roundtrip|versions|corrupt REPO_ROOT\n", argv[0]);
		return 2;
	}
	const std::string mode = argv[1];
	const std::string root = argv[2];
	if (mode == "roundtrip") {
		roundTrip("events_save", eventSaves(root, 1), eventLoads);
		roundTrip("battle_record", records(root, 200), recordLoads);
		const acc::Context context(root);
		const rules::Database& db = support::data(context);
		rules::Result<std::vector<rules::Macro>> macros = rules::parseMacros(rules::serializeMacros(db.macros));
		std::printf("macros_roundtrip %d 1\n", macros.ok() && rules::serializeMacros(*macros.value) == rules::serializeMacros(db.macros) ? 1 : 0);
		rules::ComboLog log;
		for (int seed = 1; seed <= 60; ++seed) {
			support::playRandomBattle(db, static_cast<std::uint64_t>(seed), log);
		}
		rules::Result<rules::ComboLog> combos = rules::parseComboLog(rules::serializeComboLog(log));
		std::printf("combo_log_roundtrip %d 1 %zu\n", combos.ok() && rules::serializeComboLog(*combos.value) == rules::serializeComboLog(log) ? 1 : 0, log.entries().size());
	} else if (mode == "versions") {
		const std::string save = eventSaves(root, 9).at(3);
		std::string rewritten;
		int version = 0;
		std::sscanf(save.c_str(), "events-save %d", &version);
		std::printf("events_save_version %d\n", version);
		std::printf("events_save_resealed_loads %d\n", eventLoads(eventsWithVersion(save, version), rewritten) ? 1 : 0);
		int older = 0;
		for (int v = 1; v < version; ++v) {
			older += eventLoads(eventsWithVersion(save, v), rewritten) ? 1 : 0;
		}
		std::printf("events_save_older_loads %d %d\n", older, version - 1);
		std::printf("events_save_newer_loads %d\n", eventLoads(eventsWithVersion(save, version + 1), rewritten) ? 1 : 0);

		const std::string record = records(root, 1).front();
		const std::string body = recordBody(record);
		const std::size_t space = body.find(' ');
		const std::size_t end = body.find('\n');
		const int format = std::stoi(body.substr(space + 1, end - space - 1));
		std::printf("battle_record_format %d\n", format);
		std::printf("battle_record_resealed_loads %d\n", recordLoads(recordWithBody(body), rewritten) ? 1 : 0);
		int recordOlder = 0;
		for (int v = 1; v < format; ++v) {
			recordOlder += recordLoads(recordWithBody(body.substr(0, space + 1) + std::to_string(v) + body.substr(end)), rewritten) ? 1 : 0;
		}
		std::printf("battle_record_older_loads %d %d\n", recordOlder, format - 1);
		std::printf("battle_record_newer_loads %d\n", recordLoads(recordWithBody(body.substr(0, space + 1) + std::to_string(format + 1) + body.substr(end)), rewritten) ? 1 : 0);
		const std::size_t endLine = body.rfind("end\n");
		const std::string withChunk = recordWithBody(body.substr(0, endLine) + "acc_future_chunk 1 placeholder\n" + body.substr(endLine));
		const bool loads = recordLoads(withChunk, rewritten);
		std::printf("battle_record_unknown_line %d %d\n", loads ? 1 : 0, loads && rewritten.find("acc_future_chunk") != std::string::npos ? 1 : 0);
	} else if (mode == "corrupt") {
		const std::vector<unsigned char> masks = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0xff};
		std::vector<std::string> saves = eventSaves(root, 5);
		corrupt("events_save", saves, masks, eventLoads);
		corrupt("battle_record", records(root, 12), masks, recordLoads);
		std::printf("events_save_samples %zu\n", saves.size());
	} else {
		return 2;
	}
	return 0;
}
