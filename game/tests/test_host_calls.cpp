#include "test.h"

#include "game/area.h"
#include "game/runner.h"
#include "game/save.h"

#include "rules/battle.h"
#include "rules/formulas.h"
#include "rules/progression.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace game;

namespace {

void setParty(Session& s, const std::vector<std::string>& ids, int level) {
	World& w = s.world();
	w.roster.clear();
	w.lineup.clear();
	for (const std::string& id : ids) {
		w.roster.push_back(rules::makePartyMember(s.data().db, *s.data().db.character(id), level));
		w.lineup.push_back(id);
	}
}

bool hasKey(const std::vector<Output>& output, const std::string& key) {
	return std::any_of(output.begin(), output.end(), [&](const Output& o) { return o.message.key == key; });
}

}

// Discard throws items and unworn pieces away from the field menu, never more than the bag holds, and never an item the
// data marks `discardable: false`.
TEST(discardThrowsAwayWhatTheDataAllows) {
	GameData data = test::data();
	data.db.items.at("clear_gel").discardable = false;
	Session s(data, SessionOptions());
	test::placeParty(s, "tidewater", "start", Facing::East);
	s.world().items = {{"tonic", 3}, {"clear_gel", 1}};
	s.world().equipmentBag = {{"tin_cap", 1}};
	CHECK_EQ(s.discard("tonic", 1).key, std::string("game.refuse.discard_menu_only"));
	CHECK(s.openMenu().empty());
	CHECK_EQ(s.discard("tonic", 0).key, std::string("game.refuse.discard_one"));
	CHECK_EQ(s.discard("tonic", 4).key, std::string("game.refuse.discard_not_held"));
	CHECK_EQ(s.discard("clear_gel", 1).key, std::string("game.refuse.not_discardable"));
	CHECK_EQ(s.world().items.at("clear_gel"), 1);
	s.drainOutput();
	CHECK(s.discard("tonic", 2).empty());
	CHECK_EQ(s.world().items.at("tonic"), 1);
	CHECK(hasKey(s.drainOutput(), "game.discarded"));
	CHECK(s.discard("tonic", 1).empty());
	CHECK(!s.world().items.count("tonic"));
	CHECK(s.discard("tin_cap", 1).empty());
	CHECK(s.world().equipmentBag.empty());
	CHECK_EQ(s.discard("tin_cap", 1).key, std::string("game.refuse.discard_not_held"));
}

// The allies a one-ally item or ability cannot reach, in the field and in a battle: a tonic and the medic's mend never
// reach the fallen scout, a revive reaches nobody standing.
TEST(alliesOutOfReachAreTheOnesTheSessionWouldRefuse) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "reed_path", "west_gate", Facing::East);
	setParty(s, {"medic", "scout", "mystic"}, 5);
	s.world().member("scout")->hp = 0;
	s.world().items = {{"tonic", 2}, {"second_wind", 1}};
	CHECK(s.alliesOutOfReach(rules::CommandKind::Item, "tonic", "") == std::vector<std::string>{"scout"});
	CHECK(s.alliesOutOfReach(rules::CommandKind::Ability, "mend", "medic") == std::vector<std::string>{"scout"});
	CHECK((s.alliesOutOfReach(rules::CommandKind::Item, "second_wind", "") == std::vector<std::string>{"medic", "mystic"}));
	CHECK(s.alliesOutOfReach(rules::CommandKind::Attack, "", "medic").empty());
	CHECK(s.openMenu().empty());
	CHECK_EQ(s.useItem("tonic", "scout").key, std::string("field.no_target"));
	CHECK(s.closeMenu().empty());

	CHECK(s.devBattle("training").empty());
	CHECK(s.mode() == Mode::Battle);
	CHECK(s.alliesOutOfReach(rules::CommandKind::Item, "tonic", "medic") == std::vector<std::string>{"scout"});
	CHECK(s.alliesOutOfReach(rules::CommandKind::Ability, "mend", "medic") == std::vector<std::string>{"scout"});
	CHECK(s.alliesOutOfReach(rules::CommandKind::Ability, "mend", "nobody").empty());
	CHECK_EQ(s.setCommand(0, rules::Command::item("tonic", 1)).key, std::string("command.no_effect_on_target"));
}

// A revive of `hp: 0` and a heal, used from the field menu, stand a fallen member at exactly the heal, as in battle, for an
// item (a reusable one too) and a technique, on one ally and on all; a living target the revive passes by takes the heal.
TEST(aFieldReviveOfZeroAndHealEndsAtTheHeal) {
	GameData data = test::data();
	rules::Effect revive;
	revive.kind = rules::EffectKind::Revive;
	revive.zeroHp = true;
	revive.livingTarget = true;
	rules::Effect heal;
	heal.kind = rules::EffectKind::Heal;
	heal.power = 30;
	data.db.items.at("second_wind").effects = {revive, heal};
	data.db.items["reusable_wind"] = data.db.items.at("second_wind");
	data.db.items.at("reusable_wind").id = "reusable_wind";
	data.db.items.at("reusable_wind").consumed = false;
	data.db.items["wide_wind"] = data.db.items.at("second_wind");
	data.db.items.at("wide_wind").id = "wide_wind";
	data.db.items.at("wide_wind").targeting = rules::Targeting::AllAllies;
	data.db.abilities.at("rekindle").effects = {revive, heal};
	data.db.abilities.at("wide_mend").effects = {revive, heal};
	for (const std::string& use : std::vector<std::string>{"second_wind", "reusable_wind", "wide_wind", "rekindle", "wide_mend", "rekindle living"}) {
		Session s(data, SessionOptions());
		test::placeParty(s, "reed_path", "west_gate", Facing::East);
		setParty(s, {"medic", "scout"}, 20);
		const bool living = use == "rekindle living";
		const int before = living ? s.world().member("scout")->hp - 40 : 0;
		s.world().member("scout")->hp = before;
		s.world().items = {{"second_wind", 1}, {"reusable_wind", 1}, {"wide_wind", 1}};
		CHECK(s.openMenu().empty());
		s.drainOutput();
		const bool ability = use.rfind("rekindle", 0) == 0 || use == "wide_mend";
		const std::string id = living ? "rekindle" : use;
		const std::string target = use.rfind("wide", 0) == 0 ? "" : "scout";
		CHECK((ability ? s.useAbility("medic", id, target) : s.useItem(id, target)).empty());
		int healed = -1;
		bool revived = false;
		for (const Output& o : s.drainOutput()) {
			if (!o.message.params.empty() && o.message.params[0] == "scout") {
				healed = o.message.key == "game.healed" ? std::stoi(o.message.params[1]) : healed;
				revived = revived || o.message.key == "game.revived";
			}
		}
		CHECK(healed > 1);
		CHECK_EQ(revived, !living);
		CHECK_EQ(s.world().member("scout")->hp, before + healed);
		if (!ability) {
			CHECK_EQ(healed, 30);
		}
	}
}

// A field skill with an `attack_stat` heals with that stat from the field menu, as in battle, not with the user's mental:
// a fighter with no mental stands the fallen scout at the heal and heals the others by it.
TEST(aFieldAbilityHealsWithItsAttackStat) {
	const std::vector<std::optional<rules::Stat>> attackStats = {rules::Stat::Strength, rules::Stat::Attack, std::nullopt};
	for (const std::optional<rules::Stat>& attackStat : attackStats) {
		GameData data = test::data();
		data.db.characters.at("fighter").base.mental = 0;
		data.db.characters.at("fighter").base.strength = 20;
		data.db.equipment.at("copper_blade").attackBonus = 6;
		rules::AbilityDef rally;
		rally.id = "field_rally";
		rally.name = "Field Rally";
		rally.kind = rules::AbilityKind::Skill;
		rally.uses = 3;
		rally.targeting = rules::Targeting::AllAllies;
		rally.field = true;
		rally.attackStat = attackStat;
		for (const char* status : {"poison", "sleep", "paralysis"}) {
			rules::Effect cure;
			cure.kind = rules::EffectKind::CureStatus;
			cure.status = status;
			rally.effects.push_back(cure);
		}
		rules::Effect revive;
		revive.kind = rules::EffectKind::Revive;
		revive.zeroHp = true;
		rally.effects.push_back(revive);
		rules::Effect heal;
		heal.kind = rules::EffectKind::Heal;
		rally.effects.push_back(heal);
		for (rules::Effect& effect : rally.effects) {
			effect.kinds = {rules::CreatureKind::Living};
		}
		data.db.abilities[rally.id] = rally;
		data.db.characters.at("fighter").abilities.push_back(rally.id);

		Session s(data, SessionOptions());
		test::placeParty(s, "reed_path", "west_gate", Facing::East);
		setParty(s, {"fighter", "medic", "scout"}, 1);
		s.world().member("medic")->hp = 1;
		s.world().member("scout")->hp = 0;
		const int stat = rules::memberCombatant(s.data().db, *s.world().member("fighter")).effective(attackStat.value_or(rules::Stat::Mental));
		const int amount = rules::formulas::healAmount(0, stat, rules::formulas::kSpreadMean);
		CHECK(amount > 1 || !attackStat);
		CHECK(s.openMenu().empty());
		s.drainOutput();
		CHECK(s.useAbility("fighter", "field_rally", "").empty());
		std::map<std::string, int> healed;
		std::string revived;
		for (const Output& o : s.drainOutput()) {
			if (o.message.key == "game.healed") {
				healed[o.message.params[0]] = std::stoi(o.message.params[1]);
			} else if (o.message.key == "game.revived") {
				revived = o.message.params[0] + " " + o.message.params[1];
			}
		}
		CHECK_EQ(revived, "scout " + std::to_string(amount > 0 ? 0 : 1));
		CHECK_EQ(healed["medic"], amount);
		CHECK_EQ(healed["scout"], amount);
		CHECK_EQ(s.world().member("medic")->hp, 1 + amount);
		CHECK_EQ(s.world().member("scout")->hp, std::max(1, amount));
	}
}

// Each save summary says whether it is the one SaveStore::latest picks; a damaged slot never is.
TEST(saveSummariesMarkTheNewest) {
	SessionOptions options;
	options.saveDirectory = test::freshDirectory("newest");
	Session s(test::data(), options);
	test::placeParty(s, "tidewater", "start", Facing::East);
	CHECK(s.openMenu().empty());
	CHECK(s.saveManual("manual_1").empty());
	CHECK(s.saveManual("manual_0").empty());
	std::vector<SaveSummary> list = s.store()->list();
	CHECK_EQ(std::count_if(list.begin(), list.end(), [](const SaveSummary& x) { return x.newest; }), 1);
	for (const SaveSummary& summary : list) {
		CHECK_EQ(summary.newest, summary.slot == s.store()->latest());
	}
	CHECK_EQ(s.store()->latest(), std::string("manual_0"));
	std::ofstream(s.store()->path("manual_0")) << "damaged";
	list = s.store()->list();
	for (const SaveSummary& summary : list) {
		CHECK_EQ(summary.newest, summary.slot == "manual_1");
	}
	CHECK_EQ(s.store()->latest(), std::string("manual_1"));
}

// partyTalkLines gives partyTalk's lines with their speakers from the menu too, and shows and marks nothing.
TEST(partyTalkLinesComeWithoutShowingAnything) {
	Session s(test::data(), SessionOptions());
	CHECK(s.partyTalkLines().empty());
	test::placeParty(s, "reed_path", "west_gate", Facing::East);
	s.world().flags["tidewater.met_guide"] = true;
	setParty(s, {"fighter", "mystic"}, 3);
	CHECK(s.openMenu().empty());
	const World before = s.world();
	const std::vector<TalkLineView> lines = s.partyTalkLines();
	CHECK_EQ(lines.size(), 1u);
	if (lines.size() == 1) {
		CHECK_EQ(lines[0].speaker, std::string("mystic"));
		CHECK_EQ(lines[0].speakerName, std::string("Test Mystic"));
		CHECK_EQ(lines[0].textId, std::string("talk.reeds.1"));
		CHECK_EQ(lines[0].text, std::string("Every step out here squelches. I am counting them."));
	}
	CHECK(s.drainOutput().empty());
	CHECK(s.world().ambientHeard == before.ambientHeard);
	CHECK(s.menuKind() == MenuKind::Field);
}

// dev combo sets the recipe's parts as commands that lead the round, so the combination fires; it marks the game.
TEST(devComboMakesTheCombinationFire) {
	for (std::uint64_t seed = 1; seed <= 6; ++seed) {
		SessionOptions options;
		options.seed = seed;
		Session s(test::data(), options);
		CHECK_EQ(s.devCombo("storm_spark").key, std::string("game.refuse.no_battle"));
		test::placeParty(s, "reed_path", "west_gate", Facing::East);
		setParty(s, {"fighter", "mystic", "scout"}, 5);
		CHECK(s.devBattle("training").empty());
		CHECK_EQ(s.devCombo("nothing").key, std::string("game.refuse.combo_not_found"));
		const rules::Message noMember = s.devCombo("mending_chorus");
		CHECK(noMember.key == "game.refuse.combo_no_member" && noMember.params == std::vector<std::string>{"mend"});
		CHECK(s.devCombo("storm_spark").empty());
		CHECK(s.devMarked());
		CHECK(s.setCommand(0, rules::Command::defend()).empty());
		const std::vector<rules::Event> events = s.resolveRound();
		CHECK(std::any_of(events.begin(), events.end(), [](const rules::Event& e) { return e.type == rules::EventType::ComboTriggered; }));
	}
}

// An area's name may be a plain text key, one variant with neither `from` nor `flag`.
TEST(anAreaNameMayBeAPlainTextKey) {
	const char* plain = R"({"id": "nook", "map": ["###", "#.#", "###"], "markers": {"start": [1, 1]}, "name": "area.wren_hollow"})";
	const rules::Result<Area> area = parseArea("nook.json", plain);
	CHECK(area.ok());
	if (area.ok()) {
		CHECK_EQ(area.value->names.size(), 1u);
		CHECK(area.value->names[0].key == "area.wren_hollow" && area.value->names[0].from.empty() && area.value->names[0].flag.empty());
	}
	const rules::Result<Area> empty = parseArea("nook.json", R"({"id": "nook", "map": ["###", "#.#", "###"], "markers": {"start": [1, 1]}, "name": ""})");
	CHECK(!empty.ok() && empty.problems[0].message.key == "data.field_cannot_empty");
	const rules::Result<Area> number = parseArea("nook.json", R"({"id": "nook", "map": ["###", "#.#", "###"], "markers": {"start": [1, 1]}, "name": 4})");
	CHECK(!number.ok() && number.problems[0].message.key == "data.field_wrong_type");
}

// An item that changed nothing is kept by default and used up under consume_when_no_effect: the field's notice and the
// terminal log say which.
TEST(anItemWithNoEffectSaysWhetherItWasKeptOrUsedUp) {
	for (const bool consume : {false, true}) {
		GameData data = test::data();
		data.db.items.at("tonic").consumeWhenNoEffect = consume;
		Session s(data, SessionOptions());
		test::placeParty(s, "reed_path", "west_gate", Facing::East);
		setParty(s, {"medic"}, 5);
		s.world().items = {{"tonic", 2}};
		CHECK(s.openMenu().empty());
		CHECK_EQ(s.useItem("tonic", "medic").key, std::string(consume ? "game.no_effect_used" : "game.no_effect"));
		CHECK_EQ(s.world().items["tonic"], consume ? 1 : 2);
	}
	for (const bool consume : {false, true}) {
		GameData data = test::data();
		data.db.items.at("tonic").consumeWhenNoEffect = consume;
		Session s(data, SessionOptions());
		test::placeParty(s, "reed_path", "west_gate", Facing::East);
		setParty(s, {"medic"}, 20);
		s.world().items = {{"tonic", 2}};
		std::ostringstream out;
		Runner runner(s, out);
		CHECK(s.devBattle("training").empty());
		runner.settle();
		runner.execute("item medic tonic p1");
		runner.execute("round");
		CHECK(out.str().find(consume ? "  Tonic has no effect and is used up.\n" : "  Tonic has no effect and is kept.\n") != std::string::npos);
	}
}
