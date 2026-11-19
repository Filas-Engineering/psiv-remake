#pragma once

#include "rules/command.h"
#include "rules/result.h"
#include "rules/setup.h"
#include "rules/version.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rules {

struct RecordedCommand {
	int actor = -1;
	Command command;
};

// The commands the host set before resolving a round. Commands the battle filled in are not recorded,
// because the battle fills them the same way on replay.
struct RoundInput {
	std::vector<RecordedCommand> commands;
	std::vector<int> macroBlock;
};

struct BattleRecord {
	int rulesVersion = kRulesVersion;
	int eventSchemaVersion = kEventSchemaVersion;
	std::uint64_t contentHash = 0;
	BattleSetup setup;
	std::vector<std::string> knownCombos;
	std::vector<RoundInput> rounds;
};

std::string serializeRecord(const BattleRecord& record);
Result<BattleRecord> parseRecord(const std::string& text);

}
