#include "support.h"

#include "rules/auto_policy.h"
#include "rules/formulas.h"
#include "rules/loader.h"
#include "rules/names.h"
#include "rules/progression.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>

using namespace rules;

namespace support {

const Database& data(const acc::Context& t) {
	static const Database db = [&t] {
		Result<Database> loaded = loadDatabaseFromDirectory(t.path("core/data"));
		if (!loaded.ok()) {
			std::printf("FAIL\tcannot load core/data: %s\n", loaded.error.c_str());
			std::exit(0);
		}
		return *loaded.value;
	}();
	return db;
}

std::map<std::string, std::string> dataTexts(const acc::Context& t) {
	std::map<std::string, std::string> files;
	for (const std::string& name : dataFileNames()) {
		files[name] = readFile(t.path("core/data/" + name));
	}
	return files;
}

BattleSetup setup(const Database& db, const std::vector<std::string>& enemies, std::uint64_t seed, int level) {
	BattleSetup s;
	for (const std::string& id : db.lineup) {
		s.party.push_back(makePartyMember(db, *db.character(id), level));
	}
	for (const std::string& id : enemies) {
		s.enemies.push_back({id, id});
	}
	s.inventory = db.startingInventory;
	s.seed = seed;
	return s;
}

Battle create(const Database& db, const BattleSetup& s, ComboLog* log) {
	Result<Battle> battle = Battle::create(db, s, log);
	if (!battle.ok()) {
		std::printf("FAIL\tcannot create battle: %s\n", battle.error.c_str());
		std::exit(0);
	}
	return *battle.value;
}

void othersDefend(Battle& battle) {
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		if (battle.combatant(i).alive() && battle.command(i).kind == CommandKind::None) {
			battle.setCommand(i, Command::defend());
		}
	}
}

void sturdy(Battle& battle, int index, int hp) {
	battle.combatant(index).stats.maxHp = hp;
	battle.combatant(index).hp = hp;
}

std::vector<Event> ofType(const std::vector<Event>& events, EventType type) {
	std::vector<Event> out;
	for (const Event& e : events) {
		if (e.type == type) {
			out.push_back(e);
		}
	}
	return out;
}

int countType(const std::vector<Event>& events, EventType type) {
	return static_cast<int>(ofType(events, type).size());
}

std::vector<int> actingOrder(const std::vector<Event>& events) {
	std::vector<int> order;
	for (const Event& e : events) {
		switch (e.type) {
		case EventType::ActionStarted:
		case EventType::ActionCancelled:
		case EventType::EscapeAttempted:
			order.push_back(e.actor);
			break;
		case EventType::ComboTriggered:
			order.insert(order.end(), e.participants.begin(), e.participants.end());
			break;
		default:
			break;
		}
	}
	return order;
}

std::vector<int> expectedFirstRoundOrder(const Battle& battle, std::uint64_t seed, int* ties) {
	Rng rng(seed);
	return expectedOrder(battle, rng, ties);
}

std::vector<int> expectedOrder(const Battle& battle, Rng& rng, int* ties) {
	struct Slot {
		int actor;
		int initiative;
	};
	std::vector<Slot> slots;
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		const Combatant& c = battle.combatant(i);
		if (!c.alive() || battle.command(i).kind == CommandKind::None) {
			continue;
		}
		slots.push_back({i, c.effective(Stat::Agility)});
	}
	int highest = 0;
	for (const Slot& slot : slots) {
		highest = std::max(highest, slot.initiative);
	}
	for (Slot& slot : slots) {
		slot.initiative = formulas::initiative(slot.initiative, rng.range(0, formulas::initiativeSpread(highest)));
	}
	std::sort(slots.begin(), slots.end(), [&battle](const Slot& a, const Slot& b) {
		if (a.initiative != b.initiative) {
			return a.initiative > b.initiative;
		}
		if (battle.isParty(a.actor) != battle.isParty(b.actor)) {
			return battle.isParty(a.actor);
		}
		return a.actor < b.actor;
	});
	std::vector<int> order;
	for (std::size_t i = 0; i < slots.size(); ++i) {
		order.push_back(slots[i].actor);
		if (ties && i > 0 && slots[i].initiative == slots[i - 1].initiative) {
			++*ties;
		}
	}
	return order;
}

bool CombatantState::operator==(const CombatantState& o) const {
	return hp == o.hp && tp == o.tp && skillUses == o.skillUses && statuses == o.statuses && modifiers == o.modifiers;
}

bool BattleState::operator==(const BattleState& o) const {
	return combatants == o.combatants && inventory == o.inventory && outcome == o.outcome;
}

BattleState capture(const Battle& battle) {
	BattleState state;
	for (const Combatant& c : battle.combatants()) {
		CombatantState s;
		s.hp = c.hp;
		s.tp = c.tp;
		s.skillUses = c.skillUses;
		for (const ActiveStatus& status : c.statuses) {
			s.statuses.insert(status.id);
		}
		for (const ActiveModifier& modifier : c.modifiers) {
			s.modifiers[modifier.stat] = modifier.percent;
		}
		state.combatants.push_back(s);
	}
	state.inventory = battle.inventory();
	state.outcome = battle.outcome();
	return state;
}

std::string apply(BattleState& state, const Event& e, int partyCount) {
	const int count = static_cast<int>(state.combatants.size());
	auto at = [&](int index) -> CombatantState* {
		return index >= 0 && index < count ? &state.combatants[index] : nullptr;
	};
	CombatantState* target = at(e.target);
	CombatantState* actor = at(e.actor);
	const std::string type = toString(e.type);
	// BAT-115: Damaged, Healed, ticks and revives carry the resulting hit points.
	auto resulting = [&](const CombatantState& c) -> std::string {
		if (e.hp != c.hp) {
			return type + " says the target has " + std::to_string(e.hp) + " hp, applying the stream gives " + std::to_string(c.hp);
		}
		return "";
	};
	switch (e.type) {
	case EventType::BattleStarted:
	case EventType::RoundStarted:
	case EventType::CommandsLocked:
	case EventType::OrderResolved:
	case EventType::Retargeted:
	case EventType::ActionStarted:
	case EventType::ActionCancelled:
	case EventType::ComboTriggered:
	case EventType::ComboDiscovered:
	case EventType::ItemHadNoEffect:
	case EventType::Hit:
	case EventType::Missed:
	case EventType::Critical:
	case EventType::StatusResisted:
	case EventType::Message:
	case EventType::RoundEnded:
		return "";
	case EventType::ResourceSpent:
		if (!actor) {
			return type + " without an actor";
		}
		if (e.resource == ResourceKind::Tp) {
			actor->tp -= e.amount;
		} else if (e.resource == ResourceKind::SkillUse) {
			actor->skillUses[e.ability] -= e.amount;
		} else if (e.resource == ResourceKind::Item) {
			if (e.actor >= partyCount) {
				return "an enemy spent an item";
			}
			state.inventory[e.ability] -= e.amount;
		} else {
			return type + " with no resource";
		}
		return "";
	case EventType::DamageDealt:
	case EventType::StatusTicked:
		if (!target) {
			return type + " without a target";
		}
		target->hp -= e.amount;
		return resulting(*target);
	case EventType::Healed:
		if (!target) {
			return type + " without a target";
		}
		target->hp += e.amount;
		return resulting(*target);
	case EventType::Revived:
		if (!target) {
			return type + " without a target";
		}
		target->hp = e.amount;
		target->statuses.erase(kDefeatedStatus);
		return resulting(*target);
	case EventType::StatusApplied:
		if (!target) {
			return type + " without a target";
		}
		target->statuses.insert(e.status);
		return "";
	case EventType::StatusRemoved:
		if (!target || !target->statuses.erase(e.status)) {
			return type + " removes a status the target does not have";
		}
		return "";
	case EventType::StatModified:
		if (!target) {
			return type + " without a target";
		}
		target->modifiers[e.stat] = e.amount;
		return "";
	case EventType::StatModifierExpired:
		if (!target || !target->modifiers.erase(e.stat)) {
			return type + " removes a modifier the target does not have";
		}
		return "";
	case EventType::CombatantDefeated:
		if (!target) {
			return type + " without a target";
		}
		target->hp = 0;
		target->statuses.insert(kDefeatedStatus);
		if (target->statuses.size() != 1) {
			return type + " clears a status that got no StatusRemoved event";
		}
		target->modifiers.clear();
		return "";
	case EventType::EscapeAttempted:
		if (e.succeeded) {
			state.outcome = Outcome::Escaped;
		}
		return "";
	case EventType::BattleEnded:
		state.outcome = e.outcome;
		return "";
	}
	return "unknown event type";
}

std::string difference(const BattleState& a, const BattleState& b) {
	if (a.combatants.size() != b.combatants.size()) {
		return "combatant count differs";
	}
	for (std::size_t i = 0; i < a.combatants.size(); ++i) {
		const CombatantState& x = a.combatants[i];
		const CombatantState& y = b.combatants[i];
		if (x.hp != y.hp) {
			return "combatant " + std::to_string(i) + " hp " + std::to_string(x.hp) + " vs " + std::to_string(y.hp);
		}
		if (x.tp != y.tp) {
			return "combatant " + std::to_string(i) + " tp " + std::to_string(x.tp) + " vs " + std::to_string(y.tp);
		}
		if (x.skillUses != y.skillUses) {
			return "combatant " + std::to_string(i) + " skill uses differ";
		}
		if (x.statuses != y.statuses) {
			return "combatant " + std::to_string(i) + " statuses differ";
		}
		if (x.modifiers != y.modifiers) {
			return "combatant " + std::to_string(i) + " stat modifiers differ";
		}
	}
	if (a.inventory != b.inventory) {
		return "inventory differs";
	}
	if (a.outcome != b.outcome) {
		return std::string("outcome ") + toString(a.outcome) + " vs " + toString(b.outcome);
	}
	return "";
}

PlayedBattle playRandomBattle(const Database& db, std::uint64_t seed, ComboLog& log, int maxRounds) {
	Rng pick(seed * 7919 + 17);
	std::vector<std::string> encounters;
	for (const auto& [id, encounter] : db.encounters) {
		encounters.push_back(id);
	}
	const std::vector<std::string> difficulties = {"", "original", "hard"};
	std::vector<std::string> carried;
	for (const auto& [id, status] : db.statuses) {
		if (status.persistsOutsideBattle) {
			carried.push_back(id);
		}
	}

	PlayedBattle played;
	played.setup = setup(db, {}, seed, pick.range(1, 12));
	useEncounter(played.setup, db, encounters[pick.range(0, static_cast<int>(encounters.size()) - 1)]);
	played.setup.difficulty = difficulties[pick.range(0, 2)];
	played.setup.opening = static_cast<Opening>(pick.range(0, 2));
	played.setup.rewardMultiplier = static_cast<RewardMultiplier>(pick.range(0, 2));
	played.setup.flags.defeatHasBranch = pick.chance(50);
	if (pick.chance(20)) {
		played.setup.scriptedEnd.kind = ScriptedEndKind::SurviveRounds;
		played.setup.scriptedEnd.rounds = pick.range(1, 4);
	} else if (pick.chance(15)) {
		played.setup.scriptedEnd.kind = ScriptedEndKind::EnemyDefeated;
		played.setup.scriptedEnd.enemySlot = pick.range(0, static_cast<int>(played.setup.enemies.size()) - 1);
	}
	for (PartyMember& member : played.setup.party) {
		if (!carried.empty() && pick.chance(10)) {
			member.statuses.push_back({carried[pick.range(0, static_cast<int>(carried.size()) - 1)], pick.range(0, 3)});
		}
	}

	Battle battle = create(db, played.setup, &log);
	played.start = capture(battle);
	std::vector<std::string> items;
	for (const auto& [id, item] : db.items) {
		items.push_back(id);
	}
	if (!db.macros.empty() && pick.chance(15)) {
		battle.startAuto(db.macros[pick.range(0, static_cast<int>(db.macros.size()) - 1)]);
	}
	const int total = static_cast<int>(battle.combatants().size());
	while (battle.outcome() == Outcome::Ongoing && battle.round() < maxRounds) {
		battle.setPlayTime(static_cast<std::int64_t>(battle.round()) * 4000 + 1000);
		if (battle.autoActive() && battle.fillFromAuto()) {
			if (pick.chance(5)) {
				battle.cancelAuto();
			}
		} else if (battle.sideActsNextRound(Side::Party)) {
			if (!db.macros.empty() && pick.chance(15)) {
				battle.applyMacro(db.macros[pick.range(0, static_cast<int>(db.macros.size()) - 1)]);
			} else {
				for (int i = 0; i < battle.partyCount(); ++i) {
					if (!battle.combatant(i).alive()) {
						continue;
					}
					const int roll = pick.range(1, 100);
					if (roll <= 70) {
						battle.setCommand(i, autoCommand(battle, i));
					} else if (roll <= 80) {
						battle.setCommand(i, Command::defend());
					} else if (roll <= 90) {
						battle.setCommand(i, Command::item(items[pick.range(0, static_cast<int>(items.size()) - 1)], pick.range(0, total - 1)));
					} else if (roll <= 93) {
						battle.setCommand(i, Command::escape());
					} else {
						battle.setCommand(i, Command::attack(pick.range(0, total - 1)));
					}
				}
			}
		}
		const std::vector<Event> events = battle.resolveRound();
		played.events.insert(played.events.end(), events.begin(), events.end());
	}
	played.record = battle.record();
	played.end = capture(battle);
	played.rewards = battle.rewards();
	played.gameOver = battle.gameOver();
	return played;
}

std::string readFile(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	std::ostringstream text;
	text << in.rdbuf();
	return text.str();
}

std::vector<std::string> listFiles(const std::string& directory, const std::string& suffix) {
	std::vector<std::string> files;
	if (DIR* dir = opendir(directory.c_str())) {
		while (dirent* entry = readdir(dir)) {
			const std::string name = entry->d_name;
			if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
				files.push_back(directory + "/" + name);
			}
		}
		closedir(dir);
	}
	std::sort(files.begin(), files.end());
	return files;
}

}
