#include "fixtures.h"

#include "rules/auto_policy.h"
#include "rules/loader.h"
#include "rules/progression.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace rules;

namespace fixtures {

const Database& data() {
	static const Database db = [] {
		Result<Database> loaded = loadDatabaseFromDirectory("data");
		if (!loaded.ok()) {
			std::printf("cannot load data/: %s\n", loaded.error.c_str());
			std::exit(1);
		}
		return *loaded.value;
	}();
	return db;
}

std::map<std::string, std::string> dataTexts() {
	std::map<std::string, std::string> files;
	for (const char* name : {"statuses.json", "abilities.json", "items.json", "party.json", "enemies.json", "combos.json"}) {
		std::ifstream in(std::string("data/") + name);
		std::ostringstream text;
		text << in.rdbuf();
		files[name] = text.str();
	}
	return files;
}

namespace {

Battle create(const Database& db, const std::vector<std::string>& enemies, std::uint64_t seed, const Inventory& inventory, ComboLog* log) {
	std::vector<PartyMember> party;
	for (const std::string& id : db.lineup) {
		party.push_back(makePartyMember(db, *db.character(id), 1));
	}
	Result<Battle> battle = Battle::create(db, party, enemies, inventory, seed, log);
	if (!battle.ok()) {
		std::printf("cannot create battle: %s\n", battle.error.c_str());
		std::exit(1);
	}
	return *battle.value;
}

}

Battle makeBattle(const Database& db, const std::vector<std::string>& enemies, std::uint64_t seed, const Inventory& inventory) {
	return create(db, enemies, seed, inventory, nullptr);
}

Battle makeBattle(const Database& db, const std::vector<std::string>& enemies, std::uint64_t seed, ComboLog* log) {
	return create(db, enemies, seed, db.startingInventory, log);
}

void othersDefend(Battle& battle) {
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		if (battle.combatant(i).alive() && battle.command(i).kind == CommandKind::None) {
			battle.setCommand(i, Command::defend());
		}
	}
}

void partyDefends(Battle& battle) {
	for (int i = 0; i < battle.partyCount(); ++i) {
		if (battle.combatant(i).alive() && battle.command(i).kind == CommandKind::None) {
			battle.setCommand(i, Command::defend());
		}
	}
}

void addStatus(Battle& battle, int index, const std::string& status, int remaining) {
	battle.combatant(index).statuses.push_back({status, remaining});
}

std::vector<Event> ofType(const std::vector<Event>& events, EventType type) {
	std::vector<Event> result;
	for (const Event& e : events) {
		if (e.type == type) {
			result.push_back(e);
		}
	}
	return result;
}

std::optional<Event> firstBy(const std::vector<Event>& events, EventType type, int actor) {
	for (const Event& e : events) {
		if (e.type == type && e.actor == actor) {
			return e;
		}
	}
	return std::nullopt;
}

std::string serialize(const std::vector<Event>& events) {
	std::ostringstream s;
	for (const Event& e : events) {
		s << static_cast<int>(e.type) << ' ' << e.round << ' ' << e.actor << ' ' << e.target << ' ' << e.ability << ' '
			<< e.status << ' ' << e.amount << ' ' << static_cast<int>(e.effectiveness) << ' ' << e.succeeded << '\n';
	}
	return s.str();
}

std::vector<Event> playToEnd(Battle& battle, int maxRounds) {
	std::vector<Event> all;
	while (battle.outcome() == Outcome::Ongoing && battle.round() < maxRounds) {
		for (int i = 0; i < battle.partyCount(); ++i) {
			if (battle.combatant(i).alive()) {
				battle.setCommand(i, autoCommand(battle, i));
			}
		}
		const std::vector<Event> events = battle.resolveRound();
		all.insert(all.end(), events.begin(), events.end());
	}
	return all;
}

}
