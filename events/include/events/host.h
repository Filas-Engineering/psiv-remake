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
	// `header`: text shown with the options, empty when the script gave none.
	Text header;
	std::vector<Text> options;
	bool yes_no = false;
	// `keep`: once answered, the choice stays on screen with the picked row until `choice clear` or the event ends.
	bool keep = false;
	// Each option's state word (`option <text> state <word> [if <condition>]`), "" for none, filled as the choice is
	// presented; empty when no option has one.
	std::vector<std::string> states;
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
	place_actor,
	camera_to,
	camera_reset,
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
	// move: markers walked through, in order, before the target. pathfind: the host finds its own way to the target
	// around what stands in the way.
	std::vector<std::string> waypoints;
	bool pathfind = false;
	// place_actor: north, east, south or west, or empty to keep the actor's facing.
	std::string facing;
	// camera_to: the scene camera's goal, a marker in `target` or a cell (x column, y row) in `position` with has_position,
	// reached over `seconds` (0 cuts). camera_reset: back on the party leader over `seconds`.
	// music_play: `fade <ms>` (above 0) or `cut` (0); -1 when the script gives neither and the game's own rule holds.
	int fade_ms = -1;
	// warp: `followers <placement>` (stacked, behind, up, down, left or right), where the game stands the followers on
	// arrival; empty for the game's own rule.
	std::string followers;
	// show_actor, hide_actor: `target` may name a transition (`show <actor> with <animation>`), played for `seconds`.
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

// What `party swap` does with the pieces the replaced member wears: the game's own rule for a member who leaves, or
// the script's `equipment keep` or `equipment bag`.
enum class SwapEquipment : int { rule = 0, keep = 1, bag = 2 };

// `party swap <slot|member> <member>`: the member in a lineup slot (1 is the leader) or the named lineup member goes to
// the reserve and `member` takes that slot.
struct PartySwap {
	// 1 or more, or 0 when `replaced` names the member.
	int slot = 0;
	std::string replaced;
	std::string member;
	SwapEquipment equipment = SwapEquipment::rule;
	// `silent`: no notice that the members changed places.
	bool silent = false;
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
	// For the `party` and `item` tests: whether the member is in the active party, and how many of the item the party
	// holds. The defaults say nobody and none.
	virtual bool in_party(const std::string& member);
	virtual int item_count(const std::string& item);
	// For the `leader`, `music`, `area` and `tile` tests (`test` is that word): whether the game is in that state now. The
	// default says no.
	virtual bool game_state(const std::string& test, const std::string& value);
	// The member learns the ability for good.
	virtual void teach_ability(const std::string& member, const std::string& ability);
	// A character who walks with the party without joining it; empty for none.
	virtual void set_follower(const std::string& actor);
	// For `money give|take` and the `money` test. The defaults hold no money and change nothing.
	virtual std::int64_t money();
	virtual void change_money(std::int64_t delta);
	// The member puts the piece on from the bag, or takes it off, by the game's equipment rules.
	virtual void equip_piece(const std::string& member, const std::string& piece);
	virtual void unequip_piece(const std::string& member, const std::string& piece);
	// The track the area the party is in plays from now on, saved with the game; empty goes back to the area's own.
	virtual void set_area_music(const std::string& track);
	// The actor is gone for good, or until the party next enters an area, saved with the game.
	virtual void remove_actor(const std::string& actor, bool until_reload);
	// `party save` and `party restore`: a named snapshot of who is in the lineup and the reserve, in order, saved with
	// the game; saving again under a name replaces it. restore_party returns false when no snapshot has that name,
	// which stops the script with a runtime error. The defaults keep nothing, so every restore fails.
	virtual void save_party(const std::string& snapshot);
	virtual bool restore_party(const std::string& snapshot);
	// `party swap`. A swap the game cannot make is the game's to refuse; the default does nothing.
	virtual void swap_party_member(const PartySwap& swap);
	// `party add <member> silent` and `party remove <member> silent`: as add_party_member and remove_party_member,
	// without the notice that the member joined or left. The defaults call those.
	virtual void add_party_member_silently(const std::string& member);
	virtual void remove_party_member_silently(const std::string& member);
	// `party order`: the named lineup members lead the lineup in that order and the others follow in their own order,
	// with no notice. Returns false, which stops the script with a runtime error, when a name is not in the lineup;
	// the default changes nothing and accepts only names in_party knows.
	virtual bool order_party(const std::vector<std::string>& members);
	// `spawn`: start the background event beside the field, without taking control (FORMAT.md, "Background events"). The
	// game decides when it runs and what stops it; the default does nothing.
	virtual void spawn_event(const std::string& event);
	// `look`: draw the actor with that enemy's field figure, or with a look the game names, or with its own figure when
	// both are empty; game state, saved with the game. The default does nothing.
	virtual void set_actor_look(const std::string& actor, const std::string& enemy, const std::string& look);
	// `credits`: play the credits the game data names, done when the host says they finished. The default returns 0,
	// which stops the script with a runtime error, like the other blocking calls.
	virtual Handle play_credits(const std::string& credits);
	// `end game`: the story is over. The event ends after this call; the game decides what comes next (records the
	// cleared game and goes back to the title, say). The default does nothing.
	virtual void end_game();
	// `picture show`, `picture hide` and `picture clear`: the stack of story pictures on screen, game state saved with the
	// game. hide_picture with an empty name takes off the top one. The defaults do nothing.
	virtual void show_picture(const std::string& picture);
	virtual void hide_picture(const std::string& picture);
	virtual void clear_pictures();
	// `register town` and `unregister town`: the area joins or leaves the towns the party may return to. The default
	// does nothing.
	virtual void register_town(const std::string& area, bool registered);
};

// What a standard animation leaves behind once it has played (FORMAT.md, "Actors").
enum class AnimationEnd { none, shown, hidden };

// An animation name every host knows, with its length when the data gives none.
struct StandardAnimation {
	const char* name;
	double seconds;
	AnimationEnd end;
};

// fade_in, fade_out, dissolve, blink_out, hop and shake, in that order.
const std::vector<StandardAnimation>& standard_animations();
// Null for a name that is not a standard animation.
const StandardAnimation* standard_animation(const std::string& name);
// north, east, south or west: a `face` target that names a direction, and a `place` facing.
bool is_direction(const std::string& word);

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
std::string describe(const PartySwap& swap);
const char* battle_result_name(BattleResult result);
const char* battle_opening_name(BattleOpening opening);

}
