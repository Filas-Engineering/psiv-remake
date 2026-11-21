#include "support.h"

#include "rules/names.h"

#include <set>

using namespace rules;

ACC_CHECK(acc_bat_13, "ACC-BAT-13", "BAT-110, BAT-111, BAT-115") {
	const Database& db = support::data(t);
	ComboLog log;
	std::set<EventType> seen;
	int events = 0;
	int ended = 0;
	int withDrops = 0;
	for (std::uint64_t seed = 1; seed <= 1000; ++seed) {
		const support::PlayedBattle played = support::playRandomBattle(db, seed, log);
		support::BattleState state = played.start;
		std::string problem;
		for (const Event& e : played.events) {
			seen.insert(e.type);
			problem = support::apply(db, state, e, static_cast<int>(played.setup.party.size()));
			if (!problem.empty()) {
				break;
			}
		}
		events += static_cast<int>(played.events.size());
		t.expect(problem.empty(), "battle " + std::to_string(seed) + ": " + problem);
		const std::string diff = support::difference(state, played.end);
		t.expect(diff.empty(), "battle " + std::to_string(seed) + ": start snapshot plus event stream differs from the end state: " + diff);

		// BAT-115 payloads the state replay does not cover.
		const std::vector<Event> started = support::ofType(played.events, EventType::BattleStarted);
		if (t.expect(started.size() == 1 && played.events.front().type == EventType::BattleStarted, "battle " + std::to_string(seed) + ": the stream does not open with one BattleStarted")) {
			const Event& b = started[0];
			// Free places in the formation hold no combatant.
			std::vector<FormationSlot> standing;
			for (const FormationSlot& slot : played.setup.enemies) {
				if (!slot.enemy.empty()) {
					standing.push_back(slot);
				}
			}
			bool formation = b.partyCount == static_cast<int>(played.setup.party.size()) && b.ids.size() == played.setup.party.size() + standing.size() && b.groups.size() == b.ids.size();
			for (std::size_t i = 0; formation && i < played.setup.party.size(); ++i) {
				formation = b.ids[i] == played.setup.party[i].characterId;
			}
			for (std::size_t i = 0; formation && i < standing.size(); ++i) {
				formation = b.ids[played.setup.party.size() + i] == standing[i].enemy && b.groups[played.setup.party.size() + i] == standing[i].group;
			}
			t.expect(formation, "battle " + std::to_string(seed) + ": BattleStarted does not list the combatants and formation of the setup");
			t.expect(b.opening == played.setup.opening && b.flags.escapeAllowed == played.setup.flags.escapeAllowed && b.flags.defeatHasBranch == played.setup.flags.defeatHasBranch,
				"battle " + std::to_string(seed) + ": BattleStarted does not carry the setup's opening and flags");
		}
		for (const Event& e : played.events) {
			if (e.type == EventType::StatModified) {
				t.expect(e.duration == 0, "battle " + std::to_string(seed) + ": StatModified carries a duration, though stat changes last until the battle ends");
			}
			if (e.type == EventType::BattleEnded) {
				++ended;
				t.expect(e.experience == played.rewards.experience && e.currency == played.rewards.currency && e.gameOver == played.gameOver,
					"battle " + std::to_string(seed) + ": BattleEnded carries rewards " + std::to_string(e.experience) + "/" + std::to_string(e.currency) + ", the battle pays " +
					std::to_string(played.rewards.experience) + "/" + std::to_string(played.rewards.currency));
				// BattleEnded lists item and equipment drops together.
				Inventory paid = played.rewards.items;
				paid.insert(played.rewards.equipment.begin(), played.rewards.equipment.end());
				t.expect(e.items == paid, "battle " + std::to_string(seed) + ": BattleEnded carries other item drops than rewards() pays");
				withDrops += e.items.empty() ? 0 : 1;
				// A drop is one of the encounter's item or piece, and only after a Victory.
				const ItemDrop& drop = db.encounter(played.setup.enemyGroup)->drop;
				const bool allowed = e.items.empty() || (e.outcome == Outcome::Victory && e.items == Inventory{{drop.equipment.empty() ? drop.item : drop.equipment, 1}});
				t.expect(allowed, "battle " + std::to_string(seed) + ": dropped something other than one of the encounter's item after a Victory");
			}
		}
	}

	// CommandsLocked names the macro that set the party's commands.
	{
		Battle battle = support::create(db, support::setup(db, {"rust_hound"}, 8, 6));
		battle.applyMacro(db.macros.front());
		const std::vector<Event> locked = support::ofType(battle.resolveRound(), EventType::CommandsLocked);
		t.expect(locked.size() == 1 && locked[0].macro == db.macros.front().name, "CommandsLocked does not name the macro used for the round");
	}

	// The renderer only reads the returned events; resolving the same round twice from identical battles agrees.
	{
		Battle a = support::create(db, support::setup(db, {"rust_hound", "gel_mite"}, 77));
		Battle b = support::create(db, support::setup(db, {"rust_hound", "gel_mite"}, 77));
		const std::vector<Event> first = a.resolveRound();
		t.expect(serializeEvents(first) == serializeEvents(b.resolveRound()), "two identical battles returned different event lists for the same round");
		t.expect(!first.empty() && first.back().type == EventType::RoundEnded && support::countType(first, EventType::RoundStarted) == 1, "resolveRound did not return the whole round");
	}

	const std::vector<EventType> required = {EventType::BattleStarted, EventType::RoundStarted, EventType::CommandsLocked, EventType::OrderResolved, EventType::ActionStarted, EventType::ActionCancelled,
		EventType::Retargeted, EventType::ResourceSpent, EventType::Hit, EventType::Missed, EventType::Critical, EventType::DamageDealt, EventType::Healed, EventType::StatusApplied,
		EventType::StatusResisted, EventType::StatusRemoved, EventType::StatModified, EventType::CombatantDefeated, EventType::Revived, EventType::ComboTriggered,
		EventType::ComboDiscovered, EventType::EscapeAttempted, EventType::Message, EventType::RoundEnded, EventType::BattleEnded};
	std::string unseen;
	for (EventType type : required) {
		if (!seen.count(type)) {
			unseen += std::string(unseen.empty() ? "" : ", ") + toString(type);
		}
	}
	// Drops are drawn from the battle's own generator, so a replayed record drops the same items.
	{
		const support::PlayedBattle played = support::playRandomBattle(db, 4242, log);
		ComboLog replayLog;
		std::vector<Event> replayed;
		Result<Battle> again = replayBattle(db, played.record, replayLog, replayed);
		const std::vector<Event> a = support::ofType(played.events, EventType::BattleEnded);
		const std::vector<Event> b = support::ofType(replayed, EventType::BattleEnded);
		t.expect(again.ok() && a.size() == 1 && b.size() == 1 && a[0].items == b[0].items, "a replayed battle record does not drop the same items");
	}
	t.expect(withDrops > 0, "no random battle dropped an item, so BattleEnded's item rewards were never seen");
	t.note("1000 random battles (" + std::to_string(events) + " events, " + std::to_string(seen.size()) + " event types, " + std::to_string(ended) +
		" ended): the start snapshot plus the stream reproduces hit points (each Damaged, Healed and revive carries the resulting hit points), technique points, skill uses, statuses, modifiers, inventory and outcome exactly; BattleStarted carries the combatants, formation, opening and flags; BattleEnded the rewards paid, item drops included (" + std::to_string(withDrops) + " battles dropped one of the encounter's item, each after a Victory, the same on replay); CommandsLocked the macro" +
		(unseen.empty() ? "; every BAT-115 event type occurred" : "; BAT-115 types never produced by the random battles: " + unseen));
}
