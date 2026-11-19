#pragma once

#include "rules/battle.h"
#include "rules/database.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace fixtures {

// The shipped placeholder data in core/data. Tests run from core/.
const rules::Database& data();
std::map<std::string, std::string> dataTexts();

rules::Battle makeBattle(const rules::Database& db, const std::vector<std::string>& enemies, std::uint64_t seed, rules::ComboLog* log = nullptr);
rules::Battle makeBattle(const rules::Database& db, const std::vector<std::string>& enemies, std::uint64_t seed, const rules::Inventory& inventory);

// Every living combatant without a command defends, so only the commands a test sets do anything.
void othersDefend(rules::Battle& battle);
// Party members without a command defend; enemies still ask their AI.
void partyDefends(rules::Battle& battle);
void addStatus(rules::Battle& battle, int index, const std::string& status, int remaining);

std::vector<rules::Event> ofType(const std::vector<rules::Event>& events, rules::EventType type);
std::optional<rules::Event> firstBy(const std::vector<rules::Event>& events, rules::EventType type, int actor);
std::string serialize(const std::vector<rules::Event>& events);
std::vector<rules::Event> playToEnd(rules::Battle& battle, int maxRounds);

// Party indexes follow the lineup in data/party.json.
constexpr int kFighter = 0;
constexpr int kMystic = 1;
constexpr int kScout = 2;
constexpr int kMedic = 3;
constexpr int kEngineer = 4;
constexpr int kFirstEnemy = 5;

}
