#include "rules/battle.h"

#include "rules/formulas.h"

#include <algorithm>

namespace rules {

const StatusDef* Battle::statusWith(int actor, StatusAction action) const {
	for (const ActiveStatus& active : combatants_[actor].statuses) {
		const StatusDef* def = db_->status(active.id);
		if (def && def->action == action) {
			return def;
		}
	}
	return nullptr;
}

void Battle::endOfRound(std::vector<Event>& out) {
	for (int i = 0; i < static_cast<int>(combatants_.size()); ++i) {
		Combatant& c = combatants_[i];
		if (!c.alive()) {
			continue;
		}
		const std::vector<ActiveStatus> before = c.statuses;
		for (const ActiveStatus& active : before) {
			const StatusDef* def = db_->status(active.id);
			if (!def || def->tickDamagePercent <= 0) {
				continue;
			}
			const int damage = std::min(c.hp, formulas::statusTick(c.stats.maxHp, def->tickDamagePercent));
			c.hp -= damage;
			Event& e = emit(out, EventType::StatusTicked);
			e.target = i;
			e.status = def->id;
			e.amount = damage;
			e.hp = c.hp;
			if (!c.alive()) {
				defeat(i, out);
				break;
			}
		}
		if (!c.alive()) {
			continue;
		}

		std::vector<std::string> expired;
		for (ActiveStatus& active : c.statuses) {
			if (active.remaining > 0 && --active.remaining == 0) {
				expired.push_back(active.id);
			}
		}
		for (const std::string& id : expired) {
			emitStatusRemoved(-1, i, id, StatusEnd::Duration, out);
		}

		std::vector<Stat> worn;
		for (ActiveModifier& modifier : c.modifiers) {
			if (--modifier.remaining <= 0) {
				worn.push_back(modifier.stat);
			}
		}
		c.modifiers.erase(std::remove_if(c.modifiers.begin(), c.modifiers.end(), [](const ActiveModifier& m) {
			return m.remaining <= 0;
		}), c.modifiers.end());
		for (Stat stat : worn) {
			Event& e = emit(out, EventType::StatModifierExpired);
			e.target = i;
			e.stat = stat;
		}
	}
	updateOutcome();
	const ScriptedEnd& end = record_.setup.scriptedEnd;
	if (outcome_ == Outcome::Ongoing && end.kind == ScriptedEndKind::SurviveRounds && round_ >= end.rounds) {
		outcome_ = Outcome::ScriptedEnd;
	}
}

}
