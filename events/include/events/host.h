#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace events {

// Issued by the host, never 0. Each handle is given back exactly once:
// through release() after the interpreter has consumed it, or through cancel().
using Handle = std::uint64_t;

struct Text {
	// When true, value is a localization string ID for the host to look up.
	bool is_id = false;
	std::string value;
};

struct Line {
	// Empty for narration.
	std::string speaker;
	Text text;
	std::string portrait;
};

struct ChoiceRequest {
	// prompt.value is empty when the script gave no prompt.
	Text prompt;
	std::vector<Text> options;
	bool yes_no = false;
};

enum class ActionType {
	move,
	face,
	animate,
	show_actor,
	hide_actor,
	panel_show,
	panel_hide,
	panel_clear,
	camera_cut,
	camera_move,
	fade_in,
	fade_out,
	shake,
	music_play,
	music_stop,
	music_fade,
	sound,
	warp,
};

struct Vec3 {
	double x = 0;
	double y = 0;
	double z = 0;
};

// Field use per type is listed in README.md ("Implementing Host").
struct Action {
	ActionType type = ActionType::move;
	std::string subject;
	std::string target;
	std::string slot;
	bool has_position = false;
	Vec3 position;
	double seconds = 0;
	double speed = 0;
	double strength = 0;
	bool loop = false;
};

enum class BattleResult : int { win = 0, lose = 1, flee = 2 };

// What a `battle` block asks of the host's battle rules. Both options follow
// from which sections the script wrote (see README.md, "Battles").
struct BattleRequest {
	std::string encounter;
	// The script has a `flee` section. When false the battle must not offer
	// escape, and the host must never report BattleResult::flee.
	bool escape_allowed = false;
	// The script has a `lose` section. When false a defeat goes to the game's
	// game-over flow and the host must never report BattleResult::lose.
	bool defeat_handled = false;
};

class Host {
public:
	virtual ~Host() = default;

	virtual Handle start_action(const Action& action) = 0;
	virtual Handle show_line(const Line& line) = 0;
	virtual Handle present_choice(const ChoiceRequest& request) = 0;
	// Done when the battle is over; result() is then a BattleResult.
	virtual Handle start_battle(const BattleRequest& request) = 0;
	virtual bool is_done(Handle handle) = 0;
	// Only read once is_done() is true. Choice: the picked option index
	// (yes = 0, no = 1). Battle: a BattleResult value.
	virtual int result(Handle handle) = 0;
	virtual void cancel(Handle handle) = 0;
	virtual void release(Handle handle) = 0;

	virtual bool get_flag(const std::string& name) = 0;
	virtual void set_flag(const std::string& name, bool value) = 0;
	virtual std::int64_t get_var(const std::string& name) = 0;
	virtual void set_var(const std::string& name, std::int64_t value) = 0;
	virtual void give_item(const std::string& item, int count) = 0;
	virtual void take_item(const std::string& item, int count) = 0;
	virtual void add_party_member(const std::string& member) = 0;
	virtual void remove_party_member(const std::string& member) = 0;
	virtual void set_player_control(bool enabled) = 0;

	// The game state (flags, variables, items, party, player control and
	// whatever else the game saves) as an opaque blob. The interpreter takes
	// one when an event starts and keeps it in SaveState::start_world.
	virtual std::string snapshot_world() = 0;
	// Puts back a blob from snapshot_world(). Called only when a save made
	// with different scripts is restored; returns false if it cannot.
	virtual bool restore_world(const std::string& snapshot) = 0;
};

const char* action_type_name(ActionType type);
// Render values back in script syntax, for traces and tools.
std::string describe(const Text& text);
std::string describe(const Action& action);
std::string describe(const Line& line);
std::string describe(const ChoiceRequest& request);
std::string describe(const BattleRequest& request);
const char* battle_result_name(BattleResult result);

}
