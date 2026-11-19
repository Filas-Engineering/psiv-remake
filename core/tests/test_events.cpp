#include "fixtures.h"
#include "rules/auto_policy.h"
#include "rules/names.h"
#include "rules/text.h"
#include "test.h"

#include <algorithm>
#include <map>
#include <set>

using namespace rules;
using namespace fixtures;

namespace {

// The state the event stream must reproduce (BAT-111).
struct State {
	std::vector<int> hp;
	std::vector<int> tp;
	std::vector<std::map<std::string, int>> uses;
	std::vector<std::set<std::string>> statuses;
	std::vector<std::map<Stat, int>> modifiers;
	Inventory inventory;
	Outcome outcome = Outcome::Ongoing;
};

State capture(const Battle& battle) {
	State s;
	for (const Combatant& c : battle.combatants()) {
		s.hp.push_back(c.hp);
		s.tp.push_back(c.tp);
		s.uses.push_back(c.skillUses);
		std::set<std::string> ids;
		for (const ActiveStatus& active : c.statuses) {
			ids.insert(active.id);
		}
		s.statuses.push_back(ids);
		std::map<Stat, int> mods;
		for (const ActiveModifier& m : c.modifiers) {
			mods[m.stat] = m.percent;
		}
		s.modifiers.push_back(mods);
	}
	s.inventory = battle.inventory();
	s.outcome = battle.outcome();
	return s;
}

// Returns an empty string, or why the event does not fit the state it applies to.
std::string apply(State& s, const Event& e) {
	switch (e.type) {
	case EventType::ResourceSpent:
		if (e.resource == ResourceKind::Tp) {
			s.tp[e.actor] -= e.amount;
		} else if (e.resource == ResourceKind::SkillUse) {
			s.uses[e.actor][e.ability] -= e.amount;
		} else {
			s.inventory[e.ability] -= e.amount;
		}
		return "";
	case EventType::DamageDealt:
	case EventType::StatusTicked:
		if (s.hp[e.target] - e.amount != e.hp) {
			return std::string(toString(e.type)) + " hp does not follow from the amount";
		}
		s.hp[e.target] = e.hp;
		return "";
	case EventType::Healed:
		if (s.hp[e.target] + e.amount != e.hp) {
			return "Healed hp does not follow from the amount";
		}
		s.hp[e.target] = e.hp;
		return "";
	case EventType::Revived:
		s.hp[e.target] = e.hp;
		s.statuses[e.target].erase(kDefeatedStatus);
		return "";
	case EventType::StatusApplied:
		s.statuses[e.target].insert(e.status);
		return "";
	case EventType::StatusRemoved:
		return s.statuses[e.target].erase(e.status) ? "" : "StatusRemoved for a status the target lacks";
	case EventType::StatModified:
		s.modifiers[e.target][e.stat] = e.amount;
		return "";
	case EventType::StatModifierExpired:
		return s.modifiers[e.target].erase(e.stat) ? "" : "StatModifierExpired for a modifier the target lacks";
	case EventType::CombatantDefeated:
		if (!s.statuses[e.target].empty()) {
			return "CombatantDefeated while statuses remain without a removal event";
		}
		s.hp[e.target] = 0;
		s.statuses[e.target] = {kDefeatedStatus};
		s.modifiers[e.target].clear();
		return "";
	case EventType::BattleEnded:
		s.outcome = e.outcome;
		return "";
	default:
		return "";
	}
}

}

TEST(firstRoundOpensWithTheBattleAndItsCommandPhase) {
	BattleSetup setup = makeSetup(data(), {"gel_mite", "rust_hound"}, 3);
	setup.opening = Opening::PartyFirst;
	setup.flags.defeatHasBranch = true;
	Battle battle = makeBattle(data(), setup);
	battle.applyMacro(*data().macro("opener"));
	const std::vector<Event> events = battle.resolveRound();
	CHECK(events.size() > 5);
	if (events.size() <= 5) {
		return;
	}
	const Event& started = events[0];
	CHECK(started.type == EventType::BattleStarted && started.round == 0);
	CHECK_EQ(started.ids, (std::vector<std::string>{"fighter", "mystic", "scout", "medic", "engineer", "gel_mite", "rust_hound"}));
	CHECK_EQ(started.groups, (std::vector<std::string>{"party", "party", "party", "party", "party", "gel_mite", "rust_hound"}));
	CHECK_EQ(started.partyCount, 5);
	CHECK(started.opening == Opening::PartyFirst && started.flags.defeatHasBranch && started.flags.escapeAllowed);
	CHECK(events[1].type == EventType::Message && events[1].message == "battle.opening.party_first");
	CHECK(events[2].type == EventType::RoundStarted && events[2].round == 1);
	const Event& locked = events[3];
	CHECK(locked.type == EventType::CommandsLocked && locked.macro == "opener");
	// The enemies lost this round to the opening, so only the party has commands.
	CHECK_EQ(locked.commands.size(), std::size_t(5));
	CHECK(!locked.commands.empty() && locked.commands[1].actor == kMystic && locked.commands[1].command.id == "spark");
	CHECK(events[4].type == EventType::OrderResolved && events[4].order == battle.lastTurnOrder());

	partyDefends(battle);
	std::size_t living = 0;
	for (const Combatant& c : battle.combatants()) {
		living += c.alive() ? 1 : 0;
	}
	const std::vector<Event> next = battle.resolveRound();
	CHECK(next.front().type == EventType::RoundStarted);
	const std::vector<Event> nextLocked = ofType(next, EventType::CommandsLocked);
	CHECK(nextLocked.size() == 1 && nextLocked[0].macro.empty() && nextLocked[0].commands.size() == living);
	CHECK(ofType(next, EventType::BattleStarted).empty());
}

TEST(retargetIsAnEventBeforeTheAction) {
	Database db = data();
	db.characters["mystic"].base.agility = 300;
	db.characters["scout"].base.agility = 200;
	Battle battle = makeBattle(db, {"gel_mite", "gel_mite", "rust_hound"}, 21);
	battle.combatant(kFirstEnemy + 1).hp = 1;
	battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy + 1));
	battle.setCommand(kScout, Command::ability("weaken", kFirstEnemy + 1));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	const auto retarget = std::find_if(events.begin(), events.end(), [](const Event& e) { return e.type == EventType::Retargeted; });
	CHECK(retarget != events.end());
	if (retarget == events.end()) {
		return;
	}
	CHECK(retarget->actor == kScout && retarget->previousTarget == kFirstEnemy + 1 && retarget->target == kFirstEnemy + 2);
	CHECK((retarget + 1)->type == EventType::ActionStarted && (retarget + 1)->target == kFirstEnemy + 2);
}

TEST(hitsAndResultsCarryTheResultingHitPoints) {
	Database db = data();
	db.characters["fighter"].base.dexterity = 500;
	db.characters["fighter"].base.critical = 100;
	Battle battle = makeBattle(db, {"gel_mite"}, 2);
	battle.combatant(kFirstEnemy).hp = 500;
	battle.combatant(kMystic).hp = 10;
	battle.setCommand(kFighter, Command::attack(kFirstEnemy));
	battle.setCommand(kMedic, Command::ability("mend", kMystic));
	battle.setCommand(kScout, Command::ability("weaken", kFirstEnemy));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();

	const auto hit = std::find_if(events.begin(), events.end(), [](const Event& e) { return e.type == EventType::Hit; });
	CHECK(hit != events.end() && events.end() - hit >= 3);
	if (hit != events.end() && events.end() - hit >= 3) {
		CHECK(hit->actor == kFighter && hit->target == kFirstEnemy && hit->ability == "strike");
		CHECK((hit + 1)->type == EventType::Critical && (hit + 1)->ability == "strike");
		CHECK((hit + 2)->type == EventType::DamageDealt && (hit + 2)->actor == kFighter);
		CHECK_EQ((hit + 2)->hp, 500 - (hit + 2)->amount);
	}
	const std::vector<Event> heals = ofType(events, EventType::Healed);
	CHECK(heals.size() == 1 && heals[0].hp == 10 + heals[0].amount && heals[0].hp == battle.combatant(kMystic).hp);
	const std::vector<Event> modified = ofType(events, EventType::StatModified);
	CHECK(modified.size() == 1 && modified[0].duration == 3 && modified[0].amount == -30);
}

TEST(battleEndedCarriesTheRewards) {
	Database db = data();
	db.characters["fighter"].base.dexterity = 500;
	Battle battle = makeBattle(db, {"rust_hound"}, 1);
	battle.combatant(kFirstEnemy).hp = 1;
	battle.setCommand(kFighter, Command::attack(kFirstEnemy));
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	CHECK(battle.outcome() == Outcome::Victory);
	const Event& ended = events.back();
	CHECK(ended.type == EventType::BattleEnded && ended.outcome == Outcome::Victory && !ended.gameOver);
	CHECK_EQ(ended.experience, battle.rewards().experience);
	CHECK_EQ(ended.currency, battle.rewards().currency);
	CHECK_EQ(ended.experience, 10);
}

TEST(anActorThatFellBeforeItsTurnIsCancelled) {
	Database db = data();
	db.enemies["brass_warden"].stats.agility = 900;
	db.enemies["brass_warden"].stats.dexterity = 5000;
	Battle battle = makeBattle(db, {"brass_warden"}, 28);
	battle.combatant(kMystic).hp = 1;
	battle.setCommand(kFirstEnemy, Command::attack(kMystic));
	battle.setCommand(kMystic, Command::ability("spark", kFirstEnemy));
	othersDefend(battle);
	const std::optional<Event> cancelled = firstBy(battle.resolveRound(), EventType::ActionCancelled, kMystic);
	CHECK(cancelled && cancelled->cancel == CancelReason::ActorUnable && cancelled->status == kDefeatedStatus && cancelled->ability == "spark");
}

TEST(defeatRemovesEveryStatusWithAnEvent) {
	Battle battle = makeBattle(data(), {"gel_mite"}, 1);
	battle.combatant(kFighter).hp = 2;
	addStatus(battle, kFighter, "poison", 4);
	addStatus(battle, kFighter, "silence", 3);
	othersDefend(battle);
	const std::vector<Event> events = battle.resolveRound();
	std::vector<EventType> tail;
	std::vector<std::string> removed;
	bool ticked = false;
	for (const Event& e : events) {
		if (e.target != kFighter) {
			continue;
		}
		ticked = ticked || e.type == EventType::StatusTicked;
		if (ticked) {
			tail.push_back(e.type);
		}
		if (e.type == EventType::StatusRemoved) {
			CHECK(e.statusEnd == StatusEnd::Defeated);
			CHECK_EQ(e.message, statusMessageKey(e.status, "removed"));
			removed.push_back(e.status);
		}
	}
	CHECK_EQ(tail, (std::vector<EventType>{EventType::StatusTicked, EventType::StatusRemoved, EventType::StatusRemoved, EventType::CombatantDefeated}));
	CHECK_EQ(removed, (std::vector<std::string>{"poison", "silence"}));
}

TEST(statusEventsCarryMessageKeys) {
	Battle battle = makeBattle(data(), {"brass_warden"}, 1);
	battle.setCommand(kEngineer, Command::ability("drowse", kFirstEnemy));
	battle.setCommand(kMystic, Command::ability("hush", kFirstEnemy));
	othersDefend(battle);
	for (const Event& e : battle.resolveRound()) {
		if (e.type == EventType::StatusResisted && e.status == "sleep") {
			CHECK_EQ(e.message, std::string("status.sleep.immune"));
		}
		if (e.type == EventType::StatusApplied) {
			CHECK_EQ(e.message, std::string("status.silence.applied"));
		}
		if (e.type == EventType::StatusResisted && e.status == "silence") {
			CHECK_EQ(e.message, std::string("status.silence.resisted"));
		}
	}
}

// Over many played battles, the start state plus every event reproduces the end state, and every status
// event carries a key core lists.
TEST(eventStreamReproducesTheEndState) {
	const Database& db = data();
	const std::vector<std::string> keys = messageKeys(db);
	const std::vector<std::string> encounters = {"training", "marsh", "warden"};
	int battles = 0;
	for (std::uint64_t seed = 1; seed <= 60; ++seed) {
		BattleSetup setup = makeSetup(db, {}, seed);
		useEncounter(setup, db, encounters[seed % 3]);
		setup.difficulty = seed % 2 ? "hard" : "original";
		for (PartyMember& member : setup.party) {
			member.level = 1 + static_cast<int>(seed % 7);
		}
		ComboLog log;
		Battle battle = makeBattle(db, setup, &log);
		State state = capture(battle);
		std::string problem;
		for (int round = 0; round < 40 && battle.outcome() == Outcome::Ongoing; ++round) {
			for (int i = 0; i < battle.partyCount(); ++i) {
				if (battle.combatant(i).alive() && battle.sideActsNextRound(Side::Party)) {
					battle.setCommand(i, round % 4 == 3 ? Command::item("tonic", i) : autoCommand(battle, i));
				}
			}
			for (const Event& e : battle.resolveRound()) {
				const std::string why = apply(state, e);
				problem = problem.empty() ? why : problem;
				if (e.type == EventType::StatusApplied || e.type == EventType::StatusResisted || e.type == EventType::StatusRemoved) {
					CHECK(std::binary_search(keys.begin(), keys.end(), e.message));
				}
			}
		}
		const State end = capture(battle);
		CHECK_EQ(problem, std::string());
		CHECK(state.hp == end.hp && state.tp == end.tp && state.uses == end.uses);
		CHECK(state.statuses == end.statuses && state.modifiers == end.modifiers);
		CHECK(state.inventory == end.inventory && state.outcome == end.outcome);
		++battles;
	}
	CHECK_EQ(battles, 60);
}

TEST(eventTypesHaveStableNames) {
	Event cancelled;
	cancelled.type = EventType::ActionCancelled;
	cancelled.round = 2;
	cancelled.actor = 1;
	cancelled.ability = "spark";
	cancelled.command = CommandKind::Ability;
	cancelled.cancel = CancelReason::ActorUnable;
	cancelled.status = "sleep";
	CHECK_EQ(serializeEvent(cancelled), std::string("2 ActionCancelled actor=1 ability=spark status=sleep command=ability cancel=actor_unable"));

	Event locked;
	locked.type = EventType::CommandsLocked;
	locked.round = 1;
	locked.macro = "opener";
	locked.commands = {{0, Command::attack(5)}, {1, Command::ability("spark", -1)}};
	CHECK_EQ(serializeEvent(locked), std::string("1 CommandsLocked macro=opener commands=0:attack:-:5,1:ability:spark:-1"));

	Event damage;
	damage.type = EventType::DamageDealt;
	damage.round = 3;
	damage.target = 5;
	damage.amount = 12;
	damage.hp = 0;
	CHECK_EQ(serializeEvent(damage), std::string("3 DamageDealt target=5 amount=12 hp=0"));
}
