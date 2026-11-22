#pragma once

#include "rules/combo.h"
#include "rules/command.h"
#include "rules/result.h"
#include "rules/setup.h"
#include "rules/version.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rules {

// The commands the host set before resolving a round. Commands the battle filled in are not recorded,
// because the battle fills them the same way on replay.
struct RoundInput {
	std::vector<RecordedCommand> commands;
	std::vector<int> macroBlock;
	std::string macroName;
	std::int64_t playTimeMs = 0;
	// Party combatants that did nothing by choice (Battle::passing); written only when there are any.
	std::vector<int> passed;
};

struct BattleRecord {
	int rulesVersion = kRulesVersion;
	int eventSchemaVersion = kEventSchemaVersion;
	std::uint64_t contentHash = 0;
	BattleSetup setup;
	std::vector<ComboLogEntry> knownCombos;
	std::vector<RoundInput> rounds;
};

// The text ends with a checksum line over everything before it.
std::string serializeRecord(const BattleRecord& record);
// Refuses a record whose checksum does not match, so a damaged record is never replayed.
Result<BattleRecord> parseRecord(const std::string& text);

}
