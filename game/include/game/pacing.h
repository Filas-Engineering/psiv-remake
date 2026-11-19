#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace game {

// Per-area pacing numbers (encounters.md, "Pacing measurement").
struct AreaPacing {
	int visits = 0;
	int battles = 0;
	int steps = 0;
	std::int64_t fieldMs = 0;
	std::int64_t battleMs = 0;
	std::int64_t eventMs = 0;
	int experience = 0;
	int money = 0;
	// Field time and steps between the end of one battle and the start of the next (M2).
	std::vector<std::int64_t> msBetweenBattles;
	std::vector<int> stepsBetweenBattles;
};

struct BossPacing {
	std::string flag;
	std::string encounter;
	std::string area;
	// Average active party level, in hundredths, at the first command input (M5).
	int averageLevelX100 = 0;
	// Time from entering the dungeon to the end of the boss battle (M6), or -1 outside a dungeon.
	std::int64_t dungeonMs = -1;
};

class PacingRecorder {
public:
	void enterArea(const std::string& area, bool dungeon);
	void fieldTime(int ms);
	void eventTime(int ms);
	void step();
	void battleStarted();
	void battleEnded(int rounds, int roundMs, int experience, int money);
	void bossStarted(const std::string& flag, const std::string& encounter, int averageLevelX100);
	void bossEnded();

	const std::map<std::string, AreaPacing>& areas() const { return areas_; }
	const std::vector<BossPacing>& bosses() const { return bosses_; }
	int totalBattles() const;

	// A plain text table, one row per area in first-visit order, then one row per boss.
	std::string table() const;

private:
	AreaPacing& current();

	std::map<std::string, AreaPacing> areas_;
	std::vector<std::string> order_;
	std::vector<BossPacing> bosses_;
	std::string area_;
	bool inDungeon_ = false;
	std::int64_t dungeonMs_ = 0;
	std::int64_t sinceBattleMs_ = 0;
	int sinceBattleSteps_ = 0;
	bool hadBattle_ = false;
	bool bossOpen_ = false;
};

}
