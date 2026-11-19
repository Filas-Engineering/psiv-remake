#include "support.h"

#include "rules/formulas.h"
#include "rules/names.h"

using namespace rules;

namespace {

constexpr int kFirst = 5;

// A placeholder ability that does one effect, so a status can be applied or cured on demand.
void addAbility(Database& db, const std::string& id, EffectKind kind, const std::string& status) {
	AbilityDef ability;
	ability.id = id;
	ability.name = "Test Ability";
	ability.kind = AbilityKind::Attack;
	ability.targeting = Targeting::OneEnemy;
	Effect effect;
	effect.kind = kind;
	effect.status = status;
	effect.chance = 100;
	ability.effects = {effect};
	db.abilities[id] = ability;
}

void actFirst(Battle& battle, int actor) {
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		battle.combatant(i).stats.agility = i == actor ? 900 : 1;
	}
}

bool has(const std::vector<Event>& events, EventType type, const std::string& status, int target) {
	for (const Event& e : events) {
		if (e.type == type && e.status == status && (e.target == target || e.actor == target)) {
			return true;
		}
	}
	return false;
}

}

ACC_CHECK(acc_bat_07, "ACC-BAT-07", "BAT-081") {
	Database db = support::data(t);
	int tested = 0;
	for (const auto& [id, status] : support::data(t).statuses) {
		if (id == kDefeatedStatus) {
			continue;
		}
		++tested;
		addAbility(db, "acc_inflict", EffectKind::ApplyStatus, id);
		db.enemies["rust_hound"].statusResist.clear();

		// Applies, then blocks or prevents the target's action in the same round, ticks, and counts down.
		BattleSetup s = support::setup(db, {"rust_hound"}, 40 + static_cast<std::uint64_t>(tested));
		Battle battle = support::create(db, s);
		battle.combatant(0).basicAttack = "acc_inflict";
		support::sturdy(battle, kFirst);
		actFirst(battle, 0);
		battle.setCommand(0, Command::attack(kFirst));
		battle.setCommand(kFirst, Command::ability("venom_spit", 1));
		support::othersDefend(battle);
		std::vector<Event> events = battle.resolveRound();
		t.expect(has(events, EventType::StatusApplied, id, kFirst), id + ": a 100% application did not apply");

		bool recovered = has(events, EventType::StatusExpired, id, kFirst);
		if (status.skipsTurn) {
			t.expect(has(events, EventType::ActionPrevented, id, kFirst) || (status.recoverChance > 0 && recovered), id + ": the target still acted while the status skips its turn");
		}
		if (status.blocksTechniques && !status.skipsTurn) {
			bool silenced = false;
			for (const Event& e : events) {
				silenced = silenced || (e.type == EventType::ActionFailed && e.actor == kFirst && e.fail == FailReason::Silenced);
			}
			t.expect(silenced, id + ": the target used a technique while the status blocks techniques");
		}
		if (!status.skipsTurn && !status.blocksTechniques) {
			t.expect(support::countType(events, EventType::ActionPrevented) == 0, id + ": an action was prevented by a status whose data does not skip turns");
		}
		const int tick = formulas::statusTick(battle.combatant(kFirst).stats.maxHp, status.tickDamagePercent);
		int expiredRound = 0;
		for (int round = 1; round <= 12 && battle.outcome() == Outcome::Ongoing; ++round) {
			if (round > 1) {
				support::othersDefend(battle);
				events = battle.resolveRound();
				recovered = recovered || has(events, EventType::StatusExpired, id, kFirst);
			}
			bool ticked = false;
			for (const Event& e : events) {
				ticked = ticked || (e.type == EventType::StatusTicked && e.status == id && e.target == kFirst && e.amount == tick);
				if (e.type == EventType::StatusExpired && e.status == id && e.statusEnd == StatusEnd::Duration) {
					expiredRound = round;
				}
			}
			const bool active = battle.combatant(kFirst).hasStatus(id) || expiredRound == round;
			if (status.tickDamagePercent > 0 && active) {
				t.expect(ticked, id + ": no tick of " + std::to_string(tick) + " at the end of round " + std::to_string(round));
			}
			if (status.tickDamagePercent == 0) {
				t.expect(!ticked, id + ": ticked although its data has no tick");
			}
			if (!battle.combatant(kFirst).hasStatus(id)) {
				break;
			}
		}
		if (status.duration > 0 && expiredRound > 0) {
			t.expect(expiredRound == status.duration, id + ": expired after " + std::to_string(expiredRound) + " rounds, data says " + std::to_string(status.duration));
		} else if (status.duration > 0) {
			t.expect(recovered, id + ": never expired although its data lasts " + std::to_string(status.duration) + " rounds");
		}

		// Wakes on damage, per data.
		{
			Battle woken = support::create(db, support::setup(db, {"rust_hound"}, 7));
			support::sturdy(woken, kFirst);
			woken.combatant(kFirst).statuses.push_back({id, 0});
			woken.combatant(kFirst).stats.evasion = 0;
			woken.combatant(1).stats.mental = 50;
			actFirst(woken, 1);
			woken.setCommand(1, Command::ability("spark", kFirst));
			support::othersDefend(woken);
			const std::vector<Event> hit = woken.resolveRound();
			bool woke = false;
			for (const Event& e : hit) {
				woke = woke || (e.type == EventType::StatusExpired && e.status == id && e.statusEnd == StatusEnd::WokeOnDamage);
			}
			t.expect(woke == status.wakesOnDamage, id + (status.wakesOnDamage ? ": damage did not end a status that wakes on damage" : ": damage ended a status that does not wake on damage"));
		}

		// Cured by a cure effect.
		{
			addAbility(db, "acc_cure", EffectKind::CureStatus, id);
			db.abilities["acc_cure"].targeting = Targeting::OneAlly;
			Battle cured = support::create(db, support::setup(db, {"rust_hound"}, 8));
			cured.combatant(3).basicAttack = "acc_cure";
			cured.combatant(0).statuses.push_back({id, 0});
			actFirst(cured, 3);
			cured.setCommand(3, Command::attack(0));
			support::othersDefend(cured);
			t.expect(has(cured.resolveRound(), EventType::StatusCured, id, 0) && !cured.combatant(0).hasStatus(id), id + ": a cure effect did not remove the status");
		}
	}

	t.gap("status data has no duration rule for 'until battle end' or 'persists outside battle', and PartyMember carries no statuses into or out of battle (BAT-081)");
	t.gap("status data has no 'forced command' action rule, no stacking rule (reapplying always replaces) and no 'counts toward Defeat' flag (BAT-081)");
	t.note(std::to_string(tested) + " statuses from core/data apply, prevent or block actions, tick, count down, wake and cure exactly per their data");
}
