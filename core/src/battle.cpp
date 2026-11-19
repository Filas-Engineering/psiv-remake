#include "rules/battle.h"

#include "rules/progression.h"

#include <algorithm>

namespace rules {

namespace {

constexpr const char* kPartyGroup = "party";

Combatant fromEnemy(const Database& db, const EnemyDef& def, const std::string& group) {
	Combatant c;
	c.defId = def.id;
	c.name = def.name;
	c.side = Side::Enemy;
	c.group = group.empty() ? def.id : group;
	c.stats = def.stats;
	c.hp = def.stats.maxHp;
	c.tp = def.stats.maxTp;
	c.basicAttack = def.basicAttack;
	c.abilities = def.abilities;
	c.affinities = def.affinities;
	c.statusResist = def.statusResist;
	for (const std::string& id : def.abilities) {
		const AbilityDef* ability = db.ability(id);
		if (ability && ability->kind == AbilityKind::Skill) {
			c.skillUses[id] = ability->uses;
		}
	}
	return c;
}

std::string checkEquipment(const Database& db, const PartyMember& member) {
	for (const auto& [slot, id] : member.equipment) {
		const std::string error = canEquip(db, member.characterId, id);
		if (!error.empty()) {
			return error;
		}
		if (db.equipmentPiece(id)->slot != slot) {
			return "'" + id + "' does not go in that slot";
		}
	}
	return "";
}

Combatant fromMember(const Database& db, const CharacterDef& def, const PartyMember& member) {
	Combatant c;
	c.defId = def.id;
	c.name = def.name;
	c.side = Side::Party;
	c.group = kPartyGroup;
	c.stats = memberStats(db, member);
	c.hp = std::clamp(member.hp, 0, c.stats.maxHp);
	c.tp = std::clamp(member.tp, 0, c.stats.maxTp);
	c.basicAttack = def.basicAttack;
	c.abilities = knownAbilities(db, member);
	c.skillUses = member.skillUses;
	c.affinities = def.affinities;
	c.statusResist = def.statusResist;
	for (const auto& [slot, id] : member.equipment) {
		const EquipmentDef& piece = *db.equipmentPiece(id);
		if (!piece.attack.empty()) {
			c.basicAttack = piece.attack;
		}
		if (piece.element != Element::None) {
			c.attackElement = piece.element;
		}
		for (const auto& [status, percent] : piece.statusResist) {
			c.statusResist[status] = std::min(100, c.resistance(status) + percent);
		}
	}
	if (!c.alive()) {
		c.statuses.push_back({kDefeatedStatus, 0});
	}
	return c;
}

}

Result<Battle> Battle::create(const Database& db, const BattleSetup& setup, ComboLog* comboLog) {
	if (setup.party.empty() || setup.party.size() > static_cast<std::size_t>(kMaxPartySize)) {
		return Result<Battle>::failure("a battle needs 1 to " + std::to_string(kMaxPartySize) + " party members");
	}
	if (setup.enemies.empty() || setup.enemies.size() > static_cast<std::size_t>(kMaxEnemies)) {
		return Result<Battle>::failure("a battle needs 1 to " + std::to_string(kMaxEnemies) + " enemies");
	}
	const DifficultyDef* difficulty = nullptr;
	if (!setup.difficulty.empty()) {
		difficulty = db.difficulty(setup.difficulty);
		if (!difficulty) {
			return Result<Battle>::failure("unknown difficulty '" + setup.difficulty + "'");
		}
	}

	Battle battle;
	battle.db_ = std::make_shared<const Database>(difficulty ? applyDifficulty(db, *difficulty) : db);
	const Database& rulesDb = *battle.db_;
	battle.rng_ = Rng(setup.seed);
	battle.comboLog_ = comboLog;
	battle.record_.contentHash = db.contentHash;
	battle.record_.setup = setup;
	if (comboLog) {
		battle.record_.knownCombos.assign(comboLog->discovered().begin(), comboLog->discovered().end());
	}
	battle.inventory_ = setup.inventory;
	battle.partyCount_ = static_cast<int>(setup.party.size());
	battle.rewardPercent_ = difficulty ? difficulty->rewardPercent : 100;

	for (const PartyMember& member : setup.party) {
		const CharacterDef* def = rulesDb.character(member.characterId);
		if (!def) {
			return Result<Battle>::failure("unknown character '" + member.characterId + "'");
		}
		const std::string equipmentError = checkEquipment(rulesDb, member);
		if (!equipmentError.empty()) {
			return Result<Battle>::failure(equipmentError);
		}
		battle.combatants_.push_back(fromMember(rulesDb, *def, member));
	}
	for (const FormationSlot& slot : setup.enemies) {
		const EnemyDef* def = rulesDb.enemy(slot.enemy);
		if (!def) {
			return Result<Battle>::failure("unknown enemy '" + slot.enemy + "'");
		}
		battle.combatants_.push_back(fromEnemy(rulesDb, *def, slot.group));
	}
	battle.commands_.resize(battle.combatants_.size());
	battle.lastCommands_.resize(battle.combatants_.size());

	for (const ComboRecipe& recipe : rulesDb.combos) {
		battle.combosLongestFirst_.push_back(&recipe);
	}
	std::stable_sort(battle.combosLongestFirst_.begin(), battle.combosLongestFirst_.end(), [](const ComboRecipe* a, const ComboRecipe* b) {
		return a->abilities.size() > b->abilities.size();
	});

	battle.updateOutcome();
	return Result<Battle>::success(std::move(battle));
}

bool Battle::surprised(Side side, int round) const {
	if (round > db_->battleRules.surpriseRounds) {
		return false;
	}
	const Opening opening = record_.setup.opening;
	return (opening == Opening::PartyFirst && side == Side::Enemy) || (opening == Opening::EnemyFirst && side == Side::Party);
}

bool Battle::sideActsNextRound(Side side) const {
	return !surprised(side, round_ + 1);
}

std::string Battle::setCommand(int actor, const Command& command) {
	if (actor < 0 || actor >= static_cast<int>(combatants_.size())) {
		return "no combatant at index " + std::to_string(actor);
	}
	const Combatant& c = combatants_[actor];
	if (!c.alive()) {
		return c.name + " is defeated";
	}
	if (!sideActsNextRound(c.side)) {
		return c.name + " is caught off guard this round";
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
	case CommandKind::Escape:
		if (!isParty(actor)) {
			return "enemies cannot escape";
		}
		if (!escapeAllowed()) {
			return "escape is not allowed in this battle";
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
		if (!combatants_[i].alive() || commands_[i].kind != CommandKind::None || surprised(combatants_[i].side, round_)) {
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
	RoundInput input;
	for (int i = 0; i < static_cast<int>(commands_.size()); ++i) {
		if (commands_[i].kind != CommandKind::None) {
			input.commands.push_back({i, commands_[i]});
		}
	}
	input.macroBlock = forcedOrder_;
	record_.rounds.push_back(input);

	++round_;
	emit(out, EventType::RoundStarted);
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		if (surprised(combatants_[i].side, round_)) {
			commands_[i] = Command();
		}
	}
	fillMissingCommands();
	lastCommands_ = commands_;
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

Battle::Attacker Battle::attackerOf(int actor) const {
	const Combatant& c = combatants_[actor];
	Attacker a;
	a.strength = c.effective(Stat::Strength);
	a.mental = c.effective(Stat::Mental);
	a.dexterity = c.effective(Stat::Dexterity);
	a.critical = c.stats.critical;
	return a;
}

// Totals are scaled once and rounded down, so a 1.5x multiplier never loses more than one point.
Rewards Battle::rewards() const {
	Rewards rewards;
	rewards.memberExperience.assign(static_cast<std::size_t>(partyCount_), 0);
	if (outcome_ != Outcome::Victory) {
		return rewards;
	}
	long long experience = 0;
	long long currency = 0;
	for (int i = partyCount_; i < static_cast<int>(combatants_.size()); ++i) {
		if (const EnemyDef* def = db_->enemy(combatants_[i].defId)) {
			experience += def->experience;
			currency += def->currency;
		}
	}
	const long long scale = static_cast<long long>(rewardPercent_) * rewardMultiplierPercent(record_.setup.rewardMultiplier);
	rewards.experience = static_cast<int>(experience * scale / 10000);
	rewards.currency = static_cast<int>(currency * scale / 10000);

	int living = 0;
	for (int i = 0; i < partyCount_; ++i) {
		living += combatants_[i].alive() ? 1 : 0;
	}
	for (int i = 0; i < partyCount_; ++i) {
		const bool alive = combatants_[i].alive();
		switch (db_->battleRules.experienceSplit) {
		case ExperienceSplit::EachLiving:
			rewards.memberExperience[i] = alive ? rewards.experience : 0;
			break;
		case ExperienceSplit::SplitLiving:
			rewards.memberExperience[i] = alive ? rewards.experience / std::max(1, living) : 0;
			break;
		case ExperienceSplit::EachMember:
			rewards.memberExperience[i] = rewards.experience;
			break;
		}
	}
	return rewards;
}

std::vector<PartyMember> Battle::partyState() const {
	std::vector<PartyMember> party = record_.setup.party;
	for (int i = 0; i < partyCount_; ++i) {
		party[i].hp = combatants_[i].hp;
		party[i].tp = combatants_[i].tp;
		party[i].skillUses = combatants_[i].skillUses;
	}
	return party;
}

Macro Battle::lastRoundAsMacro(const std::string& name) const {
	Macro macro;
	macro.name = name;
	for (int actor : lastTurnOrder_) {
		if (!isParty(actor) || lastCommands_[actor].kind == CommandKind::None) {
			continue;
		}
		const Command& command = lastCommands_[actor];
		MacroStep step;
		step.member = combatants_[actor].defId;
		step.kind = command.kind;
		step.id = command.id;

		const AbilityDef* ability = nullptr;
		Targeting targeting = Targeting::Self;
		bool aimed = true;
		if (command.kind == CommandKind::Attack) {
			ability = db_->ability(combatants_[actor].basicAttack);
		} else if (command.kind == CommandKind::Ability) {
			ability = db_->ability(command.id);
		}
		if (ability) {
			targeting = ability->targeting;
		} else if (const ItemDef* item = command.kind == CommandKind::Item ? db_->item(command.id) : nullptr) {
			targeting = item->targeting;
		} else {
			aimed = false;
		}

		const int t = command.target;
		const bool foeTargeting = targeting == Targeting::OneEnemy || targeting == Targeting::EnemyGroup;
		if (!aimed) {
			step.target.kind = MacroTargetKind::Default;
		} else if (targeting == Targeting::AllEnemies || targeting == Targeting::AllAllies) {
			step.target.kind = MacroTargetKind::All;
		} else if (targeting == Targeting::Self) {
			step.target.kind = MacroTargetKind::Self;
		} else if (t < 0) {
			step.target.kind = MacroTargetKind::Default;
		} else if (foeTargeting && !isParty(t)) {
			step.target.kind = MacroTargetKind::Enemy;
			step.target.enemySlot = t - partyCount_;
		} else if (!foeTargeting && isParty(t)) {
			step.target.kind = t == actor ? MacroTargetKind::Self : MacroTargetKind::Member;
			step.target.member = t == actor ? "" : combatants_[t].defId;
		}
		macro.steps.push_back(step);
	}
	return macro;
}

}
