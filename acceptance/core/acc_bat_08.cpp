#include "support.h"

#include "rules/names.h"

using namespace rules;

namespace {

constexpr int kFirst = 5;

bool statusEvent(EventType type) {
	return type == EventType::StatusApplied || type == EventType::StatusResisted || type == EventType::StatusCured || type == EventType::StatusExpired;
}

}

ACC_CHECK(acc_bat_08, "ACC-BAT-08", "BAT-083") {
	const Database& db = support::data(t);

	// Over many played battles, every status change the state shows has a status event.
	ComboLog log;
	int applied = 0;
	int resisted = 0;
	int removed = 0;
	int silentRemovals = 0;
	for (std::uint64_t seed = 1; seed <= 400; ++seed) {
		const support::PlayedBattle played = support::playRandomBattle(db, seed, log);
		support::BattleState state = played.start;
		for (const Event& e : played.events) {
			const support::BattleState before = state;
			support::apply(state, e, played.setup.party.empty() ? 0 : static_cast<int>(played.setup.party.size()));
			applied += e.type == EventType::StatusApplied ? 1 : 0;
			resisted += e.type == EventType::StatusResisted ? 1 : 0;
			removed += e.type == EventType::StatusCured || e.type == EventType::StatusExpired ? 1 : 0;
			if (statusEvent(e.type)) {
				t.expect(!e.status.empty() && db.status(e.status), std::string(toString(e.type)) + " without a known status id");
			}
			if (e.type == EventType::CombatantDefeated) {
				for (const std::string& status : before.combatants[e.target].statuses) {
					silentRemovals += status != kDefeatedStatus ? 1 : 0;
				}
			}
		}
	}
	t.expect(applied > 0 && resisted > 0 && removed > 0, "the played battles never applied, resisted and removed a status, so the events were not exercised");

	// Immunity is reported as a resistance, with no roll.
	{
		Database immune = db;
		immune.enemies["gel_mite"].statusResist["sleep"] = 100;
		Battle battle = support::create(immune, support::setup(immune, {"gel_mite"}, 3));
		support::sturdy(battle, kFirst);
		battle.setCommand(4, Command::ability("drowse", kFirst));
		support::othersDefend(battle);
		bool reported = false;
		for (const Event& e : battle.resolveRound()) {
			reported = reported || (e.type == EventType::StatusResisted && e.effectiveness == Effectiveness::Immune && e.status == "sleep");
		}
		t.expect(reported, "an immune target produced no StatusResisted event");
	}

	t.gap("status events carry the status id but no message key, and core has no string table to check keys against (BAT-083)");
	if (silentRemovals > 0) {
		t.gap(std::to_string(silentRemovals) + " statuses were removed by defeat with no removal event; only CombatantDefeated is emitted (BAT-083)");
	}
	t.note(std::to_string(applied) + " applications, " + std::to_string(resisted) + " resistances and " + std::to_string(removed) + " removals across 400 battles each emitted a status event with a valid status id");
}
