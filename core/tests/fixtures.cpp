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
	for (const std::string& name : dataFileNames()) {
		std::ifstream in("data/" + name);
		std::ostringstream text;
		text << in.rdbuf();
		files[name] = text.str();
	}
	return files;
}

BattleSetup makeSetup(const Database& db, const std::vector<std::string>& enemies, std::uint64_t seed) {
	BattleSetup setup;
	for (const std::string& id : db.lineup) {
		setup.party.push_back(makePartyMember(db, *db.character(id), 1));
	}
	for (const std::string& id : enemies) {
		setup.enemies.push_back({id, id});
	}
	setup.inventory = db.startingInventory;
	setup.seed = seed;
	return setup;
}

Battle makeBattle(const Database& db, const BattleSetup& setup, ComboLog* log) {
	Result<Battle> battle = Battle::create(db, setup, log);
	if (!battle.ok()) {
		std::printf("cannot create battle: %s\n", battle.error.c_str());
		std::exit(1);
	}
	return *battle.value;
}

Battle makeBattle(const Database& db, const std::vector<std::string>& enemies, std::uint64_t seed, const Inventory& inventory) {
	BattleSetup setup = makeSetup(db, enemies, seed);
	setup.inventory = inventory;
	return makeBattle(db, setup, nullptr);
}

Battle makeBattle(const Database& db, const std::vector<std::string>& enemies, std::uint64_t seed, ComboLog* log) {
	return makeBattle(db, makeSetup(db, enemies, seed), log);
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

std::vector<Event> playRound(Battle& battle) {
	for (int i = 0; i < battle.partyCount(); ++i) {
		if (battle.combatant(i).alive() && battle.sideActsNextRound(Side::Party)) {
			battle.setCommand(i, autoCommand(battle, i));
		}
	}
	return battle.resolveRound();
}

std::vector<Event> playToEnd(Battle& battle, int maxRounds) {
	std::vector<Event> all;
	while (battle.outcome() == Outcome::Ongoing && battle.round() < maxRounds) {
		const std::vector<Event> events = playRound(battle);
		all.insert(all.end(), events.begin(), events.end());
	}
	return all;
}

}
