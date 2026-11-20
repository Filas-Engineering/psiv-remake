#include "rules/battle.h"

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
		if (!c.alive() || c.aboard) {
			continue;
		}
		const std::vector<ActiveStatus> held = c.statuses;
		for (const ActiveStatus& active : held) {
			const StatusDef* def = db_->status(active.id);
			if (def && def->recoverChance > 0 && rng_.chance(def->recoverChance)) {
				emitStatusRemoved(-1, i, active.id, StatusEnd::Recovered, out);
			}
		}

		std::vector<std::string> expired;
		for (ActiveStatus& active : c.statuses) {
			if (active.remaining > 0 && --active.remaining == 0) {
				expired.push_back(active.id);
			}
		}
		for (const std::string& id : expired) {
			// As in the original, a status that runs out leaves the stats it held at 1 for the rest of the battle;
			// only a cure or a recovery gives them back.
			for (Stat stat : db_->status(id)->statsToOne) {
				c.setModifier(stat, 1 - c.base(stat));
				Event& e = emit(out, EventType::StatModified);
				e.target = i;
				e.stat = stat;
				e.amount = 1 - c.base(stat);
			}
			emitStatusRemoved(-1, i, id, StatusEnd::Duration, out);
		}
	}
	updateOutcome();
	const ScriptedEnd& end = record_.setup.scriptedEnd;
	if (outcome_ == Outcome::Ongoing && end.kind == ScriptedEndKind::SurviveRounds && round_ >= end.rounds) {
		outcome_ = Outcome::ScriptedEnd;
	}
}

}
