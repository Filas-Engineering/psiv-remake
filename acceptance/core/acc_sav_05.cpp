#include "support.h"

#include "rules/formulas.h"
#include "rules/progression.h"

using namespace rules;

ACC_CHECK(acc_sav_05, "ACC-SAV-05", "SAV-022, SAV-030, SAV-033") {
	const Database& db = support::data(t);

	for (const auto& [id, character] : db.characters) {
		// Level-ups follow the experience curve, one point at a time.
		PartyMember member = makePartyMember(db, character, 1);
		for (int level = 1; level < formulas::kMaxLevel; ++level) {
			const int needed = experienceToNextLevel(character, level);
			t.expect(grantExperience(member, db, needed - 1).levelsGained == 0, id + ": levelled up one point short of level " + std::to_string(level + 1));
			const int hp = member.hp;
			const int tp = member.tp;
			const LevelUp up = grantExperience(member, db, 1);
			t.expect(up.levelsGained == 1 && member.level == level + 1, id + ": did not reach level " + std::to_string(level + 1) + " with exactly the curve's experience");
			t.expect(member.hp == hp && member.tp == tp, id + ": reaching level " + std::to_string(level + 1) + " changed current hit points or technique points");
			// Stat growth: base plus growth per level, with nothing dropped at any level.
			const Stats s = statsAtLevel(character, member.level);
			t.expect(s.maxHp == character.base.maxHp + character.growth.maxHp * (member.level - 1) && s.strength == character.base.strength + character.growth.strength * (member.level - 1) &&
				s.defense == character.base.defense + character.growth.defense * (member.level - 1) && s.agility == character.base.agility + character.growth.agility * (member.level - 1),
				id + ": stats at level " + std::to_string(member.level) + " are not base plus growth");
		}

		// The level cap: experience past it changes nothing, and stats at the cap are the growth data's values.
		const Stats atCap = statsAtLevel(character, formulas::kMaxLevel);
		t.expect(grantExperience(member, db, 1000000).levelsGained == 0 && member.level == formulas::kMaxLevel, id + ": levelled past the cap");
		t.expect(statsAtLevel(character, formulas::kMaxLevel + 5).maxHp == atCap.maxHp, id + ": stats keep growing past the cap");
		t.expect(atCap.mental == character.base.mental + character.growth.mental * (formulas::kMaxLevel - 1) && atCap.dexterity == character.base.dexterity + character.growth.dexterity * (formulas::kMaxLevel - 1),
			id + ": stats at the cap are not applied as the growth data intends");
	}

	// Battle rewards and the experience split at Original difficulty: an even share, rounded down, for each member
	// not defeated, an asleep one included.
	BattleSetup s = support::setup(db, {"gel_mite", "rust_hound"}, 3, 20);
	s.difficulty = "original";
	Battle battle = support::create(db, s);
	battle.combatant(2).hp = 0;
	battle.combatant(2).statuses = {{kDefeatedStatus, 0}};
	battle.combatant(4).statuses = {{"sleep", 0}};
	for (int e = 5; e < 7; ++e) {
		battle.combatant(e).hp = 1;
	}
	for (int round = 0; round < 10 && battle.outcome() == Outcome::Ongoing; ++round) {
		battle.resolveRound();
	}
	t.expect(battle.outcome() == Outcome::Victory, "setup: the reward battle was not won");
	const Rewards rewards = battle.rewards();
	const int experience = db.enemy("gel_mite")->experience + db.enemy("rust_hound")->experience;
	t.expect(rewards.experience == experience, "battle experience is not the defeated enemies' total");
	int living = 0;
	for (int i = 0; i < battle.partyCount(); ++i) {
		living += battle.combatant(i).alive() ? 1 : 0;
	}
	t.expect(living == 4, "setup: four members should stand, one of them asleep");
	for (int i = 0; i < battle.partyCount(); ++i) {
		const int share = battle.combatant(i).alive() ? experience / living : 0;
		t.expect(rewards.memberExperience[i] == share, "member " + std::to_string(i) + " got " + std::to_string(rewards.memberExperience[i]) + " experience, an even share among those not defeated gives " + std::to_string(share));
	}

	t.note("every character levels exactly on the core curve to the cap without a level-up changing current hit points or technique points, stats are base plus growth at every level including the cap, battle experience is the defeated enemies' total split evenly among the members not defeated");
}
