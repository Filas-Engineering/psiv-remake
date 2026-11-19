#include "rules/battle.h"

#include "rules/progression.h"

#include <algorithm>

namespace rules {

namespace {

Combatant fromEnemy(const Database& db, const EnemyDef& def) {
	Combatant c;
	c.defId = def.id;
	c.name = def.name;
	c.side = Side::Enemy;
	c.stats = def.stats;
	c.hp = def.stats.maxHp;
	c.tp = def.stats.maxTp;
	c.basicAttack = def.basicAttack;
	c.abilities = def.abilities;
	c.affinities = def.affinities;
	for (const std::string& id : def.abilities) {
		const AbilityDef* ability = db.ability(id);
		if (ability && ability->kind == AbilityKind::Skill) {
			c.skillUses[id] = ability->uses;
		}
	}
	return c;
}

}

Result<Battle> Battle::create(
	const Database& db,
	const std::vector<PartyMember>& party,
	const std::vector<std::string>& enemyIds,
	const Inventory& inventory,
	std::uint64_t seed,
	ComboLog* comboLog) {
	if (party.empty() || party.size() > static_cast<std::size_t>(kMaxPartySize)) {
		return Result<Battle>::failure("a battle needs 1 to " + std::to_string(kMaxPartySize) + " party members");
	}
	if (enemyIds.empty() || enemyIds.size() > static_cast<std::size_t>(kMaxEnemies)) {
		return Result<Battle>::failure("a battle needs 1 to " + std::to_string(kMaxEnemies) + " enemies");
	}
	Battle battle;
	battle.db_ = &db;
	battle.rng_ = Rng(seed);
	battle.comboLog_ = comboLog;
	battle.partySource_ = party;
	battle.inventory_ = inventory;
	battle.partyCount_ = static_cast<int>(party.size());

	for (const PartyMember& member : party) {
		const CharacterDef* def = db.character(member.characterId);
		if (!def) {
			return Result<Battle>::failure("unknown character '" + member.characterId + "'");
		}
		Combatant c;
		c.defId = def->id;
		c.name = def->name;
		c.side = Side::Party;
		c.stats = statsAtLevel(*def, member.level);
		c.hp = std::clamp(member.hp, 0, c.stats.maxHp);
		c.tp = std::clamp(member.tp, 0, c.stats.maxTp);
		c.basicAttack = def->basicAttack;
		c.abilities = def->abilities;
		c.skillUses = member.skillUses;
		c.affinities = def->affinities;
		if (!c.alive()) {
			c.statuses.push_back({kDefeatedStatus, 0});
		}
		battle.combatants_.push_back(c);
	}
	for (const std::string& id : enemyIds) {
		const EnemyDef* def = db.enemy(id);
		if (!def) {
			return Result<Battle>::failure("unknown enemy '" + id + "'");
		}
		battle.combatants_.push_back(fromEnemy(db, *def));
	}
	battle.commands_.resize(battle.combatants_.size());

	for (const ComboRecipe& recipe : db.combos) {
		battle.combosLongestFirst_.push_back(&recipe);
	}
	std::stable_sort(battle.combosLongestFirst_.begin(), battle.combosLongestFirst_.end(), [](const ComboRecipe* a, const ComboRecipe* b) {
		return a->abilities.size() > b->abilities.size();
	});

	battle.updateOutcome();
	return Result<Battle>::success(std::move(battle));
}

std::string Battle::setCommand(int actor, const Command& command) {
	if (actor < 0 || actor >= static_cast<int>(combatants_.size())) {
		return "no combatant at index " + std::to_string(actor);
	}
	const Combatant& c = combatants_[actor];
	if (!c.alive()) {
		return c.name + " is defeated";
	}
	if (command.target >= static_cast<int>(combatants_.size())) {
		return "no combatant at index " + std::to_string(command.target);
	}
	switch (command.kind) {
	case CommandKind::Ability: {
		const AbilityDef* ability = db_->ability(command.id);
		if (!ability || !c.knows(command.id)) {
			return c.name + " does not know '" + command.id + "'";
		}
		if (ability->kind == AbilityKind::Combo) {
			return "combo abilities cannot be chosen directly";
		}
		break;
	}
	case CommandKind::Item:
		if (!db_->item(command.id)) {
			return "unknown item '" + command.id + "'";
		}
		break;
	case CommandKind::Flee:
		if (!isParty(actor)) {
			return "enemies cannot flee";
		}
		break;
	case CommandKind::None:
	case CommandKind::Attack:
	case CommandKind::Defend:
		break;
	}
	commands_[actor] = command;
	return "";
}

void Battle::fillMissingCommands() {
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		if (!combatants_[i].alive() || commands_[i].kind != CommandKind::None) {
			continue;
		}
		commands_[i] = isParty(i) ? Command::attack(-1) : chooseEnemyCommand(i);
	}
}

std::vector<Event> Battle::resolveRound() {
	std::vector<Event> out;
	if (outcome_ != Outcome::Ongoing) {
		return out;
	}
	++round_;
	emit(out, EventType::RoundStarted);
	fillMissingCommands();
	for (std::size_t i = 0; i < combatants_.size(); ++i) {
		combatants_[i].defending = combatants_[i].alive() && commands_[i].kind == CommandKind::Defend;
	}

	const std::vector<int> order = computeTurnOrder();
	lastTurnOrder_ = order;
	for (std::size_t i = 0; i < order.size() && outcome_ == Outcome::Ongoing;) {
		const int consumed = tryCombo(order, i, out);
		if (consumed > 0) {
			i += static_cast<std::size_t>(consumed);
		} else {
			takeTurn(order[i], out);
			++i;
		}
		updateOutcome();
	}
	if (outcome_ == Outcome::Ongoing) {
		endOfRound(out);
	}
	emit(out, EventType::RoundEnded);
	if (outcome_ != Outcome::Ongoing) {
		emit(out, EventType::BattleEnded).outcome = outcome_;
	}

	for (Combatant& c : combatants_) {
		c.defending = false;
	}
	std::fill(commands_.begin(), commands_.end(), Command());
	forcedOrder_.clear();
	return out;
}

void Battle::updateOutcome() {
	if (outcome_ != Outcome::Ongoing) {
		return;
	}
	bool partyAlive = false;
	bool enemiesAlive = false;
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		if (combatants_[i].alive()) {
			(isParty(i) ? partyAlive : enemiesAlive) = true;
		}
	}
	if (!partyAlive) {
		outcome_ = Outcome::Defeat;
	} else if (!enemiesAlive) {
		outcome_ = Outcome::Victory;
	}
}

Event& Battle::emit(std::vector<Event>& out, EventType type) {
	Event event;
	event.type = type;
	event.round = round_;
	out.push_back(event);
	return out.back();
}

Rewards Battle::rewards() const {
	Rewards rewards;
	if (outcome_ != Outcome::Victory) {
		return rewards;
	}
	for (int i = partyCount_; i < static_cast<int>(combatants_.size()); ++i) {
		if (const EnemyDef* def = db_->enemy(combatants_[i].defId)) {
			rewards.experience += def->experience;
			rewards.currency += def->currency;
		}
	}
	return rewards;
}

std::vector<PartyMember> Battle::partyState() const {
	std::vector<PartyMember> party = partySource_;
	for (int i = 0; i < partyCount_; ++i) {
		party[i].hp = combatants_[i].hp;
		party[i].tp = combatants_[i].tp;
		party[i].skillUses = combatants_[i].skillUses;
	}
	return party;
}

}
