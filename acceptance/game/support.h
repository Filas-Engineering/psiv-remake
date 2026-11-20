#pragma once

#include "acc.h"

#include "game/data.h"
#include "game/save.h"
#include "game/session.h"

#include <functional>
#include <string>
#include <vector>

namespace support {

// game/data, loaded once. Exits with a FAIL line when it cannot be loaded.
const game::GameData& data(const acc::Context& t);

// A new empty folder outside the checkout.
std::string tempDirectory(const std::string& name);

// The scripted playthrough (game/data/scripts/playthrough.txt) with auto battles and seed 10, writing its saves to
// `saveDirectory`. `after` runs after every command that the session accepted, with the command line.
void playThrough(const acc::Context& t, game::Session& session, const std::function<void(const std::string& line)>& after);
game::SessionOptions options(const std::string& saveDirectory);

// Replaces a save's checksum trailer so an edited body still passes it.
std::string resign(const std::string& text);
// The save text without its header's write time and its checksum trailer, which a resave may change.
std::string withoutWriteTime(const std::string& text);

}
