#include "support.h"

#include "rules/names.h"

#include <algorithm>

using namespace rules;

namespace {

constexpr int kFirst = 5;

// The party member who knows each ability at level 6 in core/data.
int knower(const Battle& battle, const std::string& ability, const std::vector<int>& taken) {
	for (int i = 0; i < battle.partyCount(); ++i) {
		if (battle.combatant(i).knows(ability) && std::find(taken.begin(), taken.end(), i) == taken.end()) {
			return i;
		}
	}
	return -1;
}

Macro comboMacro(const Battle& battle, const ComboRecipe& recipe, std::vector<int>& actors) {
	Macro macro;
	macro.name = "acc_" + recipe.id;
	for (const std::string& ability : recipe.abilities) {
		const int actor = knower(battle, ability, actors);
		actors.push_back(actor);
		MacroStep step;
		step.member = actor >= 0 ? battle.combatant(actor).defId : "";
		step.kind = CommandKind::Ability;
		step.id = ability;
		macro.steps.push_back(step);
	}
	return macro;
}

}

ACC_CHECK(acc_bat_11, "ACC-BAT-11", "BAT-060, BAT-061, BAT-062") {
	const Database& db = support::data(t);
	for (const ComboRecipe& recipe : db.combos) {
		// Components back to back through a macro: the combination fires, replaces the effects and every component pays.
		{
			Battle battle = support::create(db, support::setup(db, {"brass_warden"}, 3, 6));
			support::sturdy(battle, kFirst);
			for (int i = 0; i < battle.partyCount(); ++i) {
				battle.combatant(i).hp = battle.combatant(i).stats.maxHp / 2;
			}
			std::vector<int> actors;
			battle.applyMacro(comboMacro(battle, recipe, actors));
			std::vector<int> tpBefore;
			for (int i = 0; i < battle.partyCount(); ++i) {
				tpBefore.push_back(battle.combatant(i).tp);
			}
			support::othersDefend(battle);
			const std::vector<Event> events = battle.resolveRound();
			const std::vector<Event> fired = support::ofType(events, EventType::ComboTriggered);
			t.expect(fired.size() == 1 && fired[0].ability == recipe.result, recipe.id + ": components run back to back did not trigger the combination");
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

		// A cancelled action between the components prevents it.
		{
			Battle battle = support::create(db, support::setup(db, {"brass_warden"}, 4, 6));
			support::sturdy(battle, kFirst);
			std::vector<int> actors;
			Macro macro = comboMacro(battle, recipe, actors);
			const int blocker = knower(battle, "frostbite", actors) >= 0 ? knower(battle, "frostbite", actors) : knower(battle, "cleave", actors);
			if (blocker >= 0) {
				MacroStep step;
				step.member = battle.combatant(blocker).defId;
				step.kind = CommandKind::Ability;
				step.id = battle.combatant(blocker).knows("frostbite") ? "frostbite" : "cleave";
				macro.steps.insert(macro.steps.begin() + 1, step);
				battle.applyMacro(macro);
				battle.combatant(blocker).tp = 0;
				support::othersDefend(battle);
				const std::vector<Event> events = battle.resolveRound();
				bool cancelled = false;
				for (const Event& e : events) {
					cancelled = cancelled || (e.type == EventType::ActionFailed && e.actor == blocker);
				}
				t.expect(cancelled, recipe.id + ": setup: the in-between action was not cancelled");
				t.expect(support::countType(events, EventType::ComboTriggered) == 0, recipe.id + ": a cancelled action between the components did not prevent the combination");
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
		for (const std::string& ability : pair.abilities) {
			const int actor = knower(battle, ability, actors);
			actors.push_back(actor);
			battle.setCommand(actor, Command::ability(ability, kFirst));
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
		const bool together = std::abs(static_cast<int>(a - b)) == 1;
		(together ? adjacent : split) += 1;
		t.expect((support::countType(events, EventType::ComboTriggered) == 1) == together, pair.id + " seed " + std::to_string(seed) + (together ? ": adjacent components did not trigger" : ": an enemy action between the components did not prevent the combination"));
	}
	t.expect(adjacent > 0 && split > 0, "the seeded orders never produced both adjacent and split components");

	t.gap("core/data defines " + std::to_string(db.combos.size()) + " combinations, not the 15 the scope counts (BAT-060)");
	t.gap("combination recipes cannot tie an ability to a character or require an order (BAT-060)");
	t.note("each defined combination triggers back to back, replaces component effects and charges every component; a cancelled action or an enemy action between components prevents it (" +
		std::to_string(adjacent) + " adjacent, " + std::to_string(split) + " split orders)");
}
