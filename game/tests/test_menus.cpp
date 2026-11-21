#include "test.h"

#include "game/save.h"
#include "rules/formulas.h"
#include "rules/progression.h"

using namespace game;

namespace {

void setParty(Session& s, const std::vector<std::string>& ids, int level) {
	World& w = s.world();
	w.roster.clear();
	w.lineup.clear();
	for (const std::string& id : ids) {
		w.roster.push_back(rules::makePartyMember(test::data().db, *test::data().db.character(id), level));
		w.lineup.push_back(id);
	}
}

rules::MacroStep step(const std::string& member, rules::CommandKind kind, const std::string& id = "",
	rules::MacroTargetKind target = rules::MacroTargetKind::Default, const std::string& targetMember = "", int enemySlot = -1) {
	return {member, kind, id, {target, targetMember, enemySlot}};
}

const MacroView* findView(const std::vector<MacroView>& views, const std::string& name) {
	for (const MacroView& v : views) {
		if (v.name == name) {
			return &v;
		}
	}
	return nullptr;
}

World reloaded(const Session& s) {
	rules::Result<SaveFile> loaded = parseSave(serializeSave(s.makeSave(false)));
	CHECK(loaded.ok());
	return loaded.ok() ? loaded.value->world : World();
}

}

TEST(theLineupIsReorderedFromTheFieldMenuAndSavesKeepTheOrder) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "start", Facing::East);
	setParty(s, {"fighter", "mystic", "medic"}, 3);
	CHECK_EQ(s.swapLineup(0, 2).key, std::string("game.refuse.party_menu_only"));
	CHECK(s.openMenu().empty());
	CHECK(s.swapLineup(0, 2).empty());
	CHECK(s.world().lineup == (std::vector<std::string>{"medic", "mystic", "fighter"}));
	const rules::Message outside = s.swapLineup(1, 3);
	CHECK_EQ(outside.key, std::string("game.refuse.party_no_position"));
	CHECK(outside.params == std::vector<std::string>{"3"});
	CHECK_EQ(s.swapLineup(-1, 0).key, std::string("game.refuse.party_no_position"));
	CHECK(s.swapLineup(1, 1).empty());
	CHECK(s.world().lineup == (std::vector<std::string>{"medic", "mystic", "fighter"}));
	CHECK(reloaded(s).lineup == (std::vector<std::string>{"medic", "mystic", "fighter"}));
	rules::Result<SaveSummary> summary = readSummary(serializeSave(s.makeSave(false)));
	CHECK(summary.ok() && summary.value->party == (std::vector<std::string>{"medic:3", "mystic:3", "fighter:3"}));
}

TEST(macrosAreCreatedRenamedMovedAndDeletedFromTheFieldMenu) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "start", Facing::East);
	setParty(s, {"fighter", "mystic", "medic"}, 3);
	CHECK_EQ(s.macroViews().size(), test::data().db.macros.size());
	CHECK_EQ(s.createMacro("pair").key, std::string("game.refuse.macro_menu_only"));
	CHECK(s.openMenu().empty());
	CHECK(s.createMacro("  pair ").empty());
	CHECK_EQ(s.macroViews().back().name, std::string("pair"));
	CHECK_EQ(s.createMacro("pair").key, std::string("game.refuse.macro_name_taken"));
	CHECK_EQ(s.createMacro("   ").key, std::string("game.refuse.name_length"));
	CHECK_EQ(s.createMacro("seventeen letters").key, std::string("game.refuse.name_length"));

	CHECK(s.moveMacro("pair", 0).empty());
	CHECK_EQ(s.macroViews().front().name, std::string("pair"));
	CHECK_EQ(s.moveMacro("pair", 4).key, std::string("game.refuse.macro_no_position"));
	CHECK_EQ(s.moveMacro("nothing", 0).key, std::string("game.refuse.no_macro"));

	CHECK(s.setAutoMacro("pair").empty());
	CHECK(s.renameMacro("pair", "duo").empty());
	CHECK_EQ(s.world().autoMacro, std::string("duo"));
	CHECK(s.renameMacro("duo", "duo").empty());
	CHECK_EQ(s.renameMacro("duo", "opener").key, std::string("game.refuse.macro_name_taken"));
	CHECK_EQ(s.renameMacro("pair", "trio").key, std::string("game.refuse.no_macro"));

	while (static_cast<int>(s.world().macros.size()) < kMaxMacros) {
		CHECK(s.createMacro("spare " + std::to_string(s.world().macros.size())).empty());
	}
	const rules::Message full = s.createMacro("one more");
	CHECK_EQ(full.key, std::string("game.refuse.macro_slots_full"));
	CHECK(full.params == std::vector<std::string>{"8"});

	s.drainOutput();
	CHECK(s.deleteMacro("duo").empty());
	CHECK_EQ(s.world().autoMacro, std::string(""));
	const std::vector<Output> output = s.drainOutput();
	CHECK(output.size() == 1 && output[0].message.key == "game.auto_macro_cleared" && output[0].message.params == std::vector<std::string>{"duo"});
	CHECK(s.setAutoMacro("opener").empty());
	CHECK(s.deleteMacro("tempest").empty());
	CHECK_EQ(s.world().autoMacro, std::string("opener"));
	CHECK(s.drainOutput().empty());
	CHECK_EQ(s.deleteMacro("duo").key, std::string("game.refuse.no_macro"));
	CHECK(s.closeMenu().empty());
	CHECK_EQ(s.deleteMacro("opener").key, std::string("game.refuse.macro_menu_only"));
}

TEST(macroStepsAreValidatedAndShownWithNamesAndIssues) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "start", Facing::East);
	setParty(s, {"fighter", "mystic", "medic"}, 3);
	CHECK(s.openMenu().empty());
	CHECK(s.createMacro("pair").empty());
	using K = rules::CommandKind;
	using T = rules::MacroTargetKind;
	CHECK(s.setMacroStep("pair", 0, step("medic", K::Ability, "glint", T::Enemy, "", 1)).empty());
	CHECK(s.setMacroStep("pair", 1, step("mystic", K::Ability, "spark")).empty());
	CHECK(s.setMacroStep("pair", 2, step("fighter", K::Item, "tonic", T::Member, "mystic", 4)).empty());

	auto refusal = [&](int index, const rules::MacroStep& st) { return s.setMacroStep("pair", index, st); };
	CHECK_EQ(refusal(4, step("fighter", K::Attack)).key, std::string("game.refuse.macro_no_step"));
	CHECK_EQ(refusal(3, step("mystic", K::Defend)).key, std::string("game.refuse.macro_member_twice"));
	CHECK(refusal(2, step("fighter", K::Ability, "spark")).params == (std::vector<std::string>{"fighter", "spark"}));
	CHECK_EQ(refusal(2, step("fighter", K::Ability, "no_such")).key, std::string("game.refuse.macro_unknown_ability"));
	CHECK_EQ(refusal(2, step("engineer", K::Attack)).key, std::string("game.refuse.macro_unknown_member"));
	CHECK_EQ(refusal(2, step("fighter", K::Item, "no_such")).key, std::string("game.refuse.macro_unknown_item"));
	CHECK_EQ(refusal(2, step("fighter", K::Attack, "strike")).key, std::string("game.refuse.macro_bad_command"));
	CHECK_EQ(refusal(2, step("fighter", K::None)).key, std::string("game.refuse.macro_bad_command"));
	CHECK_EQ(refusal(2, step("fighter", K::Defend, "", T::Enemy, "", 0)).key, std::string("game.refuse.macro_target"));
	CHECK_EQ(refusal(2, step("fighter", K::Attack, "", T::Member, "mystic")).key, std::string("game.refuse.macro_target"));
	CHECK_EQ(refusal(2, step("fighter", K::Attack, "", T::Enemy, "", 8)).key, std::string("game.refuse.macro_target"));
	CHECK_EQ(refusal(2, step("medic", K::Ability, "mend", T::Member, "scout")).key, std::string("game.refuse.macro_member_twice"));
	CHECK_EQ(refusal(0, step("medic", K::Ability, "mend", T::Member, "scout")).key, std::string("game.refuse.macro_unknown_member"));

	s.world().member("medic")->tp = 0;
	s.world().items.erase("tonic");
	std::vector<MacroView> views = s.macroViews();
	const MacroView* pair = findView(views, "pair");
	CHECK(pair && pair->steps.size() == 3);
	if (pair && pair->steps.size() == 3) {
		const MacroStepView& glint = pair->steps[0];
		CHECK(glint.memberName == "Test Medic" && glint.commandName == "Glint" && glint.step.target.enemySlot == 1);
		CHECK(glint.issue == rules::MacroIssue::NotEnoughTp);
		CHECK(pair->steps[1].issue == rules::MacroIssue::None && pair->steps[1].step.target.kind == T::Default);
		const MacroStepView& tonic = pair->steps[2];
		CHECK(tonic.targetName == "Test Mystic" && tonic.step.target.enemySlot == -1 && tonic.issue == rules::MacroIssue::NoItem);
	}
	const MacroView* opener = findView(views, "opener");
	CHECK(opener && opener->steps[1].step.member == "scout" && opener->steps[1].issue == rules::MacroIssue::NotInParty);

	CHECK(s.moveMacroStep("pair", 2, 0).empty());
	CHECK(s.world().macros.back().steps[0].member == "fighter" && s.world().macros.back().steps[1].member == "medic");
	CHECK_EQ(s.moveMacroStep("pair", 0, 3).key, std::string("game.refuse.macro_no_step"));
	CHECK(s.removeMacroStep("pair", 0).empty());
	CHECK_EQ(s.world().macros.back().steps.size(), 2u);
	CHECK_EQ(s.removeMacroStep("pair", 2).key, std::string("game.refuse.macro_no_step"));
	CHECK(sameWorld(reloaded(s), s.world()));

	setParty(s, {"fighter", "mystic", "medic", "scout", "engineer"}, 3);
	for (const std::string& member : {"fighter", "scout", "engineer"}) {
		CHECK(s.setMacroStep("pair", static_cast<int>(s.world().macros.back().steps.size()), step(member, K::Defend)).empty());
	}
	const rules::Message full = s.setMacroStep("pair", 5, step("fighter", K::Attack));
	CHECK_EQ(full.key, std::string("game.refuse.macro_steps_full"));
	CHECK(full.params == std::vector<std::string>{"5"});
}

TEST(theComboLogIsReadWithItsRecipes) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "start", Facing::East);
	CHECK(s.comboLog().empty());
	s.world().comboLog.discover("twin_arc", 4200, {"medic", "mystic"});
	const std::vector<ComboView> log = s.comboLog();
	CHECK_EQ(log.size(), 1u);
	if (!log.empty()) {
		CHECK(log[0].recipe == "twin_arc" && log[0].result == "twin_arc" && log[0].resultName == "Twin Arc");
		CHECK(log[0].abilities == (std::vector<std::string>{"glint", "spark"}));
		CHECK(log[0].characters == (std::vector<std::string>{"medic", "mystic"}));
		CHECK_EQ(log[0].playTimeMs, 4200);
	}
}

TEST(memberDetailAddsWhatAStatusScreenNeeds) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "start", Facing::East);
	setParty(s, {"fighter", "mystic", "engineer"}, 3);
	s.world().member("fighter")->experience = 100;
	const std::optional<MemberDetail> fighter = s.memberDetail("fighter");
	CHECK(fighter.has_value());
	if (fighter) {
		CHECK_EQ(fighter->experienceToNext, rules::formulas::experienceToNextLevel(3) - 100);
		CHECK_EQ(fighter->equipmentBonus.size(), 3u);
		CHECK_EQ(fighter->equipmentBonus.at(rules::EquipSlot::Weapon).strength, 4);
		CHECK_EQ(fighter->equipmentBonus.at(rules::EquipSlot::Weapon).critical, 4);
		CHECK_EQ(fighter->equipmentBonus.at(rules::EquipSlot::Body).maxHp, 10);
		CHECK_EQ(fighter->stats.critical, rules::memberStats(test::data().db, *s.world().member("fighter")).critical);
		CHECK(fighter->attackElement == rules::Element::None);
		CHECK(fighter->statusResist.empty());
	}
	const std::optional<MemberDetail> mystic = s.memberDetail("mystic");
	CHECK(mystic && mystic->attackElement == rules::Element::Heat);
	CHECK(mystic && mystic->affinities.size() == 4 && mystic->affinities.at(rules::Element::Shock) == 50 && mystic->affinities.at(rules::Element::Heat) == 100);
	CHECK(mystic && mystic->statusResist.at("sleep") == 100);
	const std::optional<MemberDetail> engineer = s.memberDetail("engineer");
	CHECK(engineer && engineer->statusResist.at("paralysis") == 50 && engineer->equipmentBonus.at(rules::EquipSlot::Body).agility == -2);

	s.world().member("fighter")->level = rules::formulas::kMaxLevel;
	CHECK(s.memberDetail("fighter") && s.memberDetail("fighter")->experienceToNext == 0);
	CHECK(!s.memberDetail("scout"));
}

TEST(equipPreviewComparesAPieceAsABattleWouldSeeIt) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "start", Facing::East);
	setParty(s, {"fighter", "mystic", "engineer"}, 3);
	const rules::Database& db = test::data().db;

	// The fighter's padded coat against a rivet plate the bag does not hold.
	const std::optional<EquipPreview> plate = s.equipPreview("fighter", "rivet_plate");
	CHECK(plate.has_value());
	if (plate) {
		CHECK(plate->refusal.empty());
		CHECK_EQ(plate->replaces, std::string("padded_coat"));
		CHECK(plate->piece.slot == rules::EquipSlot::Body && plate->piece.bonus.defense == 6);
		CHECK(plate->wearers == (std::vector<std::string>{"fighter", "engineer"}));
		CHECK_EQ(plate->after.stats.defense - plate->before.stats.defense, 6 - 3);
		CHECK_EQ(plate->after.stats.maxHp - plate->before.stats.maxHp, 20 - 10);
		CHECK_EQ(plate->after.stats.agility - plate->before.stats.agility, -2);
		rules::PartyMember worn = *s.world().member("fighter");
		rules::EquipmentBag bag = {{"rivet_plate", 1}};
		CHECK(rules::equip(worn, db, bag, "rivet_plate").empty());
		CHECK_EQ(plate->after.stats.defense, rules::memberCombatant(db, worn).stats.defense);
	}
	CHECK(!s.world().equipmentBag.count("rivet_plate"));
	CHECK_EQ(s.world().member("fighter")->equipment.at(rules::EquipSlot::Body), std::string("padded_coat"));

	// A rod moves the element; a charm adds a resistance; a ring adds an ability.
	const std::optional<EquipPreview> rod = s.equipPreview("mystic", "ember_rod");
	CHECK(rod && rod->after.attackElement == rules::Element::Heat);
	const std::optional<EquipPreview> ring = s.equipPreview("mystic", "spark_ring");
	CHECK(ring && ring->replaces == "calm_charm" && ring->before.statusResist.count("sleep") && !ring->after.statusResist.count("sleep"));
	CHECK(ring && std::count(ring->after.abilities.begin(), ring->after.abilities.end(), "glint") == 1);
	const std::optional<EquipPreview> sling = s.equipPreview("engineer", "volley_sling");
	CHECK(sling && sling->refusal.key == "equip.wrong_category");
	CHECK(sling && sling->after.stats.dexterity == sling->before.stats.dexterity && sling->after.basicAttack == sling->before.basicAttack);
	CHECK(sling && sling->wearers.empty());

	// The status screen reads the same numbers.
	const std::optional<MemberDetail> detail = s.memberDetail("fighter");
	CHECK(detail && plate && detail->stats.defense == plate->before.stats.defense);
	CHECK(!s.equipPreview("scout", "tin_cap"));
	CHECK(!s.equipPreview("fighter", "tonic"));
}

TEST(theBagIsListedInFixedOrdersWithoutChangingIt) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "start", Facing::East);
	World& w = s.world();
	w.items = {{"tonic", 3}, {"spark_flask", 5}, {"clear_gel", 1}, {"second_wind", 5}};
	w.equipmentBag = {{"tin_cap", 1}, {"copper_blade", 2}};
	auto ids = [&](ItemSort order) {
		std::vector<std::string> out;
		for (const BagEntry& e : s.bag(order)) {
			out.push_back(e.id + (e.equipment ? "*" : "") + std::to_string(e.count));
		}
		return out;
	};
	CHECK(ids(ItemSort::Id) == (std::vector<std::string>{"clear_gel1", "second_wind5", "spark_flask5", "tonic3", "copper_blade*2", "tin_cap*1"}));
	// Field items (Clear Gel, Second Wind, Tonic) by name, the battle-only flask, then the weapon before the cap.
	CHECK(ids(ItemSort::Kind) == (std::vector<std::string>{"clear_gel1", "second_wind5", "tonic3", "spark_flask5", "copper_blade*2", "tin_cap*1"}));
	CHECK(ids(ItemSort::Name) == (std::vector<std::string>{"clear_gel1", "copper_blade*2", "second_wind5", "spark_flask5", "tin_cap*1", "tonic3"}));
	CHECK(ids(ItemSort::Count) == (std::vector<std::string>{"second_wind5", "spark_flask5", "tonic3", "copper_blade*2", "clear_gel1", "tin_cap*1"}));
	CHECK_EQ(std::string(toString(ItemSort::Count)), std::string("count"));
	// Sorting reads: the save is the same before and after.
	const std::string before = serializeSave(s.makeSave(false));
	s.bag(ItemSort::Name);
	CHECK_EQ(serializeSave(s.makeSave(false)), before);
}

TEST(difficultyPresetsAreListedAndChangeOnlyInAGameOutsideBattle) {
	Session s(test::data(), SessionOptions());
	const std::vector<DifficultyView> presets = s.difficulties();
	CHECK_EQ(presets.size(), 2u);
	if (presets.size() == 2) {
		CHECK(presets[0].id == "hard" && presets[0].name == "Hard" && presets[0].rewardPercent == 120);
		CHECK(presets[0].description.find("smarter") != std::string::npos);
		CHECK(presets[1].id == "original" && presets[1].rewardPercent == 100);
	}
	CHECK(!s.canChangeSettings());
	CHECK_EQ(s.setSettings({"hard", rules::RewardMultiplier::X1}).key, std::string("game.refuse.settings_no_game"));
	test::placeParty(s, "tidewater", "start", Facing::East);
	CHECK(s.canChangeSettings());
	CHECK(s.setSettings({"hard", rules::RewardMultiplier::X2}).empty());
	CHECK_EQ(s.world().settings.difficulty, std::string("hard"));
	CHECK(s.openMenu().empty());
	CHECK(s.canChangeSettings());
}

TEST(aMacroIsMadeFromADiscoveredCombination) {
	Session s(test::data(), SessionOptions());
	test::placeParty(s, "tidewater", "start", Facing::East);
	setParty(s, {"fighter", "mystic", "medic"}, 3);
	s.world().comboLog.discover("twin_arc", 1000, {"mystic", "medic"});
	s.world().comboLog.discover("storm_spark", 2000, {"mystic", "scout"});
	CHECK_EQ(s.macroFromCombo("twin_arc", "Arc").key, std::string("game.refuse.macro_menu_only"));
	CHECK(s.openMenu().empty());
	CHECK_EQ(s.macroFromCombo("cinder_gale", "Gale").key, std::string("game.refuse.combo_not_found"));
	const rules::Message noGust = s.macroFromCombo("storm_spark", "Storm");
	CHECK(noGust.key == "game.refuse.combo_no_member" && noGust.params == std::vector<std::string>{"gust"});
	CHECK(s.macroFromCombo("twin_arc", "  Arc  ").empty());
	const std::vector<MacroView> views = s.macroViews();
	const MacroView* arc = findView(views, "Arc");
	CHECK(arc && arc->steps.size() == 2);
	if (arc && arc->steps.size() == 2) {
		// The glint is tied to the medic; the spark goes to the mystic, whom the log recorded.
		CHECK(arc->steps[0].step.member == "medic" && arc->steps[0].step.id == "glint" && arc->steps[0].step.kind == rules::CommandKind::Ability);
		CHECK(arc->steps[1].step.member == "mystic" && arc->steps[1].step.id == "spark");
		CHECK(arc->steps[0].step.target.kind == rules::MacroTargetKind::Default && arc->steps[0].issue == rules::MacroIssue::None);
	}
	CHECK_EQ(s.macroFromCombo("twin_arc", "Arc").key, std::string("game.refuse.macro_name_taken"));
	CHECK_EQ(s.macroFromCombo("twin_arc", "").key, std::string("game.refuse.name_length"));

	// An ordered recipe keeps its order; with the scout in the party a member who knows each ability is found.
	setParty(s, {"fighter", "mystic", "scout"}, 4);
	s.world().comboLog.discover("pinpoint_rend", 3000, {"scout", "fighter"});
	const rules::Message rend = s.macroFromCombo("pinpoint_rend", "Rend");
	CHECK(rend.empty());
	const std::vector<MacroView> after = s.macroViews();
	const MacroView* made = findView(after, "Rend");
	CHECK(made && made->steps.size() == 2 && made->steps[0].step.id == "weaken" && made->steps[1].step.id == "focus_shot");
	CHECK(made && made->steps[0].step.member != made->steps[1].step.member);
}
