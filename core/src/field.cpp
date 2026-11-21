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
	Battle battle = Battle::partyOnly(db, party);
	const int user = chosen ? target : 0;
	const std::vector<int> targets = battle.resolveTargets(user, item->targeting, target, effectsRevive(item->effects));
	if (targets.empty()) {
		return refuse("field.no_target", {itemId});
	}
	if (!battle.applyEffects(-1, Battle::Attacker(), item->id, item->effects, targets, true, use.events)) {
		Event& kept = battle.emit(use.events, EventType::ItemHadNoEffect);
		kept.ability = item->id;
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
	const std::vector<int> targets = battle.resolveTargets(user, ability->targeting, target, ability->revives());
	if (targets.empty()) {
		return refuse("field.no_target", {abilityId});
	}
	FieldUse use;
	battle.payCost(user, *ability, use.events);
	battle.applyEffects(user, battle.attackerOf(user), ability->id, ability->effects, targets, false, use.events);
	writeBack(battle, party);
	return use;
}

}
