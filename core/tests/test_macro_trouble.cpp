#include "fixtures.h"
#include "rules/auto_policy.h"
#include "rules/loader.h"
#include "rules/progression.h"
#include "test.h"

#include <algorithm>
#include <map>
#include <optional>
#include <utility>
#include <string>
#include <tuple>
#include <vector>

using namespace rules;
using namespace fixtures;

namespace {

void replaceOnce(std::string& text, const std::string& from, const std::string& to) {
	const std::size_t at = text.find(from);
	if (at == std::string::npos) {
		::test::fail(__FILE__, __LINE__, "fixture text not found: " + from);
		return;
	}
	text.replace(at, from.size(), to);
}

MacroStep step(const char* member, CommandKind kind, const char* id = "", MacroTargetKind target = MacroTargetKind::Default, int enemySlot = -1) {
	MacroStep s;
	s.member = member;
	s.kind = kind;
	s.id = id;
	s.target.kind = target;
	s.target.enemySlot = enemySlot;
	return s;
}

// The mystic's spark at the first enemy and the fighter's attack: the scout, medic and engineer have no step.
Macro sparkAndStrike() {
	Macro macro;
	macro.name = "pair";
	macro.steps.push_back(step("mystic", CommandKind::Ability, "spark", MacroTargetKind::Enemy, 0));
	macro.steps.push_back(step("fighter", CommandKind::Attack));
	return macro;
}

void enemiesDefend(Battle& battle) {
	for (int i = battle.partyCount(); i < static_cast<int>(battle.combatants().size()); ++i) {
		battle.setCommand(i, Command::defend());
	}
}

bool sameCommand(const Command& a, const Command& b) {
	return a.kind == b.kind && a.id == b.id && a.target == b.target;
}

bool actedIn(const std::vector<Event>& events, int actor) {
	return std::any_of(events.begin(), events.end(), [actor](const Event& e) { return e.type == EventType::ActionStarted && e.actor == actor; });
}

}

// MacroFallback::Attack turns every step that cannot run into a basic attack; LoseTurn leaves the member out of the round
// at no cost, and the record replays it.
TEST(failedStepsCanAttackOrLoseTheTurn) {
	Battle attack = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	attack.combatant(kMystic).tp = 0;
	std::vector<MacroStepResult> results = attack.applyMacro(sparkAndStrike(), MacroFallback::Attack);
	CHECK(results[0].status == MacroStepStatus::FellBack && results[0].issue == MacroIssue::NotEnoughTp);
	CHECK(attack.command(kMystic).kind == CommandKind::Attack && attack.command(kMystic).target == kFirstEnemy);

	Battle lose = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	lose.combatant(kMystic).tp = 2;
	results = lose.applyMacro(sparkAndStrike(), MacroFallback::LoseTurn);
	CHECK(results[0].status == MacroStepStatus::LostTurn && results[0].issue == MacroIssue::NotEnoughTp);
	CHECK(lose.passing(kMystic) && lose.command(kMystic).kind == CommandKind::None);
	enemiesDefend(lose);
	const std::vector<Event> events = lose.resolveRound();
	CHECK(!actedIn(events, kMystic) && actedIn(events, kFighter));
	CHECK(ofType(events, EventType::ActionCancelled).empty());
	CHECK_EQ(lose.combatant(kMystic).tp, 2);
	CHECK(!lose.passing(kMystic));
	CHECK(lose.record().rounds[0].passed == std::vector<int>{kMystic});

	const std::string text = serializeRecord(lose.record());
	CHECK(text.find("\npass 1\n") != std::string::npos);
	const Result<BattleRecord> parsed = parseRecord(text);
	CHECK(parsed.ok());
	if (parsed.ok()) {
		ComboLog log;
		std::vector<Event> replayed;
		const Result<Battle> again = replayBattle(data(), *parsed.value, log, replayed);
		CHECK(again.ok() && !actedIn(replayed, kMystic) && actedIn(replayed, kFighter));
	}
	// Without a pass the record has no pass line.
	Battle plain = makeBattle(data(), {"gel_mite"}, 1);
	plain.resolveRound();
	CHECK(serializeRecord(plain.record()).find("\npass") == std::string::npos);
}

// Members a macro has no step for: a basic attack the battle fills in (the default), nothing, or core's choice.
TEST(unnamedMembersAttackPassOrTakeCoresChoice) {
	Battle byDefault = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	byDefault.applyMacro(sparkAndStrike());
	CHECK(byDefault.command(kScout).kind == CommandKind::None && !byDefault.passing(kScout));

	Macro none = sparkAndStrike();
	none.unnamed = MacroUnnamed::None;
	Battle passing = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	passing.setCommand(kEngineer, Command::defend());
	passing.applyMacro(none);
	CHECK(passing.passing(kScout) && passing.passing(kMedic) && !passing.passing(kEngineer) && !passing.passing(kFighter));
	enemiesDefend(passing);
	const std::vector<Event> events = passing.resolveRound();
	CHECK(!actedIn(events, kScout) && !actedIn(events, kMedic) && actedIn(events, kEngineer));

	Macro automatic = sparkAndStrike();
	automatic.unnamed = MacroUnnamed::Auto;
	Battle chosen = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	chosen.applyMacro(automatic);
	CHECK(sameCommand(chosen.command(kScout), autoCommand(chosen, kScout)) && sameCommand(chosen.command(kMedic), autoCommand(chosen, kMedic)));

	Database db = data();
	db.battleRules.macroUnnamed = MacroUnnamed::None;
	Battle byRule = makeBattle(db, {"gel_mite", "rust_hound"}, 1);
	byRule.applyMacro(sparkAndStrike());
	CHECK(byRule.passing(kScout));
	byRule.applyMacro(automatic);
	CHECK(sameCommand(byRule.command(kScout), autoCommand(byRule, kScout)));
}

// Trouble holds while a party combatant taking part is down or any step cannot run as written; a step for a member outside
// the battle, such as a vehicle's step in a party battle, is not trouble.
TEST(troubleIsAFallenMemberOrAStepThatCannotRun) {
	Macro macro = sparkAndStrike();
	macro.steps.push_back(step("reed_skimmer", CommandKind::Attack));
	Battle calm = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	CHECK(!calm.macroTrouble(macro).holds);

	Battle troubled = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	troubled.combatant(kMystic).tp = 0;
	troubled.combatant(kMedic).hp = 0;
	addStatus(troubled, kFighter, "sleep", 2);
	const MacroTroubleReport report = troubled.macroTrouble(macro);
	CHECK(report.holds && report.fallen == std::vector<int>{kMedic});
	CHECK(report.troubledSteps == (std::vector<int>{0, 1}) && report.troubledMembers == (std::vector<int>{kMystic, kFighter}));

	MacroStepResult retargeted;
	retargeted.status = MacroStepStatus::Retargeted;
	retargeted.issue = MacroIssue::TargetGone;
	MacroStepResult offGuard;
	offGuard.status = MacroStepStatus::Skipped;
	offGuard.issue = MacroIssue::CaughtOffGuard;
	MacroStepResult unpaid;
	unpaid.issue = MacroIssue::NoUsesLeft;
	CHECK(!stepTroubled(retargeted) && !stepTroubled(offGuard) && stepTroubled(unpaid));
}

// Continue retries the macro with per-step fallback; Recover hands the members recover_who names to core's choice for a
// round where trouble holds and plays the macro in full once it does not.
TEST(recoverRoundsHandMembersToCoresChoice) {
	const Macro macro = sparkAndStrike();
	auto troubledBattle = [](const Database& db) {
		Battle battle = makeBattle(db, {"gel_mite", "rust_hound"}, 1);
		battle.combatant(kMystic).tp = 0;
		return battle;
	};
	Battle continued = troubledBattle(data());
	MacroTroubleReport report = continued.applyMacroRound(macro, MacroTrouble::Continue);
	CHECK(report.holds && report.mode == MacroTrouble::Continue && report.recovered.empty());
	CHECK(sameCommand(continued.command(kMystic), autoCommand(continued, kMystic)) && continued.command(kFighter).kind == CommandKind::Attack);
	CHECK(continued.command(kScout).kind == CommandKind::None);

	Battle all = troubledBattle(data());
	report = all.applyMacroRound(macro, MacroTrouble::Recover);
	CHECK(report.recovered == (std::vector<int>{kFighter, kMystic, kScout, kMedic, kEngineer}));
	for (int i = 0; i < kFirstEnemy; ++i) {
		CHECK(sameCommand(all.command(i), autoCommand(all, i)));
	}
	enemiesDefend(all);
	CHECK(ofType(all.resolveRound(), EventType::CommandsLocked).front().macro.empty());

	Macro uncoveredMacro = macro;
	uncoveredMacro.recoverWho = MacroRecoverWho::Uncovered;
	Battle uncovered = troubledBattle(data());
	report = uncovered.applyMacroRound(uncoveredMacro, MacroTrouble::Recover);
	CHECK(report.recovered == (std::vector<int>{kScout, kMedic, kEngineer}));
	CHECK(sameCommand(uncovered.command(kScout), autoCommand(uncovered, kScout)) && uncovered.command(kFighter).kind == CommandKind::Attack);

	Database db = data();
	db.battleRules.macroRecoverWho = MacroRecoverWho::Troubled;
	Battle troubledOnly = troubledBattle(db);
	report = troubledOnly.applyMacroRound(macro, MacroTrouble::Recover);
	CHECK(report.recovered == std::vector<int>{kMystic} && troubledOnly.command(kScout).kind == CommandKind::None);

	Battle calm = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	report = calm.applyMacroRound(macro, MacroTrouble::Recover);
	CHECK(!report.holds && report.recovered.empty() && calm.command(kMystic).id == "spark");
}

// Auto-battle under Continue or Recover keeps going after a member falls and a step cannot be paid for; Stop keeps the stop
// rules, in party and vehicle battles.
TEST(autoBattleStopsOnlyUnderStop) {
	auto play = [](MacroTrouble mode, bool aboard) {
		BattleSetup setup = makeSetup(data(), {"gel_mite", "rust_hound"}, 3);
		if (aboard) {
			setup.flags.vehicleBattle = true;
			setup.vehicle = "reed_skimmer";
		}
		Battle battle = makeBattle(data(), setup);
		Macro macro = sparkAndStrike();
		macro.steps.push_back(step("reed_skimmer", CommandKind::Ability, "hull_vent", MacroTargetKind::Enemy, 0));
		battle.combatant(kMystic).tp = 0;
		battle.combatant(aboard ? static_cast<int>(setup.party.size()) : kMedic).skillUses["hull_vent"] = 0;
		battle.startAuto(macro, mode);
		battle.combatant(kMedic).hp = aboard ? battle.combatant(kMedic).hp : 0;
		int rounds = 0;
		while (battle.outcome() == Outcome::Ongoing && rounds < 3 && battle.fillFromAuto()) {
			battle.resolveRound();
			++rounds;
		}
		return std::make_pair(rounds, battle.autoStop());
	};
	for (bool aboard : {false, true}) {
		const auto stopped = play(MacroTrouble::Stop, aboard);
		CHECK(stopped.first == 0 && stopped.second == AutoStop::CannotPay);
		for (MacroTrouble mode : {MacroTrouble::Continue, MacroTrouble::Recover}) {
			const auto kept = play(mode, aboard);
			CHECK(kept.first > 0 && (kept.second == AutoStop::None || kept.second == AutoStop::BattleEnded));
		}
	}
	Battle reported = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	reported.combatant(kMystic).tp = 0;
	reported.startAuto(sparkAndStrike(), MacroTrouble::Recover);
	CHECK(reported.fillFromAuto() && reported.autoActive() && reported.lastAutoTrouble().mode == MacroTrouble::Recover);
	CHECK(reported.lastAutoTrouble().troubledMembers == std::vector<int>{kMystic} && reported.lastAutoTrouble().recovered.size() == 5);
}

// checkMacro: each step's state this round, for a host marking steps before the player confirms.
TEST(checkMacroGivesEachStepsStateThisRound) {
	Macro macro;
	macro.name = "check";
	macro.steps.push_back(step("mystic", CommandKind::Ability, "spark", MacroTargetKind::Enemy, 0));
	macro.steps.push_back(step("fighter", CommandKind::Attack));
	macro.steps.push_back(step("scout", CommandKind::Ability, "cleave"));
	macro.steps.push_back(step("medic", CommandKind::Ability, "rekindle"));
	macro.steps.push_back(step("engineer", CommandKind::Item, "gear_salve"));
	Battle battle = makeBattle(data(), {"gel_mite", "rust_hound"}, 1);
	std::vector<MacroStepCheck> checks = battle.checkMacro(macro);
	CHECK(checks.size() == 5 && checks[0].ok && checks[1].ok);
	CHECK(!checks[2].ok && checks[2].issue == MacroIssue::UnknownAbility && checks[2].refusal.key == "command.unknown_ability");
	CHECK(!checks[3].ok && checks[3].issue == MacroIssue::NoTarget);
	CHECK(!checks[4].ok && checks[4].issue == MacroIssue::NoItem);

	battle.combatant(kMystic).tp = 0;
	addStatus(battle, kFighter, "paralysis", 2);
	checks = battle.checkMacro(macro);
	CHECK(checks[0].issue == MacroIssue::NotEnoughTp && !checks[0].ok);
	CHECK(checks[1].issue == MacroIssue::CannotAct && checks[1].status == "paralysis" && checks[1].actor == kFighter);
	CHECK(battle.command(kMystic).kind == CommandKind::None);
}

// reset_stat takes one stat's battle change away and leaves the others.
TEST(resetStatClearsOneStatsChange) {
	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["abilities.json"], "\"id\": \"glint\"", "\"id\": \"glint_reset\", \"name\": \"Reset\", \"kind\": \"technique\", \"cost\": 0, \"target\": \"one_enemy\", \"effects\": [{\"type\": \"reset_stat\", \"stat\": \"defense\"}]}, {\"id\": \"glint\"");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok());
	if (!loaded.ok()) {
		return;
	}
	Database db = *loaded.value;
	db.characters["mystic"].abilities.push_back("glint_reset");
	Battle battle = makeBattle(db, {"gel_mite"}, 1);
	battle.combatant(kFirstEnemy).setModifier(Stat::Defense, -3);
	battle.combatant(kFirstEnemy).setModifier(Stat::Agility, 2);
	battle.setCommand(kMystic, Command::ability("glint_reset", kFirstEnemy));
	othersDefend(battle);
	const std::optional<Event> reset = firstBy(battle.resolveRound(), EventType::StatModified, kMystic);
	CHECK(reset && reset->stat == Stat::Defense && reset->amount == 0);
	const Combatant& enemy = battle.combatant(kFirstEnemy);
	CHECK(enemy.effective(Stat::Defense) == enemy.base(Stat::Defense) && enemy.effective(Stat::Agility) == enemy.base(Stat::Agility) + 2);
	std::map<std::string, std::string> bad = texts;
	replaceOnce(bad["abilities.json"], "\"stat\": \"defense\"}]", "\"stat\": \"luck\"}]");
	CHECK(!loadDatabaseFromTexts(bad).ok());
}

// A raise of attack moves the attack stat only; a raise of strength moves strength, and attack with it.
TEST(attackAndStrengthChangesMoveTheirOwnValues) {
	Battle battle = makeBattle(data(), {"gel_mite"}, 1);
	Combatant& fighter = battle.combatant(kFighter);
	const int strength = fighter.effective(Stat::Strength);
	const int attack = fighter.effective(Stat::Attack);
	fighter.setModifier(Stat::Attack, 7);
	CHECK(fighter.effective(Stat::Strength) == strength && fighter.effective(Stat::Attack) == attack + 7);
	fighter.setModifier(Stat::Strength, 5);
	CHECK(fighter.effective(Stat::Strength) == strength + 5 && fighter.effective(Stat::Attack) == attack + 12);
}

// A flat revive stands the target up at exactly its amount, 1 included; a percent of 0 stands it up at 1.
TEST(revivesStandAtTheirExactAmount) {
	for (const char* effect : {"{\"type\": \"revive\", \"hp\": 1}", "{\"type\": \"revive\", \"power\": 0}", "{\"type\": \"revive\", \"hp\": 7}"}) {
		std::map<std::string, std::string> texts = dataTexts();
		replaceOnce(texts["abilities.json"], "\"id\": \"glint\"", std::string("\"id\": \"raise_exact\", \"name\": \"Raise\", \"kind\": \"technique\", \"cost\": 0, \"target\": \"one_fallen_ally\", \"field\": true, \"effects\": [") + effect + "]}, {\"id\": \"glint\"");
		const Result<Database> loaded = loadDatabaseFromTexts(texts);
		CHECK(loaded.ok());
		if (!loaded.ok()) {
			continue;
		}
		Database db = *loaded.value;
		db.characters["medic"].abilities.push_back("raise_exact");
		const int expected = std::string(effect).find("7") != std::string::npos ? 7 : 1;
		Battle battle = makeBattle(db, {"gel_mite"}, 1);
		battle.combatant(kFighter).hp = 0;
		addStatus(battle, kFighter, "defeated", 0);
		battle.setCommand(kMedic, Command::ability("raise_exact", kFighter));
		othersDefend(battle);
		const std::optional<Event> revived = firstBy(battle.resolveRound(), EventType::Revived, kMedic);
		CHECK(revived && revived->hp == expected);
		CHECK_EQ(battle.partyState()[kFighter].hp, expected);

		std::vector<PartyMember> party = makeSetup(db, {"gel_mite"}, 1).party;
		party[kFighter].hp = 0;
		party[kFighter].statuses.push_back({"defeated", 0});
		const FieldUse used = useAbilityOutsideBattle(db, party, kMedic, "raise_exact", kFighter);
		CHECK(used.rejection.empty());
		CHECK_EQ(party[kFighter].hp, expected);
	}
}

// A revive of `hp: 0` stands the target at 0 for the rest of the action, so a heal after it lands exactly its amount (not one
// more, as after `hp: 1`); with nothing raising it, the target stands at 1.
TEST(aReviveOfZeroLetsTheHealAfterItCountFromZero) {
	for (const auto& [effects, expected] : std::vector<std::pair<std::string, int>>{
			 {"{\"type\": \"revive\", \"hp\": 0}, {\"type\": \"heal\", \"power\": 30}", 30},
			 {"{\"type\": \"revive\", \"hp\": 1}, {\"type\": \"heal\", \"power\": 30}", 31},
			 {"{\"type\": \"revive\", \"hp\": 0}", 1}}) {
		std::map<std::string, std::string> texts = dataTexts();
		replaceOnce(texts["items.json"], "{\"id\": \"tonic\"", "{\"id\": \"mend\", \"name\": \"Mend\", \"target\": \"one_fallen_ally\", \"field\": true, \"effects\": [" + effects + "]}, {\"id\": \"tonic\"");
		const Result<Database> loaded = loadDatabaseFromTexts(texts);
		CHECK(loaded.ok());
		if (!loaded.ok()) {
			continue;
		}
		const Database& db = *loaded.value;
		Battle battle = makeBattle(db, {"gel_mite"}, 1, Inventory{{"mend", 1}});
		battle.combatant(kFighter).hp = 0;
		addStatus(battle, kFighter, "defeated", 0);
		CHECK(battle.setCommand(kMedic, Command::item("mend", kFighter)).key.empty());
		othersDefend(battle);
		const std::optional<Event> revived = firstBy(battle.resolveRound(), EventType::Revived, kMedic);
		CHECK(revived && revived->hp == (expected == 1 ? 1 : expected == 30 ? 0 : 1));
		CHECK_EQ(battle.partyState()[kFighter].hp, expected);

		std::vector<PartyMember> party = makeSetup(db, {"gel_mite"}, 1).party;
		party[kFighter].hp = 0;
		party[kFighter].statuses.push_back({"defeated", 0});
		Inventory inventory{{"mend", 1}};
		CHECK(useItemOutsideBattle(db, party, inventory, "mend", kFighter).rejection.empty());
		CHECK_EQ(party[kFighter].hp, expected);
	}
}

// Field use skips the effects that mean nothing outside battle (modify_stat, reset_stat, strip_modifiers, grant_affinity for
// the battle) and still loads with them beside one that acts; with only those it is refused at load.
TEST(fieldUseSkipsBattleOnlyEffects) {
	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["items.json"], "{\"id\": \"tonic\"", "{\"id\": \"brace\", \"name\": \"Brace\", \"target\": \"one_ally\", \"field\": true, \"effects\": [{\"type\": \"reset_stat\", \"stat\": \"defense\"}, {\"type\": \"modify_stat\", \"stat\": \"defense\", \"direction\": \"up\", \"power\": 5}, {\"type\": \"strip_modifiers\"}, {\"type\": \"heal\", \"power\": 10}]}, {\"id\": \"tonic\"");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok());
	if (loaded.ok()) {
		std::vector<PartyMember> party = makeSetup(*loaded.value, {"gel_mite"}, 1).party;
		const int max = memberStats(*loaded.value, party[kFighter]).maxHp;
		party[kFighter].hp = max - 20;
		Inventory inventory{{"brace", 1}};
		const FieldUse used = useItemOutsideBattle(*loaded.value, party, inventory, "brace", kFighter);
		CHECK(used.rejection.empty());
		CHECK_EQ(party[kFighter].hp, max - 10);
		CHECK(!inventory.count("brace"));
		party[kFighter].hp = max;
		inventory["brace"] = 1;
		CHECK(useItemOutsideBattle(*loaded.value, party, inventory, "brace", kFighter).rejection.empty() && inventory.count("brace"));
	}
	std::map<std::string, std::string> only = dataTexts();
	replaceOnce(only["items.json"], "{\"id\": \"tonic\"", "{\"id\": \"brace\", \"name\": \"Brace\", \"target\": \"one_ally\", \"field\": true, \"effects\": [{\"type\": \"reset_stat\", \"stat\": \"defense\"}]}, {\"id\": \"tonic\"");
	CHECK(!loadDatabaseFromTexts(only).ok());
}

// consume_when_no_effect (rules.json, an item's own wins): a consumable item that changed nothing is used up, in battle and
// in the field, with its no-effect event; never one with consumed false.
TEST(itemsThatChangeNothingCanStillBeUsedUp) {
	for (const auto& [rulesKey, itemKeys, used] : std::vector<std::tuple<std::string, std::string, bool>>{
			 {"", "", false},
			 {"\"consume_when_no_effect\": true,", "", true},
			 {"\"consume_when_no_effect\": true,", "\"consume_when_no_effect\": false, ", false},
			 {"", "\"consume_when_no_effect\": true, ", true},
			 {"\"consume_when_no_effect\": true,", "\"consumed\": false, ", false}}) {
		std::map<std::string, std::string> texts = dataTexts();
		replaceOnce(texts["rules.json"], "{", "{" + rulesKey);
		replaceOnce(texts["items.json"], "{\"id\": \"tonic\",", "{\"id\": \"tonic\", " + itemKeys);
		const Result<Database> loaded = loadDatabaseFromTexts(texts);
		CHECK(loaded.ok());
		if (!loaded.ok()) {
			continue;
		}
		const Database& db = *loaded.value;
		Battle battle = makeBattle(db, {"gel_mite"}, 1, Inventory{{"tonic", 2}});
		CHECK(battle.setCommand(kMedic, Command::item("tonic", kFighter)).key.empty());
		othersDefend(battle);
		enemiesDefend(battle);
		const std::vector<Event> events = battle.resolveRound();
		CHECK(!ofType(events, EventType::ItemHadNoEffect).empty());
		CHECK_EQ(ofType(events, EventType::ResourceSpent).empty(), !used);
		CHECK_EQ(battle.inventory().at("tonic"), used ? 1 : 2);

		std::vector<PartyMember> party = makeSetup(db, {"gel_mite"}, 1).party;
		Inventory inventory{{"tonic", 2}};
		const FieldUse field = useItemOutsideBattle(db, party, inventory, "tonic", kFighter);
		CHECK(field.rejection.empty());
		CHECK(std::any_of(field.events.begin(), field.events.end(), [](const Event& e) { return e.type == EventType::ItemHadNoEffect; }));
		CHECK_EQ(inventory.at("tonic"), used ? 1 : 2);
	}
}

// rules.json's trouble defaults and a data macro's options load; "default" or an unknown word is refused.
TEST(troubleDefaultsAndMacroOptionsLoad) {
	CHECK(data().battleRules.autoMacroOnTrouble == MacroTrouble::Stop && data().battleRules.namedMacroOnTrouble == MacroTrouble::Continue);
	CHECK(data().battleRules.macroRecoverWho == MacroRecoverWho::All && data().battleRules.macroUnnamed == MacroUnnamed::Attack);
	CHECK(data().macro("opener")->onTrouble == MacroTrouble::Default);
	std::map<std::string, std::string> texts = dataTexts();
	replaceOnce(texts["rules.json"], "{", "{\"auto_macro_on_trouble\": \"recover\", \"named_macro_on_trouble\": \"stop\", \"macro_recover_who\": \"uncovered\", \"macro_unnamed\": \"auto\",");
	replaceOnce(texts["party.json"], "\"name\": \"opener\",", "\"name\": \"opener\", \"on_trouble\": \"continue\", \"recover_who\": \"troubled\", \"unnamed\": \"none\",");
	const Result<Database> loaded = loadDatabaseFromTexts(texts);
	CHECK(loaded.ok());
	if (loaded.ok()) {
		const BattleRules& rules = loaded.value->battleRules;
		CHECK(rules.autoMacroOnTrouble == MacroTrouble::Recover && rules.namedMacroOnTrouble == MacroTrouble::Stop);
		CHECK(rules.macroRecoverWho == MacroRecoverWho::Uncovered && rules.macroUnnamed == MacroUnnamed::Auto);
		const Macro* opener = loaded.value->macro("opener");
		CHECK(opener->onTrouble == MacroTrouble::Continue && opener->recoverWho == MacroRecoverWho::Troubled && opener->unnamed == MacroUnnamed::None);
	}
	for (const char* bad : {"{\"macro_unnamed\": \"default\",", "{\"auto_macro_on_trouble\": \"retry\","}) {
		std::map<std::string, std::string> broken = dataTexts();
		replaceOnce(broken["rules.json"], "{", bad);
		CHECK(!loadDatabaseFromTexts(broken).ok());
	}
	std::map<std::string, std::string> brokenMacro = dataTexts();
	replaceOnce(brokenMacro["party.json"], "\"name\": \"opener\",", "\"name\": \"opener\", \"on_trouble\": \"default\",");
	CHECK(!loadDatabaseFromTexts(brokenMacro).ok());
}
