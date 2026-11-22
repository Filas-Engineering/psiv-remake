#include "game/profile.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace game {

namespace {

std::uint64_t checksum(const std::string& text) {
	std::uint64_t hash = 1469598103934665603ULL;
	for (unsigned char c : text) {
		hash ^= c;
		hash *= 1099511628211ULL;
	}
	return hash;
}

std::string hex(std::uint64_t value) {
	char buffer[17];
	std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(value));
	return buffer;
}

bool parseSwitch(const std::string& text, bool& out) {
	if (text == "on" || text == "off") {
		out = text == "on";
		return true;
	}
	return false;
}

bool parsePercent(const std::string& text, int low, int high, int& out) {
	char* end = nullptr;
	const long v = std::strtol(text.c_str(), &end, 10);
	if (text.empty() || *end != '\0' || v < low || v > high) {
		return false;
	}
	out = static_cast<int>(v);
	return true;
}

const char* onOff(bool value) {
	return value ? "on" : "off";
}

const char* const kKeys[] = {"text_speed", "auto_advance", "show_shipped_names", "text_language", "text_size", "battle_speed",
	"ambient_talk", "reduced_flash", "music_volume", "ambient_volume", "effects_volume", "always_run", "flashing", "flash_notice_seen"};

bool knownKey(const std::string& key) {
	for (const char* k : kKeys) {
		if (key == k) {
			return true;
		}
	}
	return false;
}

}

const char* toString(TextSpeed speed) {
	switch (speed) {
	case TextSpeed::Slow: return "slow";
	case TextSpeed::Normal: return "normal";
	case TextSpeed::Fast: return "fast";
	case TextSpeed::Instant: return "instant";
	}
	return "normal";
}

const char* toString(BattleSpeed speed) {
	switch (speed) {
	case BattleSpeed::X1: return "1x";
	case BattleSpeed::X1_5: return "1.5x";
	case BattleSpeed::X2: return "2x";
	case BattleSpeed::X3: return "3x";
	}
	return "1x";
}

const char* toString(FlashLevel level) {
	switch (level) {
	case FlashLevel::Unset: return "";
	case FlashLevel::Reduced: return "reduced";
	case FlashLevel::Standard: return "standard";
	case FlashLevel::Original: return "original";
	}
	return "";
}

bool parseFlashLevel(const std::string& text, FlashLevel& out) {
	for (FlashLevel l : {FlashLevel::Reduced, FlashLevel::Standard, FlashLevel::Original}) {
		if (text == toString(l)) {
			out = l;
			return true;
		}
	}
	return false;
}

bool parseTextSpeed(const std::string& text, TextSpeed& out) {
	for (TextSpeed s : {TextSpeed::Slow, TextSpeed::Normal, TextSpeed::Fast, TextSpeed::Instant}) {
		if (text == toString(s)) {
			out = s;
			return true;
		}
	}
	return false;
}

bool parseBattleSpeed(const std::string& text, BattleSpeed& out) {
	for (BattleSpeed s : {BattleSpeed::X1, BattleSpeed::X1_5, BattleSpeed::X2, BattleSpeed::X3}) {
		if (text == toString(s)) {
			out = s;
			return true;
		}
	}
	return false;
}

std::string setProfileValue(Profile& p, const std::string& key, const std::string& value) {
	bool ok = false;
	if (key == "text_speed") {
		ok = parseTextSpeed(value, p.textSpeed);
	} else if (key == "auto_advance") {
		ok = parseSwitch(value, p.autoAdvance);
	} else if (key == "show_shipped_names") {
		ok = parseSwitch(value, p.showShippedNames);
	} else if (key == "text_language") {
		ok = !value.empty() && value.find_first_not_of("abcdefghijklmnopqrstuvwxyz-") == std::string::npos;
		if (ok) {
			p.textLanguage = value;
		}
	} else if (key == "text_size") {
		ok = parsePercent(value, 100, 150, p.textSizePercent);
	} else if (key == "battle_speed") {
		ok = parseBattleSpeed(value, p.battleSpeed);
	} else if (key == "ambient_talk") {
		ok = parseSwitch(value, p.ambientTalk);
	} else if (key == "reduced_flash") {
		ok = parseSwitch(value, p.reducedFlash);
	} else if (key == "music_volume") {
		ok = parsePercent(value, 0, 100, p.musicVolume);
	} else if (key == "ambient_volume") {
		ok = parsePercent(value, 0, 100, p.ambientVolume);
	} else if (key == "effects_volume") {
		ok = parsePercent(value, 0, 100, p.effectsVolume);
	} else if (key == "always_run") {
		ok = parseSwitch(value, p.alwaysRun);
	} else if (key == "flashing") {
		ok = parseFlashLevel(value, p.flashing);
	} else if (key == "flash_notice_seen") {
		ok = parseSwitch(value, p.flashNoticeSeen);
	} else {
		return "unknown setting '" + key + "'";
	}
	return ok ? "" : "bad value '" + value + "' for " + key;
}

std::string serializeProfile(const Profile& p) {
	std::string out = std::string(kProfileMagic) + " " + std::to_string(kProfileVersion) + "\n";
	auto line = [&](const char* key, const std::string& value) { out += std::string(key) + " " + value + "\n"; };
	line("text_speed", toString(p.textSpeed));
	line("auto_advance", onOff(p.autoAdvance));
	line("show_shipped_names", onOff(p.showShippedNames));
	line("text_language", p.textLanguage);
	line("text_size", std::to_string(p.textSizePercent));
	line("battle_speed", toString(p.battleSpeed));
	line("ambient_talk", onOff(p.ambientTalk));
	line("reduced_flash", onOff(p.reducedFlash));
	line("music_volume", std::to_string(p.musicVolume));
	line("ambient_volume", std::to_string(p.ambientVolume));
	line("effects_volume", std::to_string(p.effectsVolume));
	if (p.alwaysRun) {
		line("always_run", onOff(p.alwaysRun));
	}
	if (p.flashing != FlashLevel::Unset) {
		line("flashing", toString(p.flashing));
	}
	if (p.flashNoticeSeen) {
		line("flash_notice_seen", onOff(p.flashNoticeSeen));
	}
	for (const auto& [key, value] : p.unknown) {
		out += key + " " + value + "\n";
	}
	out += "end " + hex(checksum(out)) + "\n";
	return out;
}

rules::Result<Profile> parseProfile(const std::string& text) {
	using R = rules::Result<Profile>;
	const std::string magic = std::string(kProfileMagic) + " ";
	if (text.compare(0, magic.size(), magic) != 0) {
		return R::failure("not a profile file");
	}
	const std::size_t endLine = text.rfind("\nend ");
	if (endLine == std::string::npos) {
		return R::failure("the profile is truncated");
	}
	const std::string body = text.substr(0, endLine + 1);
	std::string sum = text.substr(endLine + 5);
	while (!sum.empty() && (sum.back() == '\n' || sum.back() == '\r')) {
		sum.pop_back();
	}
	if (sum != hex(checksum(body))) {
		return R::failure("the profile is damaged (checksum mismatch)");
	}
	std::istringstream in(body);
	std::string line;
	std::getline(in, line);
	if (line != magic + std::to_string(kProfileVersion)) {
		return R::failure("the profile is from another build (" + line + ")");
	}
	Profile p;
	while (std::getline(in, line)) {
		std::istringstream words(line);
		std::string key;
		std::string value;
		std::string extra;
		if (!(words >> key)) {
			continue;
		}
		if (!(words >> value) || (words >> extra)) {
			return R::failure("profile line '" + line + "' needs one value");
		}
		if (!knownKey(key)) {
			p.unknown.push_back({key, value});
			continue;
		}
		const std::string problem = setProfileValue(p, key, value);
		if (!problem.empty()) {
			return R::failure(problem);
		}
	}
	return R::success(p);
}

}
