#include "support.h"

#include "rules/names.h"

#include <algorithm>

using namespace rules;

namespace {

constexpr int kFirst = 5;

// The party member who knows each ability at level 6 in core/data, or the recipe's tied character.
int knower(const Battle& battle, const std::string& ability, const std::string& character, const std::vector<int>& taken) {
	for (int i = 0; i < battle.partyCount(); ++i) {
		if (!character.empty() && battle.combatant(i).defId != character) {
			continue;
		}
		if (battle.combatant(i).knows(ability) && std::find(taken.begin(), taken.end(), i) == taken.end()) {
			return i;
		}
	}
	return -1;
}

std::string tiedTo(const ComboRecipe& recipe, std::size_t k) {
	return k < recipe.characters.size() ? recipe.characters[k] : "";
}

// The recipe's components as macro steps, in `order` (indexes into the recipe), each by a member who knows it.
Macro comboMacro(const Battle& battle, const ComboRecipe& recipe, const std::vector<std::size_t>& order, std::vector<int>& actors) {
	Macro macro;
	macro.name = "acc_" + recipe.id;
	actors.assign(recipe.abilities.size(), -1);
	std::vector<int> taken;
	for (std::size_t k = 0; k < recipe.abilities.size(); ++k) {
		actors[k] = knower(battle, recipe.abilities[k], tiedTo(recipe, k), taken);
		taken.push_back(actors[k]);
	}
	for (std::size_t k : order) {
		MacroStep step;
		step.member = actors[k] >= 0 ? battle.combatant(actors[k]).defId : "";
		step.kind = CommandKind::Ability;
		step.id = recipe.abilities[k];
		macro.steps.push_back(step);
	}
	return macro;
}

std::vector<std::size_t> listed(const ComboRecipe& recipe) {
	std::vector<std::size_t> order;
	for (std::size_t k = 0; k < recipe.abilities.size(); ++k) {
		order.push_back(k);
	}
	return order;
}

Battle comboBattle(const Database& db, std::uint64_t seed) {
	Battle battle = support::create(db, support::setup(db, {"brass_warden"}, seed, 6));
	support::sturdy(battle, kFirst);
	for (int i = 0; i < battle.partyCount(); ++i) {
		battle.combatant(i).hp = battle.combatant(i).stats.maxHp / 2;
	}
	return battle;
}

std::vector<Event> fired(Battle& battle) {
	support::othersDefend(battle);
	return support::ofType(battle.resolveRound(), EventType::ComboTriggered);
}

}

ACC_CHECK(acc_bat_11, "ACC-BAT-11", "BAT-060, BAT-061, BAT-062") {
	const Database& db = support::data(t);
	t.expect(db.combos.size() == 15, "core/data defines " + std::to_string(db.combos.size()) + " combinations, not the 15 the scope counts (BAT-060)");
	int ordered = 0;
	int tied = 0;
	for (const ComboRecipe& recipe : db.combos) {
		// Components back to back through a macro: the combination fires, replaces the effects and every component pays.
		{
			Battle battle = comboBattle(db, 3);
			std::vector<int> actors;
			battle.applyMacro(comboMacro(battle, recipe, listed(recipe), actors));
			if (!t.expect(std::find(actors.begin(), actors.end(), -1) == actors.end(), recipe.id + ": setup: no party member at level 6 can use every component")) {
				continue;
			}
			std::vector<int> tpBefore;
			for (int i = 0; i < battle.partyCount(); ++i) {
				tpBefore.push_back(battle.combatant(i).tp);
			}
			support::othersDefend(battle);
			const std::vector<Event> events = battle.resolveRound();
			const std::vector<Event> combos = support::ofType(events, EventType::ComboTriggered);
			t.expect(combos.size() == 1 && combos[0].combo == recipe.id && combos[0].ability == recipe.result, recipe.id + ": components run back to back did not trigger the combination");
			for (const Event& e : events) {
				const bool component = e.type == EventType::ActionStarted && std::find(actors.begin(), actors.end(), e.actor) != actors.end();
				t.expect(!component, recipe.id + ": a component's own action still ran alongside the combination");
			}
			for (std::size_t k = 0; k < actors.size(); ++k) {
				const AbilityDef& ability = *db.ability(recipe.abilities[k]);
				const int actor = actors[k];
				if (ability.kind == AbilityKind::Technique) {
					t.expect(battle.combatant(actor).tp == tpBefore[actor] - ability.cost, recipe.id + ": " + ability.id + " did not pay its technique points");
				} else {
					t.expect(battle.combatant(actor).skillUses.at(ability.id) == ability.uses - 1, recipe.id + ": " + ability.id + " did not spend a skill use");
				}
			}
			const AbilityDef& result = *db.ability(recipe.result);
			for (const Event& e : events) {
				if (e.type == EventType::DamageDealt && e.target == kFirst) {
					bool fromResult = false;
					for (const Effect& effect : result.effects) {
						fromResult = fromResult || effect.element == e.element;
					}
					t.expect(fromResult, recipe.id + ": damage of an element the combination does not deal, so a component effect applied");
				}
			}
		}

		// Order: an ordered recipe fires only in the listed order; any other recipe fires in reverse order too.
		{
			Battle battle = comboBattle(db, 5);
			std::vector<std::size_t> reversed = listed(recipe);
			std::reverse(reversed.begin(), reversed.end());
			std::vector<int> actors;
			battle.applyMacro(comboMacro(battle, recipe, reversed, actors));
			const std::vector<Event> combos = fired(battle);
			const bool fires = std::any_of(combos.begin(), combos.end(), [&](const Event& e) { return e.combo == recipe.id; });
			t.expect(fires != recipe.ordered, recipe.id + (recipe.ordered ? ": an ordered combination fired with its components in reverse order" : ": an unordered combination did not fire with its components in reverse order"));
			ordered += recipe.ordered ? 1 : 0;
		}

		// A component tied to a character does not count when someone else uses that ability.
		for (std::size_t k = 0; k < recipe.abilities.size(); ++k) {
			if (tiedTo(recipe, k).empty()) {
				continue;
			}
			++tied;
			Battle battle = comboBattle(db, 6);
			std::vector<int> actors;
			Macro macro = comboMacro(battle, recipe, listed(recipe), actors);
			int stranger = -1;
			for (int i = 0; i < battle.partyCount() && stranger < 0; ++i) {
				if (std::find(actors.begin(), actors.end(), i) == actors.end()) {
					stranger = i;
				}
			}
			battle.combatant(stranger).abilities.push_back(recipe.abilities[k]);
			battle.combatant(stranger).skillUses[recipe.abilities[k]] = 9;
			battle.combatant(stranger).tp = 99;
			macro.steps[k].member = battle.combatant(stranger).defId;
			battle.applyMacro(macro);
			const std::vector<Event> combos = fired(battle);
			t.expect(std::none_of(combos.begin(), combos.end(), [&](const Event& e) { return e.combo == recipe.id; }),
				recipe.id + ": fired with " + recipe.abilities[k] + " used by " + battle.combatant(stranger).defId + " instead of its tied character " + tiedTo(recipe, k));
		}

		// A cancelled action between the components prevents it.
		{
			Battle battle = comboBattle(db, 4);
			std::vector<int> actors;
			Macro macro = comboMacro(battle, recipe, listed(recipe), actors);
			int blocker = -1;
			std::string blockerAbility;
			for (const char* candidate : {"frostbite", "cleave", "hush", "drowse", "mend"}) {
				const int who = knower(battle, candidate, "", actors);
				if (blocker < 0 && who >= 0 && std::find(recipe.abilities.begin(), recipe.abilities.end(), candidate) == recipe.abilities.end()) {
					blocker = who;
					blockerAbility = candidate;
				}
			}
			if (t.expect(blocker >= 0, recipe.id + ": setup: no free party member to put an action between the components")) {
				MacroStep step;
				step.member = battle.combatant(blocker).defId;
				step.kind = CommandKind::Ability;
				step.id = blockerAbility;
				macro.steps.insert(macro.steps.begin() + 1, step);
				battle.applyMacro(macro);
				battle.combatant(blocker).tp = 0;
				support::othersDefend(battle);
				const std::vector<Event> events = battle.resolveRound();
				bool cancelled = false;
				for (const Event& e : events) {
					cancelled = cancelled || (e.type == EventType::ActionCancelled && e.actor == blocker);
				}
				t.expect(cancelled, recipe.id + ": setup: the in-between action was not cancelled");
				const std::vector<Event> combos = support::ofType(events, EventType::ComboTriggered);
				t.expect(std::none_of(combos.begin(), combos.end(), [&](const Event& e) { return e.combo == recipe.id; }), recipe.id + ": a cancelled action between the components did not prevent the combination");
			}
		}
	}

	// An enemy action between the components prevents it; adjacency decides, over many orders.
	const ComboRecipe& pair = db.combos.front();
	int adjacent = 0;
	int split = 0;
	for (std::uint64_t seed = 1; seed <= 3000; ++seed) {
		Battle battle = support::create(db, support::setup(db, {"brass_warden", "bog_wisp"}, seed, 6));
		support::sturdy(battle, kFirst);
		support::sturdy(battle, kFirst + 1);
		std::vector<int> actors;
		for (std::size_t k = 0; k < pair.abilities.size(); ++k) {
			const int actor = knower(battle, pair.abilities[k], tiedTo(pair, k), actors);
			actors.push_back(actor);
			battle.setCommand(actor, Command::ability(pair.abilities[k], kFirst));
		}
		for (int i = 0; i < battle.partyCount(); ++i) {
			if (battle.command(i).kind == CommandKind::None) {
				battle.combatant(i).hp = 0;
				battle.combatant(i).statuses = {{kDefeatedStatus, 0}};
			}
		}
		battle.setCommand(kFirst, Command::defend());
		battle.setCommand(kFirst + 1, Command::defend());
		const std::vector<Event> events = battle.resolveRound();
		const std::vector<int>& order = battle.lastTurnOrder();
		const auto a = std::find(order.begin(), order.end(), actors[0]);
		const auto b = std::find(order.begin(), order.end(), actors[1]);
		const bool together = std::abs(static_cast<int>(a - b)) == 1 && (!pair.ordered || a < b);
		(together ? adjacent : split) += 1;
		t.expect((support::countType(events, EventType::ComboTriggered) == 1) == together, pair.id + " seed " + std::to_string(seed) + (together ? ": adjacent components did not trigger" : ": an enemy action between the components did not prevent the combination"));
	}
	t.expect(adjacent > 0 && split > 0, "the seeded orders never produced both adjacent and split components");

	t.note(std::to_string(db.combos.size()) + " combinations each trigger back to back, replace component effects and charge every component; " + std::to_string(ordered) + " ordered ones fire only in order and the rest in any order; " +
		std::to_string(tied) + " character-tied components refuse another user; a cancelled action or an enemy action between components prevents it (" + std::to_string(adjacent) + " adjacent, " + std::to_string(split) + " split orders)");
}
