#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
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
	ambient_play,
	ambient_stop,
	sound,
	warp,
};

// EVT-023: every action except warp only changes what is on screen or heard.
// While skipping, the interpreter applies these to its presentation state
// instead of calling the host, and rebuilds the screen when the skip stops.
bool presentation_only(ActionType type);

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
	// Reach the end state at once, with no transition, and report done
	// immediately. Set when the interpreter rebuilds the screen after a skip
	// or a restore, and on warps run while skipping. When the end state is
	// already on screen (the music is already playing), do nothing.
	bool instant = false;
};

enum class BattleResult : int { win = 0, lose = 1, flee = 2 };

enum class BattleOpening : int { normal = 0, party_first = 1, enemy_first = 2 };

// A scripted end (`until` on `battle`): the battle also ends after a number of
// rounds, or when a named enemy falls. The host reports it as a win.
enum class BattleEndKind : int { none = 0, survive_rounds = 1, enemy_defeated = 2 };

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
	BattleOpening opening = BattleOpening::normal;
	BattleEndKind end = BattleEndKind::none;
	// survive_rounds: the rounds the party must last.
	int end_rounds = 0;
	// enemy_defeated: the enemy (by its game ID) whose fall ends the battle.
	std::string end_enemy;
	// Empty, or the vehicle the party fights aboard.
	std::string vehicle;
};

struct RestRequest {
	// Empty for a free full heal; otherwise a rest point (an inn) whose own
	// rules, such as its price, apply.
	std::string point;
	// Set while skipping: heal without the rest's usual presentation.
	bool instant = false;
};

class Host {
public:
	virtual ~Host() = default;

	virtual Handle start_action(const Action& action) = 0;
	virtual Handle show_line(const Line& line) = 0;
	virtual Handle present_choice(const ChoiceRequest& request) = 0;
	// Done when the battle is over; result() is then a BattleResult. The
	// host may call Interpreter::save() from inside this call to write the
	// autosave before an event battle (EVT-050); see Interpreter::save().
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

	// Blocking calls for the `shop`, `rest`, `save prompt` and `name entry`
	// commands, done when the menu closes or the rest is over. The defaults
	// return 0, which stops a script that uses the command with a runtime
	// error, so hosts that never run these commands need not override them.
	virtual Handle open_shop(const std::string& shop);
	virtual Handle rest_party(const RestRequest& request);
	virtual Handle open_save_prompt();
	virtual Handle open_name_entry(const std::string& member);
	// A line a skip passed over, for the text log (TXT-028).
	virtual void log_skipped_line(const Line& line);
	// Vehicles in the field (FLD-060), for the `vehicle` commands and the `vehicle` and `aboard` tests. The changes are
	// synchronous, like give_item; `give` also counts as acquiring the vehicle. The defaults do nothing and report that
	// the party has no vehicle and rides none.
	virtual void give_vehicle(const std::string& vehicle, const std::string& map, const std::string& marker);
	virtual void place_vehicle(const std::string& vehicle, const std::string& map, const std::string& marker);
	virtual void remove_vehicle(const std::string& vehicle);
	virtual bool has_vehicle(const std::string& vehicle);
	virtual bool aboard_vehicle(const std::string& vehicle);
};

const char* action_type_name(ActionType type);
// The action named as timing configurations and game data write it: the type name with `_` for spaces ("move",
// "panel_show", "camera_move").
bool parse_action_type(const std::string& name, ActionType& type);
// Authored seconds for an action (FORMAT.md, "Timing configuration"): the entry for its type and subject, else the one
// for its type, else the action's own `seconds`.
double action_seconds(const Action& action, const std::map<ActionType, double>& by_type,
	const std::map<std::pair<ActionType, std::string>, double>& by_subject);
// Render values back in script syntax, for traces and tools.
std::string describe(const Text& text);
std::string describe(const Action& action);
std::string describe(const Line& line);
std::string describe(const ChoiceRequest& request);
std::string describe(const BattleRequest& request);
std::string describe(const RestRequest& request);
const char* battle_result_name(BattleResult result);
const char* battle_opening_name(BattleOpening opening);

}
