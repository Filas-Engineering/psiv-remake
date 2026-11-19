#include "support.h"

#include "events/compiler.h"
#include "events/interpreter.h"
#include "events/recording_host.h"
#include "events/save_state.h"
#include "rules/record.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Exercises the two save payloads the libraries offer (a battle record and an interpreter SaveState) for
// the ACC-SAV tools checks. Prints "key value" lines.

namespace {

std::string read(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

std::vector<std::string> eventSaves(const std::string& root) {
	std::vector<events::Source> sources;
	for (const char* name : {"harbor.ev", "lighthouse.ev", "market.ev"}) {
		sources.push_back({name, read(root + "/events/samples/" + name)});
	}
	const events::Program program = events::compile(sources).program;
	std::vector<std::string> saves;
	for (const char* event : {"harbor_arrival", "lighthouse_ambush", "market_haggle"}) {
		for (int k = 1; k < 200; k += 7) {
			events::RecordingHost host;
			host.set_line_duration(0.3);
			host.set_duration(events::ActionType::move, 0.4);
			events::Interpreter it(program, host);
			it.start(event);
			for (int i = 0; i < k && it.status() == events::Status::running; ++i) {
				host.advance(0.1);
				it.step(0.1);
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

// Truncations at every length and single bit flips at every byte: how many load, and how many of those load
// as something other than the original (damage the format cannot detect).
template <class Load>
void corrupt(const char* name, const std::vector<std::string>& texts, Load load) {
	long truncatedLoaded = 0;
	long truncated = 0;
	long flippedLoaded = 0;
	long flippedUndetected = 0;
	long flipped = 0;
	for (std::size_t n = 0; n < texts.size() && n < 6; ++n) {
		const std::string& text = texts[n];
		for (std::size_t length = 0; length < text.size(); ++length) {
			std::string rewritten;
			++truncated;
			truncatedLoaded += load(text.substr(0, length), rewritten) ? 1 : 0;
		}
		for (std::size_t at = 0; at < text.size(); ++at) {
			std::string damaged = text;
			damaged[at] = static_cast<char>(damaged[at] ^ 0x04);
			std::string rewritten;
			++flipped;
			if (load(damaged, rewritten)) {
				++flippedLoaded;
				flippedUndetected += rewritten != text ? 1 : 0;
			}
		}
	}
	std::printf("%s_truncated %ld %ld\n%s_flipped %ld %ld %ld\n", name, truncatedLoaded, truncated, name, flippedLoaded, flippedUndetected, flipped);
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
		roundTrip("events_save", eventSaves(root), eventLoads);
		roundTrip("battle_record", records(root, 50), recordLoads);
	} else if (mode == "versions") {
		const std::string save = eventSaves(root).at(3);
		std::string rewritten;
		const std::string header = "events-save 2";
		const bool current = save.rfind(header, 0) == 0;
		std::printf("events_save_current_header %d\n", current ? 1 : 0);
		std::printf("events_save_v1_loads %d\n", eventLoads("events-save 1" + save.substr(header.size()), rewritten) ? 1 : 0);
		std::printf("events_save_v3_loads %d\n", eventLoads("events-save 3" + save.substr(header.size()), rewritten) ? 1 : 0);
		std::string record = records(root, 1).front();
		const std::size_t space = record.find(' ');
		const std::size_t end = record.find('\n');
		const std::string bumped = record.substr(0, space + 1) + std::to_string(std::stoi(record.substr(space + 1, end - space - 1)) + 1) + record.substr(end);
		std::printf("battle_record_newer_format_loads %d\n", recordLoads(bumped, rewritten) ? 1 : 0);
		std::printf("battle_record_unknown_line_kept %d\n", recordLoads(record + "acc_future_chunk 1\n", rewritten) && rewritten.find("acc_future_chunk") != std::string::npos ? 1 : 0);
	} else if (mode == "corrupt") {
		corrupt("events_save", eventSaves(root), eventLoads);
		corrupt("battle_record", records(root, 6), recordLoads);
	} else {
		return 2;
	}
	return 0;
}
