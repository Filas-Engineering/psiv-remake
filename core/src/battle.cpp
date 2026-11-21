#include "rules/battle.h"

#include "rules/formulas.h"
#include "rules/names.h"
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

Combatant fromVehicle(const Database& db, const VehicleDef& def) {
	Combatant c;
	c.defId = def.id;
	c.name = def.name;
	c.side = Side::Party;
	c.group = kPartyGroup;
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

std::string checkMember(const Database& db, const PartyMember& member) {
	for (const auto& [slot, id] : member.equipment) {
		const Message refused = canEquip(db, member.characterId, id);
		if (!refused.empty()) {
			return "'" + member.characterId + "' cannot wear '" + id + "' (" + refused.key + ")";
		}
		if (db.equipmentPiece(id)->slot != slot) {
			return "'" + id + "' does not go in that slot";
		}
	}
	for (const ActiveStatus& active : member.statuses) {
		const StatusDef* status = db.status(active.id);
		if (!status || active.id == kDefeatedStatus) {
			return "'" + member.characterId + "' carries unknown status '" + active.id + "'";
		}
		if (!status->persistsOutsideBattle) {
			return "'" + member.characterId + "' carries status '" + active.id + "', which does not persist outside battle";
		}
	}
	return "";
}

}

Combatant memberCombatant(const Database& db, const PartyMember& member) {
	const CharacterDef& def = *db.character(member.characterId);
	Combatant c;
	c.defId = def.id;
	c.name = member.name.empty() ? def.name : member.name;
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
	// Defeat clears every other status, so a fallen member carries none in.
	if (c.alive()) {
		c.statuses = member.statuses;
	} else {
		c.statuses.push_back({kDefeatedStatus, 0});
	}
	return c;
}

namespace {

std::string checkScriptedEnd(const ScriptedEnd& end, int enemyCount) {
	switch (end.kind) {
	case ScriptedEndKind::None:
		return "";
	case ScriptedEndKind::SurviveRounds:
		return end.rounds >= 1 ? "" : "a survive-rounds end needs at least 1 round";
	case ScriptedEndKind::EnemyDefeated:
		return end.enemySlot >= 0 && end.enemySlot < enemyCount ? "" : "the scripted end names formation position " + std::to_string(end.enemySlot) + ", which is empty";
	}
	return "";
}

std::string actionId(const Combatant& c, const Command& command) {
	return command.kind == CommandKind::Attack ? c.basicAttack : command.id;
}

}

Result<Battle> Battle::create(const Database& db, const BattleSetup& setup, ComboLog* comboLog) {
	if (setup.party.empty() || setup.party.size() > static_cast<std::size_t>(kMaxPartySize)) {
		return Result<Battle>::failure("a battle needs 1 to " + std::to_string(kMaxPartySize) + " party members");
	}
	if (setup.enemies.empty() || setup.enemies.size() > static_cast<std::size_t>(kMaxEnemies)) {
		return Result<Battle>::failure("a battle needs 1 to " + std::to_string(kMaxEnemies) + " enemies");
	}
	const std::string endError = checkScriptedEnd(setup.scriptedEnd, static_cast<int>(setup.enemies.size()));
	if (!endError.empty()) {
		return Result<Battle>::failure(endError);
	}
	if (setup.flags.vehicleBattle ? !db.vehicle(setup.vehicle) : !setup.vehicle.empty()) {
		return Result<Battle>::failure(setup.flags.vehicleBattle ? "a vehicle battle needs a known vehicle, not '" + setup.vehicle + "'"
			: "vehicle '" + setup.vehicle + "' needs the vehicle battle flag");
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
		battle.record_.knownCombos = comboLog->entries();
	}
	battle.inventory_ = setup.inventory;
	battle.partyCount_ = static_cast<int>(setup.party.size()) + (setup.flags.vehicleBattle ? 1 : 0);
	battle.rewardPercent_ = difficulty ? difficulty->rewardPercent : 100;

	for (const PartyMember& member : setup.party) {
		const CharacterDef* def = rulesDb.character(member.characterId);
		if (!def) {
			return Result<Battle>::failure("unknown character '" + member.characterId + "'");
		}
		const std::string memberError = checkMember(rulesDb, member);
		if (!memberError.empty()) {
			return Result<Battle>::failure(memberError);
		}
		battle.combatants_.push_back(memberCombatant(rulesDb, member));
		battle.combatants_.back().aboard = setup.flags.vehicleBattle;
	}
	if (const VehicleDef* vehicle = setup.flags.vehicleBattle ? rulesDb.vehicle(setup.vehicle) : nullptr) {
		battle.vehicleIndex_ = static_cast<int>(battle.combatants_.size());
		battle.combatants_.push_back(fromVehicle(rulesDb, *vehicle));
	}
	for (const FormationSlot& slot : setup.enemies) {
		const EnemyDef* def = rulesDb.enemy(slot.enemy);
		if (!def) {
			return Result<Battle>::failure("unknown enemy '" + slot.enemy + "'");
		}
		battle.combatants_.push_back(fromEnemy(rulesDb, *def, slot.group));
	}
	BattleSetup& kept = battle.record_.setup;
	if (kept.escapeRating < 0) {
		for (int i = battle.partyCount_; i < static_cast<int>(battle.combatants_.size()); ++i) {
			kept.escapeRating = std::max(kept.escapeRating, battle.combatants_[i].effective(Stat::Agility));
		}
	}
	kept.flags.escapeAllowed = kept.flags.escapeAllowed && kept.escapeRating < formulas::kEscapeForbiddenRating;
	battle.commands_.resize(battle.combatants_.size());
	battle.lastCommands_.resize(battle.combatants_.size());
	battle.forced_.assign(battle.combatants_.size(), false);
	battle.aiTurn_.assign(battle.combatants_.size(), false);
	for (int i = 0; i < static_cast<int>(battle.combatants_.size()); ++i) {
		battle.syncPins(i);
	}
	battle.updateOutcome();
	return Result<Battle>::success(std::move(battle));
}

// A party with no enemies, for out-of-battle use. The data is borrowed: the battle must not outlive `db`.
Battle Battle::partyOnly(const Database& db, const std::vector<PartyMember>& party) {
	Battle battle;
	battle.db_ = std::shared_ptr<const Database>(std::shared_ptr<const Database>(), &db);
	battle.partyCount_ = static_cast<int>(party.size());
	for (const PartyMember& member : party) {
		battle.combatants_.push_back(memberCombatant(db, member));
	}
	battle.commands_.resize(battle.combatants_.size());
	battle.forced_.assign(battle.combatants_.size(), false);
	battle.aiTurn_.assign(battle.combatants_.size(), false);
	return battle;
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

Message Battle::setCommand(int actor, const Command& command) {
	if (actor < 0 || actor >= static_cast<int>(combatants_.size())) {
		return {"command.no_combatant", {std::to_string(actor)}};
	}
	const Combatant& c = combatants_[actor];
	if (c.aboard) {
		return {"command.aboard", {c.defId}};
	}
	if (!c.alive()) {
		return {"command.actor_defeated", {c.defId}};
	}
	if (!sideActsNextRound(c.side)) {
		return {"command.caught_off_guard", {c.defId}};
	}
	if (const StatusDef* forced = statusWith(actor, StatusAction::Forced)) {
		return {"command.forced_by_status", {c.defId, forced->id}};
	}
	if (command.target >= static_cast<int>(combatants_.size())) {
		return {"command.no_combatant", {std::to_string(command.target)}};
	}
	switch (command.kind) {
	case CommandKind::Ability: {
		const AbilityDef* ability = db_->ability(command.id);
		if (!ability || !c.knows(command.id)) {
			return {"command.unknown_ability", {c.defId, command.id}};
		}
		if (ability->kind == AbilityKind::Combo) {
			return {"command.combo_not_selectable", {command.id}};
		}
		if (travelEffect(ability->effects)) {
			return {"command.field_only", {command.id}};
		}
		break;
	}
	case CommandKind::Item:
		if (!db_->item(command.id)) {
			return {"command.unknown_item", {command.id}};
		}
		if (travelEffect(db_->item(command.id)->effects)) {
			return {"command.field_only", {command.id}};
		}
		if (actor == vehicleIndex_) {
			return {"command.vehicle_cannot", {c.defId, toString(command.kind)}};
		}
		break;
	case CommandKind::Escape:
		if (!isParty(actor)) {
			return {"command.enemy_cannot_escape", {c.defId}};
		}
		if (!escapeAllowed()) {
			return {"command.escape_forbidden", {}};
		}
		break;
	case CommandKind::Defend:
		if (actor == vehicleIndex_) {
			return {"command.vehicle_cannot", {c.defId, toString(command.kind)}};
		}
		break;
	case CommandKind::None:
	case CommandKind::Attack:
		break;
	}
	commands_[actor] = command;
	return {};
}

bool Battle::startAuto(const Macro& macro) {
	if (outcome_ != Outcome::Ongoing) {
		return false;
	}
	autoMacro_ = macro;
	autoActive_ = true;
	autoCancelRequested_ = false;
	autoMemberFell_ = false;
	autoStop_ = AutoStop::None;
	return true;
}

bool Battle::fillFromAuto() {
	if (!autoActive_) {
		return false;
	}
	AutoStop stop = AutoStop::None;
	if (outcome_ != Outcome::Ongoing) {
		stop = AutoStop::BattleEnded;
	} else if (autoCancelRequested_) {
		stop = AutoStop::Cancelled;
	} else if (autoMemberFell_) {
		stop = AutoStop::MemberFell;
	} else {
		for (const MacroStepResult& result : applyMacro(autoMacro_)) {
			if (isCostIssue(result.issue)) {
				stop = AutoStop::CannotPay;
			}
		}
		if (stop != AutoStop::None) {
			for (int i = 0; i < partyCount_; ++i) {
				commands_[i] = Command();
			}
			forcedOrder_.clear();
			macroName_.clear();
		}
	}
	if (stop == AutoStop::None) {
		return true;
	}
	autoActive_ = false;
	autoStop_ = stop;
	return false;
}

void Battle::fillMissingCommands() {
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		forced_[i] = false;
		aiTurn_[i] = false;
		if (!combatants_[i].alive() || combatants_[i].aboard || surprised(combatants_[i].side, round_)) {
			continue;
		}
		// A forced command replaces whatever was chosen (BAT-012).
		if (const StatusDef* status = statusWith(i, StatusAction::Forced)) {
			commands_[i] = forcedCommand(i, *status);
			forced_[i] = true;
			continue;
		}
		if (commands_[i].kind == CommandKind::None) {
			commands_[i] = Command::attack(-1);
			aiTurn_[i] = !isParty(i);
		}
	}
}

Command Battle::forcedCommand(int actor, const StatusDef& status) {
	const Side side = combatants_[actor].side;
	std::vector<int> pool;
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		const bool ally = combatants_[i].side == side;
		const bool wanted = status.forcedTarget == ForcedTarget::RandomAny || (status.forcedTarget == ForcedTarget::RandomAlly) == ally;
		if (i != actor && combatants_[i].alive() && !combatants_[i].aboard && wanted) {
			pool.push_back(i);
		}
	}
	if (pool.empty()) {
		pool = livingFoes(actor);
	}
	if (pool.empty()) {
		return Command::attack(-1);
	}
	return Command::attack(pool[rng_.range(0, static_cast<int>(pool.size()) - 1)]);
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
	input.macroName = macroName_;
	input.playTimeMs = playTimeMs_;
	record_.rounds.push_back(input);

	if (round_ == 0) {
		Event& started = emit(out, EventType::BattleStarted);
		for (const Combatant& c : combatants_) {
			started.ids.push_back(c.defId);
			started.groups.push_back(c.group);
		}
		started.partyCount = partyCount_;
		started.opening = record_.setup.opening;
		started.flags = record_.setup.flags;
		if (record_.setup.opening != Opening::Normal) {
			emit(out, EventType::Message).message = record_.setup.opening == Opening::PartyFirst ? "battle.opening.party_first" : "battle.opening.enemy_first";
		}
	}
	++round_;
	emit(out, EventType::RoundStarted);
	// The host may have changed statuses between rounds.
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		syncPins(i);
	}
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		if (surprised(combatants_[i].side, round_)) {
			commands_[i] = Command();
		}
	}
	fillMissingCommands();
	lastCommands_ = commands_;
	Event& locked = emit(out, EventType::CommandsLocked);
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		if (commands_[i].kind != CommandKind::None) {
			locked.commands.push_back({i, commands_[i]});
		}
	}
	locked.macro = macroName_;
	for (std::size_t i = 0; i < combatants_.size(); ++i) {
		combatants_[i].defending = combatants_[i].alive() && commands_[i].kind == CommandKind::Defend;
	}

	const std::vector<int> order = computeTurnOrder();
	lastTurnOrder_ = order;
	emit(out, EventType::OrderResolved).order = order;
	// Every enemy the AI drives draws its foe now, in index order; its action waits for its turn (BAT-090).
	for (int i = partyCount_; i < static_cast<int>(combatants_.size()); ++i) {
		if (aiTurn_[i] && std::find(order.begin(), order.end(), i) != order.end()) {
			commands_[i].target = drawFoeByPartyOrder(i);
		}
	}
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
		drawDrops();
		const Rewards earned = rewards();
		Event& ended = emit(out, EventType::BattleEnded);
		ended.outcome = outcome_;
		ended.experience = earned.experience;
		ended.currency = earned.currency;
		ended.items = earned.items;
		ended.gameOver = gameOver();
		if (autoActive_) {
			autoActive_ = false;
			autoStop_ = AutoStop::BattleEnded;
		}
	}

	for (Combatant& c : combatants_) {
		c.defending = false;
	}
	std::fill(commands_.begin(), commands_.end(), Command());
	forcedOrder_.clear();
	macroName_.clear();
	return out;
}

// An enemy is down only when defeated; a party member also while a status that counts toward defeat holds it.
bool Battle::countsAsDown(int index) const {
	const Combatant& c = combatants_[index];
	if (!c.alive()) {
		return true;
	}
	if (!isParty(index)) {
		return false;
	}
	for (const ActiveStatus& active : c.statuses) {
		const StatusDef* def = db_->status(active.id);
		if (def && def->countsTowardDefeat) {
			return true;
		}
	}
	return false;
}

void Battle::updateOutcome() {
	if (outcome_ != Outcome::Ongoing) {
		return;
	}
	bool partyStanding = false;
	bool enemiesStanding = false;
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		if (!combatants_[i].aboard && !countsAsDown(i)) {
			(isParty(i) ? partyStanding : enemiesStanding) = true;
		}
	}
	const ScriptedEnd& end = record_.setup.scriptedEnd;
	if (!partyStanding) {
		outcome_ = Outcome::Defeat;
	} else if (!enemiesStanding) {
		outcome_ = Outcome::Victory;
	} else if (end.kind == ScriptedEndKind::EnemyDefeated && !combatants_[partyCount_ + end.enemySlot].alive()) {
		outcome_ = Outcome::ScriptedEnd;
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

// One roll of 0 to 127 for the encounter's drop, only after a Victory (SAV-031).
void Battle::drawDrops() {
	const Encounter* encounter = db_->encounter(record_.setup.enemyGroup);
	if (outcome_ != Outcome::Victory || !encounter || encounter->drop.item.empty()) {
		return;
	}
	if (rng_.range(0, 127) < encounter->drop.rate) {
		++drops_[encounter->drop.item];
	}
}

// Totals are scaled once and rounded down, so a 1.5x multiplier never loses more than one point. A vehicle battle
// halves the experience. Each party member who is not defeated gets an even share, rounded down; in a vehicle
// battle those are the members aboard, and the vehicle gets none.
Rewards Battle::rewards() const {
	Rewards rewards;
	const int members = static_cast<int>(record_.setup.party.size());
	rewards.memberExperience.assign(static_cast<std::size_t>(members), 0);
	if (outcome_ != Outcome::Victory && outcome_ != Outcome::ScriptedEnd) {
		return rewards;
	}
	rewards.items = drops_;
	long long experience = 0;
	long long currency = 0;
	for (int i = partyCount_; i < static_cast<int>(combatants_.size()); ++i) {
		const EnemyDef* def = db_->enemy(combatants_[i].defId);
		if (def && (outcome_ == Outcome::Victory || !combatants_[i].alive())) {
			experience += def->experience;
			currency += def->currency;
		}
	}
	const long long scale = static_cast<long long>(rewardPercent_) * rewardMultiplierPercent(record_.setup.rewardMultiplier);
	rewards.experience = static_cast<int>(experience * scale / 10000) / (vehicleIndex_ >= 0 ? 2 : 1);
	rewards.currency = static_cast<int>(currency * scale / 10000);

	int living = 0;
	for (int i = 0; i < members; ++i) {
		living += combatants_[i].alive() ? 1 : 0;
	}
	for (int i = 0; i < members; ++i) {
		rewards.memberExperience[i] = combatants_[i].alive() ? rewards.experience / std::max(1, living) : 0;
	}
	return rewards;
}

std::vector<PartyMember> Battle::partyState() const {
	std::vector<PartyMember> party = record_.setup.party;
	for (int i = 0; i < static_cast<int>(party.size()); ++i) {
		party[i].hp = combatants_[i].hp;
		party[i].tp = combatants_[i].tp;
		party[i].skillUses = combatants_[i].skillUses;
		party[i].statuses.clear();
		for (const ActiveStatus& active : combatants_[i].statuses) {
			const StatusDef* def = db_->status(active.id);
			if (def && def->persistsOutsideBattle) {
				party[i].statuses.push_back(active);
			}
		}
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
		const bool allyTargeting = targeting == Targeting::OneAlly || targeting == Targeting::OneFallenAlly;
		if (!aimed || targeting == Targeting::RandomEnemy) {
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
		} else if (allyTargeting && isParty(t)) {
			step.target.kind = t == actor ? MacroTargetKind::Self : MacroTargetKind::Member;
			step.target.member = t == actor ? "" : combatants_[t].defId;
		}
		macro.steps.push_back(step);
	}
	return macro;
}

void Battle::cancel(int actor, const Command& command, CancelReason reason, const std::string& status, std::vector<Event>& out) {
	Event& e = emit(out, EventType::ActionCancelled);
	e.actor = actor;
	e.ability = actionId(combatants_[actor], command);
	e.command = command.kind;
	e.cancel = reason;
	e.status = status;
}

}
