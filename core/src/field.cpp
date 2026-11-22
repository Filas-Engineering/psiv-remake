#include "rules/field.h"

#include "rules/battle.h"
#include "rules/progression.h"

#include <algorithm>

namespace rules {

namespace {

FieldUse refuse(std::string key, std::vector<std::string> params) {
	FieldUse use;
	use.rejection = {std::move(key), std::move(params)};
	return use;
}

// The first member whose character or equipment the data does not have, or an empty string.
std::string checkParty(const Database& db, const std::vector<PartyMember>& party) {
	for (const PartyMember& member : party) {
		bool known = db.character(member.characterId) != nullptr;
		for (const auto& [slot, id] : member.equipment) {
			known = known && db.equipmentPiece(id) != nullptr;
		}
		if (!known) {
			return member.characterId;
		}
	}
	return "";
}

bool needsTarget(Targeting targeting) {
	return targeting == Targeting::OneAlly || targeting == Targeting::OneFallenAlly;
}

// Copies what the effects can change back to the party: hit points, technique points, skill uses and the
// statuses that persist outside battle.
void writeBack(const Battle& battle, std::vector<PartyMember>& party) {
	for (std::size_t i = 0; i < party.size(); ++i) {
		const Combatant& c = battle.combatant(static_cast<int>(i));
		party[i].hp = c.hp;
		party[i].tp = c.tp;
		party[i].skillUses = c.skillUses;
		for (const auto& [element, affinity] : c.grantedAffinities) {
			party[i].affinities[element] = affinity;
		}
		party[i].statuses.clear();
		for (const ActiveStatus& active : c.statuses) {
			const StatusDef* def = battle.database().status(active.id);
			if (def && def->persistsOutsideBattle) {
				party[i].statuses.push_back(active);
			}
		}
	}
}

}

FieldUse useItemOutsideBattle(const Database& db, std::vector<PartyMember>& party, Inventory& inventory, const std::string& itemId, int target) {
	const ItemDef* item = db.item(itemId);
	if (!item) {
		return refuse("field.unknown_item", {itemId});
	}
	if (!item->field) {
		return refuse("field.not_usable_here", {itemId});
	}
	const auto stock = inventory.find(itemId);
	if (stock == inventory.end() || stock->second <= 0) {
		return refuse("field.no_stock", {itemId});
	}
	const std::string unknown = checkParty(db, party);
	if (party.empty() || !unknown.empty()) {
		return refuse("field.unknown_member", {unknown});
	}
	const bool chosen = target >= 0 && target < static_cast<int>(party.size());
	if (needsTarget(item->targeting) && !chosen) {
		return refuse("field.no_target", {itemId});
	}

	FieldUse use;
	const bool consumeAnyway = item->consumed && item->consumeWhenNoEffect.value_or(db.battleRules.consumeWhenNoEffect);
	if (travelEffect(item->effects)) {
		// The game has already moved the party; using the item only spends it.
		if (--stock->second == 0) {
			inventory.erase(stock);
		}
		Event spent;
		spent.type = EventType::ResourceSpent;
		spent.ability = item->id;
		spent.resource = ResourceKind::Item;
		spent.amount = 1;
		use.events.push_back(spent);
		return use;
	}
	if (const Effect* teach = teachEffect(item->effects)) {
		PartyMember& learner = party[static_cast<std::size_t>(target)];
		const std::vector<std::string> known = knownAbilities(db, learner);
		const bool knew = std::find(known.begin(), known.end(), teach->ability) != known.end();
		if (knew) {
			Event kept;
			kept.type = EventType::ItemHadNoEffect;
			kept.ability = item->id;
			use.events.push_back(kept);
			if (!consumeAnyway) {
				return use;
			}
		} else {
			learner.taught.push_back(teach->ability);
			const AbilityDef* taught = db.ability(teach->ability);
			if (taught && taught->kind == AbilityKind::Skill && !learner.skillUses.count(taught->id)) {
				learner.skillUses[taught->id] = maxSkillUses(db, learner, taught->id);
			}
		}
		if (--stock->second == 0) {
			inventory.erase(stock);
		}
		if (!knew) {
			Event learned;
			learned.type = EventType::Message;
			learned.target = target;
			learned.message = "field.learned";
			learned.params = {learner.characterId, teach->ability};
			use.events.push_back(learned);
		}
		Event spent;
		spent.type = EventType::ResourceSpent;
		spent.ability = item->id;
		spent.resource = ResourceKind::Item;
		spent.amount = 1;
		use.events.push_back(spent);
		return use;
	}
	Battle battle = Battle::partyOnly(db, party);
	const int user = chosen ? target : 0;
	const std::vector<Effect> effects = fieldEffects(item->effects);
	const std::vector<int> targets = battle.resolveTargets(user, item->targeting, target, effectsRevive(effects), effectsTakeLiving(effects), &effects);
	if (targets.empty() || (chosen && needsTarget(item->targeting) && !battle.canAffect(target, effects))) {
		if (!chosen || !needsTarget(item->targeting) || !db.spentOnNoReach(*item)) {
			return refuse("field.no_target", {itemId});
		}
		Event kept;
		kept.type = EventType::ItemHadNoEffect;
		kept.ability = item->id;
		use.events.push_back(kept);
		if (--stock->second == 0) {
			inventory.erase(stock);
		}
		Event spent;
		spent.type = EventType::ResourceSpent;
		spent.ability = item->id;
		spent.resource = ResourceKind::Item;
		spent.amount = 1;
		use.events.push_back(spent);
		return use;
	}
	if (!battle.applyEffects(-1, Battle::Attacker(), item->id, effects, targets, true, use.events)) {
		Event& kept = battle.emit(use.events, EventType::ItemHadNoEffect);
		kept.ability = item->id;
		if (!consumeAnyway) {
			return use;
		}
	}
	if (!item->consumed) {
		return use;
	}
	if (--stock->second == 0) {
		inventory.erase(stock);
	}
	Event& spent = battle.emit(use.events, EventType::ResourceSpent);
	spent.ability = item->id;
	spent.resource = ResourceKind::Item;
	spent.amount = 1;
	writeBack(battle, party);
	return use;
}

std::vector<int> fieldTargetsOutOfReach(const Database& db, const std::vector<PartyMember>& party, const std::string& id, bool item, int user) {
	const ItemDef* itemDef = item ? db.item(id) : nullptr;
	const AbilityDef* ability = item ? nullptr : db.ability(id);
	std::vector<int> out;
	if ((!itemDef && !ability) || party.empty() || !checkParty(db, party).empty() || (itemDef && db.spentOnNoReach(*itemDef))) {
		return out;
	}
	const Targeting targeting = itemDef ? itemDef->targeting : ability->targeting;
	const bool field = itemDef ? itemDef->field : ability->field;
	const std::vector<Effect>& all = itemDef ? itemDef->effects : ability->effects;
	if (!field || !needsTarget(targeting) || travelEffect(all) || (itemDef && teachEffect(all))) {
		return out;
	}
	const Battle battle = Battle::partyOnly(db, party);
	const std::vector<Effect> effects = fieldEffects(all);
	for (int target = 0; target < static_cast<int>(party.size()); ++target) {
		const int from = itemDef || user < 0 || user >= static_cast<int>(party.size()) ? target : user;
		const std::vector<int> targets = battle.resolveTargets(from, targeting, target, effectsRevive(effects), effectsTakeLiving(effects), &effects);
		if (targets.empty() || !battle.canAffect(target, effects)) {
			out.push_back(target);
		}
	}
	return out;
}

FieldUse useAbilityOutsideBattle(const Database& db, std::vector<PartyMember>& party, int user, const std::string& abilityId, int target) {
	const AbilityDef* ability = db.ability(abilityId);
	if (!ability) {
		return refuse("field.unknown_ability", {abilityId});
	}
	if (!ability->field) {
		return refuse("field.not_usable_here", {abilityId});
	}
	const std::string unknown = checkParty(db, party);
	if (!unknown.empty()) {
		return refuse("field.unknown_member", {unknown});
	}
	if (user < 0 || user >= static_cast<int>(party.size())) {
		return refuse("field.unknown_member", {std::to_string(user)});
	}
	const bool chosen = target >= 0 && target < static_cast<int>(party.size());
	if (needsTarget(ability->targeting) && !chosen) {
		return refuse("field.no_target", {abilityId});
	}

	Battle battle = Battle::partyOnly(db, party);
	const Combatant& self = battle.combatant(user);
	if (!self.alive()) {
		return refuse("field.user_down", {self.defId});
	}
	if (!self.knows(abilityId)) {
		return refuse("field.not_known", {self.defId, abilityId});
	}
	switch (battle.checkUsable(user, *ability)) {
	case CancelReason::None:
		break;
	case CancelReason::Silenced:
		return refuse("field.silenced", {self.defId});
	case CancelReason::NotEnoughTp:
		return refuse("field.not_enough_tp", {self.defId, abilityId});
	case CancelReason::NoUsesLeft:
		return refuse("field.no_uses_left", {self.defId, abilityId});
	case CancelReason::ActorUnable:
	case CancelReason::NoItem:
	case CancelReason::NoTarget:
		return refuse("field.no_target", {abilityId});
	}
	const std::vector<Effect> effects = fieldEffects(ability->effects);
	const std::vector<int> targets = battle.resolveTargets(user, ability->targeting, target, effectsRevive(effects), effectsTakeLiving(effects), &effects);
	if (targets.empty() || (chosen && needsTarget(ability->targeting) && !battle.canAffect(target, effects))) {
		return refuse("field.no_target", {abilityId});
	}
	FieldUse use;
	battle.payCost(user, *ability, use.events);
	battle.applyEffects(user, battle.attackerOf(user), ability->id, effects, targets, false, use.events);
	writeBack(battle, party);
	return use;
}

}
