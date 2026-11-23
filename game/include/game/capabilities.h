#pragma once

#include "rules/data_keys.h"

#include <string>
#include <vector>

namespace game {

// The library version and C API ABI the bindings build with (bindings/c checks PSIV_ABI_VERSION against it).
constexpr const char* kLibraryVersion = "0.22.0";
constexpr int kAbiVersion = 23;
// The version of capabilitiesJson()'s layout; bumped when a key of it is renamed, removed or changes meaning.
constexpr int kCapabilitiesSchema = 1;

// Every object of the game's own data (game.json, journal.json, area files), sorted by file and object: the loaders
// accept exactly these keys (JsonReader::only takes nothing else).
const std::vector<rules::DataKeySet>& gameDataKeys();
// One set; a name gameDataKeys() lacks is a programming error and aborts.
const rules::DataKeySet& gameKeys(const std::string& file, const std::string& object);
// The fixed word sets of the game's data, which its parsers read.
const std::vector<rules::DataEnum>& gameDataEnums();
// The position of `word` in the named set, or -1.
int gameEnumIndex(const std::string& name, const std::string& word);

// What this build supports, as JSON with a stable key order (game/README.md, "Capabilities").
std::string capabilitiesJson();

}
