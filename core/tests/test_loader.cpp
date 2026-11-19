#include "fixtures.h"
#include "rules/loader.h"
#include "test.h"

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
	CHECK(db.statuses.at("paralysis").skipsTurn);
	CHECK_EQ(db.startingInventory.at("tonic"), 3);
	const Macro* opener = db.macro("opener");
	CHECK(opener != nullptr);
	if (opener) {
		CHECK_EQ(opener->steps.size(), std::size_t(5));
		CHECK_EQ(opener->steps[4].allyMember, std::string("fighter"));
		CHECK_EQ(opener->steps[0].enemySlot, 0);
	}
	CHECK_EQ(db.combos.size(), std::size_t(3));
}

TEST(loaderRejectsEnemyWithMissingAbility) {
	auto files = fixtures::dataTexts();
	replaceOnce(files["enemies.json"], "\"abilities\": [\"venom_spit\"]", "\"abilities\": [\"venom_spat\"]");
	expectError(files, "enemies.json:20: enemy 'rust_hound': names missing ability 'venom_spat'");
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
		replaceOnce(files["party.json"], "{\"member\": \"mystic\", \"command\": \"ability\", \"id\": \"spark\", \"target\": 0}",
			"{\"member\": \"mystic\", \"command\": \"ability\", \"id\": \"mend\", \"target\": 0}");
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
		replaceOnce(files["abilities.json"], "\"target\": \"one_ally\", \"effects\": [\n\t\t\t{\"type\": \"revive\"", "\"target\": \"all_enemies\", \"effects\": [\n\t\t\t{\"type\": \"revive\"");
		expectError(files, "ability 'rekindle': revive effects need target one_ally or all_allies");
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
