#include "rules/auto_policy.h"
#include "rules/battle.h"
#include "rules/loader.h"
#include "rules/names.h"
#include "rules/progression.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace rules;

namespace {

struct Options {
	std::uint64_t seed = 1;
	bool autoPlay = false;
	std::string dataDir = "data";
	std::string encounter = "training";
	std::string macro;
	int level = 1;
	int maxRounds = 50;
};

const char* usage =
	"usage: battle_cli [--seed N] [--auto] [--data DIR] [--encounter ID] [--level N] [--macro NAME] [--max-rounds N]\n"
	"  --auto plays the whole battle with the built-in policy (or the named macro every round) and prints the event log.\n";

bool parseOptions(int argc, char** argv, Options& options) {
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		const bool hasValue = i + 1 < argc;
		if (arg == "--auto") {
			options.autoPlay = true;
		} else if (arg == "--seed" && hasValue) {
			options.seed = std::strtoull(argv[++i], nullptr, 10);
		} else if (arg == "--data" && hasValue) {
			options.dataDir = argv[++i];
		} else if (arg == "--encounter" && hasValue) {
			options.encounter = argv[++i];
		} else if (arg == "--level" && hasValue) {
			options.level = std::atoi(argv[++i]);
		} else if (arg == "--macro" && hasValue) {
			options.macro = argv[++i];
		} else if (arg == "--max-rounds" && hasValue) {
			options.maxRounds = std::atoi(argv[++i]);
		} else {
			return false;
		}
	}
	return true;
}

std::string nameOf(const Battle& battle, int index) {
	if (index < 0 || index >= static_cast<int>(battle.combatants().size())) {
		return "?";
	}
	const Combatant& c = battle.combatant(index);
	if (battle.isParty(index)) {
		return c.name;
	}
	return c.name + " #" + std::to_string(index - battle.partyCount() + 1);
}

std::string actionName(const Battle& battle, const std::string& id) {
	if (const AbilityDef* ability = battle.database().ability(id)) {
		return ability->name;
	}
	if (const ItemDef* item = battle.database().item(id)) {
		return item->name;
	}
	return id;
}

std::string statusName(const Battle& battle, const std::string& id) {
	const StatusDef* status = battle.database().status(id);
	return status ? status->name : id;
}

const char* failText(FailReason reason) {
	switch (reason) {
	case FailReason::Silenced: return "is silenced";
	case FailReason::NotEnoughTp: return "lacks technique points";
	case FailReason::NoUsesLeft: return "has no uses left";
	case FailReason::NoItem: return "has none left";
	case FailReason::NoTarget: return "has no target";
	case FailReason::None: return "fails";
	}
	return "fails";
}

const char* outcomeText(Outcome outcome) {
	switch (outcome) {
	case Outcome::Victory: return "Victory";
	case Outcome::Defeat: return "Defeat";
	case Outcome::Fled: return "Fled";
	case Outcome::Ongoing: return "Ongoing";
	}
	return "?";
}

std::string describe(const Battle& battle, const Event& e) {
	std::ostringstream s;
	const std::string actor = nameOf(battle, e.actor);
	const std::string target = nameOf(battle, e.target);
	switch (e.type) {
	case EventType::RoundStarted:
		s << "-- Round " << e.round << " --";
		break;
	case EventType::ActionStarted:
		if (e.command == CommandKind::Defend) {
			s << actor << " defends.";
		} else {
			s << actor << " uses " << actionName(battle, e.ability);
			if (e.target >= 0) {
				s << " on " << target;
			}
			s << ".";
		}
		break;
	case EventType::ActionFailed:
		s << actor << " tries " << actionName(battle, e.ability) << " but " << failText(e.fail) << ".";
		break;
	case EventType::ActionPrevented:
		s << actor << " cannot act (" << statusName(battle, e.status) << ").";
		break;
	case EventType::ComboTriggered: {
		s << "Combo! ";
		for (std::size_t i = 0; i < e.participants.size(); ++i) {
			s << (i ? " + " : "") << nameOf(battle, e.participants[i]);
		}
		s << " unleash " << actionName(battle, e.ability) << (e.firstDiscovery ? " (new combo discovered)" : "") << ".";
		break;
	}
	case EventType::Missed:
		s << actor << " misses " << target << ".";
		break;
	case EventType::DamageDealt:
		s << target << " takes " << e.amount << " damage";
		if (e.element != Element::None) {
			s << " (" << toString(e.element) << ")";
		}
		if (e.effectiveness == Effectiveness::Weak) {
			s << ", a weak point";
		} else if (e.effectiveness == Effectiveness::Resisted) {
			s << ", resisted";
		} else if (e.effectiveness == Effectiveness::Immune) {
			s << ", immune";
		}
		s << ".";
		break;
	case EventType::Healed:
		s << target << " recovers " << e.amount << " HP.";
		break;
	case EventType::Revived:
		s << target << " is back on their feet with " << e.amount << " HP.";
		break;
	case EventType::StatusApplied:
		s << target << " is afflicted with " << statusName(battle, e.status) << ".";
		break;
	case EventType::StatusResisted:
		s << target << " shrugs off " << statusName(battle, e.status) << ".";
		break;
	case EventType::StatusCured:
		s << target << " is cured of " << statusName(battle, e.status) << ".";
		break;
	case EventType::StatusTicked:
		s << target << " loses " << e.amount << " HP to " << statusName(battle, e.status) << ".";
		break;
	case EventType::StatusExpired:
		s << target << (e.statusEnd == StatusEnd::WokeOnDamage ? " wakes from " : " is free of ") << statusName(battle, e.status) << ".";
		break;
	case EventType::StatModified:
		s << target << "'s " << toString(e.stat) << (e.amount > 0 ? " rises" : " falls") << " by " << (e.amount > 0 ? e.amount : -e.amount) << "%.";
		break;
	case EventType::StatModifierExpired:
		s << target << "'s " << toString(e.stat) << " returns to normal.";
		break;
	case EventType::CombatantDefeated:
		s << target << " is defeated.";
		break;
	case EventType::FleeAttempted:
		s << actor << (e.succeeded ? " leads the party away." : " tries to flee but is cut off.");
		break;
	case EventType::RoundEnded:
		s << "-- End of round " << e.round << " --";
		break;
	case EventType::BattleEnded:
		s << "Battle over: " << outcomeText(e.outcome) << ".";
		break;
	}
	return s.str();
}

void printState(const Battle& battle) {
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		const Combatant& c = battle.combatant(i);
		std::cout << "  [" << i << "] " << nameOf(battle, i) << "  HP " << c.hp << "/" << c.stats.maxHp;
		if (c.stats.maxTp > 0) {
			std::cout << "  TP " << c.tp << "/" << c.stats.maxTp;
		}
		for (const ActiveStatus& status : c.statuses) {
			std::cout << "  <" << statusName(battle, status.id) << ">";
		}
		std::cout << "\n";
	}
}

bool readLine(const std::string& prompt, std::string& out) {
	std::cout << prompt << std::flush;
	return static_cast<bool>(std::getline(std::cin, out));
}

int readTarget() {
	std::string line;
	if (!readLine("    target index (blank for default): ", line) || line.empty()) {
		return -1;
	}
	return std::atoi(line.c_str());
}

// Returns false when input ends or the player quits.
bool chooseCommands(Battle& battle) {
	std::string line;
	std::cout << "Macros:";
	for (const Macro& macro : battle.database().macros) {
		std::cout << " " << macro.name;
	}
	std::cout << "\n";
	if (!readLine("Enter a macro name, blank to choose commands, or q to quit: ", line) || line == "q") {
		return false;
	}
	if (!line.empty()) {
		if (const Macro* macro = battle.database().macro(line)) {
			for (const MacroStepResult& result : battle.applyMacro(*macro)) {
				if (result.status != MacroStepStatus::Applied) {
					std::cout << "  macro step for " << result.member << " adjusted\n";
				}
			}
			return true;
		}
		std::cout << "  no macro named '" << line << "', choose commands instead\n";
	}
	for (int i = 0; i < battle.partyCount(); ++i) {
		const Combatant& c = battle.combatant(i);
		if (!c.alive()) {
			continue;
		}
		while (true) {
			if (!readLine(c.name + ": [a]ttack [t]echnique/skill [i]tem [d]efend [f]lee > ", line)) {
				return false;
			}
			Command command;
			if (line == "a") {
				command = Command::attack(readTarget());
			} else if (line == "d") {
				command = Command::defend();
			} else if (line == "f") {
				command = Command::flee();
			} else if (line == "t") {
				for (std::size_t k = 0; k < c.abilities.size(); ++k) {
					const AbilityDef* ability = battle.database().ability(c.abilities[k]);
					std::cout << "    " << k << ") " << ability->name << " (" << toString(ability->kind);
					if (ability->kind == AbilityKind::Technique) {
						std::cout << ", " << ability->cost << " TP";
					} else if (ability->kind == AbilityKind::Skill) {
						const auto uses = c.skillUses.find(ability->id);
						std::cout << ", " << (uses == c.skillUses.end() ? 0 : uses->second) << " uses";
					}
					std::cout << ", " << toString(ability->targeting) << ")\n";
				}
				if (!readLine("    choice: ", line)) {
					return false;
				}
				const std::size_t pick = static_cast<std::size_t>(std::atoi(line.c_str()));
				if (pick >= c.abilities.size()) {
					continue;
				}
				command = Command::ability(c.abilities[pick], readTarget());
			} else if (line == "i") {
				std::vector<std::string> ids;
				for (const auto& [id, count] : battle.inventory()) {
					if (count > 0) {
						std::cout << "    " << ids.size() << ") " << actionName(battle, id) << " x" << count << "\n";
						ids.push_back(id);
					}
				}
				if (ids.empty() || !readLine("    choice: ", line)) {
					continue;
				}
				const std::size_t pick = static_cast<std::size_t>(std::atoi(line.c_str()));
				if (pick >= ids.size()) {
					continue;
				}
				command = Command::item(ids[pick], readTarget());
			} else {
				continue;
			}
			const std::string error = battle.setCommand(i, command);
			if (error.empty()) {
				break;
			}
			std::cout << "    " << error << "\n";
		}
	}
	return true;
}

}

int main(int argc, char** argv) {
	Options options;
	if (!parseOptions(argc, argv, options)) {
		std::cerr << usage;
		return 2;
	}
	Result<Database> loaded = loadDatabaseFromDirectory(options.dataDir);
	if (!loaded.ok()) {
		std::cerr << "data error:\n" << loaded.error << "\n";
		return 1;
	}
	const Database& db = *loaded.value;
	const Encounter* encounter = db.encounter(options.encounter);
	if (!encounter) {
		std::cerr << "unknown encounter '" << options.encounter << "'\n";
		return 2;
	}
	const Macro* autoMacro = nullptr;
	if (!options.macro.empty()) {
		autoMacro = db.macro(options.macro);
		if (!autoMacro) {
			std::cerr << "unknown macro '" << options.macro << "'\n";
			return 2;
		}
	}

	std::vector<PartyMember> party;
	for (const std::string& id : db.lineup) {
		party.push_back(makePartyMember(db, *db.character(id), options.level));
	}
	ComboLog comboLog;
	Result<Battle> created = Battle::create(db, party, encounter->enemies, db.startingInventory, options.seed, &comboLog);
	if (!created.ok()) {
		std::cerr << created.error << "\n";
		return 1;
	}
	Battle& battle = *created.value;

	std::cout << "Encounter: " << encounter->id << "  seed " << options.seed << "\n";
	while (battle.outcome() == Outcome::Ongoing && battle.round() < options.maxRounds) {
		if (options.autoPlay) {
			if (autoMacro) {
				battle.applyMacro(*autoMacro);
			} else {
				for (int i = 0; i < battle.partyCount(); ++i) {
					if (battle.combatant(i).alive()) {
						battle.setCommand(i, autoCommand(battle, i));
					}
				}
			}
		} else {
			printState(battle);
			if (!chooseCommands(battle)) {
				std::cout << "Quit.\n";
				return 0;
			}
		}
		for (const Event& event : battle.resolveRound()) {
			std::cout << describe(battle, event) << "\n";
		}
	}
	if (battle.outcome() == Outcome::Ongoing) {
		std::cout << "Round limit reached.\n";
		return 3;
	}

	printState(battle);
	std::cout << "Outcome: " << outcomeText(battle.outcome()) << " after " << battle.round() << " rounds\n";
	const Rewards rewards = battle.rewards();
	if (battle.outcome() == Outcome::Victory) {
		std::cout << "Rewards: " << rewards.experience << " experience, " << rewards.currency << " currency\n";
		for (PartyMember& member : battle.partyState()) {
			if (member.hp <= 0) {
				continue;
			}
			const CharacterDef& character = *db.character(member.characterId);
			const int gained = grantExperience(member, character, rewards.experience);
			if (gained > 0) {
				std::cout << "  " << character.name << " reaches level " << member.level << "\n";
			}
		}
	}
	std::cout << "Combos discovered:";
	for (const std::string& id : comboLog.discovered()) {
		std::cout << " " << id;
	}
	std::cout << (comboLog.discovered().empty() ? " none\n" : "\n");
	return 0;
}
