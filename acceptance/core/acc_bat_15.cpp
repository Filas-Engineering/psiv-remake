#include "support.h"

#include "rules/names.h"
#include "rules/record.h"

#include <cstdio>
#include <regex>

using namespace rules;

namespace {

std::string capture(const std::string& command) {
	std::string output;
	if (FILE* pipe = popen(command.c_str(), "r")) {
		char buffer[4096];
		std::size_t n;
		while ((n = fread(buffer, 1, sizeof buffer, pipe)) > 0) {
			output.append(buffer, n);
		}
		pclose(pipe);
	}
	return output;
}

}

ACC_CHECK(acc_bat_15, "ACC-BAT-15", "BAT-130, BAT-131, BAT-132") {
	const Database& db = support::data(t);

	// Records round-trip through text and replay to byte-identical streams.
	ComboLog log;
	int replayed = 0;
	int repeated = 0;
	for (std::uint64_t seed = 1; seed <= 1000; ++seed) {
		ComboLog before = log;
		const support::PlayedBattle played = support::playRandomBattle(db, seed, log);
		const std::string recordText = serializeRecord(played.record);
		// The same setup, seed and commands, played again from the same combo log, give the same bytes.
		const support::PlayedBattle again = support::playRandomBattle(db, seed, before);
		repeated += t.expect(serializeEvents(again.events) == serializeEvents(played.events) && serializeRecord(again.record) == recordText,
			"battle " + std::to_string(seed) + ": playing the same setup, seed and commands twice gave different streams") ? 1 : 0;
		Result<BattleRecord> parsed = parseRecord(recordText);
		if (!t.expect(parsed.ok(), "battle " + std::to_string(seed) + ": its record does not parse: " + parsed.error)) {
			continue;
		}
		t.expect(serializeRecord(*parsed.value) == recordText, "battle " + std::to_string(seed) + ": record text changes on a parse and write round trip");
		ComboLog replayLog;
		std::vector<Event> events;
		Result<Battle> battle = replayBattle(db, *parsed.value, replayLog, events);
		if (!t.expect(battle.ok(), "battle " + std::to_string(seed) + ": replay refused: " + battle.error)) {
			continue;
		}
		t.expect(serializeEvents(events) == serializeEvents(played.events), "battle " + std::to_string(seed) + ": the replayed stream differs from the recorded run");
		++replayed;
	}

	// A record from other data or another rules version is refused rather than replayed differently.
	{
		const support::PlayedBattle played = support::playRandomBattle(db, 1, log);
		BattleRecord other = played.record;
		other.contentHash ^= 1;
		ComboLog l;
		std::vector<Event> events;
		t.expect(!replayBattle(db, other, l, events).ok(), "a record made with other data was replayed");
		other = played.record;
		other.rulesVersion += 1;
		t.expect(!replayBattle(db, other, l, events).ok(), "a record from another rules version was replayed");
	}

	// The same battles from three builds of the core sources: the core Makefile's -O1 library, and -O0 and -O3 copies.
	const std::string build = t.path("acceptance/build");
	const std::string reference = capture("'" + build + "/record_dump_core' '" + t.root() + "' 300");
	t.expect(reference.size() > 1000, "record_dump_core produced no output");
	for (const char* variant : {"record_dump_O0", "record_dump_O3"}) {
		const std::string other = capture("'" + build + "/" + variant + "' '" + t.root() + "' 300");
		t.expect(other == reference, std::string(variant) + " produced a different serialized stream from the core library build");
	}

	// No floating-point type in rules code. The JSON reader parses numbers as double before the loader
	// checks they are whole, so it is reported separately.
	const std::regex floating(R"(\b(float|double|long double)\b)");
	std::string loaderUse;
	for (const std::string& dir : {t.path("core/src"), t.path("core/include/rules")}) {
		for (const std::string& suffix : {std::string(".cpp"), std::string(".h")}) {
			for (const std::string& file : support::listFiles(dir, suffix)) {
				const std::string name = file.substr(file.rfind('/') + 1);
				const bool reader = name.rfind("json", 0) == 0 || name.rfind("loader", 0) == 0;
				if (std::regex_search(support::readFile(file), floating)) {
					if (reader) {
						loaderUse += (loaderUse.empty() ? "" : ", ") + name;
					} else {
						t.expect(false, name + " uses a floating-point type in rules code");
					}
				}
			}
		}
	}

	t.note(std::to_string(repeated) + " random setups (encounter, level, difficulty, opening, multiplier, flags, scripted end, carried statuses, macros and auto-battle) played twice give byte-identical streams and records");
	t.note(std::to_string(replayed) + " records round-trip and replay byte-identically; records from other data or rules versions are refused; 300 battles are byte-identical across -O0, -O1 and -O3 builds");
	t.note("no floating-point type in rules code (the JSON number reader in " + loaderUse + " uses double only while loading data)");
	t.note("platforms covered: this host only (Apple clang, one architecture)");
}
