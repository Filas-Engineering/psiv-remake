#include "game/pacing.h"

#include <algorithm>
#include <cstdio>

namespace game {

AreaPacing& PacingRecorder::current() {
	return areas_[area_];
}

void PacingRecorder::enterArea(const std::string& area, bool dungeon) {
	if (!areas_.count(area)) {
		order_.push_back(area);
	}
	area_ = area;
	current().visits++;
	if (dungeon && !inDungeon_) {
		dungeonMs_ = 0;
	}
	inDungeon_ = dungeon;
}

void PacingRecorder::fieldTime(int ms) {
	current().fieldMs += ms;
	sinceBattleMs_ += ms;
	if (inDungeon_) {
		dungeonMs_ += ms;
	}
}

void PacingRecorder::eventTime(int ms) {
	current().eventMs += ms;
	if (inDungeon_) {
		dungeonMs_ += ms;
	}
}

void PacingRecorder::step() {
	current().steps++;
	sinceBattleSteps_++;
}

void PacingRecorder::battleStarted() {
	AreaPacing& a = current();
	a.battles++;
	if (hadBattle_) {
		a.msBetweenBattles.push_back(sinceBattleMs_);
		a.stepsBetweenBattles.push_back(sinceBattleSteps_);
	}
}

void PacingRecorder::battleEnded(int rounds, int roundMs, int experience, int money) {
	AreaPacing& a = current();
	a.battleMs += static_cast<std::int64_t>(rounds) * roundMs;
	a.experience += experience;
	a.money += money;
	if (inDungeon_) {
		dungeonMs_ += static_cast<std::int64_t>(rounds) * roundMs;
	}
	hadBattle_ = true;
	sinceBattleMs_ = 0;
	sinceBattleSteps_ = 0;
}

void PacingRecorder::bossStarted(const std::string& flag, const std::string& encounter, int averageLevelX100) {
	BossPacing b;
	b.flag = flag;
	b.encounter = encounter;
	b.area = area_;
	b.averageLevelX100 = averageLevelX100;
	bosses_.push_back(b);
	bossOpen_ = true;
}

void PacingRecorder::bossEnded() {
	if (bossOpen_ && !bosses_.empty()) {
		bosses_.back().dungeonMs = inDungeon_ ? dungeonMs_ : -1;
	}
	bossOpen_ = false;
}

int PacingRecorder::totalBattles() const {
	int n = 0;
	for (const auto& [id, a] : areas_) {
		n += a.battles;
	}
	return n;
}

namespace {

std::string seconds(std::int64_t ms) {
	char buffer[32];
	std::snprintf(buffer, sizeof buffer, "%lld.%01lld", static_cast<long long>(ms / 1000), static_cast<long long>((ms % 1000) / 100));
	return buffer;
}

template <class T>
T median(std::vector<T> values) {
	if (values.empty()) {
		return T();
	}
	std::sort(values.begin(), values.end());
	const std::size_t mid = values.size() / 2;
	return values.size() % 2 ? values[mid] : (values[mid - 1] + values[mid]) / 2;
}

std::string pad(const std::string& text, std::size_t width) {
	return text.size() >= width ? text + " " : text + std::string(width - text.size(), ' ');
}

}

std::string PacingRecorder::table() const {
	std::string out;
	out += pad("area", 14) + pad("visits", 8) + pad("battles", 9) + pad("steps", 7) + pad("field_s", 9) + pad("battle_s", 10) +
		pad("event_s", 9) + pad("med_gap_s", 11) + pad("med_gap_steps", 15) + pad("exp", 6) + "money\n";
	for (const std::string& id : order_) {
		const AreaPacing& a = areas_.at(id);
		out += pad(id, 14) + pad(std::to_string(a.visits), 8) + pad(std::to_string(a.battles), 9) + pad(std::to_string(a.steps), 7) +
			pad(seconds(a.fieldMs), 9) + pad(seconds(a.battleMs), 10) + pad(seconds(a.eventMs), 9) +
			pad(a.msBetweenBattles.empty() ? "-" : seconds(median(a.msBetweenBattles)), 11) +
			pad(a.stepsBetweenBattles.empty() ? "-" : std::to_string(median(a.stepsBetweenBattles)), 15) + pad(std::to_string(a.experience), 6) +
			std::to_string(a.money) + "\n";
	}
	out += pad("boss_flag", 22) + pad("encounter", 11) + pad("area", 12) + pad("avg_level", 11) + "dungeon_s\n";
	for (const BossPacing& b : bosses_) {
		char level[16];
		std::snprintf(level, sizeof level, "%d.%02d", b.averageLevelX100 / 100, b.averageLevelX100 % 100);
		out += pad(b.flag, 22) + pad(b.encounter, 11) + pad(b.area, 12) + pad(level, 11) + (b.dungeonMs < 0 ? "-" : seconds(b.dungeonMs)) + "\n";
	}
	return out;
}

}
