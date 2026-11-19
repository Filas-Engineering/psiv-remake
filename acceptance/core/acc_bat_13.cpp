#include "support.h"

#include "rules/names.h"

#include <set>

using namespace rules;

ACC_CHECK(acc_bat_13, "ACC-BAT-13", "BAT-110, BAT-111, BAT-115") {
	const Database& db = support::data(t);
	ComboLog log;
	std::set<EventType> seen;
	int events = 0;
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
	}

	// The renderer only reads the returned events; resolving the same round twice from identical battles agrees.
	{
		Battle a = support::create(db, support::setup(db, {"rust_hound", "gel_mite"}, 77));
		Battle b = support::create(db, support::setup(db, {"rust_hound", "gel_mite"}, 77));
		const std::vector<Event> first = a.resolveRound();
		t.expect(serializeEvents(first) == serializeEvents(b.resolveRound()), "two identical battles returned different event lists for the same round");
		t.expect(!first.empty() && first.front().type == EventType::RoundStarted && first.back().type == EventType::RoundEnded, "resolveRound did not return the whole round");
	}

	t.gap("BAT-115 events missing from core: BattleStarted, CommandsLocked, OrderResolved, Retargeted, Hit, ComboDiscovered, Message");
	t.gap("BAT-115 payloads missing: Damaged/Healed carry no resulting HP, status events no message key, StatModified no duration, BattleEnded no rewards; ActionCancelled is split into ActionFailed and ActionPrevented");
	t.note("1000 random battles (" + std::to_string(events) + " events, " + std::to_string(seen.size()) + " event types): the start snapshot plus the stream reproduces hit points, technique points, skill uses, statuses, modifiers, inventory and outcome exactly");
}
