#pragma once

#include "rules/result.h"

#include <string>
#include <utility>
#include <vector>

namespace game {

enum class TextSpeed { Slow, Normal, Fast, Instant };
enum class BattleSpeed { X1, X1_5, X2, X3 };

const char* toString(TextSpeed speed);
const char* toString(BattleSpeed speed);
bool parseTextSpeed(const std::string& text, TextSpeed& out);
bool parseBattleSpeed(const std::string& text, BattleSpeed& out);

// The global profile (SAV-005): every setting of UI-013 except difficulty, the reward multiplier and the
// auto-battle macro, which belong to a save. Controls are the platform's business.
struct Profile {
	TextSpeed textSpeed = TextSpeed::Normal;
	bool autoAdvance = false;
	bool showShippedNames = false;
	std::string textLanguage = "en";
	int textSizePercent = 100;
	BattleSpeed battleSpeed = BattleSpeed::X1;
	bool ambientTalk = true;
	bool reducedFlash = false;
	int musicVolume = 80;
	int ambientVolume = 80;
	int effectsVolume = 80;
	// Lines this build does not know, kept and written back unchanged so a newer build's settings survive.
	std::vector<std::pair<std::string, std::string>> unknown;
};

constexpr const char* kProfileMagic = "PSR-PROFILE";
constexpr int kProfileVersion = 1;

// Sets one setting by its file key (text_speed, auto_advance, ...). Returns an empty string or the problem.
std::string setProfileValue(Profile& profile, const std::string& key, const std::string& value);

// "PSR-PROFILE 1", one "key value" line per setting, then "end <FNV-1a 64 of everything before>".
std::string serializeProfile(const Profile& profile);
rules::Result<Profile> parseProfile(const std::string& text);

}
