#include "rules/auto_policy.h"
#include "rules/battle.h"
#include "rules/loader.h"
#include "rules/names.h"
#include "rules/progression.h"
#include "rules/record.h"
#include "rules/setup.h"
#include "rules/text.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace rules;

namespace {

struct Options {
	std::uint64_t seed = 1;
	bool autoPlay = false;
	bool help = false;
	std::string dataDir = "data";
	std::string encounter = "training";
	std::string difficulty = "original";
	std::string macro;
	std::string recordPath;
	std::string replayPath;
	Opening opening = Opening::Normal;
	RewardMultiplier reward = RewardMultiplier::X1;
	int level = 1;
	int maxRounds = 50;
};

const char* usage =
	"usage: battle_cli [options]\n"
	"  --seed N             battle seed (default 1)\n"
	"  --auto               play the whole battle with the built-in policy and print the event log\n"
	"  --macro NAME         with --auto, auto-battle with this macro; when it stops the built-in policy takes over\n"
	"  --encounter ID       enemy group from enemies.json (default training)\n"
	"  --difficulty ID      difficulty from difficulties.json (default original)\n"
	"  --reward 1|1.5|2     reward multiplier (default 1)\n"
	"  --opening KIND       normal, party_first or enemy_first (default normal)\n"
	"  --level N            party level (default 1)\n"
	"  --max-rounds N       stop after N rounds (default 50)\n"
	"  --data DIR           data directory (default data)\n"
	"  --record FILE        write the battle record to FILE when the battle stops\n"
	"  --replay FILE        replay a battle record instead of playing; prints the same log\n"
	"  --help               show this help\n"
	"Exit codes: 0 battle ended or quit, 1 data or record error, 2 bad arguments, 3 round limit reached.\n";

bool parseOptions(int argc, char** argv, Options& options) {
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		const bool hasValue = i + 1 < argc;
		if (arg == "--help" || arg == "-h") {
			options.help = true;
		} else if (arg == "--auto") {
			options.autoPlay = true;
		} else if (arg == "--seed" && hasValue) {
			options.seed = std::strtoull(argv[++i], nullptr, 10);
		} else if (arg == "--data" && hasValue) {
			options.dataDir = argv[++i];
		} else if (arg == "--encounter" && hasValue) {
			options.encounter = argv[++i];
		} else if (arg == "--difficulty" && hasValue) {
			options.difficulty = argv[++i];
		} else if (arg == "--reward" && hasValue) {
			if (!parseRewardMultiplier(argv[++i], options.reward)) {
				return false;
			}
		} else if (arg == "--opening" && hasValue) {
			if (!parseOpening(argv[++i], options.opening)) {
				return false;
			}
		} else if (arg == "--level" && hasValue) {
			options.level = std::atoi(argv[++i]);
		} else if (arg == "--macro" && hasValue) {
			options.macro = argv[++i];
		} else if (arg == "--max-rounds" && hasValue) {
			options.maxRounds = std::atoi(argv[++i]);
		} else if (arg == "--record" && hasValue) {
			options.recordPath = argv[++i];
		} else if (arg == "--replay" && hasValue) {
			options.replayPath = argv[++i];
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

// Message parameters are ids or numbers; ids print as their display names.
std::string paramName(const Database& db, const std::string& param) {
	if (const AbilityDef* ability = db.ability(param)) {
		return ability->name;
	}
	if (const ItemDef* item = db.item(param)) {
		return item->name;
	}
	if (const StatusDef* status = db.status(param)) {
		return status->name;
	}
	if (const CharacterDef* character = db.character(param)) {
		return character->name;
	}
	if (const EnemyDef* enemy = db.enemy(param)) {
		return enemy->name;
	}
	if (const EquipmentDef* piece = db.equipmentPiece(param)) {
		return piece->name;
	}
	return param;
}

std::string messageText(const TextTable& text, const Database& db, const Message& message) {
	std::vector<std::string> params;
	for (const std::string& param : message.params) {
		params.push_back(paramName(db, param));
	}
	return formatText(text, message.key, params);
}

const char* outcomeText(Outcome outcome) {
	switch (outcome) {
	case Outcome::Victory: return "Victory";
	case Outcome::Defeat: return "Defeat";
	case Outcome::Escaped: return "Escaped";
	case Outcome::ScriptedEnd: return "Scripted end";
	case Outcome::Ongoing: return "Ongoing";
	}
	return "?";
}

const char* resourceText(ResourceKind resource) {
	switch (resource) {
	case ResourceKind::Tp: return "TP";
	case ResourceKind::SkillUse: return "use";
	case ResourceKind::Item: return "item";
	case ResourceKind::None: return "";
	}
	return "";
}

// An empty line means the event is not printed.
std::string describe(const Battle& battle, const TextTable& text, const Event& e) {
	std::ostringstream s;
	const std::string actor = nameOf(battle, e.actor);
	const std::string target = nameOf(battle, e.target);
	switch (e.type) {
	case EventType::BattleStarted:
	case EventType::CommandsLocked:
	case EventType::OrderResolved:
	case EventType::Hit:
		break;
	case EventType::RoundStarted:
		s << "-- Round " << e.round << " --";
		break;
	case EventType::Retargeted:
		s << actor << " turns from " << nameOf(battle, e.previousTarget) << " to " << target << ".";
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
	case EventType::ActionCancelled: {
		const std::string cause = e.cancel == CancelReason::ActorUnable ? statusName(battle, e.status) : actionName(battle, e.ability);
		s << formatText(text, std::string("cancel.") + toString(e.cancel), {actor, cause});
		break;
	}
	case EventType::ComboTriggered: {
		s << "Combo! ";
		for (std::size_t i = 0; i < e.participants.size(); ++i) {
			s << (i ? " + " : "") << nameOf(battle, e.participants[i]);
		}
		s << " unleash " << actionName(battle, e.ability) << ".";
		break;
	}
	case EventType::ComboDiscovered:
		s << "New combination discovered: " << e.combo << ".";
		break;
	case EventType::ResourceSpent:
		s << "  (" << actor << " spends " << e.amount << " " << resourceText(e.resource) << ")";
		break;
	case EventType::ItemHadNoEffect:
		s << actionName(battle, e.ability) << " has no effect and is kept.";
		break;
	case EventType::Missed:
		s << actor << " misses " << target << ".";
		break;
	case EventType::Critical:
		s << "A critical hit on " << target << "!";
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
		s << target << " recovers " << e.amount << (e.resource == ResourceKind::Tp ? " TP." : " HP.");
		break;
	case EventType::Revived:
		s << target << " is back on their feet with " << e.amount << " HP.";
		break;
	case EventType::StatusApplied:
	case EventType::StatusResisted:
	case EventType::StatusRemoved:
		s << formatText(text, e.message, {target, statusName(battle, e.status)});
		break;
	case EventType::StatModified:
		s << target << "'s " << toString(e.stat) << " is now " << (e.amount < 0 ? "" : "+") << e.amount << " from its base.";
		break;
	case EventType::CombatantDefeated:
		s << target << " is defeated.";
		break;
	case EventType::EscapeAttempted:
		s << actor << (e.succeeded ? " leads the party away." : " tries to escape but is cut off.");
		break;
	case EventType::Message:
		s << messageText(text, battle.database(), {e.message, e.params});
		break;
	case EventType::RoundEnded:
		s << "-- End of round " << e.round << " --";
		break;
	case EventType::BattleEnded:
		s << "Battle over: " << outcomeText(e.outcome) << ".";
		break;
	case EventType::EnemyJoined:
		s << target << " joins the fight.";
		break;
	case EventType::EnemyLeft:
		s << target << " merges into " << actor << ".";
		break;
	case EventType::EnemyTransformed:
		s << "The foe takes a new form: " << target << ".";
		break;
	}
	return s.str();
}

void printEvents(const Battle& battle, const TextTable& text, const std::vector<Event>& events) {
	for (const Event& event : events) {
		const std::string line = describe(battle, text, event);
		if (!line.empty()) {
			std::cout << line << "\n";
		}
	}
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

const Macro* findMacro(const Battle& battle, const std::vector<Macro>& saved, const std::string& name) {
	for (const Macro& macro : saved) {
		if (macro.name == name) {
			return &macro;
		}
	}
	return battle.database().macro(name);
}

// Returns false when input ends or the player quits.
bool chooseCommands(Battle& battle, const TextTable& text, std::vector<Macro>& saved) {
	std::string line;
	while (true) {
		std::cout << "Macros:";
		for (const Macro& macro : battle.database().macros) {
			std::cout << " " << macro.name;
		}
		for (const Macro& macro : saved) {
			std::cout << " " << macro.name;
		}
		std::cout << "\n";
		if (!readLine("Enter a macro name, 'save NAME' to keep last round as a macro, blank to choose commands, or q to quit: ", line) || line == "q") {
			return false;
		}
		if (line.compare(0, 5, "save ") != 0 || line.size() <= 5) {
			break;
		}
		if (battle.round() == 0) {
			std::cout << "  no round to save yet\n";
			continue;
		}
		saved.push_back(battle.lastRoundAsMacro(line.substr(5)));
		std::cout << "  saved macro '" << saved.back().name << "' with " << saved.back().steps.size() << " steps\n";
	}
	if (!line.empty()) {
		if (const Macro* macro = findMacro(battle, saved, line)) {
			for (const MacroStepResult& result : battle.applyMacro(*macro)) {
				if (result.status != MacroStepStatus::Applied) {
					std::cout << "  macro step for " << result.member << " adjusted\n";
				}
			}
			return true;
		}
		std::cout << "  no macro named '" << line << "', choose commands instead\n";
	}
	if (!battle.sideActsNextRound(Side::Party)) {
		std::cout << "  The party is caught off guard and cannot act this round.\n";
		return readLine("Press enter to continue, or q to quit: ", line) && line != "q";
	}
	for (int i = 0; i < battle.partyCount(); ++i) {
		const Combatant& c = battle.combatant(i);
		if (!c.alive()) {
			continue;
		}
		while (true) {
			const std::string escapeLabel = battle.escapeAllowed() ? "[e]scape" : "escape (not allowed here)";
			if (!readLine(c.name + ": [a]ttack [t]echnique/skill [i]tem [d]efend " + escapeLabel + " > ", line)) {
				return false;
			}
			Command command;
			if (line == "a") {
				command = Command::attack(readTarget());
			} else if (line == "d") {
				command = Command::defend();
			} else if (line == "e") {
				command = Command::escape();
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
			const Message refused = battle.setCommand(i, command);
			if (refused.empty()) {
				break;
			}
			std::cout << "    " << messageText(text, battle.database(), refused) << "\n";
		}
	}
	return true;
}

bool writeRecord(const Options& options, const Battle& battle) {
	if (options.recordPath.empty()) {
		return true;
	}
	std::ofstream out(options.recordPath, std::ios::binary);
	out << serializeRecord(battle.record());
	if (!out) {
		std::cerr << "cannot write record to " << options.recordPath << "\n";
		return false;
	}
	std::cerr << "Record written to " << options.recordPath << "\n";
	return true;
}

// Both a played battle and a replayed one end here, so their output is identical.
int printResult(const Database& db, const Battle& battle, const ComboLog& comboLog) {
	if (battle.outcome() == Outcome::Ongoing) {
		std::cout << "Round limit reached.\n";
		return 3;
	}
	printState(battle);
	std::cout << "Outcome: " << outcomeText(battle.outcome()) << " after " << battle.round() << " rounds\n";
	const Rewards rewards = battle.rewards();
	if (battle.outcome() == Outcome::Victory) {
		std::cout << "Rewards: " << rewards.experience << " experience, " << rewards.currency << " currency\n";
		for (const auto& [id, count] : rewards.items) {
			const ItemDef* item = db.item(id);
			std::cout << "  Drop: " << (item ? item->name : id) << " x" << count << "\n";
		}
		std::vector<PartyMember> party = battle.partyState();
		for (std::size_t i = 0; i < party.size(); ++i) {
			const LevelUp levelUp = grantExperience(party[i], db, rewards.memberExperience[i]);
			if (levelUp.levelsGained == 0) {
				continue;
			}
			std::cout << "  " << db.character(party[i].characterId)->name << " reaches level " << party[i].level;
			for (const std::string& id : levelUp.learned) {
				std::cout << ", learns " << actionName(battle, id);
			}
			std::cout << "\n";
		}
	}
	std::cout << "Combos discovered:";
	for (const std::string& id : comboLog.discovered()) {
		std::cout << " " << id;
	}
	std::cout << (comboLog.discovered().empty() ? " none\n" : "\n");
	return 0;
}

void printHeader(const Battle& battle) {
	const BattleSetup& setup = battle.setup();
	std::cout << "Encounter: " << setup.enemyGroup << "  seed " << setup.seed << "  difficulty "
		<< (setup.difficulty.empty() ? "none" : setup.difficulty) << "  opening " << toString(setup.opening)
		<< "  reward " << toString(setup.rewardMultiplier) << "\n";
}

int replay(const Options& options, const Database& db, const TextTable& text) {
	std::ifstream in(options.replayPath, std::ios::binary);
	if (!in) {
		std::cerr << "cannot open " << options.replayPath << "\n";
		return 1;
	}
	std::ostringstream contents;
	contents << in.rdbuf();
	const Result<BattleRecord> record = parseRecord(contents.str());
	if (!record.ok()) {
		std::cerr << options.replayPath << ": " << record.error << "\n";
		return 1;
	}
	ComboLog comboLog;
	std::vector<Event> events;
	Result<Battle> replayed = replayBattle(db, *record.value, comboLog, events);
	if (!replayed.ok()) {
		std::cerr << options.replayPath << ": " << replayed.error << "\n";
		return 1;
	}
	printHeader(*replayed.value);
	printEvents(*replayed.value, text, events);
	return printResult(db, *replayed.value, comboLog);
}

}

int main(int argc, char** argv) {
	Options options;
	if (!parseOptions(argc, argv, options)) {
		std::cerr << usage;
		return 2;
	}
	if (options.help) {
		std::cout << usage;
		return 0;
	}
	Result<Database> loaded = loadDatabaseFromDirectory(options.dataDir);
	if (!loaded.ok()) {
		std::cerr << "data error:\n" << loaded.error << "\n";
		return 1;
	}
	const Database& db = *loaded.value;
	const Result<TextTable> text = loadTextTable(options.dataDir + "/text_en.json");
	if (!text.ok()) {
		std::cerr << "data error:\n" << text.error << "\n";
		return 1;
	}
	if (!options.replayPath.empty()) {
		return replay(options, db, *text.value);
	}

	BattleSetup setup;
	const std::string encounterError = useEncounter(setup, db, options.encounter);
	if (!encounterError.empty()) {
		std::cerr << encounterError << "\n";
		return 2;
	}
	if (!db.difficulty(options.difficulty)) {
		std::cerr << "unknown difficulty '" << options.difficulty << "'\n";
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
	for (const std::string& id : db.lineup) {
		setup.party.push_back(makePartyMember(db, *db.character(id), options.level));
	}
	setup.inventory = db.startingInventory;
	setup.opening = options.opening;
	setup.difficulty = options.difficulty;
	setup.rewardMultiplier = options.reward;
	setup.seed = options.seed;

	ComboLog comboLog;
	Result<Battle> created = Battle::create(db, setup, &comboLog);
	if (!created.ok()) {
		std::cerr << created.error << "\n";
		return 1;
	}
	Battle& battle = *created.value;

	printHeader(battle);
	if (autoMacro) {
		battle.startAuto(*autoMacro);
	}
	std::vector<Macro> savedMacros;
	while (battle.outcome() == Outcome::Ongoing && battle.round() < options.maxRounds) {
		if (options.autoPlay) {
			const bool wasAuto = battle.autoActive();
			if (!battle.fillFromAuto()) {
				// The stop notice goes to stderr so a replay, which has no auto-battle, prints the same log.
				if (wasAuto) {
					std::cerr << formatText(*text.value, std::string("auto.stopped.") + toString(battle.autoStop()), {}) << "\n";
				}
				for (int i = 0; i < battle.partyCount(); ++i) {
					if (battle.combatant(i).alive() && battle.sideActsNextRound(Side::Party)) {
						battle.setCommand(i, autoCommand(battle, i));
					}
				}
			}
		} else {
			printState(battle);
			if (!chooseCommands(battle, *text.value, savedMacros)) {
				std::cout << "Quit.\n";
				return writeRecord(options, battle) ? 0 : 1;
			}
		}
		printEvents(battle, *text.value, battle.resolveRound());
	}
	if (!writeRecord(options, battle)) {
		return 1;
	}
	return printResult(db, battle, comboLog);
}
