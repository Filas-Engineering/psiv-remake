#include "fixtures.h"
#include "rules/loader.h"
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
	CHECK(db.enemies.at("rust_hound").ai[2].condition.kind == AiConditionKind::SelfHpBelow);
	CHECK_EQ(db.enemies.at("rust_hound").ai[2].condition.value, 50);
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
	expectError(files, "enemies.json:21: enemy 'rust_hound': names missing ability 'venom_spat'");
}

TEST(loaderRejectsAiAbilityTheEnemyDoesNotHave) {
	auto files = fixtures::dataTexts();
	replaceOnce(files["enemies.json"], "{\"ability\": \"bite\", \"weight\": 1}", "{\"ability\": \"spark\", \"weight\": 1}");
	expectError(files, "enemy 'gel_mite': AI uses ability 'spark' that is not in the enemy's abilities");
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
		replaceOnce(files["abilities.json"], "\"power\": 26", "\"pwer\": 26");
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
	expectError(files, "items.json: line 3: expected ',' or '}' in object");
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
	CHECK(db.battleRules.experienceSplit == ExperienceSplit::EachLiving);
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
		replaceOnce(files["rules.json"], "\"each_living\"", "\"winner_takes_all\"");
		expectError(files, "rules.json:4: unknown experience split 'winner_takes_all'");
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
}

TEST(loaderRejectsBadNewRuleFields) {
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["statuses.json"], "\"action\": \"forced\", \"forced_target\": \"random_any\"", "\"action\": \"forced\", \"forced_target\": \"random_crowd\"");
		expectError(files, "status 'jumble': unknown forced_target 'random_crowd'");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["statuses.json"], "\"blocks_techniques\": true, \"duration\": 3, \"stacking\": \"extend\"", "\"blocks_techniques\": true, \"duration\": 3, \"stacking\": \"pile\"");
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
		replaceOnce(files["difficulties.json"], "{\"ability\": \"rouse\", \"weight\": 8, \"when\": {\"type\": \"ally_defeated\"}}",
			"{\"ability\": \"rouse\", \"weight\": 8, \"when\": {\"type\": \"ally_defeated\"}, \"target\": \"last_attacker\"}");
		expectError(files, "difficulty 'hard': last_attacker needs an ability aimed at foes");
	}
	{
		auto files = fixtures::dataTexts();
		replaceOnce(files["difficulties.json"], "{\"ability\": \"steam_vent\", \"weight\": 3}", "{\"ability\": \"steam_vent\", \"weight\": 3, \"target\": \"has_status\", \"target_status\": \"smog\"}");
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
