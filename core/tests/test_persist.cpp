#include "fixtures.h"
#include "rules/json.h"
#include "rules/names.h"
#include "rules/rng.h"
#include "test.h"

using namespace rules;
using namespace fixtures;

namespace {

// Every value of the enum round-trips through its name, and an unknown name is refused.
template <class E>
int checkNames(bool (*parse)(const std::string&, E&)) {
	int count = 0;
	for (int i = 0; std::string(toString(static_cast<E>(i))) != "unknown"; ++i) {
		E parsed{};
		const std::string name = toString(static_cast<E>(i));
		if (!parse(name, parsed) || parsed != static_cast<E>(i)) {
			::test::fail(__FILE__, __LINE__, "name does not round-trip: " + name);
		}
		if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos || name.front() == '_') {
			::test::fail(__FILE__, __LINE__, "name is not lowercase with underscores: " + name);
		}
		++count;
	}
	E parsed{};
	if (parse("no_such_name", parsed) || count == 0) {
		::test::fail(__FILE__, __LINE__, "an unknown name parsed, or the enum has no names");
	}
	return count;
}

}

TEST(rngStateCanBeSavedAndRestored) {
	Rng rng(42);
	for (int i = 0; i < 5; ++i) {
		rng.next();
	}
	const std::uint64_t saved = rng.state();
	const std::uint64_t a = rng.next();
	const int b = rng.range(1, 100);
	Rng restored(7);
	restored.restore(saved);
	CHECK_EQ(restored.next(), a);
	CHECK_EQ(restored.range(1, 100), b);
	Rng copy(saved);
	CHECK_EQ(copy.next(), a);
}

TEST(macrosRoundTripThroughText) {
	std::vector<Macro> macros = data().macros;
	Macro odd;
	odd.name = "night shift: 2";
	odd.steps.push_back({"medic", CommandKind::Ability, "rekindle", {MacroTargetKind::Member, "fighter", -1}});
	odd.steps.push_back({"scout", CommandKind::Attack, "", {MacroTargetKind::Enemy, "", 3}});
	macros.push_back(odd);
	const std::string text = serializeMacros(macros);
	const Result<std::vector<Macro>> parsed = parseMacros(text);
	CHECK(parsed.ok());
	if (!parsed.ok()) {
		return;
	}
	CHECK_EQ(parsed.value->size(), macros.size());
	CHECK_EQ(serializeMacros(*parsed.value), text);
	const Macro& back = parsed.value->back();
	CHECK(back.name == odd.name && back.steps[0].target.member == "fighter" && back.steps[1].target.enemySlot == 3);
	CHECK(back.steps[1].target.kind == MacroTargetKind::Enemy && back.steps[1].id.empty());

	Macro worn;
	worn.name = "fan";
	MacroStep fan = {"scout", CommandKind::Item, "squall_fan", {MacroTargetKind::Enemy, "", 0}};
	fan.piece = true;
	worn.steps = {fan, {"medic", CommandKind::Item, "tonic", {}}};
	const Result<std::vector<Macro>> pieces = parseMacros(serializeMacros({worn}));
	CHECK(pieces.ok() && pieces.value->at(0).steps[0].piece && pieces.value->at(0).steps[0].kind == CommandKind::Item);
	CHECK(pieces.ok() && !pieces.value->at(0).steps[1].piece);
	CHECK(serializeMacros({worn}).find("step scout piece squall_fan enemy - 0\n") != std::string::npos);
	// A vehicle's step (R95) names the vehicle as its member, beside party and piece steps.
	const std::string mixed = "macros 2\nmacro mixed\nstep fighter attack - default - -1\nstep reed_skimmer ability hull_vent enemy - 0\n"
		"step scout piece squall_fan enemy - 1\nstep dune_crawler escape - default - -1\nend\n";
	const Result<std::vector<Macro>> vehicles = parseMacros(mixed);
	CHECK(vehicles.ok());
	if (vehicles.ok()) {
		const std::vector<MacroStep>& steps = vehicles.value->at(0).steps;
		CHECK(steps.size() == 4 && steps[1].member == "reed_skimmer" && steps[1].kind == CommandKind::Ability && steps[1].id == "hull_vent");
		CHECK(steps[1].target.kind == MacroTargetKind::Enemy && steps[1].target.enemySlot == 0 && steps[2].piece && steps[3].kind == CommandKind::Escape);
		CHECK_EQ(serializeMacros(*vehicles.value), mixed);
	}
	// Saves written before piece steps carry version 1, which still reads; "piece" is no command there.
	const Result<std::vector<Macro>> older = parseMacros("macros 1\nmacro a\nstep fighter item tonic default - -1\nend\n");
	CHECK(older.ok() && older.value->at(0).steps[0].kind == CommandKind::Item && !older.value->at(0).steps[0].piece);
	CHECK(parseMacros("macros 1\nmacro a\nstep fighter piece squall_fan default - -1\nend\n").error.find("bad step") != std::string::npos);
	CHECK(parseMacros("macros 3\nend\n").error.find("version 3 is not supported") != std::string::npos);
	CHECK(parseMacros("macros 1\nstep fighter attack - default - -1\nend\n").error.find("'step' before any 'macro'") != std::string::npos);
	CHECK(parseMacros("macros 1\nmacro a\n").error.find("without 'end'") != std::string::npos);
	CHECK(parseMacros("macros 1\nmacro a\nstep fighter swing - default - -1\nend\n").error.find("line 3: bad step") != std::string::npos);
}

TEST(comboLogRoundTripsThroughText) {
	ComboLog log;
	log.discover("storm_spark", 125000, {"mystic", "scout"});
	log.discover("tempest_flare", 0, {});
	const std::string text = serializeComboLog(log);
	const Result<ComboLog> parsed = parseComboLog(text);
	CHECK(parsed.ok());
	if (!parsed.ok()) {
		return;
	}
	CHECK_EQ(serializeComboLog(*parsed.value), text);
	CHECK(parsed.value->entry("storm_spark") && parsed.value->entry("storm_spark")->characters == (std::vector<std::string>{"mystic", "scout"}));
	CHECK_EQ(parsed.value->entries()[1].recipe, std::string("tempest_flare"));
	CHECK(!parseComboLog("combo_log 1\nentry storm_spark 1 -\nentry storm_spark 2 -\nend\n").ok());
	CHECK(!parseComboLog("combo_log 1\nentry storm_spark x -\nend\n").ok());
}

TEST(everyPublicEnumHasAParser) {
	CHECK_EQ(checkNames(parseStat), 7);
	CHECK_EQ(checkNames(parseSide), 2);
	CHECK_EQ(checkNames(parseTargeting), 8);
	CHECK_EQ(checkNames(parseAbilityKind), 4);
	CHECK_EQ(checkNames(parseEffectKind), 18);
	CHECK_EQ(checkNames(parseCreatureKind), 3);
	CHECK_EQ(checkNames(parseTravelTarget), 2);
	CHECK_EQ(checkNames(parseEquipSlot), 7);
	CHECK_EQ(checkNames(parseOpening), 3);
	CHECK_EQ(checkNames(parseRewardMultiplier), 3);
	CHECK_EQ(checkNames(parseStatusAction), 3);
	CHECK_EQ(checkNames(parseForcedTarget), 3);
	CHECK_EQ(checkNames(parseStatusStacking), 3);
	CHECK_EQ(checkNames(parseAiConditionKind), 18);
	CHECK_EQ(checkNames(parseAiTargetRule), 7);
	CHECK_EQ(checkNames(parseCommandKind), 6);
	CHECK_EQ(checkNames(parseMacroTargetKind), 5);
	CHECK_EQ(checkNames(parseMacroStepStatus), 5);
	CHECK_EQ(checkNames(parseMacroIssue), 14);
	CHECK_EQ(checkNames(parseScriptedEndKind), 3);
	CHECK_EQ(checkNames(parseAutoStop), 5);
	CHECK_EQ(checkNames(parseEventType), 29);
	CHECK_EQ(checkNames(parseOutcome), 5);
	CHECK_EQ(checkNames(parseCancelReason), 7);
	CHECK_EQ(checkNames(parseEffectiveness), 4);
	CHECK_EQ(checkNames(parseStatusEnd), 5);
	CHECK_EQ(checkNames(parseResourceKind), 4);
	EventType type = EventType::BattleStarted;
	CHECK(parseEventType("DamageDealt", type) && type == EventType::DamageDealt);
	CHECK_EQ(std::string(toString(EventType::DamageDealt)), std::string("damage_dealt"));
	RewardMultiplier multiplier = RewardMultiplier::X1;
	CHECK(parseRewardMultiplier("1.5", multiplier) && multiplier == RewardMultiplier::X1_5);
	CHECK_EQ(std::string(toString(RewardMultiplier::X1_5)), std::string("x1_5"));
	JsonValue::Type jsonType = JsonValue::Type::Null;
	CHECK(parseJsonType("object", jsonType) && jsonType == JsonValue::Type::Object);
	CHECK(!parseJsonType("dict", jsonType));
}
