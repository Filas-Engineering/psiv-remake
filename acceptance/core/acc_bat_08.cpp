#include "support.h"

#include "rules/names.h"
#include "rules/text.h"

using namespace rules;

namespace {

constexpr int kFirst = 5;

bool statusEvent(EventType type) {
	return type == EventType::StatusApplied || type == EventType::StatusResisted || type == EventType::StatusRemoved;
}

}

ACC_CHECK(acc_bat_08, "ACC-BAT-08", "BAT-083") {
	const Database& db = support::data(t);
	Result<TextTable> loaded = loadTextTable(t.path("core/data/text_en.json"));
	if (!t.expect(loaded.ok(), "the string table core/data/text_en.json does not load: " + loaded.error)) {
		return;
	}
	const TextTable& table = *loaded.value;
	for (const std::string& key : messageKeys(db)) {
		t.expect(table.count(key) == 1, "message key " + key + ", which core can emit, is not in the string table");
	}

	// Over many played battles, every status change the state shows has a status event with a key the table has.
	ComboLog log;
	int applied = 0;
	int resisted = 0;
	int removed = 0;
	int byDefeat = 0;
	int silentRemovals = 0;
	for (std::uint64_t seed = 1; seed <= 400; ++seed) {
		const support::PlayedBattle played = support::playRandomBattle(db, seed, log);
		support::BattleState state = played.start;
		for (const Event& e : played.events) {
			const support::BattleState before = state;
			support::apply(db, state, e, static_cast<int>(played.setup.party.size()));
			applied += e.type == EventType::StatusApplied ? 1 : 0;
			resisted += e.type == EventType::StatusResisted ? 1 : 0;
			removed += e.type == EventType::StatusRemoved ? 1 : 0;
			byDefeat += e.type == EventType::StatusRemoved && e.statusEnd == StatusEnd::Defeated ? 1 : 0;
			if (statusEvent(e.type)) {
				// A lowered stat the target resists is a StatusResisted that names the stat and no status.
				const bool statResisted = e.type == EventType::StatusResisted && e.status.empty() && e.message == "battle.stat_resisted";
				t.expect(statResisted || (!e.status.empty() && db.status(e.status)), std::string(toString(e.type)) + " without a known status id");
				t.expect(!e.message.empty() && table.count(e.message) == 1, std::string(toString(e.type)) + " for " + e.status + " carries message key '" + e.message + "', which the string table lacks");
			}
			if (e.type == EventType::CombatantDefeated) {
				for (const std::string& status : before.combatants[e.target].statuses) {
					silentRemovals += status != kDefeatedStatus ? 1 : 0;
				}
			}
		}
	}
	t.expect(applied > 0 && resisted > 0 && removed > 0, "the played battles never applied, resisted and removed a status, so the events were not exercised");
	t.expect(silentRemovals == 0, std::to_string(silentRemovals) + " statuses were cleared by defeat with no StatusRemoved event (BAT-083)");

	// Immunity is reported as a resistance, with no roll and the immune key.
	{
		Database immune = db;
		immune.enemies["gel_mite"].statusResist["sleep"] = 100;
		Battle battle = support::create(immune, support::setup(immune, {"gel_mite"}, 3));
		support::sturdy(battle, kFirst);
		battle.setCommand(4, Command::ability("drowse", kFirst));
		support::othersDefend(battle);
		bool reported = false;
		for (const Event& e : battle.resolveRound()) {
			reported = reported || (e.type == EventType::StatusResisted && e.effectiveness == Effectiveness::Immune && e.status == "sleep" && e.message == statusMessageKey("sleep", "immune"));
		}
		t.expect(reported, "an immune target produced no StatusResisted event with the immune message key");
	}

	t.note("every key messageKeys lists is in text_en.json; " + std::to_string(applied) + " applications, " + std::to_string(resisted) + " resistances and " + std::to_string(removed) + " removals (" + std::to_string(byDefeat) +
		" by defeat) across 400 battles each emitted a status event with a valid status id and a message key the string table has; immunity reports the immune key");
}
