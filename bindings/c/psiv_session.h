/*
 * psiv_session.h: a flat C interface to the headless game session in ../../game.
 *
 * Conventions
 *
 * Handles: psiv_session is opaque. Create it with psiv_session_create, free it with psiv_session_destroy.
 * A session is not thread safe: call it from one thread at a time.
 *
 * Status: every function returns a psiv_status, or a plain value where it cannot fail (a mode, a count, a
 * flag). On anything but PSIV_OK, psiv_last_error describes why: a message key with parameters (the game's
 * own text keys, core's keys for battle and equipment refusals, or "binding.*" keys for argument problems
 * the binding catches itself) and the text rendered in the profile's language. No C++ exception crosses
 * this interface.
 *
 * Strings are UTF-8 and NUL terminated. String fields of returned structs are never NULL; "" means none.
 *
 * Ownership: every pointer the library returns (strings, arrays, struct contents) is owned by the session
 * and stays valid until the next call to a function marked MUTATES below, or psiv_session_destroy.
 * Functions not marked MUTATES only read and never invalidate earlier results, so a frame can call
 * psiv_update once and then read everything it needs. A pointer the library returned may be passed back
 * in as an argument, even to a MUTATES function. The error record of psiv_last_error has its own storage:
 * it stays valid until the next call that fails, or psiv_session_destroy. Inputs are copied; the library
 * never keeps a caller's pointer.
 *
 * Booleans are int32_t: 0 is false, anything else true. Enum-typed fields are int32_t holding the values
 * of the matching psiv_* enum, so struct layouts do not depend on the compiler's enum size.
 */
#ifndef PSIV_SESSION_H
#define PSIV_SESSION_H

#include <stdint.h>

#if defined(_WIN32)
#	if defined(PSIV_BUILDING)
#		define PSIV_API __declspec(dllexport)
#	else
#		define PSIV_API __declspec(dllimport)
#	endif
#else
#	define PSIV_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped whenever a function signature, a struct layout or an enum value changes. */
#define PSIV_ABI_VERSION 13

PSIV_API uint32_t psiv_abi_version(void);
/* "psiv-bindings <version> (game <build>, rules <version>)". Static storage. */
PSIV_API const char* psiv_version(void);

/* sizeof the named struct ("psiv_battle_event"), or 0 for an unknown name, so a foreign binding can check
 * its struct layouts against this build. */
PSIV_API uint32_t psiv_sizeof(const char* struct_name);

typedef struct psiv_session psiv_session;

/* ------------------------------------------------------------------------------------------------------ */
/* Status and errors                                                                                      */

typedef enum psiv_status {
	PSIV_OK = 0,
	/* The game refused: wrong mode, not enough money, a battle command core rejects and so on. */
	PSIV_REFUSED = 1,
	/* A NULL pointer, an index out of range or a value outside its enum. Keys "binding.*". */
	PSIV_INVALID_ARGUMENT = 2,
	/* An id, slot or item that does not exist, or a state that is not there (no battle, no area). */
	PSIV_NOT_FOUND = 3,
	/* The data could not be loaded. The text lists every problem, one per line; psiv_data_problems gives each
	 * with its text key. Also a battle record that does not parse or replay. */
	PSIV_DATA_ERROR = 4,
	/* Anything unexpected inside the library, such as running out of memory. */
	PSIV_INTERNAL_ERROR = 5
} psiv_status;

typedef struct psiv_error {
	int32_t status;
	const char* key;
	int32_t param_count;
	const char* const* params;
	const char* text;
} psiv_error;

/* The last failure on `session`, or with session NULL the last failure of psiv_session_create on this
 * thread. Returns PSIV_NOT_FOUND and an empty record when there was none. */
PSIV_API psiv_status psiv_last_error(const psiv_session* session, psiv_error* out);

/* One data load problem: where, and a text key with its parameters (the game's problem table has the English
 * template of every key; `text` is the problem rendered with it). */
typedef struct psiv_problem {
	const char* file;
	/* 0 when the problem is about the whole file. */
	int32_t line;
	/* The entry being read, "error" or "warning" for a script diagnostic, or "". */
	const char* entry;
	const char* key;
	int32_t param_count;
	const char* const* params;
	const char* text;
} psiv_problem;

/* The problems of the last psiv_session_create on this thread that failed with PSIV_DATA_ERROR. Valid until
 * the next psiv_session_create on this thread. */
PSIV_API psiv_status psiv_data_problems(const psiv_problem** out, int32_t* count);

/* ------------------------------------------------------------------------------------------------------ */
/* Enums. Values mirror the C++ libraries; psiv_name_of gives each value's stable name.                   */

typedef enum psiv_mode { PSIV_MODE_TITLE, PSIV_MODE_FIELD, PSIV_MODE_EVENT, PSIV_MODE_BATTLE, PSIV_MODE_MENU, PSIV_MODE_SHOP } psiv_mode;
/* PORT: a port's destinations (psiv_travel_destinations with PSIV_TRAVEL_PORT). */
typedef enum psiv_menu_kind { PSIV_MENU_NONE, PSIV_MENU_FIELD, PSIV_MENU_INN, PSIV_MENU_SAVE_PROMPT, PSIV_MENU_NAME_ENTRY, PSIV_MENU_PORT } psiv_menu_kind;
/* PSIV_DIRECTION_NONE means no direction held. */
typedef enum psiv_direction { PSIV_DIRECTION_NONE = -1, PSIV_DIRECTION_NORTH = 0, PSIV_DIRECTION_EAST, PSIV_DIRECTION_SOUTH, PSIV_DIRECTION_WEST } psiv_direction;
/* PSIV_NOTICE_AMBIENT: a party talk line that came up by itself as the party arrived; show it without stopping play. */
typedef enum psiv_notice_kind { PSIV_NOTICE_LINE, PSIV_NOTICE_CHOICE, PSIV_NOTICE_ACTION, PSIV_NOTICE_NOTICE, PSIV_NOTICE_WARNING, PSIV_NOTICE_AUTOSAVE, PSIV_NOTICE_AMBIENT } psiv_notice_kind;
typedef enum psiv_area_kind { PSIV_AREA_TOWN, PSIV_AREA_FIELD, PSIV_AREA_DUNGEON, PSIV_AREA_VESSEL } psiv_area_kind;
typedef enum psiv_encounter_mode { PSIV_ENCOUNTERS_VISIBLE, PSIV_ENCOUNTERS_RANDOM } psiv_encounter_mode;
typedef enum psiv_enemy_state { PSIV_ENEMY_IDLE, PSIV_ENEMY_CHASING, PSIV_ENEMY_RETURNING } psiv_enemy_state;
typedef enum psiv_point_kind { PSIV_POINT_TALK, PSIV_POINT_STEP, PSIV_POINT_CHEST, PSIV_POINT_SHOP, PSIV_POINT_REST, PSIV_POINT_PORT, PSIV_POINT_HELM, PSIV_POINT_GANGWAY, PSIV_POINT_CONVEYOR } psiv_point_kind;
/* Ways to travel at once: from a port to another open port, back to a visited town of the region, out of a dungeon. */
typedef enum psiv_travel_kind { PSIV_TRAVEL_PORT, PSIV_TRAVEL_TOWN, PSIV_TRAVEL_DUNGEON_EXIT } psiv_travel_kind;
typedef enum psiv_job_state { PSIV_JOB_AVAILABLE, PSIV_JOB_ACCEPTED, PSIV_JOB_COMPLETED, PSIV_JOB_MISSED } psiv_job_state;
typedef enum psiv_text_speed { PSIV_TEXT_SLOW, PSIV_TEXT_NORMAL, PSIV_TEXT_FAST, PSIV_TEXT_INSTANT } psiv_text_speed;
typedef enum psiv_battle_speed { PSIV_BATTLE_X1, PSIV_BATTLE_X1_5, PSIV_BATTLE_X2, PSIV_BATTLE_X3 } psiv_battle_speed;
typedef enum psiv_reward_multiplier { PSIV_REWARD_X1, PSIV_REWARD_X1_5, PSIV_REWARD_X2 } psiv_reward_multiplier;
typedef enum psiv_battle_source { PSIV_BATTLE_VISIBLE, PSIV_BATTLE_RANDOM, PSIV_BATTLE_EVENT } psiv_battle_source;
typedef enum psiv_side { PSIV_SIDE_PARTY, PSIV_SIDE_ENEMY } psiv_side;
typedef enum psiv_command_kind { PSIV_COMMAND_NONE, PSIV_COMMAND_ATTACK, PSIV_COMMAND_ABILITY, PSIV_COMMAND_ITEM, PSIV_COMMAND_DEFEND, PSIV_COMMAND_ESCAPE } psiv_command_kind;
typedef enum psiv_ability_kind { PSIV_ABILITY_ATTACK, PSIV_ABILITY_TECHNIQUE, PSIV_ABILITY_SKILL, PSIV_ABILITY_COMBO } psiv_ability_kind;
typedef enum psiv_targeting {
	PSIV_TARGET_ONE_ENEMY, PSIV_TARGET_ENEMY_GROUP, PSIV_TARGET_ALL_ENEMIES, PSIV_TARGET_ONE_ALLY, PSIV_TARGET_ALL_ALLIES,
	PSIV_TARGET_SELF, PSIV_TARGET_ONE_FALLEN_ALLY, PSIV_TARGET_RANDOM_ENEMY
} psiv_targeting;
/* A piece's slot and where a member wears it. Weapons and shields are hand pieces: PSIV_SLOT_WEAPON is also the right
 * hand, and PSIV_SLOT_LEFT_HAND only a place to wear one. */
typedef enum psiv_equip_slot { PSIV_SLOT_WEAPON, PSIV_SLOT_HEAD, PSIV_SLOT_BODY, PSIV_SLOT_ARMS, PSIV_SLOT_ACCESSORY, PSIV_SLOT_SHIELD, PSIV_SLOT_LEFT_HAND, PSIV_SLOT_COUNT } psiv_equip_slot;
/* What a shop sells, for its label. */
typedef enum psiv_shop_kind { PSIV_SHOP_GENERAL, PSIV_SHOP_WEAPON, PSIV_SHOP_ARMOR, PSIV_SHOP_TOOL, PSIV_SHOP_GIFT } psiv_shop_kind;
typedef enum psiv_outcome { PSIV_OUTCOME_ONGOING, PSIV_OUTCOME_VICTORY, PSIV_OUTCOME_DEFEAT, PSIV_OUTCOME_ESCAPED, PSIV_OUTCOME_SCRIPTED_END } psiv_outcome;
typedef enum psiv_event_type {
	PSIV_EVENT_BATTLE_STARTED, PSIV_EVENT_ROUND_STARTED, PSIV_EVENT_COMMANDS_LOCKED, PSIV_EVENT_ORDER_RESOLVED, PSIV_EVENT_RETARGETED,
	PSIV_EVENT_ACTION_STARTED, PSIV_EVENT_ACTION_CANCELLED, PSIV_EVENT_COMBO_TRIGGERED, PSIV_EVENT_COMBO_DISCOVERED, PSIV_EVENT_RESOURCE_SPENT,
	PSIV_EVENT_ITEM_HAD_NO_EFFECT, PSIV_EVENT_HIT, PSIV_EVENT_MISSED, PSIV_EVENT_CRITICAL, PSIV_EVENT_DAMAGE_DEALT, PSIV_EVENT_HEALED,
	PSIV_EVENT_REVIVED, PSIV_EVENT_STATUS_APPLIED, PSIV_EVENT_STATUS_RESISTED, PSIV_EVENT_STATUS_REMOVED, PSIV_EVENT_STAT_MODIFIED,
	PSIV_EVENT_COMBATANT_DEFEATED, PSIV_EVENT_ESCAPE_ATTEMPTED, PSIV_EVENT_MESSAGE, PSIV_EVENT_ROUND_ENDED, PSIV_EVENT_BATTLE_ENDED,
	PSIV_EVENT_ENEMY_JOINED, PSIV_EVENT_ENEMY_LEFT, PSIV_EVENT_ENEMY_TRANSFORMED
} psiv_event_type;
typedef enum psiv_cancel_reason { PSIV_CANCEL_NONE, PSIV_CANCEL_ACTOR_UNABLE, PSIV_CANCEL_SILENCED, PSIV_CANCEL_NOT_ENOUGH_TP, PSIV_CANCEL_NO_USES_LEFT, PSIV_CANCEL_NO_ITEM, PSIV_CANCEL_NO_TARGET } psiv_cancel_reason;
typedef enum psiv_effectiveness { PSIV_EFFECT_NORMAL, PSIV_EFFECT_WEAK, PSIV_EFFECT_RESISTED, PSIV_EFFECT_IMMUNE } psiv_effectiveness;
typedef enum psiv_status_end { PSIV_STATUS_END_CURED, PSIV_STATUS_END_DURATION, PSIV_STATUS_END_RECOVERED, PSIV_STATUS_END_WOKE_ON_DAMAGE, PSIV_STATUS_END_DEFEATED } psiv_status_end;
typedef enum psiv_resource_kind { PSIV_RESOURCE_NONE, PSIV_RESOURCE_TP, PSIV_RESOURCE_SKILL_USE, PSIV_RESOURCE_ITEM } psiv_resource_kind;
typedef enum psiv_element { PSIV_ELEMENT_NONE, PSIV_ELEMENT_HEAT, PSIV_ELEMENT_CHILL, PSIV_ELEMENT_SHOCK, PSIV_ELEMENT_GALE, PSIV_ELEMENT_HOLY } psiv_element;
/* What a character or enemy is: a machine takes only healing that names machines (core/README.md). */
typedef enum psiv_creature_kind { PSIV_CREATURE_LIVING, PSIV_CREATURE_SPIRIT, PSIV_CREATURE_MACHINE } psiv_creature_kind;
typedef enum psiv_stat { PSIV_STAT_STRENGTH, PSIV_STAT_MENTAL, PSIV_STAT_AGILITY, PSIV_STAT_DEXTERITY, PSIV_STAT_DEFENSE, PSIV_STAT_MAGIC_DEFENSE } psiv_stat;
typedef enum psiv_opening { PSIV_OPENING_NORMAL, PSIV_OPENING_PARTY_FIRST, PSIV_OPENING_ENEMY_FIRST } psiv_opening;
typedef enum psiv_auto_stop { PSIV_AUTO_STOP_NONE, PSIV_AUTO_STOP_CANCELLED, PSIV_AUTO_STOP_MEMBER_FELL, PSIV_AUTO_STOP_CANNOT_PAY, PSIV_AUTO_STOP_BATTLE_ENDED } psiv_auto_stop;
typedef enum psiv_macro_step_status { PSIV_MACRO_APPLIED, PSIV_MACRO_RETARGETED, PSIV_MACRO_FELL_BACK, PSIV_MACRO_SKIPPED, PSIV_MACRO_AUTOMATIC } psiv_macro_step_status;
/* What psiv_battle_apply_macro_fallback does with a step that cannot run as written (core's rules::MacroFallback). */
typedef enum psiv_macro_fallback { PSIV_MACRO_FALLBACK_BASIC_ATTACK, PSIV_MACRO_FALLBACK_AUTOMATIC } psiv_macro_fallback;
typedef enum psiv_macro_issue {
	PSIV_ISSUE_NONE, PSIV_ISSUE_NOT_IN_PARTY, PSIV_ISSUE_ACTOR_DEFEATED, PSIV_ISSUE_CAUGHT_OFF_GUARD, PSIV_ISSUE_CANNOT_ACT,
	PSIV_ISSUE_FORCED_BY_STATUS, PSIV_ISSUE_UNKNOWN_ABILITY, PSIV_ISSUE_SILENCED, PSIV_ISSUE_NOT_ENOUGH_TP, PSIV_ISSUE_NO_USES_LEFT,
	PSIV_ISSUE_NO_ITEM, PSIV_ISSUE_ESCAPE_NOT_ALLOWED, PSIV_ISSUE_TARGET_GONE, PSIV_ISSUE_NO_TARGET
} psiv_macro_issue;
typedef enum psiv_macro_target_kind { PSIV_MACRO_TARGET_DEFAULT, PSIV_MACRO_TARGET_SELF, PSIV_MACRO_TARGET_MEMBER, PSIV_MACRO_TARGET_ENEMY, PSIV_MACRO_TARGET_ALL } psiv_macro_target_kind;
typedef enum psiv_action_type {
	PSIV_ACTION_MOVE, PSIV_ACTION_FACE, PSIV_ACTION_ANIMATE, PSIV_ACTION_SHOW_ACTOR, PSIV_ACTION_HIDE_ACTOR, PSIV_ACTION_PANEL_SHOW,
	PSIV_ACTION_PANEL_HIDE, PSIV_ACTION_PANEL_CLEAR, PSIV_ACTION_CAMERA_CUT, PSIV_ACTION_CAMERA_MOVE, PSIV_ACTION_FADE_IN, PSIV_ACTION_FADE_OUT,
	PSIV_ACTION_SHAKE, PSIV_ACTION_MUSIC_PLAY, PSIV_ACTION_MUSIC_STOP, PSIV_ACTION_MUSIC_FADE, PSIV_ACTION_AMBIENT_PLAY, PSIV_ACTION_AMBIENT_STOP,
	PSIV_ACTION_SOUND, PSIV_ACTION_WARP
} psiv_action_type;
/* A queued presentation record: an action starting, or an action the interpreter abandoned (a skip). */
typedef enum psiv_action_phase { PSIV_ACTION_BEGIN, PSIV_ACTION_CANCEL } psiv_action_phase;
/* INSTANT: every action is done when it starts (what the terminal and tests do). WAIT: an action is done
 * when the host calls psiv_action_finish, so the event waits on the engine's animation. These are the game's
 * own Presentation::Instant and Presentation::Queued. */
typedef enum psiv_presentation { PSIV_PRESENTATION_INSTANT, PSIV_PRESENTATION_WAIT } psiv_presentation;
/* What psiv_description describes. */
typedef enum psiv_description_kind {
	PSIV_DESCRIPTION_ITEM, PSIV_DESCRIPTION_EQUIPMENT, PSIV_DESCRIPTION_ABILITY, PSIV_DESCRIPTION_STATUS, PSIV_DESCRIPTION_ENEMY, PSIV_DESCRIPTION_DIFFICULTY
} psiv_description_kind;
/* What a camera looks at: nothing given (the host decides), a marker in an area, an actor or character it follows, or
 * the party leader. */
typedef enum psiv_camera_target { PSIV_CAMERA_NONE, PSIV_CAMERA_MARKER, PSIV_CAMERA_FOLLOW, PSIV_CAMERA_LEADER } psiv_camera_target;
/* Orders for psiv_inventory_sorted. The bag has no order of its own, so sorting changes nothing in the game or its
 * saves. KIND: field items, battle-only items, then pieces by slot; NAME: display name; COUNT: most held first. Ties go
 * to the display name, then the id. */
typedef enum psiv_item_sort { PSIV_SORT_ID, PSIV_SORT_KIND, PSIV_SORT_NAME, PSIV_SORT_COUNT } psiv_item_sort;
/* Cell flags in psiv_field_grid. The low two bits are the terrain. */
/* A cell's terrain (masked by PSIV_CELL_TERRAIN_MASK) and flags. Soft ground is crossed by a vehicle only; a barrier is a
 * wall (PSIV_CELL_WALL | PSIV_CELL_BARRIER) that a vehicle can break through. */
typedef enum psiv_cell { PSIV_CELL_FLOOR = 0, PSIV_CELL_WALL = 1, PSIV_CELL_WATER = 2, PSIV_CELL_TERRAIN_MASK = 3, PSIV_CELL_WALKABLE = 16, PSIV_CELL_SOFT = 3, PSIV_CELL_BARRIER = 8 } psiv_cell;
/* The id lists of psiv_dev_ids: what the developer commands take. Markers are "area:marker". */
typedef enum psiv_dev_list {
	PSIV_DEV_AREAS, PSIV_DEV_MARKERS, PSIV_DEV_FLAGS, PSIV_DEV_ITEMS, PSIV_DEV_EQUIPMENT, PSIV_DEV_ABILITIES, PSIV_DEV_ENEMIES, PSIV_DEV_ENCOUNTERS,
	PSIV_DEV_EVENTS, PSIV_DEV_VEHICLES, PSIV_DEV_CHARACTERS
} psiv_dev_list;

typedef enum psiv_name_kind {
	PSIV_NAME_MODE, PSIV_NAME_MENU_KIND, PSIV_NAME_DIRECTION, PSIV_NAME_NOTICE_KIND, PSIV_NAME_AREA_KIND, PSIV_NAME_ENCOUNTER_MODE,
	PSIV_NAME_ENEMY_STATE, PSIV_NAME_POINT_KIND, PSIV_NAME_JOB_STATE, PSIV_NAME_TEXT_SPEED, PSIV_NAME_BATTLE_SPEED, PSIV_NAME_REWARD_MULTIPLIER,
	PSIV_NAME_BATTLE_SOURCE, PSIV_NAME_SIDE, PSIV_NAME_COMMAND_KIND, PSIV_NAME_ABILITY_KIND, PSIV_NAME_TARGETING, PSIV_NAME_EQUIP_SLOT,
	PSIV_NAME_OUTCOME, PSIV_NAME_EVENT_TYPE, PSIV_NAME_CANCEL_REASON, PSIV_NAME_EFFECTIVENESS, PSIV_NAME_STATUS_END, PSIV_NAME_RESOURCE_KIND,
	PSIV_NAME_ELEMENT, PSIV_NAME_STAT, PSIV_NAME_OPENING, PSIV_NAME_AUTO_STOP, PSIV_NAME_MACRO_STEP_STATUS, PSIV_NAME_MACRO_ISSUE,
	PSIV_NAME_ACTION_TYPE, PSIV_NAME_MACRO_TARGET_KIND, PSIV_NAME_DESCRIPTION_KIND, PSIV_NAME_ITEM_SORT, PSIV_NAME_CAMERA_TARGET,
	PSIV_NAME_TRAVEL_KIND, PSIV_NAME_DEV_LIST, PSIV_NAME_CREATURE_KIND, PSIV_NAME_SHOP_KIND
} psiv_name_kind;

/* The stable name of an enum value as the C++ libraries spell it, lowercase with underscores ("field",
 * "victory", "not_enough_tp", "battle_ended", "x1_5"), or "" for a value out of range. Static storage. */
PSIV_API const char* psiv_name_of(int32_t kind, int32_t value);
/* The value whose name is `text`. Reward multipliers and event types also take the spellings older files use
 * ("1.5", "BattleEnded"). PSIV_NOT_FOUND for an unknown name. */
PSIV_API psiv_status psiv_parse_name(int32_t kind, const char* text, int32_t* out);

/* ------------------------------------------------------------------------------------------------------ */
/* Session                                                                                                */

typedef struct psiv_session_options {
	/* sizeof(psiv_session_options); psiv_session_options_init sets it. */
	uint32_t struct_size;
	/* The game's data directory (the one holding game.json). */
	const char* data_dir;
	/* Core's battle data. NULL or "": game.json's core_data, relative to data_dir. */
	const char* core_data_dir;
	/* Saves, the global profile and the clear-data record. NULL or "" disables writing. */
	const char* save_dir;
	uint64_t seed;
	/* -1 keeps each area's own encounter mode; otherwise a psiv_encounter_mode for every area. */
	int32_t encounter_mode;
	/* A psiv_presentation. */
	int32_t presentation;
	/* 0: save write times are a counter, so runs are reproducible. 1: seconds since the epoch. */
	int32_t wall_clock;
} psiv_session_options;

/* Defaults: no save directory, seed 1, area encounter modes, instant presentation, counter clock. */
PSIV_API void psiv_session_options_init(psiv_session_options* options);
/* Loads the data and creates a session at the title. On failure *out is NULL and psiv_last_error(NULL, ...)
 * says why. */
PSIV_API psiv_status psiv_session_create(const psiv_session_options* options, psiv_session** out);
PSIV_API void psiv_session_destroy(psiv_session* session);

typedef struct psiv_game_info {
	const char* title;
	const char* game_build;
	int32_t rules_version;
	int32_t tick_ms;
	int32_t round_ms;
	int32_t manual_slots;
	int32_t autosave_slots;
	int32_t item_cap;
	int32_t money_cap;
	int32_t language_count;
	const char* const* languages;
	/* game.json's tracks for the title, a won battle and a game over: audio ids, "" when the data names none. */
	const char* title_music;
	const char* victory_music;
	const char* defeat_music;
	/* 1 when rules.json sets magic_defense: elemental damage subtracts magic defense, which a front end then shows. */
	int32_t magic_defense;
	/* rules.json affinity_divisor: the affinity of normal damage, so above it is a weakness and below it a resistance. */
	int32_t affinity_divisor;
} psiv_game_info;

PSIV_API psiv_status psiv_game_info_get(psiv_session* session, psiv_game_info* out);

/* ------------------------------------------------------------------------------------------------------ */
/* Profile (global settings file) and per-save settings                                                   */

typedef struct psiv_profile {
	int32_t text_speed;
	int32_t auto_advance;
	int32_t show_shipped_names;
	const char* text_language;
	int32_t text_size_percent;
	int32_t battle_speed;
	int32_t ambient_talk;
	int32_t reduced_flash;
	int32_t music_volume;
	int32_t ambient_volume;
	int32_t effects_volume;
} psiv_profile;

PSIV_API psiv_status psiv_profile_get(psiv_session* session, psiv_profile* out);
/* MUTATES. Applies at once and writes the profile file. Refuses a language the game has no tables for. */
PSIV_API psiv_status psiv_profile_set(psiv_session* session, const psiv_profile* profile);
/* MUTATES. One setting by its profile file key (text_speed, show_shipped_names, text_language, ...). */
PSIV_API psiv_status psiv_profile_set_value(psiv_session* session, const char* key, const char* value);

typedef struct psiv_settings {
	const char* difficulty;
	int32_t reward_multiplier;
	/* The macro auto-battle uses, "" for none. */
	const char* auto_macro;
} psiv_settings;

PSIV_API psiv_status psiv_settings_get(psiv_session* session, psiv_settings* out);
/* MUTATES. Difficulty and reward multiplier apply from the next battle; refused during a battle
 * ("game.refuse.settings_in_battle") and at the title ("game.refuse.settings_no_game"). */
PSIV_API psiv_status psiv_settings_set(psiv_session* session, const char* difficulty, int32_t reward_multiplier);
/* A difficulty preset: core's name, its description ("" for none) and how it scales battle rewards. */
typedef struct psiv_difficulty {
	const char* id;
	const char* name;
	const char* description;
	int32_t reward_percent;
} psiv_difficulty;

/* Every difficulty preset, in id order. *can_change (may be NULL) is 1 when psiv_settings_set would accept a change now:
 * with a game in progress and outside battle. */
PSIV_API psiv_status psiv_difficulties(psiv_session* session, const psiv_difficulty** out, int32_t* count, int32_t* can_change);
/* MUTATES. NULL or "" turns auto-battle's macro off. */
PSIV_API psiv_status psiv_settings_set_auto_macro(psiv_session* session, const char* macro);
/* The save's macro names. */
PSIV_API psiv_status psiv_macros(psiv_session* session, const char* const** out, int32_t* count);

/* ------------------------------------------------------------------------------------------------------ */
/* Macro editor. Reading works any time; changes are refused outside the field menu                       */
/* ("game.refuse.macro_menu_only"). Positions and step indexes count from 0. Unknown macro names are       */
/* refused with "game.refuse.no_macro".                                                                   */

typedef struct psiv_macro_step {
	const char* member;
	const char* member_name;
	/* A psiv_command_kind. */
	int32_t command_kind;
	/* The ability or item id, "" for attack, defend and escape. */
	const char* id;
	const char* id_name;
	/* A psiv_macro_target_kind. */
	int32_t target_kind;
	/* PSIV_MACRO_TARGET_MEMBER: the character id and its name; otherwise "". */
	const char* target_member;
	const char* target_member_name;
	/* PSIV_MACRO_TARGET_ENEMY: the formation position from 0; otherwise -1. */
	int32_t target_enemy;
	/* A psiv_macro_issue saying why the step cannot run as written now (not in the party, defeated, unknown
	 * ability, silenced, not enough TP, no uses left, no item), PSIV_ISSUE_NONE otherwise. */
	int32_t issue;
	/* 1 for a PSIV_COMMAND_ITEM step whose id is an equipment piece with a battle use (worn, or cast from the bag),
	 * set with psiv_macro_set_piece_step; its issue is PSIV_ISSUE_NO_ITEM while the member does not wear it and the
	 * bag holds none it may cast from there. */
	int32_t piece;
} psiv_macro_step;

typedef struct psiv_macro {
	const char* name;
	int32_t step_count;
	const psiv_macro_step* steps;
} psiv_macro;

/* The save's macros in order, with their steps. */
PSIV_API psiv_status psiv_macro_list(psiv_session* session, const psiv_macro** out, int32_t* count);
/* MUTATES. A new empty macro at the end. Names are 1 to 16 bytes without control characters, spaces at the ends
 * dropped ("game.refuse.name_length"), and unique ("game.refuse.macro_name_taken"); there are at most 8 macros
 * ("game.refuse.macro_slots_full"). */
PSIV_API psiv_status psiv_macro_create(psiv_session* session, const char* name);
/* MUTATES. Same name rules as psiv_macro_create. The auto macro keeps being the auto macro under its new name. */
PSIV_API psiv_status psiv_macro_rename(psiv_session* session, const char* name, const char* new_name);
/* MUTATES. Deleting the auto macro turns auto-battle's macro off and queues a "game.auto_macro_cleared" notice. */
PSIV_API psiv_status psiv_macro_delete(psiv_session* session, const char* name);
/* MUTATES. Moves the macro to `position` ("game.refuse.macro_no_position"). */
PSIV_API psiv_status psiv_macro_move(psiv_session* session, const char* name, int32_t position);
/* MUTATES. Replaces step `index`, or appends when `index` is the step count ("game.refuse.macro_no_step").
 * command_kind is a psiv_command_kind and target_kind a psiv_macro_target_kind; id is the ability or item (NULL or
 * "" for attack, defend and escape), target_member is read for PSIV_MACRO_TARGET_MEMBER and target_enemy for
 * PSIV_MACRO_TARGET_ENEMY. Refusals: "game.refuse.macro_steps_full" (one step per party member, at most 5),
 * "game.refuse.macro_unknown_member" (the member or the target member is not in the roster),
 * "game.refuse.macro_member_twice", "game.refuse.macro_unknown_ability" (the member does not know it now),
 * "game.refuse.macro_unknown_item", "game.refuse.macro_bad_command" (no command, or an id on a command that takes
 * none) and "game.refuse.macro_target" (the target rule does not fit the command, or an enemy position past 7). */
PSIV_API psiv_status psiv_macro_set_step(psiv_session* session, const char* macro, int32_t index, const char* member, int32_t command_kind,
	const char* id, int32_t target_kind, const char* target_member, int32_t target_enemy);
/* MUTATES. As psiv_macro_set_step, for an item step that names `piece`, an equipment piece with a battle use, held
 * or not ("game.refuse.macro_piece_no_use" otherwise); in battle the member casts it worn or from the bag. */
PSIV_API psiv_status psiv_macro_set_piece_step(psiv_session* session, const char* macro, int32_t index, const char* member, const char* piece,
	int32_t target_kind, const char* target_member, int32_t target_enemy);
/* MUTATES. "game.refuse.macro_no_step" for an index past the steps. */
PSIV_API psiv_status psiv_macro_remove_step(psiv_session* session, const char* macro, int32_t index);
/* MUTATES. Moves step `from` to `to`. */
PSIV_API psiv_status psiv_macro_move_step(psiv_session* session, const char* macro, int32_t from, int32_t to);
/* MUTATES. A new macro named `name` from a discovered combination (its psiv_combo_entry recipe): one ability step per
 * component in the recipe's order, default targets, each component given to a lineup member who knows the ability now
 * (the character it is tied to; the members the log recorded are tried first). Refusals: the name rules of
 * psiv_macro_create, "game.refuse.combo_not_found" (not discovered) and "game.refuse.combo_no_member" (no free member
 * knows the ability named in the parameter). */
PSIV_API psiv_status psiv_macro_from_combo(psiv_session* session, const char* recipe, const char* name);

/* ------------------------------------------------------------------------------------------------------ */
/* Title and saves                                                                                        */

PSIV_API psiv_status psiv_new_game(psiv_session* session); /* MUTATES */
PSIV_API psiv_status psiv_continue_game(psiv_session* session); /* MUTATES */
PSIV_API psiv_status psiv_load(psiv_session* session, const char* slot); /* MUTATES */
/* MUTATES. After a game over, reloads the autosave taken before the lost event battle. */
PSIV_API psiv_status psiv_retry_battle(psiv_session* session);
PSIV_API psiv_status psiv_quit_to_title(psiv_session* session); /* MUTATES */
/* MUTATES. From the field menu ("manual_0" .. ) or from a save prompt an event opened. */
PSIV_API psiv_status psiv_save_manual(psiv_session* session, const char* slot);

typedef struct psiv_save_summary {
	const char* slot;
	const char* area;
	int64_t play_time_ms;
	int32_t party_count;
	/* "character:level" per active member. */
	const char* const* party;
	const char* difficulty;
	int64_t write_time;
	uint64_t sequence;
	int32_t autosave;
	int32_t damaged;
	const char* error;
	/* 1 when developer tools marked the saved game (see "Developer tools" below). */
	int32_t dev;
} psiv_save_summary;

/* Every slot with a file, manual slots first. PSIV_REFUSED when saving is disabled. */
PSIV_API psiv_status psiv_saves(psiv_session* session, const psiv_save_summary** out, int32_t* count);

typedef struct psiv_world_info {
	int32_t mode;
	int32_t menu_kind;
	int32_t money;
	int64_t play_time_ms;
	int32_t autosave_count;
	int32_t game_over;
	int32_t can_retry;
	int32_t player_control;
	/* 1 when developer tools marked the game (see "Developer tools" below). */
	int32_t dev;
	/* 0 while psiv_dev_encounters has turned battles off for this session. */
	int32_t encounters;
} psiv_world_info;

PSIV_API int32_t psiv_mode_get(const psiv_session* session); /* a psiv_mode, or -1 for a NULL session */
PSIV_API int32_t psiv_menu_kind_get(const psiv_session* session); /* a psiv_menu_kind, or -1 */
PSIV_API psiv_status psiv_world_info_get(psiv_session* session, psiv_world_info* out);
/* Whether a story flag is set. Unknown flags read as false. */
PSIV_API int32_t psiv_flag(const psiv_session* session, const char* name);

/* ------------------------------------------------------------------------------------------------------ */
/* Frame loop and field                                                                                   */

/* MUTATES. Advances the active mode by dt_ms. In the field `direction` is the held direction (a
 * psiv_direction; NONE when nothing is held). In an event it steps the interpreter. Battle and title ignore
 * it. */
PSIV_API psiv_status psiv_update(psiv_session* session, int32_t dt_ms, int32_t direction);
/* MUTATES. Holds `direction` until the leader enters the next cell or something stops the field. */
PSIV_API psiv_status psiv_walk(psiv_session* session, int32_t direction);
/* MUTATES. Turns the leader without moving. Refused outside the field or without player control. */
PSIV_API psiv_status psiv_face(psiv_session* session, int32_t direction);

typedef enum psiv_walk_status { PSIV_WALK_WALKING, PSIV_WALK_ARRIVED, PSIV_WALK_LEFT_AREA, PSIV_WALK_STOPPED, PSIV_WALK_NO_PATH, PSIV_WALK_TOO_LONG } psiv_walk_status;
/* MUTATES. Starts a walk to (x, y) along the shortest path over walkable cells, avoiding exits other than
 * the goal; visible enemies are not avoided, so bumping into one starts its battle. Then call psiv_walk_step
 * until it reports anything but PSIV_WALK_WALKING, running events and battles between the calls: an event
 * or battle on the way only stops the walk (STOPPED) if the field does not come back. */
PSIV_API psiv_status psiv_walk_to(psiv_session* session, int32_t x, int32_t y);
/* MUTATES. One cell of the walk psiv_walk_to started; *status is a psiv_walk_status. PSIV_NOT_FOUND
 * without one. */
PSIV_API psiv_status psiv_walk_step(psiv_session* session, int32_t* status);
/* MUTATES. Interacts with whatever stands in the cell the leader faces: a vehicle of the party is boarded, an open
 * port shows its destinations (PSIV_MENU_PORT). Refused while the party rides a vehicle. */
PSIV_API psiv_status psiv_interact(psiv_session* session);
/* MUTATES. Boards the party's vehicle in the cell the leader faces (psiv_interact does the same). */
PSIV_API psiv_status psiv_board(psiv_session* session);
/* MUTATES. Steps off the ridden vehicle onto the faced cell, or else the first of the other neighbors (north, east,
 * south, west) the party could walk on; the vehicle stays where it is. PSIV_REFUSED when there is none
 * (game.refuse.nowhere_leave) or the party rides nothing (game.refuse.not_aboard). */
PSIV_API psiv_status psiv_leave_vehicle(psiv_session* session);
/* MUTATES. The best party talk set's lines arrive as PSIV_NOTICE_LINE notices. The same set also comes up by itself
 * as PSIV_NOTICE_AMBIENT notices the first time control returns to the field in an area under it, while the profile's
 * ambient_talk is on; saves keep which sets have. */
PSIV_API psiv_status psiv_party_talk(psiv_session* session);

typedef struct psiv_field_info {
	const char* area_id;
	const char* area_name;
	int32_t area_kind;
	int32_t encounter_mode;
	int32_t width;
	int32_t height;
	int32_t step_ms;
	int32_t leader_x;
	int32_t leader_y;
	int32_t facing;
	int32_t player_control;
	int32_t grace_ms;
	/* The area's track outside events and battles, an audio id; "" when the data names none. */
	const char* music;
	/* The world or region the area belongs to; "" when the data names none. */
	const char* region;
	/* The vehicle the party rides, or "". */
	const char* aboard;
	/* The leader's time per cell now: step_ms on foot, the vehicle's speed aboard. */
	int32_t leader_step_ms;
	/* In a vessel: the port the party walked aboard from, where its gangway leads; "" otherwise. */
	const char* ship_port;
} psiv_field_info;

typedef struct psiv_enemy {
	int32_t id;
	const char* encounter;
	const char* zone;
	int32_t x;
	int32_t y;
	int32_t facing;
	int32_t state;
} psiv_enemy;

typedef struct psiv_marker {
	const char* name;
	int32_t x;
	int32_t y;
} psiv_marker;

typedef struct psiv_exit {
	int32_t x;
	int32_t y;
	const char* to_area;
	const char* to_marker;
} psiv_exit;

typedef struct psiv_point {
	int32_t kind;
	const char* id;
	int32_t x;
	int32_t y;
	/* Talk and step points: the event they run. */
	const char* event;
	/* Talk and step points: their flag conditions hold now. Chests: not opened yet. Shops: open now. Ports: open now
	 * (a port that its story has not opened yet is not there to see). Rests, helms and gangway cells: 1. */
	int32_t active;
	/* Rests: the price. */
	int32_t cost;
	/* Shops: a psiv_shop_kind. */
	int32_t shop_kind;
	/* Conveyor cells (one point each; active while their flags hold): the psiv_direction they carry the party toward. */
	int32_t direction;
} psiv_point;

/* One of the party's vehicles in its area. */
typedef struct psiv_vehicle {
	const char* id;
	/* Its name from the battle data. */
	const char* name;
	int32_t x;
	int32_t y;
	/* A psiv_direction. */
	int32_t facing;
	/* 1 when the party rides it; it then stands on the leader's cell. */
	int32_t aboard;
} psiv_vehicle;

/* A place a travel can go now. */
typedef struct psiv_travel_destination {
	/* A port id for PSIV_TRAVEL_PORT, an area id otherwise; what psiv_travel takes. */
	const char* id;
	const char* area;
	const char* area_name;
	const char* region;
	/* Where the party lands. */
	int32_t x;
	int32_t y;
} psiv_travel_destination;

/* One of the game's actors (game.json "actors") standing in the party's area. */
typedef struct psiv_actor {
	const char* id;
	/* Its speaker name in the profile's language. */
	const char* name;
	int32_t x;
	int32_t y;
	/* A psiv_direction. */
	int32_t facing;
	int32_t visible;
	/* 1 once a script has moved, turned, shown or hidden it; 0 while it stands untouched at its talk point. */
	int32_t scripted;
} psiv_actor;

/* PSIV_NOT_FOUND outside an area (at the title before a game). */
PSIV_API psiv_status psiv_field_info_get(psiv_session* session, psiv_field_info* out);
/* width * height bytes, row-major from the top left, each a psiv_cell terrain plus PSIV_CELL_WALKABLE. */
PSIV_API psiv_status psiv_field_grid(psiv_session* session, const uint8_t** out, int32_t* count);
/* The area's map rows as written in its data ('.', '#', '~'). */
PSIV_API psiv_status psiv_field_rows(psiv_session* session, const char* const** out, int32_t* count);
PSIV_API psiv_status psiv_field_enemies(psiv_session* session, const psiv_enemy** out, int32_t* count);
PSIV_API psiv_status psiv_field_markers(psiv_session* session, const psiv_marker** out, int32_t* count);
PSIV_API psiv_status psiv_field_exits(psiv_session* session, const psiv_exit** out, int32_t* count);
/* Talk hooks, step hooks, chests, shops, rest points and ports that stand on a cell. A port's `active` says it is
 * open. */
PSIV_API psiv_status psiv_field_points(psiv_session* session, const psiv_point** out, int32_t* count);
/* The party's vehicles in its area, in id order, the ridden one included. Saves keep where each stands and which one
 * the party rides. */
PSIV_API psiv_status psiv_field_vehicles(psiv_session* session, const psiv_vehicle** out, int32_t* count);
/* Where a travel of `kind` (a psiv_travel_kind) can go now: the other open ports while a port's menu is up, the towns
 * of the region the party has visited (the one it stands in left out), or the one way out of the dungeon it is in.
 * An empty list when there is none. */
PSIV_API psiv_status psiv_travel_destinations(psiv_session* session, int32_t kind, const psiv_travel_destination** out, int32_t* count);
/* MUTATES. Travels to `destination` (an id from psiv_travel_destinations). PORT: from a port's menu; `user` and `thing`
 * are ignored. TOWN and DUNGEON_EXIT: from the field menu, through `thing`, an item (`user` NULL or "") or a technique
 * `user` knows, whose travel effect matches; it is spent as a field use spends it. TOWN is refused inside a dungeon
 * (game.refuse.in_dungeon), DUNGEON_EXIT outside one (game.refuse.not_dungeon); DUNGEON_EXIT takes NULL or "" as its
 * destination. A ridden vehicle stays behind, or goes back to its last boarding point when the party could not reach
 * it. Other refusals: game.refuse.no_port, travel_menu_only, not_travel, no_town, no_way_out, no_destination, and
 * core's field.* keys for the cost. */
PSIV_API psiv_status psiv_travel(psiv_session* session, int32_t kind, const char* destination, const char* user, const char* thing);
/* The actors in the party's area, in game.json order, hidden ones included. An actor no script touched stands at
 * the talk point named after it, visible and facing south. Scripts move, turn, show and hide actors in the party's
 * area; an actor keeps its place when the party leaves, and its state is saved, mid-event saves included. */
PSIV_API psiv_status psiv_field_actors(psiv_session* session, const psiv_actor** out, int32_t* count);
/* Every actor id in game.json order, wherever the actor stands. A script subject that is one of these but not in
 * psiv_field_actors is an actor outside the party's area, which a front end does not draw; any other subject is a
 * character. Works at the title too. */
PSIV_API psiv_status psiv_actor_ids(psiv_session* session, const char* const** out, int32_t* count);

/* ------------------------------------------------------------------------------------------------------ */
/* Events                                                                                                 */

typedef struct psiv_line {
	/* Character or actor id, "" for narration. */
	const char* speaker_id;
	/* The chosen name, the character's name or the actor's speaker string; "" for narration. */
	const char* speaker_name;
	/* The string ID of the text, "" for inline text. */
	const char* text_id;
	/* In the profile's language, name tokens resolved for its "show shipped names". */
	const char* text;
	/* The portrait the line starts with, "" for none. */
	const char* portrait;
	/* "Speaker: text", or the text for narration: what the terminal shows. */
	const char* display;
} psiv_line;

typedef struct psiv_choice {
	/* "" when the script gave no prompt. */
	const char* prompt;
	int32_t yes_no;
	/* For yes/no requests the options are the string table's choice.yes and choice.no (yes = 0, no = 1). */
	int32_t option_count;
	const char* const* options;
} psiv_choice;

PSIV_API int32_t psiv_line_pending(const psiv_session* session);
/* PSIV_NOT_FOUND when no line is waiting. */
PSIV_API psiv_status psiv_line_get(psiv_session* session, psiv_line* out);
PSIV_API psiv_status psiv_line_confirm(psiv_session* session); /* MUTATES */
PSIV_API int32_t psiv_choice_pending(const psiv_session* session);
PSIV_API psiv_status psiv_choice_get(psiv_session* session, psiv_choice* out);
PSIV_API psiv_status psiv_choose(psiv_session* session, int32_t index); /* MUTATES */
PSIV_API int32_t psiv_can_skip(const psiv_session* session);
/* MUTATES. PSIV_REFUSED when the scene cannot be skipped now. */
PSIV_API psiv_status psiv_skip(psiv_session* session);

/* ------------------------------------------------------------------------------------------------------ */
/* Presentation actions: a polled queue, so a host never has to hand the library a callback.              */

typedef struct psiv_action {
	uint64_t handle;
	int32_t phase;
	int32_t type;
	const char* subject;
	const char* target;
	const char* slot;
	int32_t has_position;
	double x;
	double y;
	double z;
	double seconds;
	double speed;
	double strength;
	int32_t loop;
	/* Put the end state up at once; the action is already done. */
	int32_t instant;
	/* The script syntax of the action, for logs. */
	const char* description;
	/* How long the animation lasts by the data: game.json's presentation durations for the type and subject, else the
	 * type, else `seconds`; 0 for none. A host can use it for actions whose length the script does not give. */
	double duration;
} psiv_action;

typedef void (*psiv_action_callback)(void* user, const psiv_action* action);

/* A camera scripts may name, with what game.json says it looks at. A panel or the scene showing it frames that target
 * at `zoom` times the field's scale. */
typedef struct psiv_camera {
	const char* id;
	/* A psiv_camera_target. */
	int32_t target;
	/* PSIV_CAMERA_MARKER: the area and marker; otherwise "". */
	const char* area;
	const char* marker;
	/* PSIV_CAMERA_FOLLOW: the actor or character id; otherwise "". */
	const char* follow;
	/* 1 when the data gives none. */
	double zoom;
} psiv_camera;

/* Every camera in game.json order. Works at the title too. */
PSIV_API psiv_status psiv_cameras(psiv_session* session, const psiv_camera** out, int32_t* count);
/* MUTATES. Takes every queued record, oldest first. */
PSIV_API psiv_status psiv_actions_poll(psiv_session* session, const psiv_action** out, int32_t* count);
/* MUTATES. In PSIV_PRESENTATION_WAIT, marks an action's animation as ended. Unknown handles are ignored. */
PSIV_API psiv_status psiv_action_finish(psiv_session* session, uint64_t handle);
/* Optional: also called synchronously as each record is queued (the record stays in the queue too). The
 * callback may call psiv_action_finish and nothing else on the session; the record's pointers are valid
 * only during the call. NULL removes it. */
PSIV_API psiv_status psiv_actions_set_callback(psiv_session* session, psiv_action_callback callback, void* user);

/* ------------------------------------------------------------------------------------------------------ */
/* Notices: everything the game says outside event lines and battle rounds.                               */

typedef struct psiv_notice {
	int32_t kind;
	/* "" for warnings about broken data. */
	const char* key;
	int32_t param_count;
	const char* const* params;
	const char* text;
} psiv_notice;

/* MUTATES. Takes every notice, oldest first. */
PSIV_API psiv_status psiv_notices_drain(psiv_session* session, const psiv_notice** out, int32_t* count);

/* ------------------------------------------------------------------------------------------------------ */
/* Battle                                                                                                 */

typedef struct psiv_item_count {
	const char* id;
	int32_t count;
} psiv_item_count;

typedef struct psiv_active_status {
	const char* id;
	const char* name;
	/* Rounds left, 0 for no limit. */
	int32_t remaining;
} psiv_active_status;

typedef struct psiv_combatant {
	int32_t index;
	const char* id;
	const char* name;
	/* The name with A, B, ... added when several enemies share it. */
	const char* label;
	int32_t side;
	const char* group;
	int32_t hp;
	int32_t max_hp;
	int32_t tp;
	int32_t max_tp;
	int32_t alive;
	int32_t defending;
	int32_t status_count;
	const psiv_active_status* statuses;
	/* This round's command so far. */
	int32_t command_kind;
	const char* command_id;
	int32_t command_target;
	/* An enemy the data marks as flying, for a renderer to show off the ground. */
	int32_t flying;
	/* Enemies: the formation position, from 0, free places counted; -1 for the party side. */
	int32_t position;
} psiv_combatant;

typedef struct psiv_battle_info {
	/* 1 while the battle runs; 0 when this describes the battle that ended, which the game keeps until the
	 * next battle starts, psiv_battle_dismiss, a new game, a load or the title. */
	int32_t live;
	const char* encounter;
	int32_t source;
	int32_t round;
	int32_t party_count;
	int32_t combatant_count;
	int32_t outcome;
	int32_t escape_allowed;
	int32_t auto_active;
	int32_t party_acts_next_round;
	int32_t game_over;
	/* The encounter's track from game.json, an audio id; "" when the data names none. */
	const char* music;
	/* A vehicle battle's vehicle, the combatant after the members riding it; "" otherwise. */
	const char* vehicle;
	/* Places in the enemy formation, free ones included, which a stage spreads the enemies over. */
	int32_t formation_size;
} psiv_battle_info;

typedef struct psiv_battle_option {
	/* An ability or item id. */
	const char* id;
	const char* name;
	/* PSIV_COMMAND_ABILITY or PSIV_COMMAND_ITEM. */
	int32_t command;
	/* Abilities: a psiv_ability_kind. Items: -1. */
	int32_t ability_kind;
	int32_t targeting;
	int32_t cost;
	/* Skills: uses left. Items: count held. Otherwise -1. */
	int32_t uses;
	/* The psiv_cancel_reason it would be cancelled with now, PSIV_CANCEL_NONE when usable. */
	int32_t unusable;
} psiv_battle_option;

/* PSIV_NOT_FOUND when there is neither a battle nor an ended one. */
PSIV_API psiv_status psiv_battle_info_get(psiv_session* session, psiv_battle_info* out);
/* The live combatants during the battle; after it, the final combatants of the ended battle. */
PSIV_API psiv_status psiv_battle_combatants(psiv_session* session, const psiv_combatant** out, int32_t* count);

typedef struct psiv_level_up {
	const char* member;
	const char* name;
	int32_t level;
	int32_t learned_count;
	const char* const* learned;
} psiv_level_up;
/* What an ended battle paid out. Experience, money and drops are 0 and empty unless the party won or
 * reached a scripted end. */
typedef struct psiv_battle_result {
	const char* encounter;
	int32_t source;
	int32_t outcome;
	int32_t game_over;
	int32_t experience;
	int32_t money;
	int32_t drop_count;
	const psiv_item_count* drops;
	int32_t level_up_count;
	const psiv_level_up* level_ups;
} psiv_battle_result;
/* PSIV_NOT_FOUND unless an ended battle is kept. */
PSIV_API psiv_status psiv_battle_result_get(psiv_session* session, psiv_battle_result* out);
/* MUTATES. Forgets the ended battle. */
PSIV_API psiv_status psiv_battle_dismiss(psiv_session* session);
/* What `actor` (a party combatant index) can choose this round: known abilities, then the battle's items. */
PSIV_API psiv_status psiv_battle_options(psiv_session* session, int32_t actor, const psiv_battle_option** out, int32_t* count);
/* MUTATES. kind is a psiv_command_kind; id the ability or item; target a combatant index or -1 to let the
 * battle pick. Refusals are core's message keys. */
PSIV_API psiv_status psiv_battle_set_command(psiv_session* session, int32_t actor, int32_t kind, const char* id, int32_t target);

typedef struct psiv_macro_step_result {
	const char* member;
	int32_t actor;
	int32_t status;
	int32_t issue;
	int32_t command_kind;
	const char* command_id;
	int32_t command_target;
} psiv_macro_step_result;

/* MUTATES. Sets the macro's members' commands; *out describes each step. */
PSIV_API psiv_status psiv_battle_apply_macro(psiv_session* session, const char* macro, const psiv_macro_step_result** out, int32_t* count);
/* MUTATES. As psiv_battle_apply_macro with a psiv_macro_fallback. PSIV_MACRO_FALLBACK_AUTOMATIC gives each member whose
 * step cannot be paid for, has no target or would be refused core's automatic choice for the round instead (status
 * PSIV_MACRO_AUTOMATIC, the issue saying why); PSIV_MACRO_FALLBACK_BASIC_ATTACK is psiv_battle_apply_macro. */
PSIV_API psiv_status psiv_battle_apply_macro_fallback(psiv_session* session, const char* macro, int32_t fallback, const psiv_macro_step_result** out,
	int32_t* count);
/* MUTATES. Auto-battle with the save's auto macro (psiv_settings_set_auto_macro). */
PSIV_API psiv_status psiv_battle_start_auto(psiv_session* session);
PSIV_API psiv_status psiv_battle_cancel_auto(psiv_session* session); /* MUTATES */
/* MUTATES. While auto-battle runs, fills the round and sets *filled to 1. Once a stop rule fires it sets
 * *filled to 0 and the reason arrives as a notice. */
PSIV_API psiv_status psiv_battle_fill_from_auto(psiv_session* session, int32_t* filled);
/* MUTATES. Core's simple policy for every party member who acts this round. */
PSIV_API psiv_status psiv_battle_fill_from_policy(psiv_session* session);
/* MUTATES. Fills the round the way auto play does: on the first round it starts auto-battle with the save's
 * auto macro when one is set, then fills from auto-battle, or from core's policy once auto-battle is off.
 * PSIV_REFUSED when auto-battle could not start; the round is filled from the policy anyway. */
PSIV_API psiv_status psiv_battle_fill_auto_round(psiv_session* session);


typedef struct psiv_recorded_command {
	int32_t actor;
	int32_t kind;
	const char* id;
	int32_t target;
} psiv_recorded_command;

/* One typed battle event. Fields a type does not use keep their defaults (-1 for indexes, "" for strings,
 * 0 otherwise). core/README.md "Events" lists which fields each type uses. */
typedef struct psiv_battle_event {
	int32_t type;
	int32_t round;
	int32_t actor;
	int32_t target;
	int32_t previous_target;
	const char* ability;
	const char* combo;
	const char* status;
	/* Status events and Message: core's text key and parameters. */
	const char* message;
	int32_t param_count;
	const char* const* params;
	/* Status events, Message and ActionCancelled: the line in the profile's language, combatants by label.
	 * "" for other types. */
	const char* text;
	int32_t amount;
	int32_t hp;
	int32_t duration;
	int32_t command;
	int32_t element;
	int32_t effectiveness;
	int32_t stat;
	int32_t cancel;
	int32_t status_end;
	int32_t outcome;
	int32_t resource;
	int32_t opening;
	int32_t succeeded;
	int32_t game_over;
	int32_t experience;
	int32_t currency;
	int32_t item_count;
	const psiv_item_count* items;
	const char* macro;
	int32_t participant_count;
	const int32_t* participants;
	int32_t order_count;
	const int32_t* order;
	int32_t id_count;
	const char* const* ids;
	const char* const* groups;
	int32_t party_count;
	int32_t escape_allowed;
	int32_t defeat_has_branch;
	int32_t vehicle_battle;
	int32_t command_count;
	const psiv_recorded_command* commands;
} psiv_battle_event;

/* MUTATES. Resolves one round and returns its events. The battle ends inside this call when an outcome is
 * reached: rewards, level-ups and the return to the field, the event or (game over) the title happen here,
 * and their notices are queued. */
PSIV_API psiv_status psiv_battle_resolve_round(psiv_session* session, const psiv_battle_event** out, int32_t* count);
/* The events of the last round resolved, again. */
PSIV_API psiv_status psiv_battle_last_events(psiv_session* session, const psiv_battle_event** out, int32_t* count);

/* One round of psiv_battle_play: the notices from before the round was resolved, then its events. */
typedef struct psiv_auto_round {
	int32_t notice_count;
	const psiv_notice* notices;
	int32_t event_count;
	const psiv_battle_event* events;
} psiv_auto_round;
typedef struct psiv_auto_battle {
	/* Why auto-battle with the auto macro did not start, or "" with no parameters. */
	const char* refusal_key;
	int32_t refusal_param_count;
	const char* const* refusal_params;
	const char* refusal_text;
	int32_t round_count;
	const psiv_auto_round* rounds;
	/* 0 when max_rounds ran out first. */
	int32_t ended;
} psiv_auto_battle;
/* MUTATES. Plays the battle to its end with psiv_battle_fill_auto_round and psiv_battle_resolve_round, at
 * most max_rounds rounds (60 for 0 or less). Notices after the last round stay for psiv_notices_drain. */
PSIV_API psiv_status psiv_battle_play(psiv_session* session, int32_t max_rounds, psiv_auto_battle* out);

/* The battle record (core's text format, for bug reports and replays) of the live battle, or of the ended
 * one. PSIV_NOT_FOUND with neither. */
PSIV_API psiv_status psiv_battle_record(psiv_session* session, const char** out);
/* Rebuilds a battle from a record against this session's data and returns its full event stream, typed and
 * as core's stable text form (one line per event). PSIV_DATA_ERROR for a record that does not parse, or that
 * is from other rules or data. */
PSIV_API psiv_status psiv_battle_replay(psiv_session* session, const char* record, const psiv_battle_event** events, int32_t* count, const char** event_text);

/* ------------------------------------------------------------------------------------------------------ */
/* Party, inventory, menus and shops                                                                      */

typedef struct psiv_member {
	const char* id;
	const char* name;
	int32_t in_lineup;
	int32_t level;
	int32_t experience;
	int32_t hp;
	int32_t max_hp;
	int32_t tp;
	int32_t max_tp;
	int32_t strength;
	int32_t mental;
	int32_t agility;
	int32_t dexterity;
	int32_t defense;
	int32_t status_count;
	const psiv_active_status* statuses;
	/* Indexed by psiv_equip_slot; "" for an empty slot. */
	const char* equipment[PSIV_SLOT_COUNT];
	const char* equipment_names[PSIV_SLOT_COUNT];
} psiv_member;

/* The active party in battle order, then the rest of the roster. */
PSIV_API psiv_status psiv_party(psiv_session* session, const psiv_member** out, int32_t* count);

typedef struct psiv_ability_info {
	const char* id;
	const char* name;
	int32_t kind;
	int32_t targeting;
	int32_t cost;
	/* Usable outside battle from the menu. */
	int32_t field;
	/* Skills: uses left and uses after a rest. Otherwise -1. */
	int32_t uses;
	int32_t max_uses;
	/* A technique that travels: the psiv_travel_kind psiv_travel takes it with (TOWN or DUNGEON_EXIT); -1 otherwise. */
	int32_t travel;
} psiv_ability_info;

PSIV_API psiv_status psiv_member_abilities(psiv_session* session, const char* member, const psiv_ability_info** out, int32_t* count);

/* MUTATES. From the field menu ("game.refuse.party_menu_only"): swaps two lineup positions, the leader's included
 * ("game.refuse.party_no_position" for one past the lineup). Members join and leave only through events. */
PSIV_API psiv_status psiv_party_swap(psiv_session* session, int32_t a, int32_t b);

typedef struct psiv_stat_bonus {
	int32_t max_hp;
	int32_t max_tp;
	int32_t strength;
	int32_t mental;
	int32_t agility;
	int32_t dexterity;
	int32_t defense;
	int32_t critical;
	int32_t evasion;
	/* Used only when psiv_game_info's magic_defense is set. */
	int32_t magic_defense;
} psiv_stat_bonus;

typedef struct psiv_affinity {
	/* The element's index in psiv_elements. */
	int32_t element;
	/* That element's damage taken, on the rules' affinity divisor scale (psiv_game_info's affinity_divisor): above the
	 * divisor a weakness, 0 immunity. */
	int32_t percent;
	/* The element's id and its name in the profile's language. */
	const char* id;
	const char* name;
} psiv_affinity;

typedef struct psiv_status_resist {
	const char* id;
	const char* name;
	/* Percent taken off the status's chance; 100 is immunity. */
	int32_t percent;
} psiv_status_resist;

/* What a status screen shows beyond psiv_member. */
typedef struct psiv_member_detail {
	const char* id;
	/* Experience still needed for the next level; 0 at the level cap. */
	int32_t experience_to_next;
	/* With equipment, like psiv_member's stats. */
	int32_t critical;
	int32_t evasion;
	/* The worn weapon's psiv_element, PSIV_ELEMENT_NONE without one. */
	int32_t attack_element;
	/* Each worn piece's bonus, indexed by psiv_equip_slot; all 0 for an empty slot. */
	psiv_stat_bonus equipment_bonus[PSIV_SLOT_COUNT];
	/* Each element the member takes at other than the normal rate, in psiv_elements order. */
	int32_t affinity_count;
	const psiv_affinity* affinities;
	/* The character's resistances plus its pieces', capped at 100, in status id order. */
	int32_t status_resist_count;
	const psiv_status_resist* status_resists;
	/* With equipment; see psiv_game_info's magic_defense. */
	int32_t magic_defense;
} psiv_member_detail;

/* PSIV_NOT_FOUND for a character not in the roster. */
PSIV_API psiv_status psiv_member_detail_get(psiv_session* session, const char* member, psiv_member_detail* out);

/* A member's battle numbers, built by the same code that sets up a battle combatant. */
typedef struct psiv_combat_view {
	/* The member's totals: level stats plus equipment. */
	psiv_stat_bonus stats;
	/* The basic attack, which a weapon may replace, and its name. */
	const char* basic_attack;
	const char* basic_attack_name;
	/* A psiv_element: the weapon's, PSIV_ELEMENT_NONE without one. */
	int32_t attack_element;
	/* Known abilities, equipment-granted ones included, with their names. */
	int32_t ability_count;
	const char* const* abilities;
	const char* const* ability_names;
	/* The character's resistances plus its pieces', capped at 100, in status id order. */
	int32_t status_resist_count;
	const psiv_status_resist* status_resists;
} psiv_combat_view;

/* An equipment piece compared on a member before it is put on. */
typedef struct psiv_equip_preview {
	const char* member;
	const char* piece;
	const char* piece_name;
	/* A psiv_equip_slot. */
	int32_t slot;
	/* The equipment category characters' equip lists name. */
	const char* category;
	/* The piece's own bonuses, its weapon element (a psiv_element), the basic attack it replaces with ("" for none), the
	 * abilities it grants and the status resistances it adds. */
	psiv_stat_bonus bonus;
	int32_t element;
	const char* attack;
	const char* attack_name;
	int32_t granted_count;
	const char* const* granted;
	const char* const* granted_names;
	int32_t piece_resist_count;
	const psiv_status_resist* piece_resists;
	/* The piece the member wears in that slot now, "" for none. */
	const char* replaces;
	const char* replaces_name;
	/* 1 when the member can wear it; otherwise refusal_key is core's reason ("equip.wrong_category") and its text. */
	int32_t can_wear;
	const char* refusal_key;
	const char* refusal_text;
	/* Lineup members who can wear the piece, in lineup order, with their names. */
	int32_t wearer_count;
	const char* const* wearers;
	const char* const* wearer_names;
	/* The member now, and wearing the piece in place of `replaces` (the same as before when it cannot wear it). */
	psiv_combat_view before;
	psiv_combat_view after;
	/* Where the piece goes (a psiv_equip_slot): the slot, or a hand for a weapon or shield. */
	int32_t position;
	/* The other hand's piece it also sends back: beside a two-handed weapon, or a two-handed weapon a piece in the left
	 * hand replaces; "" for none. */
	const char* also_replaces;
	const char* also_replaces_name;
	/* 2 for a weapon that needs both hands, otherwise 1. */
	int32_t hands;
} psiv_equip_preview;

/* Works on any equipment piece, held or not, so shops can compare too. PSIV_NOT_FOUND for a character not in the
 * roster or an id that is not equipment. The piece goes where psiv_equip puts it. */
PSIV_API psiv_status psiv_equip_preview_get(psiv_session* session, const char* member, const char* piece, psiv_equip_preview* out);
/* The same at a position (a psiv_equip_slot): a hand for a weapon or shield. A position the piece does not fit leaves
 * can_wear 0 with refusal_key equip.wrong_position. */
PSIV_API psiv_status psiv_equip_preview_at(psiv_session* session, const char* member, const char* piece, int32_t position, psiv_equip_preview* out);

typedef struct psiv_combo_entry {
	const char* recipe;
	/* The combination's ability and its name. */
	const char* result;
	const char* result_name;
	int64_t play_time_ms;
	/* Character ids in the order they acted, and their names. */
	int32_t participant_count;
	const char* const* participants;
	const char* const* participant_names;
	/* The abilities the recipe needs, in recipe order, and their names. */
	int32_t ability_count;
	const char* const* abilities;
	const char* const* ability_names;
} psiv_combo_entry;

/* The combo log in discovery order. *total (may be NULL) is the number of recipes, discovered or not. */
PSIV_API psiv_status psiv_combo_log(psiv_session* session, const psiv_combo_entry** out, int32_t* count, int32_t* total);

typedef struct psiv_inventory_entry {
	const char* id;
	const char* name;
	int32_t count;
	/* 1 for equipment pieces in the bag, 0 for items. */
	int32_t equipment;
	/* Items: usable from the menu. Equipment: 0. */
	int32_t field;
	/* Items: a psiv_targeting. Equipment: the psiv_equip_slot. */
	int32_t targeting_or_slot;
	/* The shop's buy price, -1 when it cannot be bought or sold. */
	int32_t price;
	/* An item that travels: the psiv_travel_kind psiv_travel takes it with (TOWN or DUNGEON_EXIT); -1 otherwise. */
	int32_t travel;
	/* An item that calls a vehicle (psiv_use_item with no member): the vehicle's id; "" otherwise. */
	const char* calls_vehicle;
	/* Equipment: 2 for a weapon that needs both hands, otherwise 1. Items: 0. */
	int32_t hands;
} psiv_inventory_entry;

/* Items in id order, then unworn equipment in id order. */
PSIV_API psiv_status psiv_inventory(psiv_session* session, const psiv_inventory_entry** out, int32_t* count);
/* The same entries in a psiv_item_sort order; PSIV_SORT_ID is psiv_inventory's order. */
PSIV_API psiv_status psiv_inventory_sorted(psiv_session* session, int32_t order, const psiv_inventory_entry** out, int32_t* count);

typedef struct psiv_shop_info {
	const char* id;
	int32_t sell_percent;
	int32_t stock_count;
	/* count is what the party holds; price the buy price. Only what the shop sells now, by its stock's flags. */
	const psiv_inventory_entry* stock;
	/* A psiv_shop_kind. */
	int32_t kind;
} psiv_shop_info;

typedef struct psiv_inn_info {
	const char* id;
	int32_t cost;
} psiv_inn_info;

/* PSIV_NOT_FOUND unless a shop is open. */
PSIV_API psiv_status psiv_shop_get(psiv_session* session, psiv_shop_info* out);
/* PSIV_NOT_FOUND unless the inn menu is open. */
PSIV_API psiv_status psiv_inn_get(psiv_session* session, psiv_inn_info* out);
PSIV_API psiv_status psiv_open_menu(psiv_session* session); /* MUTATES */
/* MUTATES. Closes the menu, shop or inn; one an event opened closes back to the event. */
PSIV_API psiv_status psiv_close_menu(psiv_session* session);
PSIV_API psiv_status psiv_buy(psiv_session* session, const char* id, int32_t count); /* MUTATES */
PSIV_API psiv_status psiv_sell(psiv_session* session, const char* id, int32_t count); /* MUTATES */
PSIV_API psiv_status psiv_rest(psiv_session* session); /* MUTATES */
/* MUTATES. Field use from the menu. member is a character id of the active party, or NULL/"" for everyone. */
PSIV_API psiv_status psiv_use_item(psiv_session* session, const char* item, const char* member);
PSIV_API psiv_status psiv_use_ability(psiv_session* session, const char* user, const char* ability, const char* member); /* MUTATES */
/* MUTATES. Puts the piece on where it goes by default: a weapon in the right hand (PSIV_SLOT_WEAPON), a shield in the
 * left (PSIV_SLOT_LEFT_HAND), anything else in its slot. */
PSIV_API psiv_status psiv_equip(psiv_session* session, const char* member, const char* equipment);
/* MUTATES. Puts the piece on at a position it fits (a hand for weapons and shields); a two-handed weapon goes in the
 * right hand and sends the left hand's piece back to the bag. PSIV_REFUSED with equip.wrong_position otherwise. */
PSIV_API psiv_status psiv_equip_at(psiv_session* session, const char* member, const char* equipment, int32_t position);
PSIV_API psiv_status psiv_unequip(psiv_session* session, const char* member, int32_t slot); /* MUTATES */
/* The member a name entry is naming, "" when none is open. */
PSIV_API const char* psiv_name_entry_member(psiv_session* session);
PSIV_API psiv_status psiv_enter_name(psiv_session* session, const char* name); /* MUTATES */

/* ------------------------------------------------------------------------------------------------------ */
/* Journal, text log and text                                                                             */

typedef struct psiv_job {
	const char* id;
	const char* title;
	int32_t state;
} psiv_job;

typedef struct psiv_quest_log {
	/* "" when no objective holds. */
	const char* objective;
	int32_t job_count;
	const psiv_job* jobs;
} psiv_quest_log;

typedef struct psiv_codex_entry {
	const char* id;
	const char* title;
	const char* text;
} psiv_codex_entry;

typedef struct psiv_glossary_entry {
	const char* id;
	const char* restored;
	const char* shipped;
} psiv_glossary_entry;

PSIV_API psiv_status psiv_quest_log_get(psiv_session* session, psiv_quest_log* out);
PSIV_API psiv_status psiv_encyclopedia(psiv_session* session, const psiv_codex_entry** out, int32_t* count);
PSIV_API psiv_status psiv_glossary(psiv_session* session, const psiv_glossary_entry** out, int32_t* count);
/* The last 500 event lines since the game was loaded, rendered now, skipped ones marked. */
PSIV_API psiv_status psiv_text_log(psiv_session* session, const char* const** out, int32_t* count);
/* The pacing table as plain text, one row per line. */
PSIV_API psiv_status psiv_pacing_table(psiv_session* session, const char** out);
/* The numbers behind the table (encounters.md, "Pacing measurement"), per area in first-visit order. */
typedef struct psiv_pacing_area {
	const char* area;
	int32_t visits;
	int32_t battles;
	int32_t steps;
	int64_t field_ms;
	int64_t battle_ms;
	int64_t event_ms;
	int32_t experience;
	int32_t money;
	/* Field time and steps between one battle's end and the next one's start (M2). */
	int32_t gap_count;
	const int64_t* gap_ms;
	const int32_t* gap_steps;
} psiv_pacing_area;
typedef struct psiv_pacing_boss {
	const char* flag;
	const char* encounter;
	const char* area;
	/* Average active party level in hundredths when the boss battle started (M5). */
	int32_t average_level_x100;
	/* Dungeon entry to the boss's end (M6), -1 outside a dungeon or before the boss falls. */
	int64_t dungeon_ms;
} psiv_pacing_boss;
PSIV_API psiv_status psiv_pacing_areas(psiv_session* session, const psiv_pacing_area** out, int32_t* count);
PSIV_API psiv_status psiv_pacing_bosses(psiv_session* session, const psiv_pacing_boss** out, int32_t* count);

/* A key and parameters in the profile's language, parameters shown as display names. */
PSIV_API psiv_status psiv_render(psiv_session* session, const char* key, const char* const* params, int32_t param_count, const char** out);
/* A template filled with the parameters as given. */
PSIV_API psiv_status psiv_format_text(psiv_session* session, const char* key, const char* const* params, int32_t param_count, const char** out);
/* A character's chosen or data name, an actor's speaker name, an ability, item, status, enemy, equipment or
 * area name; the id itself when it is none of those. */
PSIV_API psiv_status psiv_display_name(psiv_session* session, const char* id, const char** out);
/* A dialogue string ID in the profile's language. */
PSIV_API psiv_status psiv_string_text(psiv_session* session, const char* id, const char** out);
/* The description of an item, equipment piece, ability, status, enemy or difficulty (kind is a psiv_description_kind)
 * in the profile's language, "" when that language has none. PSIV_NOT_FOUND when the battle data has no such id of
 * that kind. Descriptions are the optional text keys desc.<kind>.<id> of the game's text tables. */
PSIV_API psiv_status psiv_description(psiv_session* session, int32_t kind, const char* id, const char** out);

/* ------------------------------------------------------------------------------------------------------ */
/* Developer tools (../../docs/dev-tools.md). Every command that changes the game marks it: the mark is    */
/* saved (psiv_world_info.dev, psiv_save_summary.dev), and from then on the game writes no clear-data      */
/* record and adds nothing to the pacing numbers. Refusals: game.refuse.no_game at the title, the          */
/* game.refuse.dev_* keys otherwise. A refused command changes nothing and does not mark the game.         */

/* The ids of one psiv_dev_list kind, sorted. Works at the title too. */
PSIV_API psiv_status psiv_dev_ids(psiv_session* session, int32_t list, const char* const** out, int32_t* count);
/* MUTATES. From the field, to a marker of any area (a ridden vehicle stays where it was). */
PSIV_API psiv_status psiv_dev_warp(psiv_session* session, const char* area, const char* marker);
/* MUTATES. The same to a walkable cell (game.refuse.dev_bad_cell otherwise). */
PSIV_API psiv_status psiv_dev_warp_cell(psiv_session* session, const char* area, int32_t x, int32_t y);
/* MUTATES. Sets (value 1) or clears a declared flag; the area's flag hooks run as when a script sets it. */
PSIV_API psiv_status psiv_dev_set_flag(psiv_session* session, const char* flag, int32_t value);
/* MUTATES. Adds count of an item or equipment piece, capped; a negative count takes them away. Not in a battle. */
PSIV_API psiv_status psiv_dev_give(psiv_session* session, const char* id, int32_t count);
/* MUTATES. Adds amount to the money (negative takes it away), kept between 0 and the cap. Not in a battle. */
PSIV_API psiv_status psiv_dev_money(psiv_session* session, int32_t amount);
/* MUTATES. Sets a roster member's level (1 to 99) with no experience into it and refills it as a rest does, reviving it. */
PSIV_API psiv_status psiv_dev_set_level(psiv_session* session, const char* member, int32_t level);
/* MUTATES. From the field, a battle with the encounter as a random battle (a vehicle battle while riding). */
PSIV_API psiv_status psiv_dev_battle(psiv_session* session, const char* encounter);
/* MUTATES. Ends the live battle: every enemy (victory 1) or every party combatant (0) falls, then one round resolves and
 * the battle ends as that outcome leads (a defeat without a scripted branch is a game over). The round's events are
 * returned and kept as psiv_battle_last_events. game.refuse.no_battle without a live battle. */
PSIV_API psiv_status psiv_dev_end_battle(psiv_session* session, int32_t victory, const psiv_battle_event** out, int32_t* count);
/* MUTATES. From the field, gives the party a vehicle with field rules, or moves one it has, to a marker of a field area. */
PSIV_API psiv_status psiv_dev_vehicle(psiv_session* session, const char* vehicle, const char* area, const char* marker);
/* MUTATES. Turns random battles, battles aboard and visible enemies' battles off (0) or on for this session; not saved. */
PSIV_API psiv_status psiv_dev_encounters(psiv_session* session, int32_t on);
/* MUTATES. From the field, runs an event as a hook would. */
PSIV_API psiv_status psiv_dev_run_event(psiv_session* session, const char* event);
/* MUTATES. Marks the game without changing anything else, for a host's own tools. Nothing at the title. */
PSIV_API psiv_status psiv_dev_mark(psiv_session* session);

/* Where the running event is. */
typedef struct psiv_event_position {
	/* The innermost running event (a called one inside its caller). */
	const char* event;
	/* The script file, as game.json names it, and the source line of the instruction the event is at: the command it
	 * waits on, or the first live branch's while it waits on a together or race block. */
	const char* file;
	int32_t line;
} psiv_event_position;

/* PSIV_NOT_FOUND while no event runs. */
PSIV_API psiv_status psiv_event_position_get(psiv_session* session, psiv_event_position* out);

/* An encounter zone of the party's area. */
typedef struct psiv_zone {
	const char* id;
	/* The rectangle it covers, corners included. */
	int32_t from_x;
	int32_t from_y;
	int32_t to_x;
	int32_t to_y;
	/* Its encounter table: encounter ids and weights. */
	int32_t entry_count;
	const char* const* encounters;
	const int32_t* weights;
	/* Visible mode: where its enemies spawn, as x, y pairs (2 * spawn_count numbers). */
	int32_t spawn_count;
	const int32_t* spawns;
} psiv_zone;

/* The zones of the party's area in data order. PSIV_NOT_FOUND outside an area. */
PSIV_API psiv_status psiv_field_zones(psiv_session* session, const psiv_zone** out, int32_t* count);

/* The game as a save file's text, the running event's interpreter state included; during a battle, the world as the
 * battle found it (loading sets an event battle up again and drops a field battle). PSIV_REFUSED at the title
 * (game.refuse.no_game). */
PSIV_API psiv_status psiv_save_text(psiv_session* session, const char** out);
/* MUTATES. Loads such a text (or any save file's text) from the title, as psiv_load does. */
PSIV_API psiv_status psiv_load_text(psiv_session* session, const char* text);
/* MUTATES. Loads the data again from the directories the session was created with, then, when it loads, puts a new game
 * session in place of the old one with the same options, profile and encounter switch, and the game restored from
 * psiv_save_text (marked, as a developer tool's use). The handle stays the same. On data problems it returns
 * PSIV_DATA_ERROR with every problem in psiv_data_problems and keeps the old session; when the restored game does not
 * load into the new data (an area gone), PSIV_REFUSED with the load's refusal, the old session kept. What is not in
 * the text starts over: the text log, the pacing numbers, an ended battle, a field battle in progress, the presentation
 * queue (actions in flight are rebuilt as instant ones) and a game over. An event whose scripts changed starts again
 * from its first command with the world it started with. */
PSIV_API psiv_status psiv_session_reload(psiv_session* session);

/* ------------------------------------------------------------------------------------------------------ */
/* Followers, kinds, vehicle skill uses, the clear-data record and the audio list.                          */

/* The actor walking with the party without joining it (a script's follower command), "" for none. */
PSIV_API psiv_status psiv_follower(psiv_session* session, const char** out);
/* A character's kind (a psiv_creature_kind). PSIV_NOT_FOUND for an unknown character. */
PSIV_API psiv_status psiv_character_kind(psiv_session* session, const char* character, int32_t* out);
/* A vehicle's uses left of one of its skills, carried from battle to battle and refilled by rest. PSIV_NOT_FOUND when
 * the party does not have the vehicle or the vehicle has no such skill. */
PSIV_API psiv_status psiv_vehicle_skill_uses(psiv_session* session, const char* vehicle, const char* skill, int32_t* out);
/* 1 when the save directory holds a readable clear-data record (the story was finished), else 0. Works at the title. */
PSIV_API psiv_status psiv_clear_record_exists(psiv_session* session, int32_t* out);
/* Every audio id game.json's presentation lists, in data order, for a sound test. Works at the title. */
PSIV_API psiv_status psiv_audio_ids(psiv_session* session, const char* const** out, int32_t* count);

/* An element the data knows. Element fields elsewhere (psiv_battle_event, psiv_member_detail, psiv_combat_view,
 * psiv_equip_preview, psiv_affinity) hold this index: 0 is none, PSIV_ELEMENT_HEAT to PSIV_ELEMENT_HOLY the built-in
 * ones, and the elements rules.json declares follow. */
typedef struct psiv_element_info {
	int32_t index;
	const char* id;
	/* The name in the profile's language (text key name.element.<id>), else the id. */
	const char* name;
} psiv_element_info;
/* Every element, in index order from 1. Works at the title. */
PSIV_API psiv_status psiv_elements(psiv_session* session, const psiv_element_info** out, int32_t* count);

#ifdef __cplusplus
}
#endif

#endif
