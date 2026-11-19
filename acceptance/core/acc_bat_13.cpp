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
	bool itemRewards = false;
	for (std::uint64_t seed = 1; seed <= 1000; ++seed) {
		const support::PlayedBattle played = support::playRandomBattle(db, seed, log);
		support::BattleState state = played.start;
		std::string problem;
		for (const Event& e : played.events) {
			seen.insert(e.type);
			problem = support::apply(state, e, static_cast<int>(played.setup.party.size()));
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
			bool formation = b.partyCount == static_cast<int>(played.setup.party.size()) && b.ids.size() == played.setup.party.size() + played.setup.enemies.size() && b.groups.size() == b.ids.size();
			for (std::size_t i = 0; formation && i < played.setup.party.size(); ++i) {
				formation = b.ids[i] == played.setup.party[i].characterId;
			}
			for (std::size_t i = 0; formation && i < played.setup.enemies.size(); ++i) {
				formation = b.ids[played.setup.party.size() + i] == played.setup.enemies[i].enemy && b.groups[played.setup.party.size() + i] == played.setup.enemies[i].group;
			}
			t.expect(formation, "battle " + std::to_string(seed) + ": BattleStarted does not list the combatants and formation of the setup");
			t.expect(b.opening == played.setup.opening && b.flags.escapeAllowed == played.setup.flags.escapeAllowed && b.flags.defeatHasBranch == played.setup.flags.defeatHasBranch,
				"battle " + std::to_string(seed) + ": BattleStarted does not carry the setup's opening and flags");
		}
		for (const Event& e : played.events) {
			if (e.type == EventType::StatModified) {
				t.expect(e.duration > 0, "battle " + std::to_string(seed) + ": StatModified carries no duration");
			}
			if (e.type == EventType::BattleEnded) {
				++ended;
				t.expect(e.experience == played.rewards.experience && e.currency == played.rewards.currency && e.gameOver == played.gameOver,
					"battle " + std::to_string(seed) + ": BattleEnded carries rewards " + std::to_string(e.experience) + "/" + std::to_string(e.currency) + ", the battle pays " +
					std::to_string(played.rewards.experience) + "/" + std::to_string(played.rewards.currency));
				const std::string text = serializeEvent(e);
				itemRewards = itemRewards || text.find("item") != std::string::npos || text.find("drop") != std::string::npos;
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
		EventType::StatusResisted, EventType::StatusRemoved, EventType::StatusTicked, EventType::StatModified, EventType::CombatantDefeated, EventType::Revived, EventType::ComboTriggered,
		EventType::ComboDiscovered, EventType::EscapeAttempted, EventType::Message, EventType::RoundEnded, EventType::BattleEnded};
	std::string unseen;
	for (EventType type : required) {
		if (!seen.count(type)) {
			unseen += std::string(unseen.empty() ? "" : ", ") + toString(type);
		}
	}
	if (!itemRewards) {
		t.gap("BattleEnded carries experience and currency but no item rewards, which BAT-115 lists in its payload; core has no item drops, whose data is still to measure in SAV-031 (BAT-115)");
	}
	t.note("1000 random battles (" + std::to_string(events) + " events, " + std::to_string(seen.size()) + " event types, " + std::to_string(ended) +
		" ended): the start snapshot plus the stream reproduces hit points (each Damaged, Healed, tick and revive carries the resulting hit points), technique points, skill uses, statuses, modifiers, inventory and outcome exactly; BattleStarted carries the combatants, formation, opening and flags; BattleEnded the rewards paid; CommandsLocked the macro" +
		(unseen.empty() ? "; every BAT-115 event type occurred" : "; BAT-115 types never produced by the random battles: " + unseen));
}
