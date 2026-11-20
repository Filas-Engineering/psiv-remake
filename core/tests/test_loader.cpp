#include "fixtures.h"
#include "rules/loader.h"
#include "rules/text.h"
#include "test.h"

#include <algorithm>

using namespace rules;

namespace {

void replaceOnce(std::string& text, const std::string& from, const std::string& to) {
	const std::size_t at = text.find(from);
	if (at == std::string::npos) {
		::test::fail(__FILE__, __LINE__, "fixture text not found: " + from);
		return;
	}
	text.replace(at, from.size(), to);
}

void expectError(const std::map<std::string, std::string>& files, const std::string& expected) {
	const Result<Database> loaded = loadDatabaseFromTexts(files);
	if (loaded.ok()) {
		::test::fail(__FILE__, __LINE__, "expected a load error containing: " + expected);
	} else if (loaded.error.find(expected) == std::string::npos) {
		::test::fail(__FILE__, __LINE__, "expected '" + expected + "' in:\n" + loaded.error);
	}
}

}

TEST(loaderReadsShippedData) {
	const Database& db = fixtures::data();
	CHECK_EQ(db.lineup.size(), std::size_t(5));
	CHECK_EQ(db.abilities.at("spark").cost, 3);
	CHECK_EQ(db.abilities.at("focus_shot").uses, 3);
	CHECK(db.abilities.at("ember").targeting == Targeting::EnemyGroup);
	CHECK_EQ(db.enemies.at("rust_hound").affinities.at(Element::Shock), 150);
	CHECK_EQ(db.enemies.at("rust_hound").slots.size(), std::size_t(kAiSlots));
	CHECK(db.enemies.at("rust_hound").overrides.at(0).when == AiConditionKind::SelfHpHalf);
	CHECK_EQ(db.enemies.at("rust_hound").overrides.at(0).ability, std::string("venom_spit"));
	CHECK(db.statuses.at("paralysis").action == StatusAction::CannotAct);
	CHECK_EQ(db.startingInventory.at("tonic"), 3);
	const Macro* opener = db.macro("opener");
	CHECK(opener != nullptr);
	if (opener) {
		CHECK_EQ(opener->steps.size(), std::size_t(5));
		CHECK(opener->steps[4].target.kind == MacroTargetKind::Member);
		CHECK_EQ(opener->steps[4].target.member, std::string("fighter"));
		CHECK(opener->steps[0].target.kind == MacroTargetKind::Enemy);
		CHECK_EQ(opener->steps[0].target.enemySlot, 0);
	}
	CHECK_EQ(db.combos.size(), std::size_t(15));
}

TEST(loaderRejectsEnemyWithMissingAbility) {
	auto files = fixtures::dataTexts();
	replaceOnce(files["enemies.json"], "\"abilities\": [\"venom_spit\"]", "\"abilities\": [\"venom_spat\"]");
	expectError(files, "enemies.json:19: enemy 'rust_hound': names missing ability 'venom_spat'");
}

TEST(loaderRejectsBadEnemyAi) {
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["enemies.json"], "\"slots\": [\"bite\", \"bite\", \"bite\", \"bite\", \"bite\", \"bite\", \"bite\", \"bite\"]",
			"\"slots\": [\"spark\", \"bite\", \"bite\", \"bite\", \"bite\", \"bite\", \"bite\", \"bite\"]");
		expectError(files, "enemy 'gel_mite': AI uses 'spark', which is neither the enemy's basic attack nor one of its abilities");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["enemies.json"], "\"slots\": [\"bite\", \"bite\", \"bite\", \"bite\", \"bite\", \"bite\", \"bite\", \"bite\"]",
			"\"slots\": [\"bite\", \"bite\", \"bite\", \"bite\", \"bite\", \"bite\", \"bite\"]");
		expectError(files, "enemy 'gel_mite': needs exactly 8 slots");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["enemies.json"], "{\"when\": \"self_hp_half\", \"ability\": \"venom_spit\"}", "{\"when\": \"ally_defeated\", \"ability\": \"venom_spit\"}");
		expectError(files, "enemy 'rust_hound': unknown AI condition 'ally_defeated'");
	}
	{
		auto files = fixtures::dataTexts();
		const std::string one = "{\"when\": \"self_hp_half\", \"ability\": \"venom_spit\"}";
		replaceOnce(files["enemies.json"], one, one + ", " + one + ", " + one + ", " + one + ", " + one);
		expectError(files, "enemy 'rust_hound': has more than 4 overrides");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["enemies.json"], "\"drop\": {\"item\": \"tonic\", \"rate\": 58}", "\"drops\": [{\"item\": \"tonic\", \"chance\": 20}]");
		expectError(files, "encounter 'training': unknown field 'drops'");
	}
}

TEST(loaderRejectsBadReferencesAcrossFiles) {
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["combos.json"], "\"result\": \"storm_spark\"", "\"result\": \"spark\"");
		expectError(files, "combo 'storm_spark': result 'spark' must have kind combo");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["abilities.json"], "\"status\": \"silence\"", "\"status\": \"mute\"");
		expectError(files, "ability 'hush': names missing status 'mute'");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["party.json"], "{\"member\": \"mystic\", \"command\": \"ability\", \"id\": \"spark\", \"target\": {\"enemy\": 0}}",
			"{\"member\": \"mystic\", \"command\": \"ability\", \"id\": \"mend\", \"target\": {\"enemy\": 0}}");
		expectError(files, "macro 'opener': character 'mystic' does not know ability 'mend'");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["enemies.json"], "\"enemies\": [\"gel_mite\", \"gel_mite\", \"rust_hound\"]", "\"enemies\": [\"gel_mite\", \"ghost\"]");
		expectError(files, "encounter 'training': names missing enemy 'ghost'");
	}
}

TEST(loaderRejectsSchemaMistakes) {
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["abilities.json"], "\"power\": 13", "\"pwer\": 13");
		expectError(files, "ability 'spark': unknown field 'pwer'");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["abilities.json"], "\"target\": \"one_fallen_ally\", \"field\": true, \"effects\": [\n\t\t\t{\"type\": \"revive\"",
			"\"target\": \"all_enemies\", \"effects\": [\n\t\t\t{\"type\": \"revive\"");
		expectError(files, "ability 'rekindle': revive effects need target one_fallen_ally or all_allies");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["statuses.json"], "{\"id\": \"defeated\", \"name\": \"Defeated\"}", "{\"id\": \"knocked_out\", \"name\": \"Out\"}");
		expectError(files, "a status with id 'defeated' is required");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["abilities.json"], "\"uses\": 3", "\"uses\": 2.5");
		expectError(files, "field 'uses' must be a whole number");
	}
}

TEST(loaderReportsSyntaxErrorsWithFileAndLine) {
	auto files = fixtures::dataTexts();
	replaceOnce(files["items.json"], "\"name\": \"Tonic\",", "\"name\": \"Tonic\"");
	expectError(files, "items.json:3: expected ',' or '}' in object");
}

TEST(loaderCollectsEveryError) {
	auto files = fixtures::dataTexts();
	replaceOnce(files["enemies.json"], "\"abilities\": [\"venom_spit\"]", "\"abilities\": [\"venom_spat\"]");
	replaceOnce(files["combos.json"], "\"abilities\": [\"mend\", \"rally\"]", "\"abilities\": [\"mend\", \"nap\"]");
	files.erase("items.json");
	const Result<Database> loaded = loadDatabaseFromTexts(files);
	CHECK(!loaded.ok());
	CHECK(loaded.error.find("items.json: missing file") != std::string::npos);
	CHECK(loaded.error.find("venom_spat") != std::string::npos);
	CHECK(loaded.error.find("names missing ability 'nap'") != std::string::npos);
}

TEST(loaderReadsEquipmentLearningAndGroups) {
	const Database& db = fixtures::data();
	const EquipmentDef& sling = db.equipment.at("volley_sling");
	CHECK(sling.slot == EquipSlot::Weapon);
	CHECK_EQ(sling.attack, std::string("scatter_shot"));
	CHECK(db.equipment.at("ember_rod").element == Element::Heat);
	CHECK_EQ(db.equipment.at("rivet_plate").bonus.agility, -2);
	CHECK_EQ(db.characters.at("fighter").learns.at(0).ability, std::string("cleave"));
	CHECK_EQ(db.enemies.at("brass_warden").statusResist.at("sleep"), 100);
	CHECK_EQ(db.encounter("marsh")->enemies.at(0).group, std::string("wisps"));
	CHECK_EQ(db.encounter("marsh")->enemies.at(2).group, std::string("gel_mite"));
	CHECK(!db.encounter("warden")->escapeAllowed);
	CHECK(db.encounter("warden")->drop.item == "loosen_oil" && db.encounter("warden")->drop.rate == 127);
	CHECK(db.contentHash != 0);
}

TEST(loaderRejectsBadEquipmentAndLearning) {
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["equipment.json"], "\"slot\": \"head\", \"category\": \"light_armor\", \"stats\": {\"defense\": 2}",
			"\"slot\": \"head\", \"category\": \"light_armor\", \"element\": \"heat\", \"stats\": {\"defense\": 2}");
		expectError(files, "equipment 'tin_cap': only weapons have an element");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["party.json"], "\"equipment\": [\"ember_rod\", \"calm_charm\"]", "\"equipment\": [\"copper_blade\", \"calm_charm\"]");
		expectError(files, "character 'mystic': cannot equip 'copper_blade' (category 'blade')");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["party.json"], "\"equip\": [\"rod\", \"light_armor\", \"charm\"],\n\t\t\t\"equipment\": [\"ember_rod\"",
			"\"equip\": [\"wand\", \"rod\", \"light_armor\", \"charm\"],\n\t\t\t\"equipment\": [\"ember_rod\"");
		expectError(files, "character 'mystic': no equipment has category 'wand'");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["party.json"], "{\"level\": 3, \"ability\": \"cleave\"}", "{\"level\": 3, \"ability\": \"rally\"}");
		expectError(files, "character 'fighter': ability 'rally' is already known or learned");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["party.json"], "{\"member\": \"fighter\", \"command\": \"ability\", \"id\": \"rally\", \"target\": \"all\"}",
			"{\"member\": \"fighter\", \"command\": \"ability\", \"id\": \"rally\", \"target\": {\"enemy\": 1}}");
		expectError(files, "macro 'regroup': target 'enemy' does not fit this command");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["rules.json"], "\"surprise_rounds\": 1,", "\"surprise_rounds\": 1, \"experience_split\": \"each_living\",");
		expectError(files, "rules.json:2: unknown field 'experience_split'");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["enemies.json"], "\"status_resist\": {\"poison\": 100}", "\"status_resist\": {\"defeated\": 100}");
		expectError(files, "enemy 'gel_mite': the defeated status cannot be resisted");
	}
}

TEST(loaderReadsTheNewRuleFields) {
	const Database& db = fixtures::data();
	const StatusDef& jumble = db.statuses.at("jumble");
	CHECK(jumble.action == StatusAction::Forced && jumble.forcedTarget == ForcedTarget::RandomAny && jumble.stacking == StatusStacking::Keep);
	CHECK(db.statuses.at("gear_lock").countsTowardDefeat && db.statuses.at("gear_lock").persistsOutsideBattle);
	CHECK(!db.statuses.at("sleep").persistsOutsideBattle && db.statuses.at("sleep").restCures);
	CHECK(db.battleRules.restRevives);
	CHECK(db.abilities.at("rekindle").targeting == Targeting::OneFallenAlly && db.abilities.at("rekindle").field);
	CHECK(!db.abilities.at("spark").field);
	CHECK(db.items.at("jolt_pod").targeting == Targeting::RandomEnemy);
	CHECK_EQ(db.startingEquipmentBag.at("copper_blade"), 1);
	const ComboRecipe& twin = *std::find_if(db.combos.begin(), db.combos.end(), [](const ComboRecipe& r) { return r.id == "twin_arc"; });
	CHECK_EQ(twin.characters, (std::vector<std::string>{"medic", ""}));
	CHECK(!twin.ordered);
	const ComboRecipe& cinder = *std::find_if(db.combos.begin(), db.combos.end(), [](const ComboRecipe& r) { return r.id == "cinder_gale"; });
	CHECK(cinder.ordered);
	const StatusDef& paralysis = db.statuses.at("paralysis");
	CHECK(paralysis.statsToOne == (std::vector<Stat>{Stat::Agility, Stat::Dexterity}) && paralysis.removes == std::vector<std::string>{"sleep"});
	CHECK(paralysis.countsTowardDefeat && db.statuses.at("sleep").statsToOne == std::vector<Stat>{Stat::Agility});
	CHECK(db.statuses.at("silence").survivesDefeat && db.statuses.at("silence").duration == 0);
	const Effect& lower = db.abilities.at("weaken").effects.at(0);
	CHECK(lower.kind == EffectKind::ModifyStat && lower.stat == Stat::Defense && !lower.raises && lower.threshold == 40);
	CHECK(db.abilities.at("rally").effects.at(0).raises);
}

TEST(loaderRejectsBadStatusAndStatChangeFields) {
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["statuses.json"], "\"stats_to_one\": [\"agility\"]", "\"stats_to_one\": [\"strength\"]");
		expectError(files, "status 'sleep': stats_to_one takes agility and dexterity, not 'strength'");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["statuses.json"], "\"removes\": [\"sleep\"]", "\"removes\": [\"nap\"]");
		expectError(files, "status 'paralysis': removes names missing status 'nap'");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["statuses.json"], "{\"id\": \"poison\", \"name\": \"Poison\",", "{\"id\": \"poison\", \"name\": \"Poison\", \"tick_damage_percent\": 8,");
		expectError(files, "status 'poison': unknown field 'tick_damage_percent'");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["abilities.json"], "\"cost\": 4, \"target\": \"all_allies\", \"effects\": [\n\t\t\t{\"type\": \"modify_stat\", \"stat\": \"strength\", \"direction\": \"up\"}",
			"\"cost\": 4, \"target\": \"all_allies\", \"effects\": [\n\t\t\t{\"type\": \"modify_stat\", \"stat\": \"strength\", \"direction\": \"up\", \"percent\": 30, \"duration\": 3}");
		expectError(files, "ability 'rally': unknown field 'percent'");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["abilities.json"], "{\"type\": \"modify_stat\", \"stat\": \"agility\", \"direction\": \"up\"}\n\t\t]},\n\t\t{\"id\": \"flare_burst\"",
			"{\"type\": \"modify_stat\", \"stat\": \"agility\", \"direction\": \"sideways\", \"threshold\": 3}\n\t\t]},\n\t\t{\"id\": \"flare_burst\"");
		expectError(files, "ability 'overclock': unknown direction 'sideways' (up or down)");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["abilities.json"], "\"cost\": 4, \"target\": \"all_allies\", \"effects\": [\n\t\t\t{\"type\": \"modify_stat\", \"stat\": \"strength\", \"direction\": \"up\"}",
			"\"cost\": 4, \"target\": \"all_allies\", \"effects\": [\n\t\t\t{\"type\": \"modify_stat\", \"stat\": \"strength\", \"direction\": \"up\", \"threshold\": 10}");
		expectError(files, "ability 'rally': only a stat change with direction down has a threshold");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["abilities.json"], "{\"type\": \"elemental_damage\", \"element\": \"shock\"}\n\t\t]},\n\t\t{\"id\": \"tempest_flare\"",
			"{\"type\": \"elemental_damage\", \"power\": 40, \"element\": \"shock\"}\n\t\t]},\n\t\t{\"id\": \"tempest_flare\"");
		expectError(files, "ability 'storm_spark': a combo's damage takes its power from the participants' abilities, so it has no power");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["combos.json"], "\"abilities\": [\"frostbite\", \"gust\", \"glint\"]", "\"abilities\": [\"frostbite\", \"gust\", \"glint\", \"mend\", \"cleave\"]");
		expectError(files, "combo 'hailstorm': needs 2 to 4 abilities");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["party.json"], "\"abilities\": [\"hull_vent\", \"hull_patch\"]", "\"abilities\": [\"steam_vent\", \"hull_patch\"]");
		expectError(files, "vehicle 'reed_skimmer': vehicle ability 'steam_vent' must be a skill");
	}
}

TEST(loaderRejectsBadNewRuleFields) {
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["statuses.json"], "\"action\": \"forced\", \"forced_target\": \"random_any\"", "\"action\": \"forced\", \"forced_target\": \"random_crowd\"");
		expectError(files, "status 'jumble': unknown forced_target 'random_crowd'");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["statuses.json"], "\"blocks_techniques\": true, \"stacking\": \"keep\"", "\"blocks_techniques\": true, \"stacking\": \"pile\"");
		expectError(files, "status 'silence': unknown stacking 'pile'");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["abilities.json"], "\"id\": \"spark\", \"name\": \"Spark\", \"kind\": \"technique\", \"cost\": 3, \"target\": \"one_enemy\",",
			"\"id\": \"spark\", \"name\": \"Spark\", \"kind\": \"technique\", \"cost\": 3, \"target\": \"one_enemy\", \"field\": true,");
		expectError(files, "ability 'spark': field use needs a party target");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["difficulties.json"], "{\"when\": \"any_ally_hp_half\", \"ability\": \"patch\", \"target\": \"lowest_hp\"}",
			"{\"when\": \"any_ally_hp_half\", \"ability\": \"patch\", \"target\": \"last_attacker\"}");
		expectError(files, "difficulty 'hard': last_attacker needs an ability aimed at foes");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["difficulties.json"], "\"ability\": \"numbing_sting\", \"target\": \"highest_hp\"", "\"ability\": \"numbing_sting\", \"target\": \"has_status\", \"target_status\": \"smog\"");
		expectError(files, "difficulty 'hard': AI target names missing status 'smog'");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["combos.json"], "{\"ability\": \"glint\", \"character\": \"medic\"}", "{\"ability\": \"glint\", \"character\": \"engineer\"}");
		expectError(files, "combo 'twin_arc': character 'engineer' can never use 'glint'");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["combos.json"], "\"abilities\": [\"frostbite\", \"gust\", \"glint\"]", "\"abilities\": [\"frostbite\", \"spark\", \"gust\"]");
		expectError(files, "combo 'hailstorm': no set of different characters can use these abilities together");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["rules.json"], ",\n\t\"rest_revives\": true", "");
		expectError(files, "rules.json:1: missing field 'rest_revives'");
	}
}

TEST(loaderProblemsCarryKeysAndParameters) {
	auto files = fixtures::dataTexts();
	replaceOnce(files["enemies.json"], "\"abilities\": [\"venom_spit\"]", "\"abilities\": [\"venom_spat\"]");
	files.erase("difficulties.json");
	const Result<Database> loaded = loadDatabaseFromTexts(files);
	CHECK(!loaded.ok());
	// The ability list, the slots and the override that use the ability, then the missing file.
	CHECK_EQ(loaded.problems.size(), std::size_t(4));
	if (loaded.problems.size() != 4) {
		return;
	}
	const Problem& missing = loaded.problems[0];
	CHECK_EQ(missing.file, std::string("enemies.json"));
	CHECK_EQ(missing.line, 19);
	CHECK_EQ(missing.entry, std::string("enemy 'rust_hound'"));
	CHECK_EQ(missing.message.key, std::string("load.names_missing_ability"));
	CHECK(missing.message.params == std::vector<std::string>{"venom_spat"});
	CHECK_EQ(loaded.problems[1].message.key, std::string("load.ai_ability_not_in_enemy"));
	CHECK_EQ(loaded.problems[3].message.key, std::string("load.missing_file"));
	CHECK_EQ(loaded.problems[3].file, std::string("difficulties.json"));
	CHECK_EQ(loaded.problems[3].line, 0);
	CHECK_EQ(loaded.error, formatProblems(loaded.problems, problemText()));
	CHECK_EQ(formatProblem(missing, {{"load.names_missing_ability", "ability {0} is not defined"}}),
		std::string("enemies.json:19: enemy 'rust_hound': ability venom_spat is not defined"));
}

TEST(syntaxErrorsCarryAKeyToo) {
	auto files = fixtures::dataTexts();
	replaceOnce(files["items.json"], "{", "{{");
	const Result<Database> loaded = loadDatabaseFromTexts(files);
	CHECK(!loaded.ok() && !loaded.problems.empty());
	if (!loaded.problems.empty()) {
		CHECK_EQ(loaded.problems[0].file, std::string("items.json"));
		CHECK(loaded.problems[0].message.key.compare(0, 5, "json.") == 0);
		CHECK(problemText().count(loaded.problems[0].message.key) == 1);
	}
}

TEST(loaderReadsStatusThresholdsAndEncounterRatings) {
	const Database& db = fixtures::data();
	CHECK_EQ(db.abilities.at("hush").effects.at(0).threshold, 73);
	CHECK_EQ(db.statuses.at("paralysis").enemyDuration, 1);
	CHECK_EQ(db.statuses.at("paralysis").duration, 0);
	CHECK_EQ(db.statuses.at("sleep").recoverChance, 50);

	auto files = fixtures::dataTexts();
	replaceOnce(files["enemies.json"], "\"escape\": false,", "\"escape\": false, \"awareness\": 0, \"escape_rating\": 240,");
	const Result<Database> loaded = loadDatabaseFromTexts(files);
	CHECK(loaded.ok());
	if (loaded.ok()) {
		CHECK_EQ(loaded.value->encounter("warden")->awareness, 0);
		CHECK_EQ(loaded.value->encounter("warden")->escapeRating, 240);
	}
}

TEST(loaderRejectsBadThresholdsAndRatings) {
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["abilities.json"], "\"status\": \"silence\", \"threshold\": 73", "\"status\": \"silence\", \"chance\": 70");
		expectError(files, "ability 'hush': unknown field 'chance'");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["abilities.json"], "\"status\": \"silence\", \"threshold\": 73", "\"status\": \"silence\", \"threshold\": 256");
		expectError(files, "ability 'hush': field 'threshold' must be between 0 and 255");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["statuses.json"], "\"enemy_duration\": 1", "\"enemy_duration\": 100");
		expectError(files, "status 'paralysis': field 'enemy_duration' must be between 0 and 99");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["enemies.json"], "\"escape\": false,", "\"escape\": false, \"escape_rating\": 256,");
		expectError(files, "encounter 'warden': field 'escape_rating' must be between 0 and 255");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["enemies.json"], "\"escape\": false,", "\"escape\": false, \"awareness\": -1,");
		expectError(files, "encounter 'warden': field 'awareness' must be between 0 and 255");
	}
}
