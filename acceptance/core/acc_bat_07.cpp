#include "support.h"

#include "rules/formulas.h"
#include "rules/names.h"
#include "rules/progression.h"
#include "rules/text.h"

#include <cstdlib>
#include <limits>

using namespace rules;

namespace {

constexpr int kFirst = 5;

// A placeholder ability that does one effect, so a status can be applied or cured on demand.
void addAbility(Database& db, const std::string& id, EffectKind kind, const std::string& status, Targeting targeting) {
	AbilityDef ability;
	ability.id = id;
	ability.name = "Test Ability";
	ability.kind = AbilityKind::Attack;
	ability.targeting = targeting;
	Effect effect;
	effect.kind = kind;
	effect.status = status;
	effect.threshold = std::numeric_limits<int>::min();
	ability.effects = {effect};
	db.abilities[id] = ability;
}

void actFirst(Battle& battle, int actor) {
	for (int i = 0; i < static_cast<int>(battle.combatants().size()); ++i) {
		battle.combatant(i).stats.agility = i == actor ? 900 : 1;
	}
}

const Event* find(const std::vector<Event>& events, EventType type, const std::string& status, int target) {
	for (const Event& e : events) {
		if (e.type == type && e.status == status && (e.target == target || e.actor == target)) {
			return &e;
		}
	}
	return nullptr;
}

int remaining(const Combatant& c, const std::string& status) {
	for (const ActiveStatus& s : c.statuses) {
		if (s.id == status) {
			return s.remaining;
		}
	}
	return -1;
}

const CommandKind* lockedKind(const std::vector<Event>& events, int actor) {
	for (const Event& e : events) {
		if (e.type == EventType::CommandsLocked) {
			for (const RecordedCommand& c : e.commands) {
				if (c.actor == actor) {
					return &c.command.kind;
				}
			}
		}
	}
	return nullptr;
}

bool inPool(const Battle& battle, ForcedTarget pool, int actor, int target) {
	if (target == actor || target < 0) {
		return false;
	}
	const bool ally = battle.isParty(actor) == battle.isParty(target);
	switch (pool) {
	case ForcedTarget::RandomAny:
		return true;
	case ForcedTarget::RandomAlly:
		return ally;
	case ForcedTarget::RandomFoe:
		return !ally;
	}
	return false;
}

}

ACC_CHECK(acc_bat_07, "ACC-BAT-07", "BAT-081") {
	Database db = support::data(t);
	db.enemies["rust_hound"].statusResist.clear();
	addAbility(db, "acc_cure", EffectKind::CureStatus, "", Targeting::OneAlly);
	int tested = 0;
	std::map<std::string, int> rules;
	for (const auto& [id, status] : support::data(t).statuses) {
		if (id == kDefeatedStatus) {
			continue;
		}
		++tested;
		addAbility(db, "acc_inflict", EffectKind::ApplyStatus, id, Targeting::OneEnemy);

		// Applies, then acts on the target's own command in the same round per its action rule.
		BattleSetup s = support::setup(db, {"rust_hound", "rust_hound"}, 40 + static_cast<std::uint64_t>(tested));
		Battle battle = support::create(db, s);
		battle.combatant(0).basicAttack = "acc_inflict";
		support::sturdy(battle, kFirst);
		support::sturdy(battle, kFirst + 1);
		actFirst(battle, 0);
		battle.setCommand(0, Command::attack(kFirst));
		battle.setCommand(kFirst, Command::ability("venom_spit", 1));
		support::othersDefend(battle);
		std::vector<Event> events = battle.resolveRound();
		// The target is an enemy, so enemy_duration replaces duration when it is set.
		const int duration = status.enemyDuration > 0 ? status.enemyDuration : status.duration;
		const Event* applied = find(events, EventType::StatusApplied, id, kFirst);
		t.expect(applied && applied->duration == duration, id + ": a certain application did not apply with the data's duration for an enemy");

		bool recovered = find(events, EventType::StatusRemoved, id, kFirst) != nullptr;
		if (status.action == StatusAction::CannotAct) {
			const Event* cancelled = nullptr;
			for (const Event& e : events) {
				if (e.type == EventType::ActionCancelled && e.actor == kFirst) {
					cancelled = &e;
				}
			}
			t.expect(cancelled && cancelled->cancel == CancelReason::ActorUnable && cancelled->status == id,
				id + ": the target was not cancelled as unable to act while the status says cannot_act");
			++rules["cannot act"];
		} else if (status.blocksTechniques) {
			bool silenced = false;
			for (const Event& e : events) {
				silenced = silenced || (e.type == EventType::ActionCancelled && e.actor == kFirst && e.cancel == CancelReason::Silenced);
			}
			t.expect(silenced, id + ": the target used a technique while the status blocks techniques");
			++rules["blocks techniques"];
		} else if (status.action == StatusAction::Normal) {
			bool cancelled = false;
			for (const Event& e : events) {
				cancelled = cancelled || (e.type == EventType::ActionCancelled && e.actor == kFirst);
			}
			t.expect(!cancelled, id + ": an action was cancelled by a status whose action rule is normal");
			++rules["normal"];
		}

		// Forced command: the battle supplies a basic attack on a random target from the status's pool in phase 2.
		if (status.action == StatusAction::Forced) {
			++rules["forced"];
			int forcedRounds = 0;
			int inPoolCount = 0;
			for (std::uint64_t seed = 1; seed <= 200; ++seed) {
				Battle forced = support::create(db, support::setup(db, {"rust_hound", "gel_mite"}, seed));
				for (int i = 0; i < static_cast<int>(forced.combatants().size()); ++i) {
					support::sturdy(forced, i);
				}
				forced.combatant(kFirst).statuses.push_back({id, 0});
				forced.setCommand(kFirst, Command::ability("venom_spit", 1));
				support::othersDefend(forced);
				const std::vector<Event> round = forced.resolveRound();
				const CommandKind* kind = lockedKind(round, kFirst);
				t.expect(kind && *kind == CommandKind::Attack, id + ": a forced status did not replace the command with a basic attack in phase 2");
				for (const Event& e : round) {
					if (e.type == EventType::ActionStarted && e.actor == kFirst) {
						++forcedRounds;
						inPoolCount += inPool(forced, status.forcedTarget, kFirst, e.target) ? 1 : 0;
					}
				}
			}
			t.expect(forcedRounds > 0 && inPoolCount == forcedRounds, id + ": " + std::to_string(forcedRounds - inPoolCount) + " of " + std::to_string(forcedRounds) + " forced attacks hit a target outside the " + toString(status.forcedTarget) + " pool");
		}

		// Ticks each round end, and counts down to expiry after its duration; no duration lasts until cured.
		const int tick = formulas::statusTick(battle.combatant(kFirst).stats.maxHp, status.tickDamagePercent);
		int expiredRound = 0;
		for (int round = 1; round <= 8 && battle.outcome() == Outcome::Ongoing; ++round) {
			if (round > 1) {
				support::othersDefend(battle);
				events = battle.resolveRound();
				recovered = recovered || find(events, EventType::StatusRemoved, id, kFirst);
			}
			const Event* ticked = find(events, EventType::StatusTicked, id, kFirst);
			if (const Event* removed = find(events, EventType::StatusRemoved, id, kFirst); removed && removed->statusEnd == StatusEnd::Duration) {
				expiredRound = round;
			}
			const bool active = battle.combatant(kFirst).hasStatus(id) || expiredRound == round;
			if (status.tickDamagePercent > 0 && active) {
				t.expect(ticked && ticked->amount == tick, id + ": no tick of " + std::to_string(tick) + " at the end of round " + std::to_string(round));
			}
			if (status.tickDamagePercent == 0) {
				t.expect(!ticked, id + ": ticked although its data has no tick");
			}
			if (!battle.combatant(kFirst).hasStatus(id)) {
				break;
			}
		}
		if (duration > 0 && expiredRound > 0) {
			t.expect(expiredRound == duration, id + ": expired after " + std::to_string(expiredRound) + " rounds, data says " + std::to_string(duration));
		} else if (duration > 0) {
			t.expect(recovered, id + ": never expired although its data lasts " + std::to_string(duration) + " rounds");
		} else if (status.recoverChance == 0 && !status.wakesOnDamage && status.tickDamagePercent == 0) {
			t.expect(battle.combatant(kFirst).hasStatus(id), id + ": a status with no round limit wore off by itself");
		}

		// Recovery: rolled at the end of each round, after the round's actions and ticks. Nobody's action draws here, so
		// the roll follows the initiative rolls.
		if (status.recoverChance > 0) {
			++rules["recover chance"];
			int recoveries = 0;
			int mismatches = 0;
			int early = 0;
			const int trials = 4000;
			for (int seed = 1; seed <= trials; ++seed) {
				const std::uint64_t battleSeed = static_cast<std::uint64_t>(seed) * 13 + 1;
				Battle rolled = support::create(db, support::setup(db, {"rust_hound"}, battleSeed));
				support::sturdy(rolled, kFirst);
				rolled.combatant(kFirst).statuses.push_back({id, 0});
				support::othersDefend(rolled);
				Rng rng(battleSeed);
				support::expectedOrder(rolled, rng);
				const bool expected = rng.chance(status.recoverChance);
				const std::vector<Event> round = rolled.resolveRound();
				const Event* removed = find(round, EventType::StatusRemoved, id, kFirst);
				const bool recoveredNow = removed && removed->statusEnd == StatusEnd::Recovered;
				recoveries += recoveredNow ? 1 : 0;
				mismatches += recoveredNow != expected ? 1 : 0;
				if (recoveredNow) {
					for (const Event* e = removed; e != round.data() + round.size(); ++e) {
						early += e->type == EventType::ActionStarted || e->type == EventType::ActionCancelled ? 1 : 0;
					}
				}
			}
			const double rate = 100.0 * recoveries / trials;
			t.expect(mismatches == 0, id + ": " + std::to_string(mismatches) + " of " + std::to_string(trials) + " recovery rolls disagree with the replayed draw");
			t.expect(early == 0, id + ": a recovery came before an action of the same round");
			t.expect(std::abs(rate - status.recoverChance) <= 3.0, id + ": recovered in " + std::to_string(rate) + "% of rounds, data says " + std::to_string(status.recoverChance) + "%");
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
			const Event* removed = find(woken.resolveRound(), EventType::StatusRemoved, id, kFirst);
			const bool woke = removed && removed->statusEnd == StatusEnd::WokeOnDamage;
			t.expect(woke == status.wakesOnDamage, id + (status.wakesOnDamage ? ": damage did not end a status that wakes on damage" : ": damage ended a status that does not wake on damage"));
		}

		// Cured by a cure effect.
		{
			db.abilities["acc_cure"].effects[0].status = id;
			Battle cured = support::create(db, support::setup(db, {"rust_hound"}, 8));
			support::sturdy(cured, kFirst);
			cured.combatant(3).basicAttack = "acc_cure";
			cured.combatant(0).statuses.push_back({id, 0});
			actFirst(cured, 3);
			cured.setCommand(3, Command::attack(0));
			support::othersDefend(cured);
			const Event* removed = find(cured.resolveRound(), EventType::StatusRemoved, id, 0);
			t.expect(removed && removed->statusEnd == StatusEnd::Cured && !cured.combatant(0).hasStatus(id), id + ": a cure effect did not remove the status");
		}

		// Stacking: reapplying to a target with one round left.
		{
			Battle stacked = support::create(db, support::setup(db, {"rust_hound"}, 9));
			stacked.combatant(0).basicAttack = "acc_inflict";
			support::sturdy(stacked, kFirst);
			actFirst(stacked, 0);
			stacked.combatant(kFirst).statuses.push_back({id, status.duration > 0 ? 1 : 0});
			stacked.setCommand(0, Command::attack(kFirst));
			support::othersDefend(stacked);
			const std::vector<Event> round = stacked.resolveRound();
			const Event* unchanged = find(round, EventType::StatusResisted, id, kFirst);
			const int left = remaining(stacked.combatant(kFirst), id);
			switch (status.stacking) {
			case StatusStacking::Keep:
				t.expect(unchanged && unchanged->message == statusMessageKey(id, "unchanged") && !find(round, EventType::StatusApplied, id, kFirst),
					id + ": reapplying a keep status did not leave it unchanged with the unchanged message");
				if (status.duration > 0) {
					t.expect(left == -1, id + ": reapplying a keep status reset its duration");
				}
				break;
			case StatusStacking::Refresh:
				if (status.duration > 0) {
					t.expect(left == status.duration - 1, id + ": reapplying a refresh status left " + std::to_string(left) + " rounds after round end, expected " + std::to_string(status.duration - 1));
				}
				break;
			case StatusStacking::Extend:
				if (status.duration > 0) {
					t.expect(left == std::min(99, 1 + status.duration) - 1, id + ": reapplying an extend status left " + std::to_string(left) + " rounds after round end, expected " + std::to_string(status.duration));
				}
				break;
			}
			++rules[std::string("stacking ") + toString(status.stacking)];
		}

		// Counts toward Defeat: a lone enemy held by it counts as down.
		{
			Battle held = support::create(db, support::setup(db, {"rust_hound"}, 10));
			support::sturdy(held, kFirst);
			for (int i = 0; i < held.partyCount(); ++i) {
				support::sturdy(held, i);
			}
			held.combatant(kFirst).statuses.push_back({id, 0});
			support::othersDefend(held);
			held.resolveRound();
			const bool ended = held.outcome() == Outcome::Victory;
			t.expect(ended == status.countsTowardDefeat, id + (status.countsTowardDefeat ? ": a lone enemy held by a status that counts toward Defeat did not end the battle" : ": a status that does not count toward Defeat ended the battle"));
		}

		// Duration rule across battles: only a persisting status leaves the battle on the party, and comes back in.
		{
			Battle ending = support::create(db, support::setup(db, {"gel_mite"}, 11));
			ending.combatant(0).statuses.push_back({id, 0});
			support::sturdy(ending, 0);
			ending.combatant(kFirst).hp = 1;
			for (int i = 0; i < ending.partyCount(); ++i) {
				ending.combatant(i).stats.dexterity = 400;
			}
			for (int round = 0; round < 5 && ending.outcome() == Outcome::Ongoing; ++round) {
				ending.setCommand(1, Command::attack(kFirst));
				support::othersDefend(ending);
				ending.resolveRound();
			}
			t.expect(ending.outcome() == Outcome::Victory, id + ": setup: the battle did not end");
			std::vector<PartyMember> after = ending.partyState();
			bool carried = false;
			for (const ActiveStatus& active : after[0].statuses) {
				carried = carried || active.id == id;
			}
			t.expect(carried == status.persistsOutsideBattle, id + (status.persistsOutsideBattle ? ": a status that persists outside battle was not carried out of it" : ": a status that ends with the battle was carried out of it"));
			BattleSetup next = support::setup(db, {"gel_mite"}, 12);
			next.party = after;
			if (!status.persistsOutsideBattle) {
				next.party[0].statuses.push_back({id, 0});
			}
			Result<Battle> again = Battle::create(db, next, nullptr);
			if (status.persistsOutsideBattle) {
				t.expect(again.ok() && again.value->combatant(0).hasStatus(id), id + ": a persisting status was not carried into the next battle");
				PartyMember rested = after[0];
				rest(rested, db);
				bool still = false;
				for (const ActiveStatus& active : rested.statuses) {
					still = still || active.id == id;
				}
				t.expect(still != status.restCures, id + (status.restCures ? ": rest did not cure a status whose data says rest cures it" : ": rest cured a status whose data says it does not"));
				++rules["persists outside battle"];
			} else {
				t.expect(!again.ok(), id + ": a battle accepted a party member carrying a status that ends with the battle");
			}
		}
	}

	std::string covered;
	for (const auto& [rule, count] : rules) {
		covered += (covered.empty() ? "" : ", ") + rule + " " + std::to_string(count);
	}
	t.note(std::to_string(tested) + " statuses from core/data apply with their duration, act per their action rule, tick, expire, recover, wake, cure, stack, count toward Defeat and persist across battles exactly per their data (" + covered + ")");
}
