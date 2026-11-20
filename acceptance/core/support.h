#pragma once

#include "acc.h"

#include "rules/battle.h"
#include "rules/database.h"
#include "rules/rng.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace support {

// core/data, loaded once. Exits with a FAIL line when it cannot be loaded.
const rules::Database& data(const acc::Context& t);
std::map<std::string, std::string> dataTexts(const acc::Context& t);

// The data lineup at `level` in starting equipment against the given enemies, each in its own group.
rules::BattleSetup setup(const rules::Database& db, const std::vector<std::string>& enemies, std::uint64_t seed, int level = 1);
rules::Battle create(const rules::Database& db, const rules::BattleSetup& setup, rules::ComboLog* log = nullptr);

// Every living combatant that still has no command defends, so only the commands a test sets do anything.
void othersDefend(rules::Battle& battle);
// Raises a combatant's hit points so a test round cannot defeat it.
void sturdy(rules::Battle& battle, int index, int hp = 9999);

std::vector<rules::Event> ofType(const std::vector<rules::Event>& events, rules::EventType type);
int countType(const std::vector<rules::Event>& events, rules::EventType type);
// Actors of ActionStarted, ActionCancelled, EscapeAttempted and ComboTriggered events, in stream order.
std::vector<int> actingOrder(const std::vector<rules::Event>& events);

// The round's execution order recomputed from the seed with core's placeholder initiative (agility plus a roll
// drawn in combatant index order) and BAT-030's ordering: descending, ties to the party side, then lower index. Valid for the first
// round of a battle whose every command the host set, so no AI draw comes first.
std::vector<int> expectedFirstRoundOrder(const rules::Battle& battle, std::uint64_t seed, int* ties = nullptr);
// The same, continuing from a generator whose earlier draws the caller has already replayed.
std::vector<int> expectedOrder(const rules::Battle& battle, rules::Rng& rng, int* ties = nullptr);

// The battle state an event stream must reproduce (BAT-111).
struct CombatantState {
	int hp = 0;
	int tp = 0;
	std::map<std::string, int> skillUses;
	std::set<std::string> statuses;
	std::map<rules::Stat, int> modifiers;

	bool operator==(const CombatantState& other) const;
};

struct BattleState {
	std::vector<CombatantState> combatants;
	rules::Inventory inventory;
	rules::Outcome outcome = rules::Outcome::Ongoing;

	bool operator==(const BattleState& other) const;
};

BattleState capture(const rules::Battle& battle);
// Applies one event to the state. Returns an empty string, or why the event cannot apply. `db` says which statuses
// survive defeat.
std::string apply(const rules::Database& db, BattleState& state, const rules::Event& event, int partyCount);
std::string difference(const BattleState& a, const BattleState& b);

// A random but reproducible battle: encounter, difficulty, opening, multiplier, flags, scripted end and carried
// statuses picked from `seed`, played to the end with a mix of auto commands, macros, auto-battle, items,
// defends and escapes.
struct PlayedBattle {
	rules::BattleSetup setup;
	BattleState start;
	std::vector<rules::Event> events;
	rules::BattleRecord record;
	BattleState end;
	rules::Rewards rewards;
	bool gameOver = false;
};

PlayedBattle playRandomBattle(const rules::Database& db, std::uint64_t seed, rules::ComboLog& log, int maxRounds = 40);

std::string readFile(const std::string& path);
std::vector<std::string> listFiles(const std::string& directory, const std::string& suffix);

}
