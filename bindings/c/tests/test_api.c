/* Focused tests of the C interface: errors, string ownership, saves, a battle through the event stream,
 * presentation actions and text. */
#include "psiv_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define MAKE_DIR(path) _mkdir(path)
#else
#define MAKE_DIR(path) mkdir(path, 0755)
#endif

static const char* dataDir = "../game/data";
/* The placeholder data with Wren Hollow added (game/tests/actor_data.h); empty skips testScriptedActors. */
static const char* actorDataDir = "";
/* The same with Wren Cellar and the interaction rules turned on (actor_data::makeInteract); empty skips testInteraction. */
static const char* interactDataDir = "";
static const char* backgroundDataDir = "";
static const char* sceneDataDir = "";
static const char* motionDataDir = "";
static const char* scratch = "build/scratch";
static int failures;
static int checks;

#define CHECK(condition) \
	do { \
		++checks; \
		if (!(condition)) { \
			++failures; \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
		} \
	} while (0)

#define CHECK_OK(call) \
	do { \
		++checks; \
		const psiv_status status_ = (call); \
		if (status_ != PSIV_OK) { \
			psiv_error e_; \
			psiv_last_error(s, &e_); \
			++failures; \
			fprintf(stderr, "%s:%d: %s returned %d: %s\n", __FILE__, __LINE__, #call, status_, e_.text); \
		} \
	} while (0)

static void makeDirectory(const char* path) {
	char partial[512];
	for (size_t i = 0; path[i]; ++i) {
		partial[i] = path[i];
		partial[i + 1] = '\0';
		if (path[i + 1] == '/' || path[i + 1] == '\0') {
			MAKE_DIR(partial);
		}
	}
}

static void removeFiles(const char* dir) {
	static const char* names[] = {"manual_0", "manual_1", "manual_2", "manual_3", "auto_0", "auto_1", "auto_2", "profile.cfg", "clear.sav"};
	char path[512];
	for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) {
		snprintf(path, sizeof path, "%s/%s%s", dir, names[i], strcmp(names[i], "profile.cfg") == 0 || strcmp(names[i], "clear.sav") == 0 ? "" : ".sav");
		remove(path);
	}
}

static psiv_session* create(const char* saves, int encounterMode, int presentation) {
	psiv_session_options options;
	psiv_session* s = NULL;
	psiv_session_options_init(&options);
	options.data_dir = dataDir;
	options.save_dir = saves;
	options.seed = 285;
	options.encounter_mode = encounterMode;
	options.presentation = presentation;
	if (psiv_session_create(&options, &s) != PSIV_OK) {
		psiv_error e;
		psiv_last_error(NULL, &e);
		fprintf(stderr, "cannot create a session: %s\n", e.text);
		exit(1);
	}
	return s;
}

/* Runs events forward: confirms lines, answers choices with 0, until control is back. */
static void settle(psiv_session* s) {
	for (int guard = 0; guard < 10000; ++guard) {
		const int32_t mode = psiv_mode_get(s);
		if (mode != PSIV_MODE_EVENT) {
			return;
		}
		if (psiv_line_pending(s)) {
			psiv_line_confirm(s);
		} else if (psiv_choice_pending(s)) {
			psiv_choose(s, 0);
		} else {
			psiv_update(s, 100, PSIV_DIRECTION_NONE);
		}
	}
}

static void testVersion(void) {
	const char* capabilities = psiv_capabilities();
	CHECK(capabilities && capabilities == psiv_capabilities() && strncmp(capabilities, "{\"capabilities_schema\": 1, \"versions\": {\"library\": \"", 52) == 0);
	CHECK(strstr(capabilities, "\"abi\": 23,") && strstr(capabilities, "\"save_schema\": 16,") && strstr(capabilities, "\"picture show <picture>\""));
	CHECK(psiv_abi_version() == PSIV_ABI_VERSION);
	CHECK(strstr(psiv_version(), "psiv-bindings") != NULL);
	CHECK(strstr(psiv_version(), "game-0.2") != NULL);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_MODE, PSIV_MODE_FIELD), "field") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_DIRECTION, PSIV_DIRECTION_NONE), "none") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_OUTCOME, PSIV_OUTCOME_VICTORY), "victory") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_CANCEL_REASON, PSIV_CANCEL_NOT_ENOUGH_TP), "not_enough_tp") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_ACTION_TYPE, PSIV_ACTION_WARP), "warp") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_ACTION_TYPE, PSIV_ACTION_PLACE), "place") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_ACTOR_VANISH, PSIV_VANISH_BLINKED_OUT), "blinked_out") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_ACTOR_REMOVAL, PSIV_REMOVAL_UNTIL_RELOAD), "until_reload") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_ANIMATION_END, PSIV_ANIMATION_END_HIDDEN), "hidden") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_ACTOR_VANISH, 4), "") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_EVENT_TYPE, 999), "") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_EVENT_TYPE, PSIV_EVENT_BATTLE_ENDED), "battle_ended") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_REWARD_MULTIPLIER, PSIV_REWARD_X1_5), "x1_5") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_MENU_KIND, PSIV_MENU_SAVE_PROMPT), "save_prompt") == 0);
	int32_t value = -1;
	CHECK(psiv_parse_name(PSIV_NAME_REWARD_MULTIPLIER, "1.5", &value) == PSIV_OK && value == PSIV_REWARD_X1_5);
	CHECK(psiv_parse_name(PSIV_NAME_REWARD_MULTIPLIER, "x2", &value) == PSIV_OK && value == PSIV_REWARD_X2);
	CHECK(psiv_parse_name(PSIV_NAME_EVENT_TYPE, "BattleEnded", &value) == PSIV_OK && value == PSIV_EVENT_BATTLE_ENDED);
	CHECK(psiv_parse_name(PSIV_NAME_DIRECTION, "none", &value) == PSIV_OK && value == PSIV_DIRECTION_NONE);
	CHECK(psiv_parse_name(PSIV_NAME_OUTCOME, "scripted_end", &value) == PSIV_OK && value == PSIV_OUTCOME_SCRIPTED_END);
	CHECK(psiv_parse_name(PSIV_NAME_OUTCOME, "won", &value) == PSIV_NOT_FOUND);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_MACRO_TARGET_KIND, PSIV_MACRO_TARGET_ENEMY), "enemy") == 0);
	CHECK(psiv_parse_name(PSIV_NAME_MACRO_TARGET_KIND, "all", &value) == PSIV_OK && value == PSIV_MACRO_TARGET_ALL);
	CHECK(psiv_parse_name(PSIV_NAME_ACTOR_REMOVAL, "for_good", &value) == PSIV_OK && value == PSIV_REMOVAL_FOR_GOOD);
	CHECK(psiv_sizeof("psiv_route_cell") == sizeof(psiv_route_cell) && psiv_sizeof("psiv_animation") == sizeof(psiv_animation));
	CHECK(strcmp(psiv_name_of(999, 0), "") == 0);
}

static void testCreateErrors(void) {
	psiv_session* s = (psiv_session*)1;
	psiv_session_options options;
	psiv_error e;

	CHECK(psiv_session_create(NULL, &s) == PSIV_INVALID_ARGUMENT);
	CHECK(s == NULL);
	CHECK(psiv_last_error(NULL, &e) == PSIV_OK);
	CHECK(strcmp(e.key, "binding.null_argument") == 0);
	CHECK(e.param_count == 1 && strcmp(e.params[0], "options") == 0);

	psiv_session_options_init(&options);
	CHECK(psiv_session_create(&options, &s) == PSIV_INVALID_ARGUMENT);

	options.data_dir = "no/such/directory";
	CHECK(psiv_session_create(&options, &s) == PSIV_DATA_ERROR);
	CHECK(s == NULL);
	psiv_last_error(NULL, &e);
	CHECK(e.status == PSIV_DATA_ERROR);
	CHECK(strcmp(e.key, "binding.data_error") == 0);
	CHECK(strlen(e.text) > 0);
	const psiv_problem* problems;
	int32_t problemCount;
	CHECK(psiv_data_problems(&problems, &problemCount) == PSIV_OK);
	CHECK(problemCount == 1 && strcmp(problems[0].key, "data.cannot_read") == 0 && strcmp(problems[0].params[0], "no/such/directory/game.json") == 0);

	options.data_dir = dataDir;
	options.core_data_dir = "no/such/core";
	CHECK(psiv_session_create(&options, &s) == PSIV_DATA_ERROR);
	CHECK(psiv_data_problems(&problems, &problemCount) == PSIV_OK);
	CHECK(problemCount == 1 && strcmp(problems[0].key, "load.cannot_open") == 0 && strcmp(problems[0].file, "no/such/core/rules.json") == 0);
	CHECK(strcmp(problems[0].text, "no/such/core/rules.json: cannot open file") == 0);
	options.core_data_dir = "../core/data";
	CHECK(psiv_session_create(&options, &s) == PSIV_OK);
	psiv_session_destroy(s);
	CHECK(psiv_data_problems(&problems, &problemCount) == PSIV_OK && problemCount == 0);
	options.core_data_dir = NULL;

	options.data_dir = dataDir;
	options.struct_size = 4;
	CHECK(psiv_session_create(&options, &s) == PSIV_INVALID_ARGUMENT);
	psiv_last_error(NULL, &e);
	CHECK(strcmp(e.key, "binding.struct_size") == 0);

	psiv_session_options_init(&options);
	options.data_dir = dataDir;
	options.presentation = 9;
	CHECK(psiv_session_create(&options, &s) == PSIV_INVALID_ARGUMENT);

	/* Calls on a NULL session fail cleanly instead of crashing. */
	CHECK(psiv_new_game(NULL) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_mode_get(NULL) == -1);
	CHECK(psiv_line_pending(NULL) == 0);
	CHECK(psiv_flag(NULL, "x") == 0);
	CHECK(strcmp(psiv_name_entry_member(NULL), "") == 0);
	psiv_session_destroy(NULL);
}

static void testRefusals(void) {
	psiv_session* s = create(NULL, -1, PSIV_PRESENTATION_INSTANT);
	psiv_error e;

	CHECK(psiv_last_error(s, &e) == PSIV_NOT_FOUND);
	CHECK(psiv_mode_get(s) == PSIV_MODE_TITLE);
	CHECK(psiv_walk(s, PSIV_DIRECTION_EAST) == PSIV_REFUSED);
	CHECK(psiv_last_error(s, &e) == PSIV_OK);
	CHECK(e.status == PSIV_REFUSED);
	CHECK(strcmp(e.key, "game.refuse.walk_field_only") == 0);
	CHECK(strlen(e.text) > 0 && strcmp(e.text, e.key) != 0);

	CHECK(psiv_interact(s) == PSIV_REFUSED);
	CHECK(psiv_choose(s, 0) == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.no_choice") == 0);
	CHECK(psiv_battle_set_command(s, 0, PSIV_COMMAND_ATTACK, "", -1) == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.no_battle") == 0);
	CHECK(psiv_save_manual(s, "manual_0") == PSIV_REFUSED);
	CHECK(psiv_load(s, "manual_0") == PSIV_REFUSED);
	CHECK(psiv_load(s, NULL) == PSIV_INVALID_ARGUMENT);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "binding.null_argument") == 0 && strcmp(e.params[0], "slot") == 0);

	const psiv_save_summary* saves;
	int32_t count;
	CHECK(psiv_saves(s, &saves, &count) == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.saving_disabled") == 0);

	psiv_field_info field;
	CHECK(psiv_field_info_get(s, &field) == PSIV_NOT_FOUND);
	CHECK(psiv_update(s, -5, PSIV_DIRECTION_NONE) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_update(s, 16, 7) == PSIV_INVALID_ARGUMENT);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "binding.out_of_range") == 0 && strcmp(e.params[0], "direction") == 0 && strcmp(e.params[1], "7") == 0);

	CHECK_OK(psiv_new_game(s));
	CHECK(psiv_new_game(s) == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.new_game_title") == 0);
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	CHECK(psiv_buy(s, "tonic", 1) == PSIV_REFUSED);
	CHECK(psiv_unequip(s, "fighter", 42) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_settings_set_auto_macro(s, "no_such_macro") == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.no_macro") == 0 && e.param_count == 1 && strcmp(e.params[0], "no_such_macro") == 0);
	CHECK(psiv_settings_set(s, "no_such_difficulty", PSIV_REWARD_X1) == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.unknown_difficulty") == 0);
	CHECK(e.param_count == 1 && strcmp(e.params[0], "no_such_difficulty") == 0);
	CHECK(psiv_profile_set_value(s, "text_language", "xx") == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.unknown_language") == 0);
	CHECK(psiv_profile_set_value(s, "no_such_key", "1") == PSIV_REFUSED);
	CHECK(psiv_skip(s) == PSIV_REFUSED);
	psiv_session_destroy(s);
}

static void testStringOwnership(void) {
	psiv_session* s = create(NULL, -1, PSIV_PRESENTATION_INSTANT);
	psiv_field_info field;
	psiv_error e;
	char areaCopy[64];
	char nameCopy[64];

	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_field_info_get(s, &field));
	/* The default arrive_notice announces every entry; ticks timing reports the milliseconds as microseconds. */
	CHECK(field.announce_arrival == 1);
	CHECK(field.leader_step_us == (int64_t)field.leader_step_ms * 1000);
	CHECK(field.run_step_us == (int64_t)field.run_step_ms * 1000);
	CHECK(field.step_progress_us == (int64_t)field.step_progress_ms * 1000);
	snprintf(areaCopy, sizeof areaCopy, "%s", field.area_id);
	snprintf(nameCopy, sizeof nameCopy, "%s", field.area_name);
	const char* area = field.area_id;

	/* Reads never invalidate earlier results. */
	const psiv_member* party;
	const psiv_point* points;
	const char* rendered;
	int32_t count;
	for (int i = 0; i < 50; ++i) {
		CHECK_OK(psiv_party(s, &party, &count));
		CHECK_OK(psiv_field_points(s, &points, &count));
		CHECK_OK(psiv_display_name(s, "fighter", &rendered));
	}
	CHECK(strcmp(area, areaCopy) == 0);
	CHECK(strcmp(field.area_name, nameCopy) == 0);
	CHECK(strcmp(party[0].id, "fighter") == 0);

	/* A pointer the library returned can go back in, even to a MUTATES call. */
	const char* fighterId = party[0].id;
	CHECK_OK(psiv_open_menu(s));
	CHECK(psiv_equip(s, fighterId, "no_such_piece") == PSIV_REFUSED);
	CHECK(psiv_last_error(s, &e) == PSIV_OK);
	const char* errorKey = e.key;
	char errorCopy[128];
	snprintf(errorCopy, sizeof errorCopy, "%s", e.key);

	/* The error record survives later successful calls, MUTATES ones included. */
	CHECK_OK(psiv_close_menu(s));
	CHECK_OK(psiv_party(s, &party, &count));
	CHECK(strcmp(errorKey, errorCopy) == 0);

	/* Every string field is non-NULL, even when empty. */
	psiv_settings settings;
	CHECK_OK(psiv_settings_get(s, &settings));
	CHECK(settings.auto_macro != NULL && settings.auto_macro[0] == '\0');
	CHECK(strcmp(psiv_name_entry_member(s), "") == 0);

	/* Rendering and raw templates. */
	const char* params[] = {"fighter", "tonic"};
	const char* raw;
	CHECK_OK(psiv_render(s, "game.equipped", params, 2, &rendered));
	CHECK_OK(psiv_format_text(s, "game.equipped", params, 2, &raw));
	CHECK(strstr(rendered, "Test Fighter") != NULL);
	CHECK(strstr(raw, "fighter") != NULL);
	CHECK(psiv_render(s, "game.equipped", NULL, 2, &rendered) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_string_text(s, "speaker.dockhand", &rendered));
	CHECK(strcmp(rendered, "Dockhand") == 0);
	psiv_session_destroy(s);
}

static void testSaveAndLoad(void) {
	char saves[512];
	char path[600];
	snprintf(saves, sizeof saves, "%s/saves", scratch);
	makeDirectory(saves);
	removeFiles(saves);

	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	psiv_field_info field;
	psiv_world_info world;
	const psiv_save_summary* list;
	int32_t count;
	CHECK(psiv_continue_game(s) == PSIV_REFUSED);
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_world_info_get(s, &world));
	CHECK(world.autosave_count == 1);

	CHECK_OK(psiv_open_menu(s));
	CHECK(psiv_menu_kind_get(s) == PSIV_MENU_FIELD);
	CHECK(psiv_save_manual(s, "manual_99") == PSIV_REFUSED);
	CHECK(psiv_save_manual(s, "auto_0") == PSIV_REFUSED);
	CHECK_OK(psiv_save_manual(s, "manual_3"));
	CHECK_OK(psiv_close_menu(s));
	CHECK_OK(psiv_field_info_get(s, &field));
	const int savedX = field.leader_x;
	const int savedMoney = world.money;

	CHECK_OK(psiv_walk(s, PSIV_DIRECTION_SOUTH));
	CHECK_OK(psiv_walk(s, PSIV_DIRECTION_SOUTH));
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(field.leader_y != 4);

	CHECK_OK(psiv_saves(s, &list, &count));
	int found = 0;
	psiv_save_member member = {0};
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(list[i].slot, "manual_3") == 0) {
			found = 1;
			CHECK(!list[i].autosave && !list[i].damaged);
			CHECK(strcmp(list[i].area, "tidewater") == 0);
			CHECK(list[i].party_count == 1 && strcmp(list[i].party[0], "fighter:3") == 0);
			CHECK(list[i].money == savedMoney && list[i].member_count == 1);
			if (list[i].member_count == 1) {
				const psiv_save_member* m = &list[i].members[0];
				CHECK(strcmp(m->id, "fighter") == 0 && m->level == 3 && m->status_count == 0);
				member = *m;
			}
		}
	}
	CHECK(found);
	const psiv_member* party;
	int32_t partyCount;
	CHECK_OK(psiv_party(s, &party, &partyCount));
	CHECK(partyCount > 0 && member.hp == party[0].hp && member.max_hp == party[0].max_hp && member.tp == party[0].tp && member.max_tp == party[0].max_tp && member.max_hp > 0);

	CHECK_OK(psiv_quit_to_title(s));
	CHECK(psiv_mode_get(s) == PSIV_MODE_TITLE);
	CHECK_OK(psiv_load(s, "manual_3"));
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(field.leader_x == savedX && field.leader_y == 4);
	CHECK_OK(psiv_world_info_get(s, &world));
	CHECK(world.money == savedMoney);
	psiv_session_destroy(s);

	/* A second session on the same directory continues from the newest save. */
	s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	CHECK_OK(psiv_continue_game(s));
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	CHECK(psiv_flag(s, "tidewater.arrived"));
	CHECK(!psiv_flag(s, "no.such.flag"));
	CHECK_OK(psiv_quit_to_title(s));

	/* A damaged file is listed as damaged and refused. */
	snprintf(path, sizeof path, "%s/manual_3.sav", saves);
	FILE* f = fopen(path, "r+b");
	CHECK(f != NULL);
	if (f) {
		fseek(f, 40, SEEK_SET);
		fputc('#', f);
		fclose(f);
	}
	CHECK_OK(psiv_saves(s, &list, &count));
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(list[i].slot, "manual_3") == 0) {
			CHECK(list[i].damaged);
			CHECK(strlen(list[i].error) > 0);
		}
	}
	CHECK(psiv_load(s, list[0].slot) == PSIV_REFUSED);

	/* Erasing takes the damaged file away; an erased, unknown or NULL slot is refused as psiv_load refuses it. */
	CHECK_OK(psiv_erase_save(s, "manual_3"));
	CHECK_OK(psiv_saves(s, &list, &count));
	for (int32_t i = 0; i < count; ++i) {
		CHECK(strcmp(list[i].slot, "manual_3") != 0);
	}
	CHECK(psiv_erase_save(s, "manual_3") == PSIV_REFUSED);
	psiv_error e;
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.slot_unreadable") == 0 && e.param_count == 1 && strcmp(e.params[0], "manual_3") == 0);
	CHECK(psiv_erase_save(s, "manual_99") == PSIV_REFUSED);
	CHECK(psiv_erase_save(s, NULL) == PSIV_INVALID_ARGUMENT);
	psiv_session_destroy(s);
}

static void testBattleStream(void) {
	psiv_session* s = create(NULL, PSIV_ENCOUNTERS_RANDOM, PSIV_PRESENTATION_INSTANT);
	psiv_field_info field;
	CHECK_OK(psiv_new_game(s));
	settle(s);
	for (int i = 0; i < 10; ++i) {
		CHECK_OK(psiv_walk(s, PSIV_DIRECTION_EAST));
		settle(s);
	}
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.area_id, "reed_path") == 0);
	CHECK(field.encounter_mode == PSIV_ENCOUNTERS_RANDOM);

	/* Pace inside the west reeds until the random counter starts a battle. */
	int direction = PSIV_DIRECTION_EAST;
	for (int step = 0; step < 200 && psiv_mode_get(s) == PSIV_MODE_FIELD; ++step) {
		CHECK_OK(psiv_field_info_get(s, &field));
		if (field.leader_x >= 8) {
			direction = PSIV_DIRECTION_WEST;
		} else if (field.leader_x <= 2) {
			direction = PSIV_DIRECTION_EAST;
		}
		psiv_walk(s, direction);
		settle(s);
	}
	CHECK(psiv_mode_get(s) == PSIV_MODE_BATTLE);

	psiv_battle_info info;
	const psiv_combatant* combatants;
	int32_t combatantCount;
	CHECK_OK(psiv_battle_info_get(s, &info));
	CHECK(info.live && info.round == 0);
	CHECK(info.source == PSIV_BATTLE_RANDOM);
	CHECK(strcmp(info.encounter, "training") == 0);
	CHECK_OK(psiv_battle_combatants(s, &combatants, &combatantCount));
	CHECK(combatantCount == info.combatant_count);
	CHECK(info.party_count == 1);
	CHECK(combatants[0].side == PSIV_SIDE_PARTY && strcmp(combatants[0].id, "fighter") == 0);
	CHECK(combatants[1].side == PSIV_SIDE_ENEMY);

	int hp[16];
	for (int32_t i = 0; i < combatantCount && i < 16; ++i) {
		hp[i] = combatants[i].hp;
		CHECK(combatants[i].max_hp >= combatants[i].hp && combatants[i].hp > 0);
	}

	const psiv_battle_option* options;
	int32_t optionCount;
	CHECK_OK(psiv_battle_options(s, 0, &options, &optionCount));
	CHECK(optionCount > 0);
	CHECK(psiv_battle_options(s, combatantCount, &options, &optionCount) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_battle_set_command(s, 0, PSIV_COMMAND_ABILITY, "no_such_ability", -1) == PSIV_REFUSED);
	CHECK(psiv_battle_set_command(s, 0, PSIV_COMMAND_ATTACK, "", 99) == PSIV_INVALID_ARGUMENT);

	int sawStart = 0;
	int sawLocked = 0;
	int sawOrder = 0;
	int sawEnd = 0;
	int rounds = 0;
	int lastRound = 0;
	int32_t outcome = PSIV_OUTCOME_ONGOING;
	while (psiv_mode_get(s) == PSIV_MODE_BATTLE && rounds < 40) {
		CHECK_OK(psiv_battle_combatants(s, &combatants, &combatantCount));
		int target = -1;
		for (int32_t i = 0; i < combatantCount; ++i) {
			if (combatants[i].side == PSIV_SIDE_ENEMY && combatants[i].alive && target < 0) {
				target = i;
			}
		}
		CHECK_OK(psiv_battle_set_command(s, 0, PSIV_COMMAND_ATTACK, "", target));
		CHECK_OK(psiv_battle_combatants(s, &combatants, &combatantCount));
		CHECK(combatants[0].command_kind == PSIV_COMMAND_ATTACK && combatants[0].command_target == target);

		const psiv_battle_event* events;
		int32_t count;
		CHECK_OK(psiv_battle_resolve_round(s, &events, &count));
		++rounds;
		CHECK(count > 0);
		for (int32_t i = 0; i < count; ++i) {
			const psiv_battle_event* e = &events[i];
			switch (e->type) {
			case PSIV_EVENT_BATTLE_STARTED:
				sawStart = 1;
				CHECK(rounds == 1 && i == 0);
				CHECK(e->id_count == combatantCount && e->party_count == 1);
				CHECK(strcmp(e->ids[0], "fighter") == 0 && e->groups[0] != NULL);
				break;
			case PSIV_EVENT_ROUND_STARTED:
				CHECK(e->round == lastRound + 1);
				lastRound = e->round;
				break;
			case PSIV_EVENT_COMMANDS_LOCKED:
				sawLocked = 1;
				CHECK(e->command_count >= 1);
				CHECK(e->commands[0].actor == 0 && e->commands[0].kind == PSIV_COMMAND_ATTACK);
				break;
			case PSIV_EVENT_ORDER_RESOLVED:
				sawOrder = 1;
				CHECK(e->order_count >= 1);
				break;
			case PSIV_EVENT_DAMAGE_DEALT:
				CHECK(e->target >= 0 && e->target < combatantCount);
				CHECK(e->hp == (hp[e->target] - e->amount > 0 ? hp[e->target] - e->amount : 0));
				CHECK(e->rolled >= e->amount && (e->hp > 0 || e->rolled >= hp[e->target]));
				hp[e->target] = e->hp;
				break;
			case PSIV_EVENT_HEALED:
				hp[e->target] = e->hp;
				break;
			case PSIV_EVENT_STATUS_APPLIED:
			case PSIV_EVENT_STATUS_RESISTED:
			case PSIV_EVENT_STATUS_REMOVED:
			case PSIV_EVENT_MESSAGE:
				CHECK(strlen(e->text) > 0);
				break;
			case PSIV_EVENT_BATTLE_ENDED:
				sawEnd = 1;
				outcome = e->outcome;
				CHECK(i == count - 1);
				break;
			default:
				break;
			}
		}
	}
	CHECK(sawStart && sawLocked && sawOrder && sawEnd);
	CHECK(outcome == PSIV_OUTCOME_VICTORY);
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);

	/* The ended battle stays readable until the next one, with the last round applied. */
	CHECK_OK(psiv_battle_info_get(s, &info));
	CHECK(!info.live && info.outcome == PSIV_OUTCOME_VICTORY && info.round == rounds);
	CHECK_OK(psiv_battle_combatants(s, &combatants, &combatantCount));
	for (int32_t i = 0; i < combatantCount; ++i) {
		CHECK(combatants[i].hp == hp[i]);
		if (combatants[i].side == PSIV_SIDE_ENEMY) {
			CHECK(!combatants[i].alive);
		}
	}
	const psiv_battle_event* again;
	int32_t againCount;
	CHECK_OK(psiv_battle_last_events(s, &again, &againCount));
	CHECK(againCount > 0 && again[againCount - 1].type == PSIV_EVENT_BATTLE_ENDED);

	/* Rewards arrive as notices. */
	const psiv_notice* notices;
	int32_t noticeCount;
	int sawVictory = 0;
	CHECK_OK(psiv_notices_drain(s, &notices, &noticeCount));
	for (int32_t i = 0; i < noticeCount; ++i) {
		sawVictory = sawVictory || strcmp(notices[i].key, "game.victory") == 0;
	}
	CHECK(sawVictory);
	CHECK(psiv_battle_resolve_round(s, &again, &againCount) == PSIV_REFUSED);
	psiv_session_destroy(s);
}

static int callbackCount;
static uint64_t callbackHandle;

static void onAction(void* user, const psiv_action* action) {
	++*(int*)user;
	if (action->phase == PSIV_ACTION_BEGIN && action->type == PSIV_ACTION_FADE_IN) {
		callbackHandle = action->handle;
	}
}

static void testPresentation(void) {
	psiv_session* s = create(NULL, -1, PSIV_PRESENTATION_WAIT);
	const psiv_action* actions;
	int32_t count;
	CHECK_OK(psiv_actions_set_callback(s, onAction, &callbackCount));
	CHECK_OK(psiv_new_game(s));
	for (int i = 0; i < 20; ++i) {
		CHECK_OK(psiv_update(s, 100, PSIV_DIRECTION_NONE));
	}
	/* The arrival event fades in first and waits for the engine to finish the fade. */
	CHECK(psiv_mode_get(s) == PSIV_MODE_EVENT);
	CHECK(!psiv_line_pending(s));
	CHECK_OK(psiv_actions_poll(s, &actions, &count));
	CHECK(count == 1);
	CHECK(actions[0].phase == PSIV_ACTION_BEGIN && actions[0].type == PSIV_ACTION_FADE_IN);
	CHECK(actions[0].seconds == 1.0 && !actions[0].instant);
	CHECK(strcmp(actions[0].description, "fade in 1") == 0);
	CHECK(callbackCount == 1 && callbackHandle == actions[0].handle);
	const uint64_t handle = actions[0].handle;
	CHECK_OK(psiv_actions_poll(s, &actions, &count));
	CHECK(count == 0);
	CHECK_OK(psiv_action_finish(s, handle));
	CHECK_OK(psiv_update(s, 100, PSIV_DIRECTION_NONE));
	CHECK(psiv_line_pending(s));

	psiv_line line;
	CHECK_OK(psiv_line_get(s, &line));
	CHECK(strcmp(line.speaker_id, "") == 0);
	CHECK(strcmp(line.text, line.display) == 0);
	CHECK_OK(psiv_line_confirm(s));
	/* The dockhand walks over and turns before speaking; each step waits for the engine, while psiv_field_actors
	 * already reports where it ends. */
	const char* expected[] = {"move dockhand to dock_front", "move dockhand to greet_spot", "face dockhand fighter"};
	const int32_t cells[][3] = {{10, 3, PSIV_DIRECTION_SOUTH}, {5, 4, PSIV_DIRECTION_SOUTH}, {5, 4, PSIV_DIRECTION_WEST}};
	for (int step = 0; step < 3; ++step) {
		CHECK_OK(psiv_update(s, 100, PSIV_DIRECTION_NONE));
		CHECK(!psiv_line_pending(s));
		CHECK_OK(psiv_actions_poll(s, &actions, &count));
		CHECK(count == 1 && strcmp(actions[0].description, expected[step]) == 0);
		const psiv_actor* actors;
		int32_t actorCount;
		CHECK_OK(psiv_field_actors(s, &actors, &actorCount));
		CHECK(actorCount == 2 && strcmp(actors[0].id, "dockhand") == 0 && actors[0].scripted);
		CHECK(actorCount >= 1 && actors[0].x == cells[step][0] && actors[0].y == cells[step][1] && actors[0].facing == cells[step][2]);
		if (count == 1) {
			CHECK_OK(psiv_action_finish(s, actions[0].handle));
		}
	}
	CHECK_OK(psiv_update(s, 100, PSIV_DIRECTION_NONE));
	CHECK_OK(psiv_line_get(s, &line));
	CHECK(strcmp(line.speaker_id, "dockhand") == 0);
	CHECK(strcmp(line.speaker_name, "Dockhand") == 0);
	CHECK(strncmp(line.display, "Dockhand: ", 10) == 0 && strcmp(line.display + 10, line.text) == 0);
	CHECK(psiv_line_confirm(s) == PSIV_OK);
	for (int guard = 0; guard < 20 && psiv_mode_get(s) == PSIV_MODE_EVENT; ++guard) {
		CHECK_OK(psiv_update(s, 100, PSIV_DIRECTION_NONE));
		CHECK_OK(psiv_actions_poll(s, &actions, &count));
		for (int32_t i = 0; i < count; ++i) {
			CHECK_OK(psiv_action_finish(s, actions[i].handle));
		}
	}
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	psiv_session_destroy(s);

	/* Skipping a skippable scene logs the lines it passes over. */
	s = create(NULL, -1, PSIV_PRESENTATION_INSTANT);
	CHECK_OK(psiv_new_game(s));
	CHECK_OK(psiv_update(s, 100, PSIV_DIRECTION_NONE));
	CHECK(psiv_can_skip(s));
	CHECK_OK(psiv_skip(s));
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	const char* const* pages;
	int32_t pageCount;
	CHECK_OK(psiv_text_log(s, &pages, &pageCount));
	CHECK(pageCount >= 1);
	CHECK_OK(psiv_actions_poll(s, &actions, &count));
	CHECK(count >= 1 && actions[0].type == PSIV_ACTION_FADE_IN);
	psiv_session_destroy(s);
}

static void testFieldAndMenus(void) {
	psiv_session* s = create(NULL, -1, PSIV_PRESENTATION_INSTANT);
	psiv_field_info field;
	const uint8_t* grid;
	int32_t cells;
	const psiv_exit* exits;
	const psiv_point* points;
	const psiv_marker* markers;
	int32_t count;
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.area_id, "tidewater") == 0 && field.area_kind == PSIV_AREA_TOWN);
	CHECK(field.width == 14 && field.height == 9);
	CHECK(field.leader_x == 3 && field.leader_y == 4 && field.facing == PSIV_DIRECTION_EAST);
	CHECK_OK(psiv_field_grid(s, &grid, &cells));
	CHECK(cells == 14 * 9);
	CHECK((grid[0] & PSIV_CELL_TERRAIN_MASK) == PSIV_CELL_WALL && !(grid[0] & PSIV_CELL_WALKABLE));
	CHECK(grid[4 * 14 + 3] & PSIV_CELL_WALKABLE);
	/* The guide stands on a floor cell, which is therefore not walkable. */
	CHECK((grid[2 * 14 + 5] & PSIV_CELL_TERRAIN_MASK) == PSIV_CELL_FLOOR && !(grid[2 * 14 + 5] & PSIV_CELL_WALKABLE));
	CHECK_OK(psiv_field_exits(s, &exits, &count));
	CHECK(count == 1 && exits[0].x == 13 && strcmp(exits[0].to_area, "reed_path") == 0 && exits[0].fires == PSIV_EXIT_FIRES_FROM_FLOOR);
	CHECK_OK(psiv_field_markers(s, &markers, &count));
	CHECK(count == 16);
	CHECK_OK(psiv_field_points(s, &points, &count));
	int talk = 0;
	int shop = 0;
	int rest = 0;
	int chest = 0;
	for (int32_t i = 0; i < count; ++i) {
		talk += points[i].kind == PSIV_POINT_TALK;
		shop += points[i].kind == PSIV_POINT_SHOP;
		rest += points[i].kind == PSIV_POINT_REST;
		chest += points[i].kind == PSIV_POINT_CHEST;
	}
	CHECK(talk == 5 && shop == 1 && rest == 1 && chest == 1);

	/* Shop at the stall (8,2) from stall_front (8,3). */
	for (int i = 0; i < 5; ++i) {
		CHECK_OK(psiv_walk(s, PSIV_DIRECTION_EAST));
	}
	CHECK_OK(psiv_walk(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	CHECK(psiv_mode_get(s) == PSIV_MODE_SHOP);
	psiv_shop_info shopInfo;
	CHECK_OK(psiv_shop_get(s, &shopInfo));
	CHECK(shopInfo.stock_count > 0 && shopInfo.stock[0].price > 0);
	CHECK(psiv_buy(s, "rivet_plate", 99) == PSIV_REFUSED);
	CHECK_OK(psiv_buy(s, "tonic", 1));
	CHECK(psiv_sell(s, "tonic", 50) == PSIV_REFUSED);
	CHECK_OK(psiv_close_menu(s));
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);

	const psiv_inventory_entry* items;
	CHECK_OK(psiv_inventory(s, &items, &count));
	CHECK(count >= 1 && strcmp(items[0].id, "tonic") == 0 && items[0].count == 2 && !items[0].equipment);

	CHECK_OK(psiv_open_menu(s));
	CHECK(psiv_use_item(s, "tonic", "fighter") == PSIV_REFUSED);
	const psiv_ability_info* abilities;
	CHECK_OK(psiv_member_abilities(s, "fighter", &abilities, &count));
	CHECK(count >= 1);
	CHECK(psiv_member_abilities(s, "nobody", &abilities, &count) == PSIV_NOT_FOUND);
	const psiv_member* party;
	CHECK_OK(psiv_party(s, &party, &count));
	CHECK(count == 1 && party[0].in_lineup && party[0].level == 3 && party[0].max_hp >= party[0].hp);
	CHECK(party[0].equipment[PSIV_SLOT_WEAPON][0] != '\0');
	char weapon[64];
	snprintf(weapon, sizeof weapon, "%s", party[0].equipment[PSIV_SLOT_WEAPON]);
	CHECK_OK(psiv_unequip(s, "fighter", PSIV_SLOT_WEAPON));
	CHECK_OK(psiv_equip(s, "fighter", weapon));
	CHECK_OK(psiv_close_menu(s));

	psiv_quest_log log;
	CHECK_OK(psiv_quest_log_get(s, &log));
	CHECK(strlen(log.objective) > 0 && log.job_count == 1 && log.jobs[0].state == PSIV_JOB_AVAILABLE);
	const psiv_codex_entry* codex;
	CHECK_OK(psiv_encyclopedia(s, &codex, &count));
	CHECK(count == 1);
	const psiv_status talkStatus = psiv_party_talk(s);
	CHECK(talkStatus == PSIV_OK || talkStatus == PSIV_REFUSED);

	/* Profile changes show at once: the placeholder Japanese tables render differently. */
	CHECK_OK(psiv_quest_log_get(s, &log));
	char english[512];
	snprintf(english, sizeof english, "%s", log.objective);
	CHECK_OK(psiv_profile_set_value(s, "text_language", "ja"));
	psiv_profile profile;
	CHECK_OK(psiv_profile_get(s, &profile));
	CHECK(strcmp(profile.text_language, "ja") == 0);
	CHECK_OK(psiv_quest_log_get(s, &log));
	CHECK(strcmp(english, log.objective) != 0);
	profile.text_language = "en";
	CHECK_OK(psiv_profile_set(s, &profile));
	CHECK_OK(psiv_quest_log_get(s, &log));
	CHECK(strcmp(english, log.objective) == 0);

	psiv_game_info game;
	CHECK_OK(psiv_game_info_get(s, &game));
	CHECK(strcmp(game.title, "The Bell at Tidewater") == 0 && game.tick_ms == 50 && game.language_count == 2);
	psiv_session_destroy(s);
}

/* Walks to a marker of the current area with psiv_walk_to, running events between the steps. */
static int32_t walkToMarker(psiv_session* s, const char* name) {
	const psiv_marker* markers;
	int32_t count;
	int32_t status = -1;
	if (psiv_field_markers(s, &markers, &count) != PSIV_OK) {
		return status;
	}
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(markers[i].name, name) == 0 && psiv_walk_to(s, markers[i].x, markers[i].y) == PSIV_OK) {
			while (psiv_walk_step(s, &status) == PSIV_OK && status == PSIV_WALK_WALKING) {
				settle(s);
			}
			return status;
		}
	}
	return status;
}

/* The paths the placeholder adventure added for hosts: line data with portraits, localized yes and no, a
 * name entry, a lost event battle kept readable and retried, its record replayed, and the pacing numbers. */
static void testAdventurePaths(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/paths", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	psiv_line line;
	psiv_choice choice;
	psiv_error e;
	int32_t status = -1;
	CHECK_OK(psiv_new_game(s));
	settle(s);

	CHECK(walkToMarker(s, "hold_front") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_BATTLE);
	const psiv_combatant* combatants;
	int32_t count;
	CHECK_OK(psiv_battle_combatants(s, &combatants, &count));
	CHECK(count == 4 && strcmp(combatants[0].name, "Test Fighter") == 0 && strcmp(combatants[1].label, "Gel Mite A") == 0);
	CHECK(strcmp(combatants[3].id, "rust_hound") == 0 && combatants[3].level == 2 && combatants[0].level == 3);
	psiv_auto_battle played;
	CHECK_OK(psiv_battle_play(s, 0, &played));
	CHECK(played.ended && played.round_count >= 1 && strcmp(played.refusal_key, "") == 0);
	CHECK(played.rounds[0].event_count > 0 && played.rounds[0].events[0].type == PSIV_EVENT_BATTLE_STARTED);
	/* The fighter alone falls to the enemy-first ambush: a game over, with the battle still readable. */
	CHECK(psiv_mode_get(s) == PSIV_MODE_TITLE);
	psiv_battle_info info;
	CHECK_OK(psiv_battle_info_get(s, &info));
	CHECK(!info.live && info.outcome == PSIV_OUTCOME_DEFEAT && info.game_over && info.source == PSIV_BATTLE_EVENT);
	CHECK_OK(psiv_battle_combatants(s, &combatants, &count));
	CHECK(count == 4 && !combatants[0].alive);
	psiv_battle_result result;
	CHECK_OK(psiv_battle_result_get(s, &result));
	CHECK(strcmp(result.encounter, "training") == 0 && result.outcome == PSIV_OUTCOME_DEFEAT && result.game_over);
	CHECK(result.experience == 0 && result.drop_count == 0 && result.level_up_count == 0);

	const char* record;
	CHECK_OK(psiv_battle_record(s, &record));
	char* recordCopy = malloc(strlen(record) + 1);
	strcpy(recordCopy, record);
	const psiv_battle_event* replayed;
	int32_t replayedCount;
	const char* eventText;
	CHECK_OK(psiv_battle_replay(s, recordCopy, &replayed, &replayedCount, &eventText));
	CHECK(replayedCount > 0 && replayed[replayedCount - 1].type == PSIV_EVENT_BATTLE_ENDED && replayed[replayedCount - 1].outcome == PSIV_OUTCOME_DEFEAT);
	CHECK(strncmp(eventText, "0 battle_started", 16) == 0);
	recordCopy[strlen(recordCopy) / 2] ^= 1;
	CHECK(psiv_battle_replay(s, recordCopy, &replayed, &replayedCount, NULL) == PSIV_DATA_ERROR);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "binding.bad_record") == 0);
	free(recordCopy);

	psiv_world_info world;
	CHECK_OK(psiv_world_info_get(s, &world));
	CHECK(world.game_over && world.can_retry);
	CHECK_OK(psiv_retry_battle(s));
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_BATTLE);
	CHECK(psiv_battle_result_get(s, &result) == PSIV_NOT_FOUND);
	CHECK_OK(psiv_battle_play(s, 0, &played));
	CHECK(played.ended);
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_TITLE || psiv_mode_get(s) == PSIV_MODE_FIELD);
	CHECK_OK(psiv_battle_dismiss(s));
	CHECK(psiv_battle_info_get(s, &info) == PSIV_NOT_FOUND);
	CHECK(psiv_walk_step(s, &status) == PSIV_NOT_FOUND);
	psiv_session_destroy(s);

	/* The guide: a portrait on the first line, the string table's yes and no, and a name entry. */
	removeFiles(saves);
	s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK(walkToMarker(s, "guide_front") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	CHECK_OK(psiv_line_get(s, &line));
	CHECK(strcmp(line.speaker_id, "guide") == 0 && strcmp(line.speaker_name, "Harbor Guide") == 0);
	CHECK(strcmp(line.text_id, "tidewater.guide.bell") == 0 && strcmp(line.portrait, "guide_smile") == 0);
	CHECK_OK(psiv_line_confirm(s));
	CHECK_OK(psiv_choice_get(s, &choice));
	CHECK(choice.yes_no && choice.option_count == 2 && strcmp(choice.options[0], "Yes") == 0 && strcmp(choice.options[1], "No") == 0);
	CHECK(choice.header[0] == '\0' && choice.header_id[0] == '\0');
	CHECK(!choice.keep && choice.states == NULL && choice.picked == -1);
	psiv_choice kept;
	CHECK(psiv_kept_choice_get(s, &kept) == PSIV_NOT_FOUND);
	CHECK(psiv_kept_choice_get(s, NULL) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_profile_set_value(s, "text_language", "ja"));
	CHECK_OK(psiv_choice_get(s, &choice));
	CHECK(strcmp(choice.options[0], "\xE3\x81\xAF\xE3\x81\x84") == 0);
	CHECK_OK(psiv_profile_set_value(s, "text_language", "en"));
	CHECK_OK(psiv_choose(s, 0));
	CHECK_OK(psiv_line_confirm(s));
	CHECK(psiv_menu_kind_get(s) == PSIV_MENU_NAME_ENTRY);
	CHECK(strcmp(psiv_name_entry_member(s), "fighter") == 0);
	CHECK(psiv_enter_name(s, "") == PSIV_REFUSED);
	CHECK_OK(psiv_enter_name(s, "Bryn"));
	CHECK_OK(psiv_line_get(s, &line));
	CHECK(strcmp(line.speaker_id, "fighter") == 0 && strcmp(line.speaker_name, "Bryn") == 0 && strcmp(line.portrait, "") == 0);
	CHECK(strcmp(line.display, "Bryn: Tell the ferry I am staying a while.") == 0);
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	CHECK(walkToMarker(s, "no_such_marker") == -1);
	CHECK_OK(psiv_walk_to(s, 0, 0));
	CHECK_OK(psiv_walk_step(s, &status));
	CHECK(status == PSIV_WALK_NO_PATH);

	const psiv_pacing_area* areas;
	const psiv_pacing_boss* bosses;
	CHECK_OK(psiv_pacing_areas(s, &areas, &count));
	CHECK(count == 1 && strcmp(areas[0].area, "tidewater") == 0 && areas[0].visits == 1 && areas[0].steps > 0 && areas[0].battles == 0);
	CHECK_OK(psiv_pacing_bosses(s, &bosses, &count));
	CHECK(count == 0);
	psiv_session_destroy(s);
}

static const psiv_actor* findActor(const psiv_actor* actors, int32_t count, const char* id) {
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(actors[i].id, id) == 0) {
			return &actors[i];
		}
	}
	return NULL;
}

/* Script actors as state: untouched ones at their talk points, the arrival's walk, saves and loads. */
static void testActors(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/actors", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	const psiv_actor* actors;
	int32_t count;
	CHECK(psiv_field_actors(s, &actors, &count) == PSIV_NOT_FOUND);
	/* Every actor id, from game.json, already at the title. */
	const char* const* ids;
	int32_t idCount;
	const char* allActors[] = {"ashwife", "bridgewright", "cog_sweeper", "dockhand", "ferrywoman", "guide", "kilnwright", "lamplighter", "lodgekeeper", "peddler", "wayfarer"};
	CHECK(psiv_actor_ids(s, NULL, &idCount) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_actor_ids(s, &ids, NULL) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_actor_ids(NULL, &ids, &idCount) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_actor_ids(s, &ids, &idCount));
	CHECK(idCount == 11);
	for (int32_t i = 0; i < idCount && i < 11; ++i) {
		CHECK(strcmp(ids[i], allActors[i]) == 0);
	}
	CHECK_OK(psiv_new_game(s));
	CHECK(psiv_field_actors(s, NULL, &count) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_field_actors(s, &actors, NULL) == PSIV_INVALID_ARGUMENT);

	/* At the first line nothing has moved: the dockhand and the guide stand at their talk points. */
	CHECK(psiv_line_pending(s));
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	CHECK(count == 2);
	const psiv_actor* dockhand = findActor(actors, count, "dockhand");
	const psiv_actor* guide = findActor(actors, count, "guide");
	CHECK(dockhand && dockhand->x == 10 && dockhand->y == 2 && dockhand->facing == PSIV_DIRECTION_SOUTH && dockhand->visible && !dockhand->scripted);
	CHECK(dockhand && strcmp(dockhand->name, "Dockhand") == 0);
	CHECK(guide && guide->x == 5 && guide->y == 2 && !guide->scripted);

	CHECK_OK(psiv_line_confirm(s));
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	dockhand = findActor(actors, count, "dockhand");
	CHECK(dockhand && dockhand->x == 5 && dockhand->y == 4 && dockhand->facing == PSIV_DIRECTION_WEST && dockhand->scripted);
	settle(s);
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	dockhand = findActor(actors, count, "dockhand");
	CHECK(dockhand && dockhand->x == 10 && dockhand->y == 2 && dockhand->facing == PSIV_DIRECTION_SOUTH && dockhand->scripted);

	CHECK_OK(psiv_open_menu(s));
	CHECK_OK(psiv_save_manual(s, "manual_0"));
	CHECK_OK(psiv_close_menu(s));

	/* Talking to the guide turns it toward the leader below it. */
	CHECK(walkToMarker(s, "guide_front") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	CHECK(psiv_line_pending(s));
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	guide = findActor(actors, count, "guide");
	CHECK(guide && guide->x == 5 && guide->y == 2 && guide->facing == PSIV_DIRECTION_SOUTH && guide->scripted);

	/* The save has the dockhand's place and not the guide's turn. */
	CHECK_OK(psiv_quit_to_title(s));
	CHECK_OK(psiv_load(s, "manual_0"));
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	dockhand = findActor(actors, count, "dockhand");
	guide = findActor(actors, count, "guide");
	CHECK(dockhand && dockhand->scripted && dockhand->x == 10 && dockhand->y == 2);
	CHECK(guide && !guide->scripted);

	/* No actor stands on the reed path until the peddler's talk point is reached. */
	CHECK(walkToMarker(s, "east_gate") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_walk(s, PSIV_DIRECTION_EAST));
	settle(s);
	psiv_field_info field;
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.area_id, "reed_path") == 0);
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	CHECK(count == 1 && strcmp(actors[0].id, "peddler") == 0 && actors[0].x == 10 && actors[0].y == 7 && !actors[0].scripted);
	/* The dockhand and the guide are actors in another area: in the ids, not among the field's actors. */
	CHECK_OK(psiv_actor_ids(s, &ids, &idCount));
	CHECK(idCount == 11 && strcmp(ids[3], "dockhand") == 0 && strcmp(ids[5], "guide") == 0);
	CHECK(!findActor(actors, count, "dockhand") && !findActor(actors, count, "guide"));
	psiv_session_destroy(s);
}

static int32_t errorIs(psiv_session* s, const char* key);

/* Wren Hollow: the keeper behind its counter, the sentinel in an enemy's figure, the drifter's routes, animations with
 * their milliseconds, removals, and the animation table. */
static void testScriptedActors(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/scripted_actors", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	const char* shipped = dataDir;
	dataDir = actorDataDir;
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	dataDir = shipped;
	const psiv_animation* animations;
	int32_t count;
	CHECK(psiv_animations(s, NULL, &count) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_animations(s, &animations, &count));
	int found = 0;
	for (int32_t i = 0; i < count; ++i) {
		const psiv_animation* a = &animations[i];
		if (strcmp(a->id, "dissolve") == 0) {
			found += a->seconds == 0.8 && a->standard && a->end == PSIV_ANIMATION_END_HIDDEN;
		} else if (strcmp(a->id, "tally_nod") == 0) {
			found += a->seconds == 0 && !a->standard && a->end == PSIV_ANIMATION_END_NONE;
		} else if (strcmp(a->id, "fade_in") == 0) {
			found += a->seconds == 0.5 && a->standard && a->end == PSIV_ANIMATION_END_SHOWN;
		} else if (strcmp(a->id, "bow") == 0) {
			found += a->seconds == 0 && !a->standard;
		}
	}
	CHECK(found == 4);
	CHECK(count == 13);

	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_dev_warp(s, "wren_hollow", "hollow_door"));
	settle(s);
	const psiv_actor* actors;
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	CHECK(count == 2);
	const psiv_actor* sentinel = findActor(actors, count, "hollow_sentinel");
	const psiv_actor* keeper = findActor(actors, count, "tallykeeper");
	CHECK(sentinel && sentinel->x == 7 && sentinel->y == 4 && sentinel->facing == PSIV_DIRECTION_WEST && sentinel->solid && sentinel->visible);
	CHECK(sentinel && strcmp(sentinel->sprite_enemy, "aegis_sentinel") == 0 && strcmp(sentinel->look, "") == 0);
	CHECK(keeper && keeper->x == 4 && keeper->y == 1 && keeper->solid && !keeper->scripted && strcmp(keeper->animation, "") == 0);
	CHECK(keeper && keeper->vanish == PSIV_VANISH_NONE && keeper->removal == PSIV_REMOVAL_NONE && strcmp(keeper->sprite_enemy, "") == 0);

	/* The sentinel blocks the way and starts no battle. */
	CHECK(psiv_walk_to(s, 6, 4) == PSIV_OK);
	int32_t status = PSIV_WALK_WALKING;
	while (psiv_walk_step(s, &status) == PSIV_OK && status == PSIV_WALK_WALKING) {
	}
	CHECK(status == PSIV_WALK_ARRIVED);
	CHECK(psiv_walk(s, PSIV_DIRECTION_EAST) == PSIV_REFUSED && errorIs(s, "game.refuse.blocked"));
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);

	const psiv_action* records;
	CHECK_OK(psiv_actions_poll(s, &records, &count));
	CHECK(walkToMarker(s, "counter_front") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	CHECK(psiv_line_pending(s));
	CHECK_OK(psiv_line_confirm(s));
	CHECK(psiv_line_pending(s));
	CHECK_OK(psiv_line_confirm(s));
	CHECK(psiv_line_pending(s));
	CHECK_OK(psiv_actions_poll(s, &records, &count));
	int routes = 0;
	for (int32_t i = 0; i < count; ++i) {
		const psiv_action* a = &records[i];
		if (a->type == PSIV_ACTION_PLACE) {
			routes += strcmp(a->facing, "east") == 0 && a->route_count == 1 && a->route[0].x == 1 && a->route[0].y == 4;
		} else if (a->type == PSIV_ACTION_MOVE && a->pathfind) {
			routes += a->route_count == 10 && a->route[0].x == 1 && a->route[9].x == 8 && a->route[9].y == 4 && a->waypoint_count == 0;
		} else if (a->type == PSIV_ACTION_MOVE) {
			routes += a->waypoint_count == 1 && strcmp(a->waypoints[0], "shelf_front") == 0 && a->route_count == 10 && a->speed == 2;
		} else if (a->type == PSIV_ACTION_ANIMATE && strcmp(a->target, "dissolve") == 0) {
			routes += a->duration == 0.8 && a->route_count == 0;
		}
	}
	CHECK(routes == 4);
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	const psiv_actor* drifter = findActor(actors, count, "hollow_drifter");
	CHECK(drifter && !drifter->visible && drifter->vanish == PSIV_VANISH_DISSOLVED && strcmp(drifter->animation, "dissolve") == 0 &&
		drifter->animation_ms == 800 && drifter->animation_total_ms == 800);
	CHECK_OK(psiv_line_confirm(s));
	settle(s);
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	drifter = findActor(actors, count, "hollow_drifter");
	keeper = findActor(actors, count, "tallykeeper");
	CHECK(drifter && drifter->removal == PSIV_REMOVAL_UNTIL_RELOAD && !drifter->visible);
	CHECK(keeper && strcmp(keeper->animation, "shake") == 0 && keeper->animation_ms == 400 && keeper->animation_total_ms == 400);
	CHECK_OK(psiv_update(s, 200, PSIV_DIRECTION_NONE));
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	keeper = findActor(actors, count, "tallykeeper");
	CHECK(keeper && keeper->animation_ms == 200);

	/* The next area load ends the drifter's removal. */
	CHECK_OK(psiv_dev_warp(s, "wren_hollow", "hollow_door"));
	settle(s);
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	drifter = findActor(actors, count, "hollow_drifter");
	CHECK(drifter && drifter->removal == PSIV_REMOVAL_NONE && drifter->x == 1 && drifter->y == 4);
	psiv_session_destroy(s);
}

/* Wren Cellar under `interact: "nearby"`, `run_speed_percent` 200 and `vehicle_opening` normal: the prompt query, the
 * leader turning to what answers, the table's two cells, the step query, the run input and the door drawn as floor
 * while it is closed. */
static void testInteraction(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/interaction", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	const char* shipped = dataDir;
	dataDir = interactDataDir;
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	dataDir = shipped;
	CHECK(psiv_sizeof("psiv_interact_target") == sizeof(psiv_interact_target) && psiv_sizeof("psiv_field_input") == sizeof(psiv_field_input));
	CHECK(strcmp(psiv_name_of(PSIV_NAME_INTERACT_KIND, PSIV_INTERACT_ACTOR), "actor") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_INTERACT_KIND, PSIV_INTERACT_REST + 1), "") == 0);
	psiv_interact_target t;
	CHECK(psiv_interact_target_get(s, NULL) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_interact_target_get(s, &t) == PSIV_NOT_FOUND);
	CHECK(psiv_can_step(s, PSIV_DIRECTION_NORTH) == 0 && psiv_can_step(NULL, PSIV_DIRECTION_NORTH) == 0);
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_dev_warp(s, "wren_cellar", "cellar_door"));
	settle(s);

	/* The door: an exit drawn as floor while it is closed, which blocks the way. */
	const psiv_exit* exits;
	int32_t count;
	CHECK_OK(psiv_field_exits(s, &exits, &count));
	CHECK(count == 1 && !exits[0].open && exits[0].closed_tile == '.');
	const char* const* rows;
	CHECK_OK(psiv_field_rows(s, &rows, &count));
	CHECK(count == 10 && rows[9][6] == '.');
	const uint8_t* grid;
	CHECK_OK(psiv_field_grid(s, &grid, &count));
	CHECK(grid[9 * 14 + 6] == PSIV_CELL_FLOOR && grid[8 * 14 + 6] == (PSIV_CELL_FLOOR | PSIV_CELL_WALKABLE));
	CHECK(!psiv_can_step(s, PSIV_DIRECTION_SOUTH) && psiv_can_step(s, PSIV_DIRECTION_NORTH) && !psiv_can_step(s, 4));
	CHECK(psiv_walk(s, PSIV_DIRECTION_SOUTH) == PSIV_REFUSED && errorIs(s, "game.refuse.blocked"));

	/* The notice beside the leader: the prompt names it, and interact turns the leader to it. */
	CHECK(walkToMarker(s, "notice_side") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_EAST));
	CHECK_OK(psiv_interact_target_get(s, &t));
	CHECK(t.kind == PSIV_INTERACT_TALK && strcmp(t.id, "cellar_notice") == 0 && t.x == 5 && t.y == 2 && t.facing == PSIV_DIRECTION_NORTH && t.ready && !t.across);
	CHECK_OK(psiv_interact(s));
	settle(s);
	psiv_field_info field;
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(field.facing == PSIV_DIRECTION_NORTH);
	CHECK_OK(psiv_field_exits(s, &exits, &count));
	CHECK(exits[0].open);
	CHECK_OK(psiv_field_grid(s, &grid, &count));
	CHECK(grid[9 * 14 + 6] == (PSIV_CELL_FLOOR | PSIV_CELL_WALKABLE));

	/* The clerk across its counter, the table's further cell, the cook where it stands. */
	CHECK(walkToMarker(s, "clerk_front") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact_target_get(s, &t));
	CHECK(t.kind == PSIV_INTERACT_TALK && strcmp(t.id, "cellar_clerk") == 0 && t.x == 2 && t.y == 1 && t.across);
	CHECK(walkToMarker(s, "table_north") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_SOUTH));
	CHECK_OK(psiv_interact_target_get(s, &t));
	CHECK(strcmp(t.id, "cellar_table") == 0 && t.x == 9 && t.y == 2);
	const psiv_point* points;
	CHECK_OK(psiv_field_points(s, &points, &count));
	int tableCells = 0;
	for (int32_t i = 0; i < count; ++i) {
		tableCells += strcmp(points[i].id, "cellar_table") == 0 && points[i].kind == PSIV_POINT_TALK && points[i].y == 2 && (points[i].x == 8 || points[i].x == 9);
	}
	CHECK(tableCells == 2);
	CHECK(walkToMarker(s, "cook_side") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_EAST));
	CHECK_OK(psiv_interact_target_get(s, &t));
	CHECK(t.kind == PSIV_INTERACT_ACTOR && strcmp(t.id, "cellar_cook") == 0 && t.x == 10 && t.y == 5);

	/* Running: the run input halves the step; always_run turns it around. */
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(field.leader_step_ms == 250 && field.run_step_ms == 125);
	psiv_field_input input = {PSIV_DIRECTION_WEST, 1};
	CHECK_OK(psiv_update_input(s, 150, &input));
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(field.leader_x == 8 && field.leader_y == 5);
	input.running = 0;
	CHECK_OK(psiv_update_input(s, 150, &input));
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(field.leader_x == 8);
	CHECK_OK(psiv_update_input(s, 100, &input));
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(field.leader_x == 7);
	input.direction = 7;
	CHECK(psiv_update_input(s, 50, &input) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_update_input(s, 50, NULL));
	psiv_profile profile;
	CHECK_OK(psiv_profile_get(s, &profile));
	CHECK(!profile.always_run);
	profile.always_run = 1;
	CHECK_OK(psiv_profile_set(s, &profile));
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(field.leader_step_ms == 125 && field.run_step_ms == 250);
	CHECK_OK(psiv_profile_set_value(s, "always_run", "off"));
	CHECK_OK(psiv_profile_get(s, &profile));
	CHECK(!profile.always_run);
	psiv_session_destroy(s);
}

/* Wren Yard: the lamplighter's background event beside the field, the wandering goat and its touch hook, the stall's keeper,
 * what a cell holds from where the leader stands, the step and belt fields, and the patch's visible enemy by its reserved
 * actor name. */
static void testBackground(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/background", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	const char* shipped = dataDir;
	dataDir = backgroundDataDir;
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	dataDir = shipped;
	const psiv_event_position* running;
	int32_t count = -1;
	CHECK(psiv_background_events(s, NULL, &count) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_background_events(s, &running, &count));
	CHECK(count == 0);
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_dev_warp(s, "wren_yard", "yard_gate"));
	settle(s);
	CHECK_OK(psiv_background_events(s, &running, &count));
	CHECK(count == 1 && strcmp(running[0].event, "yard_lamps") == 0 && strcmp(running[0].file, "events/yard.ev") == 0 && running[0].line > 0);
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);

	/* The visible enemy's reserved name, and the goat wandering in its figure. */
	const psiv_enemy* enemies;
	CHECK_OK(psiv_field_enemies(s, &enemies, &count));
	CHECK(count == 1 && strcmp(enemies[0].actor, "enemy.yard_patch.1") == 0);
	const psiv_actor* actors;
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	int goats = 0;
	for (int32_t i = 0; i < count; ++i) {
		goats += strcmp(actors[i].id, "yard_goat") == 0 && actors[i].wanders && strcmp(actors[i].look, "goat") == 0;
		CHECK(strcmp(actors[i].id, "yard_trader") != 0 || !actors[i].wanders);
	}
	CHECK(goats == 1);

	/* The stall and its keeper, from the cell in front of it, and what a cell holds from here. */
	CHECK(walkToMarker(s, "stall_front") == PSIV_WALK_ARRIVED);
	psiv_interact_target t;
	CHECK(psiv_interact_at(s, 9, 2, NULL) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_interact_at(s, 9, 2, &t));
	CHECK(t.kind == PSIV_INTERACT_SHOP && strcmp(t.id, "yard_stall") == 0 && t.facing == PSIV_DIRECTION_NORTH && strcmp(t.keeper, "yard_trader") == 0);
	CHECK(psiv_interact_at(s, 3, 0, &t) == PSIV_NOT_FOUND);
	const psiv_point* points;
	CHECK_OK(psiv_field_points(s, &points, &count));
	int kept = 0;
	for (int32_t i = 0; i < count; ++i) {
		kept += points[i].kind == PSIV_POINT_SHOP && strcmp(points[i].keeper, "yard_trader") == 0;
		CHECK(points[i].kind == PSIV_POINT_SHOP || points[i].keeper[0] == 0);
	}
	CHECK(kept == 1);

	/* The stall's display name and the followers. */
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	psiv_shop_info stall;
	CHECK_OK(psiv_shop_get(s, &stall));
	CHECK(strcmp(stall.name_key, "place.yard_stall") == 0 && strcmp(stall.name, "Yard Stall") == 0);
	CHECK_OK(psiv_close_menu(s));
	CHECK(psiv_event_next_output(s) == PSIV_NEXT_NONE && psiv_event_next_output(NULL) == PSIV_NEXT_NONE);
	const psiv_follower_cell* followers;
	CHECK(psiv_followers(s, NULL, &count) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_followers(s, &followers, &count));
	for (int32_t i = 0; i < count; ++i) {
		CHECK(followers[i].id[0] != 0);
	}

	/* Step progress and the directions open. */
	psiv_field_info field;
	CHECK_OK(psiv_field_info_get(s, &field));
	int32_t mask = 0;
	for (int32_t d = PSIV_DIRECTION_NORTH; d <= PSIV_DIRECTION_WEST; ++d) {
		mask |= psiv_can_step(s, d) ? 1 << d : 0;
	}
	CHECK(field.step_mask == mask && field.step_progress_ms == 0 && field.conveyor_step_ms == 0 && field.conveyor_progress_ms == 0);
	CHECK_OK(psiv_update(s, 100, PSIV_DIRECTION_SOUTH));
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(field.step_progress_ms == 100);

	/* A save keeps the background event; a load resumes it at the same line. */
	CHECK_OK(psiv_background_events(s, &running, &count));
	const int32_t line = count == 1 ? running[0].line : -1;
	const char* text = NULL;
	CHECK_OK(psiv_save_text(s, &text));
	CHECK(text && strstr(text, "chunk background 1 ") != NULL);
	char* copy = malloc(strlen(text) + 1);
	strcpy(copy, text);
	psiv_quit_to_title(s);
	CHECK_OK(psiv_background_events(s, &running, &count));
	CHECK(count == 0);
	CHECK_OK(psiv_load_text(s, copy));
	free(copy);
	CHECK_OK(psiv_background_events(s, &running, &count));
	CHECK(count == 1 && running[0].line == line);
	psiv_session_destroy(s);
}

static int32_t errorIs(psiv_session* s, const char* key) {
	psiv_error e;
	return psiv_last_error(s, &e) == PSIV_OK && strcmp(e.key, key) == 0;
}

/* Wren Den: its area's own way out, then the credits its plaque plays, moved on by play time and finished by closing
 * them, and the event's end game back to the title with the clear-data record written. */
static void testDenAndCredits(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/den", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	const char* shipped = dataDir;
	dataDir = backgroundDataDir;
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	dataDir = shipped;
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_dev_warp(s, "wren_den", "plaque_front"));
	settle(s);
	const psiv_travel_destination* places;
	int32_t count = 0;
	CHECK_OK(psiv_travel_destinations(s, PSIV_TRAVEL_DUNGEON_EXIT, &places, &count));
	CHECK(count == 1 && strcmp(places[0].id, "tidewater") == 0 && places[0].x == 3 && places[0].y == 6);

	psiv_credits_info info;
	const psiv_credits_entry* entries;
	CHECK(psiv_credits_info_get(s, &info) == PSIV_NOT_FOUND);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_EAST));
	CHECK_OK(psiv_interact(s));
	settle(s);
	CHECK(psiv_menu_kind_get(s) == PSIV_MENU_CREDITS);
	CHECK(psiv_credits_info_get(s, NULL) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_credits_info_get(s, &info));
	CHECK(strcmp(info.id, "wren_end") == 0 && strcmp(info.music, "title_harbor_dawn") == 0 && info.entry_count == 4 && info.shown == 1);
	CHECK(info.elapsed_ms == 0 && info.total_ms == 8000);
	CHECK_OK(psiv_credits_entries(s, &entries, &count));
	CHECK(count == 4 && entries[0].kind == PSIV_CREDITS_HEADING && strcmp(entries[0].text, "Wren Hollow") == 0 && entries[0].pause_ms == 2500);
	CHECK(entries[1].kind == PSIV_CREDITS_LINE && strcmp(entries[1].key, "credits.wren_end.cast") == 0 && entries[1].gap == 1);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_MENU_KIND, PSIV_MENU_CREDITS), "credits") == 0);
	CHECK_OK(psiv_update(s, 4500, PSIV_DIRECTION_NONE));
	CHECK_OK(psiv_credits_info_get(s, &info));
	CHECK(info.shown == 3 && info.elapsed_ms == 4500);
	CHECK(psiv_save_manual(s, "manual_0") == PSIV_REFUSED && errorIs(s, "game.refuse.save_where"));
	CHECK_OK(psiv_close_menu(s));
	CHECK(psiv_credits_entries(s, &entries, &count) == PSIV_NOT_FOUND);
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_TITLE);
	int32_t cleared = -1;
	CHECK_OK(psiv_clear_record_exists(s, &cleared));
	/* The developer warp marked the game, so nothing was recorded. */
	CHECK(cleared == 0);
	psiv_session_destroy(s);
}

static const psiv_macro* findMacro(const psiv_macro* macros, int32_t count, const char* name) {
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(macros[i].name, name) == 0) {
			return &macros[i];
		}
	}
	return NULL;
}

/* Party order, the macro editor, member detail and the combo log, once the guide's apprentices have joined. */
static void testMenus(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/menus", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	const psiv_member* party;
	const psiv_macro* macros = NULL;
	const psiv_notice* notices;
	int32_t count;
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK(walkToMarker(s, "guide_front") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	settle(s);
	CHECK_OK(psiv_enter_name(s, "Bryn"));
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);

	/* Party order: field menu only, saved and loaded. */
	CHECK(psiv_party_swap(s, 0, 2) == PSIV_REFUSED && errorIs(s, "game.refuse.party_menu_only"));
	CHECK_OK(psiv_open_menu(s));
	CHECK_OK(psiv_party_swap(s, 0, 2));
	CHECK(psiv_party_swap(s, 0, 3) == PSIV_REFUSED && errorIs(s, "game.refuse.party_no_position"));
	CHECK_OK(psiv_party(s, &party, &count));
	CHECK(count == 3 && strcmp(party[0].id, "medic") == 0 && strcmp(party[1].id, "mystic") == 0 && strcmp(party[2].id, "fighter") == 0);
	CHECK_OK(psiv_save_manual(s, "manual_2"));
	CHECK_OK(psiv_quit_to_title(s));
	CHECK_OK(psiv_load(s, "manual_2"));
	CHECK_OK(psiv_party(s, &party, &count));
	CHECK(count == 3 && strcmp(party[0].id, "medic") == 0 && strcmp(party[2].id, "fighter") == 0);

	/* The save's macros as data, with names and the marks the editor shows. */
	CHECK_OK(psiv_macro_list(s, &macros, &count));
	CHECK(count == 3);
	const psiv_macro* opener = findMacro(macros, count, "opener");
	CHECK(opener && opener->step_count == 5);
	if (opener && opener->step_count == 5) {
		const psiv_macro_step* spark = &opener->steps[0];
		CHECK(strcmp(spark->member, "mystic") == 0 && strcmp(spark->member_name, "Test Mystic") == 0);
		CHECK(spark->command_kind == PSIV_COMMAND_ABILITY && strcmp(spark->id, "spark") == 0 && strcmp(spark->id_name, "Spark") == 0);
		CHECK(spark->target_kind == PSIV_MACRO_TARGET_ENEMY && spark->target_enemy == 0 && spark->issue == PSIV_ISSUE_NONE);
		CHECK(strcmp(opener->steps[1].member, "scout") == 0 && opener->steps[1].issue == PSIV_ISSUE_NOT_IN_PARTY);
		CHECK(opener->steps[4].target_kind == PSIV_MACRO_TARGET_MEMBER && strcmp(opener->steps[4].target_member_name, "Bryn") == 0);
	}

	/* Editing: a macro for the medic's and the mystic's combination. */
	CHECK(psiv_macro_create(s, "arc") == PSIV_REFUSED && errorIs(s, "game.refuse.macro_menu_only"));
	CHECK_OK(psiv_open_menu(s));
	CHECK_OK(psiv_macro_create(s, "arc"));
	CHECK(psiv_macro_create(s, "arc") == PSIV_REFUSED && errorIs(s, "game.refuse.macro_name_taken"));
	CHECK_OK(psiv_macro_set_step(s, "arc", 0, "mystic", PSIV_COMMAND_ABILITY, "spark", PSIV_MACRO_TARGET_ENEMY, NULL, 0));
	CHECK_OK(psiv_macro_set_step(s, "arc", 0, "medic", PSIV_COMMAND_ABILITY, "glint", PSIV_MACRO_TARGET_ENEMY, NULL, 0));
	CHECK_OK(psiv_macro_set_step(s, "arc", 1, "mystic", PSIV_COMMAND_ABILITY, "spark", PSIV_MACRO_TARGET_DEFAULT, NULL, -1));
	CHECK_OK(psiv_macro_set_step(s, "arc", 2, "fighter", PSIV_COMMAND_DEFEND, NULL, PSIV_MACRO_TARGET_DEFAULT, NULL, -1));
	CHECK(psiv_macro_set_step(s, "arc", 4, "fighter", PSIV_COMMAND_ATTACK, NULL, PSIV_MACRO_TARGET_DEFAULT, NULL, -1) == PSIV_REFUSED &&
		errorIs(s, "game.refuse.macro_no_step"));
	CHECK(psiv_macro_set_step(s, "arc", 2, "fighter", PSIV_COMMAND_ABILITY, "spark", PSIV_MACRO_TARGET_DEFAULT, NULL, -1) == PSIV_REFUSED &&
		errorIs(s, "game.refuse.macro_unknown_ability"));
	CHECK(psiv_macro_set_step(s, "arc", 2, "fighter", PSIV_COMMAND_DEFEND, NULL, PSIV_MACRO_TARGET_ALL, NULL, -1) == PSIV_REFUSED &&
		errorIs(s, "game.refuse.macro_target"));
	CHECK(psiv_macro_set_step(s, "arc", 2, "fighter", PSIV_COMMAND_DEFEND, NULL, 9, NULL, -1) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_macro_set_step(s, "nothing", 0, "fighter", PSIV_COMMAND_DEFEND, NULL, PSIV_MACRO_TARGET_DEFAULT, NULL, -1) == PSIV_REFUSED &&
		errorIs(s, "game.refuse.no_macro"));
	CHECK_OK(psiv_macro_move_step(s, "arc", 2, 0));
	CHECK_OK(psiv_macro_remove_step(s, "arc", 0));
	CHECK(psiv_macro_remove_step(s, "arc", 2) == PSIV_REFUSED);
	CHECK_OK(psiv_macro_move(s, "arc", 0));
	CHECK_OK(psiv_macro_list(s, &macros, &count));
	CHECK(count == 4 && strcmp(macros[0].name, "arc") == 0 && macros[0].step_count == 2);
	CHECK(strcmp(macros[0].steps[0].member, "medic") == 0 && strcmp(macros[0].steps[0].id_name, "Glint") == 0 && macros[0].steps[0].target_enemy == 0);
	CHECK(strcmp(macros[0].steps[1].member, "mystic") == 0 && macros[0].steps[1].target_enemy == -1);
	CHECK(macros[0].steps[0].piece == 0);

	/* A step that names a piece: kept as written, marked while the party has none to cast. */
	CHECK_OK(psiv_macro_create(s, "fan"));
	CHECK_OK(psiv_macro_set_piece_step(s, "fan", 0, "fighter", "squall_fan", PSIV_MACRO_TARGET_ENEMY, NULL, 1));
	CHECK(psiv_macro_set_piece_step(s, "fan", 0, "fighter", "rivet_plate", PSIV_MACRO_TARGET_DEFAULT, NULL, -1) == PSIV_REFUSED &&
		errorIs(s, "game.refuse.macro_piece_no_use"));
	CHECK(psiv_macro_set_piece_step(s, "fan", 0, "fighter", NULL, PSIV_MACRO_TARGET_DEFAULT, NULL, -1) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_macro_list(s, &macros, &count));
	const psiv_macro* fan = findMacro(macros, count, "fan");
	CHECK(fan && fan->step_count == 1 && fan->steps[0].piece == 1 && fan->steps[0].command_kind == PSIV_COMMAND_ITEM);
	CHECK(fan && strcmp(fan->steps[0].id, "squall_fan") == 0 && strcmp(fan->steps[0].id_name, "Squall Fan") == 0 && fan->steps[0].issue == PSIV_ISSUE_NO_ITEM);
	CHECK(fan && fan->steps[0].target_kind == PSIV_MACRO_TARGET_ENEMY && fan->steps[0].target_enemy == 1);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_MACRO_STEP_STATUS, PSIV_MACRO_AUTOMATIC), "automatic") == 0);
	CHECK_OK(psiv_macro_delete(s, "fan"));

	/* A vehicle's step, for battles aboard it: its skills, attack or escape, marked while the party has no such vehicle. */
	CHECK_OK(psiv_macro_create(s, "hull"));
	CHECK_OK(psiv_macro_set_step(s, "hull", 0, "reed_skimmer", PSIV_COMMAND_ABILITY, "hull_vent", PSIV_MACRO_TARGET_ENEMY, NULL, 0));
	CHECK(psiv_macro_set_step(s, "hull", 1, "reed_skimmer", PSIV_COMMAND_ATTACK, NULL, PSIV_MACRO_TARGET_DEFAULT, NULL, -1) == PSIV_REFUSED &&
		errorIs(s, "game.refuse.macro_member_twice"));
	CHECK(psiv_macro_set_step(s, "hull", 0, "reed_skimmer", PSIV_COMMAND_DEFEND, NULL, PSIV_MACRO_TARGET_DEFAULT, NULL, -1) == PSIV_REFUSED &&
		errorIs(s, "game.refuse.macro_bad_command"));
	CHECK(psiv_macro_set_step(s, "hull", 0, "reed_skimmer", PSIV_COMMAND_ABILITY, "spark", PSIV_MACRO_TARGET_DEFAULT, NULL, -1) == PSIV_REFUSED &&
		errorIs(s, "game.refuse.macro_unknown_ability"));
	CHECK_OK(psiv_macro_list(s, &macros, &count));
	const psiv_macro* hull = findMacro(macros, count, "hull");
	CHECK(hull && hull->step_count == 1 && strcmp(hull->steps[0].member_name, "Reed Skimmer") == 0 && hull->steps[0].issue == PSIV_ISSUE_NOT_IN_PARTY);
	CHECK_OK(psiv_macro_delete(s, "hull"));

	CHECK_OK(psiv_macro_create(s, "spare"));
	CHECK_OK(psiv_settings_set_auto_macro(s, "spare"));
	CHECK_OK(psiv_macro_rename(s, "spare", "extra"));
	psiv_settings settings;
	CHECK_OK(psiv_settings_get(s, &settings));
	CHECK(strcmp(settings.auto_macro, "extra") == 0);
	CHECK(psiv_macro_rename(s, "extra", "arc") == PSIV_REFUSED && errorIs(s, "game.refuse.macro_name_taken"));
	CHECK_OK(psiv_notices_drain(s, &notices, &count));
	CHECK_OK(psiv_macro_delete(s, "extra"));
	CHECK_OK(psiv_settings_get(s, &settings));
	CHECK(strcmp(settings.auto_macro, "") == 0);
	CHECK_OK(psiv_notices_drain(s, &notices, &count));
	CHECK(count == 1 && strcmp(notices[0].key, "game.auto_macro_cleared") == 0 && strcmp(notices[0].params[0], "extra") == 0);

	/* Member detail and skill uses. */
	psiv_member_detail detail;
	CHECK_OK(psiv_member_detail_get(s, "fighter", &detail));
	CHECK(detail.experience_to_next > 0 && detail.critical == 8 && detail.attack_element == PSIV_ELEMENT_NONE);
	CHECK(detail.equipment_bonus[PSIV_SLOT_WEAPON].strength == 4 && detail.equipment_bonus[PSIV_SLOT_BODY].max_hp == 10);
	CHECK(detail.equipment_bonus[PSIV_SLOT_ARMS].defense == 0 && detail.affinity_count == 0 && detail.status_resist_count == 0);
	CHECK_OK(psiv_member_detail_get(s, "mystic", &detail));
	CHECK(detail.attack_element == PSIV_ELEMENT_HEAT && detail.affinity_count == 1 && detail.affinities[0].element == PSIV_ELEMENT_SHOCK && detail.affinities[0].percent == 50);
	CHECK(strcmp(detail.affinities[0].id, "shock") == 0 && strcmp(detail.affinities[0].name, "Shock") == 0);
	const psiv_element_info* elements;
	CHECK_OK(psiv_elements(s, &elements, &count));
	CHECK(count >= 5 && elements[0].index == PSIV_ELEMENT_HEAT && strcmp(elements[4].id, "holy") == 0 && elements[4].index == PSIV_ELEMENT_HOLY);
	psiv_game_info elementRules;
	CHECK_OK(psiv_game_info_get(s, &elementRules));
	CHECK(elementRules.affinity_divisor == 100);
	CHECK(detail.status_resist_count == 1 && strcmp(detail.status_resists[0].name, "Sleep") == 0 && detail.status_resists[0].percent == 100);
	CHECK(psiv_member_detail_get(s, "scout", &detail) == PSIV_NOT_FOUND);
	const psiv_ability_info* abilities;
	CHECK_OK(psiv_member_abilities(s, "fighter", &abilities, &count));
	int32_t skills = 0;
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(abilities[i].id, "focus_shot") == 0) {
			skills += abilities[i].uses == 3 && abilities[i].max_uses == 3;
		} else {
			skills += abilities[i].uses != -1;
		}
	}
	CHECK(skills == 1);
	CHECK_OK(psiv_party_swap(s, 2, 0));
	CHECK_OK(psiv_close_menu(s));

	/* The macro played in the hold fight: the medic's glint and the mystic's spark make a combination. */
	int32_t total = 0;
	const psiv_combo_entry* combos;
	CHECK_OK(psiv_combo_log(s, &combos, &count, &total));
	CHECK(count == 0 && total == 15);
	CHECK(walkToMarker(s, "hold_front") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_BATTLE);
	const psiv_macro_step_result* stepResults;
	int32_t stepCount = 0;
	CHECK(psiv_battle_apply_macro_fallback(s, "arc", 7, &stepResults, &stepCount) == PSIV_INVALID_ARGUMENT);
	for (int round = 0; round < 10 && psiv_mode_get(s) == PSIV_MODE_BATTLE && count == 0; ++round) {
		const psiv_battle_event* events;
		int32_t eventCount;
		CHECK_OK(psiv_battle_apply_macro_fallback(s, "arc", PSIV_MACRO_FALLBACK_AUTOMATIC, &stepResults, &stepCount));
		CHECK(stepCount == 2);
		for (int32_t i = 0; i < stepCount; ++i) {
			CHECK(stepResults[i].status != PSIV_MACRO_AUTOMATIC || stepResults[i].issue != PSIV_ISSUE_NONE);
		}
		CHECK_OK(psiv_battle_resolve_round(s, &events, &eventCount));
		CHECK_OK(psiv_combo_log(s, &combos, &count, NULL));
	}
	CHECK(count == 1);
	if (count == 1) {
		CHECK(strcmp(combos[0].recipe, "twin_arc") == 0 && strcmp(combos[0].result_name, "Twin Arc") == 0 && combos[0].play_time_ms > 0);
		CHECK(combos[0].participant_count == 2 && strcmp(combos[0].participants[0], "medic") == 0 && strcmp(combos[0].participant_names[1], "Test Mystic") == 0);
		CHECK(combos[0].ability_count == 2 && strcmp(combos[0].abilities[0], "glint") == 0 && strcmp(combos[0].ability_names[1], "Spark") == 0);
	}

	/* A macro made from the found combination, after the fight; auto play finishes it with "arc" every round. */
	settle(s);
	CHECK(psiv_battle_set_auto_play_macro(s, "whirlwind") == PSIV_REFUSED && errorIs(s, "game.refuse.no_macro"));
	if (psiv_mode_get(s) == PSIV_MODE_BATTLE) {
		psiv_auto_battle played;
		CHECK_OK(psiv_battle_set_auto_play_macro(s, "arc"));
		CHECK_OK(psiv_battle_play(s, 60, &played));
		CHECK(played.ended && played.refusal_key[0] == '\0');
		settle(s);
	}
	CHECK_OK(psiv_battle_set_auto_play_macro(s, NULL));
	CHECK(psiv_macro_from_combo(s, "twin_arc", "Twin") == PSIV_REFUSED && errorIs(s, "game.refuse.macro_menu_only"));
	if (psiv_mode_get(s) == PSIV_MODE_FIELD) {
		CHECK_OK(psiv_open_menu(s));
		CHECK(psiv_macro_from_combo(s, "storm_spark", "Storm") == PSIV_REFUSED && errorIs(s, "game.refuse.combo_not_found"));
		CHECK_OK(psiv_macro_from_combo(s, "twin_arc", "Twin"));
		CHECK(psiv_macro_from_combo(s, "twin_arc", "Twin") == PSIV_REFUSED && errorIs(s, "game.refuse.macro_name_taken"));
		CHECK(psiv_macro_from_combo(s, NULL, "x") == PSIV_INVALID_ARGUMENT);
		CHECK_OK(psiv_macro_list(s, &macros, &count));
		const psiv_macro* twin = findMacro(macros, count, "Twin");
		CHECK(twin && twin->step_count == 2);
		if (twin && twin->step_count == 2) {
			CHECK(strcmp(twin->steps[0].member, "medic") == 0 && strcmp(twin->steps[0].id, "glint") == 0 && twin->steps[0].target_kind == PSIV_MACRO_TARGET_DEFAULT);
			CHECK(strcmp(twin->steps[1].member, "mystic") == 0 && strcmp(twin->steps[1].id, "spark") == 0 && twin->steps[1].issue == PSIV_ISSUE_NONE);
		}
	}
	psiv_session_destroy(s);
}

/* A new game played to the field after the guide's scene: Bryn (the fighter), the mystic and the medic. */
static psiv_session* afterGuide(const char* name) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/%s", scratch, name);
	makeDirectory(saves);
	removeFiles(saves);
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK(walkToMarker(s, "guide_front") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	settle(s);
	CHECK_OK(psiv_enter_name(s, "Bryn"));
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	return s;
}

static int32_t hasName(const char* const* names, int32_t count, const char* name) {
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(names[i], name) == 0) {
			return 1;
		}
	}
	return 0;
}

/* Equipment compared before it goes on, with the numbers a battle would use. */
static void testEquipPreview(void) {
	psiv_session* s = afterGuide("preview");
	psiv_equip_preview p;
	CHECK_OK(psiv_equip_preview_get(s, "fighter", "rivet_plate", &p));
	CHECK(strcmp(p.piece_name, "Rivet Plate") == 0 && p.slot == PSIV_SLOT_BODY && strcmp(p.category, "heavy_armor") == 0);
	CHECK(p.bonus.defense == 6 && p.bonus.max_hp == 20 && p.bonus.agility == -2 && p.element == PSIV_ELEMENT_NONE);
	CHECK(strcmp(p.replaces, "padded_coat") == 0 && strcmp(p.replaces_name, "Padded Coat") == 0 && p.can_wear && strcmp(p.refusal_key, "") == 0);
	CHECK(p.after.stats.defense - p.before.stats.defense == 3 && p.after.stats.max_hp - p.before.stats.max_hp == 10);
	CHECK(p.wearer_count == 1 && strcmp(p.wearers[0], "fighter") == 0 && strcmp(p.wearer_names[0], "Bryn") == 0);
	psiv_member_detail detail;
	CHECK_OK(psiv_member_detail_get(s, "fighter", &detail));
	CHECK(detail.critical == p.before.stats.critical);

	CHECK_OK(psiv_equip_preview_get(s, "mystic", "spark_ring", &p));
	CHECK(p.granted_count == 1 && strcmp(p.granted_names[0], "Glint") == 0 && strcmp(p.replaces, "calm_charm") == 0);
	CHECK(p.before.status_resist_count == 1 && p.after.status_resist_count == 0);
	CHECK(hasName(p.after.abilities, p.after.ability_count, "glint") && !hasName(p.before.abilities, p.before.ability_count, "glint"));
	CHECK(p.wearer_count == 3 && strcmp(p.wearers[1], "mystic") == 0 && strcmp(p.wearers[2], "medic") == 0);
	CHECK_OK(psiv_equip_preview_get(s, "fighter", "calm_charm", &p));
	CHECK(p.piece_resist_count == 1 && strcmp(p.piece_resists[0].name, "Sleep") == 0 && p.after.status_resist_count == 1);

	CHECK_OK(psiv_equip_preview_get(s, "fighter", "volley_sling", &p));
	CHECK(!p.can_wear && strcmp(p.refusal_key, "equip.wrong_category") == 0 && strlen(p.refusal_text) > 0);
	CHECK(strcmp(p.attack_name, "Scatter Shot") == 0 && strcmp(p.after.basic_attack, p.before.basic_attack) == 0);
	CHECK(psiv_equip_preview_get(s, "scout", "tin_cap", &p) == PSIV_NOT_FOUND);
	CHECK(psiv_equip_preview_get(s, "fighter", "tonic", &p) == PSIV_NOT_FOUND);
	CHECK(psiv_equip_preview_get(s, NULL, "tin_cap", &p) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_sizeof("psiv_equip_preview") == sizeof(psiv_equip_preview) && psiv_sizeof("psiv_combat_view") == sizeof(psiv_combat_view));
	psiv_session_destroy(s);
}

/* Descriptions for menus, shops and battle menus, in the profile's language. */
static void testDescriptions(void) {
	psiv_session* s = create(NULL, -1, PSIV_PRESENTATION_INSTANT);
	const char* text = NULL;
	CHECK_OK(psiv_description(s, PSIV_DESCRIPTION_ITEM, "tonic", &text));
	CHECK(strcmp(text, "A bitter herbal draught. Restores 30 HP to one ally.") == 0);
	CHECK_OK(psiv_description(s, PSIV_DESCRIPTION_ABILITY, "spark", &text));
	CHECK(strcmp(text, "A snap of lightning at one foe.") == 0);
	CHECK_OK(psiv_description(s, PSIV_DESCRIPTION_STATUS, "sleep", &text));
	CHECK(strlen(text) > 0);
	CHECK_OK(psiv_description(s, PSIV_DESCRIPTION_ENEMY, "bog_wisp", &text));
	CHECK(strstr(text, "marsh") != NULL);
	CHECK_OK(psiv_description(s, PSIV_DESCRIPTION_DIFFICULTY, "hard", &text));
	CHECK(strstr(text, "smarter") != NULL);
	CHECK(psiv_description(s, PSIV_DESCRIPTION_ITEM, "copper_blade", &text) == PSIV_NOT_FOUND && errorIs(s, "binding.no_such_thing"));
	CHECK(psiv_description(s, 9, "tonic", &text) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_profile_set_value(s, "text_language", "ja"));
	CHECK_OK(psiv_description(s, PSIV_DESCRIPTION_EQUIPMENT, "copper_blade", &text));
	CHECK(strcmp(text, "打ち延ばした銅の短い刀。軽くて振りやすい。") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_DESCRIPTION_KIND, PSIV_DESCRIPTION_EQUIPMENT), "equipment") == 0);
	psiv_session_destroy(s);
}

/* The bag listed in the four orders. */
static void testInventorySort(void) {
	psiv_session* s = afterGuide("sort");
	const psiv_inventory_entry* items;
	int32_t count = 0;
	CHECK_OK(psiv_open_menu(s));
	CHECK_OK(psiv_unequip(s, "fighter", PSIV_SLOT_WEAPON));
	CHECK_OK(psiv_unequip(s, "fighter", PSIV_SLOT_HEAD));
	CHECK_OK(psiv_inventory_sorted(s, PSIV_SORT_ID, &items, &count));
	CHECK(count == 3 && strcmp(items[0].id, "tonic") == 0 && strcmp(items[1].id, "copper_blade") == 0 && strcmp(items[2].id, "tin_cap") == 0);
	CHECK_OK(psiv_inventory_sorted(s, PSIV_SORT_NAME, &items, &count));
	CHECK(count == 3 && strcmp(items[0].name, "Copper Blade") == 0 && strcmp(items[1].name, "Tin Cap") == 0 && strcmp(items[2].name, "Tonic") == 0);
	CHECK_OK(psiv_inventory_sorted(s, PSIV_SORT_COUNT, &items, &count));
	CHECK(count == 3 && strcmp(items[0].id, "tonic") == 0 && items[0].count == 3);
	CHECK_OK(psiv_inventory_sorted(s, PSIV_SORT_KIND, &items, &count));
	CHECK(count == 3 && !items[0].equipment && items[1].targeting_or_slot == PSIV_SLOT_WEAPON && items[2].targeting_or_slot == PSIV_SLOT_HEAD);
	CHECK(psiv_inventory_sorted(s, 7, &items, &count) == PSIV_INVALID_ARGUMENT);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_ITEM_SORT, PSIV_SORT_KIND), "kind") == 0);
	psiv_session_destroy(s);
}

/* Difficulty presets with their text, changed only with a game in progress and outside battle. */
static void testDifficulties(void) {
	psiv_session* s = create(NULL, -1, PSIV_PRESENTATION_INSTANT);
	const psiv_difficulty* presets;
	int32_t count = 0;
	int32_t canChange = -1;
	CHECK_OK(psiv_difficulties(s, &presets, &count, &canChange));
	CHECK(count == 2 && canChange == 0);
	CHECK(strcmp(presets[0].id, "hard") == 0 && strcmp(presets[0].name, "Hard") == 0 && presets[0].reward_percent == 120 && strstr(presets[0].description, "smarter"));
	CHECK(strcmp(presets[1].id, "original") == 0 && presets[1].reward_percent == 100);
	CHECK(psiv_settings_set(s, "hard", PSIV_REWARD_X1) == PSIV_REFUSED && errorIs(s, "game.refuse.settings_no_game"));
	CHECK_OK(psiv_new_game(s));
	CHECK_OK(psiv_difficulties(s, &presets, &count, NULL));
	settle(s);
	CHECK_OK(psiv_difficulties(s, &presets, &count, &canChange));
	CHECK(canChange == 1);
	CHECK_OK(psiv_settings_set(s, "hard", PSIV_REWARD_X1_5));
	psiv_settings settings;
	CHECK_OK(psiv_settings_get(s, &settings));
	CHECK(strcmp(settings.difficulty, "hard") == 0 && settings.reward_multiplier == PSIV_REWARD_X1_5);
	CHECK(psiv_sizeof("psiv_difficulty") == sizeof(psiv_difficulty));
	psiv_session_destroy(s);
}

/* Leaves the area through its exit to `area`. */
static int32_t walkOut(psiv_session* s, const char* area) {
	const psiv_exit* exits;
	int32_t count;
	int32_t status = -1;
	if (psiv_field_exits(s, &exits, &count) != PSIV_OK) {
		return status;
	}
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(exits[i].to_area, area) == 0 && psiv_walk_to(s, exits[i].x, exits[i].y) == PSIV_OK) {
			while (psiv_walk_step(s, &status) == PSIV_OK && status == PSIV_WALK_WALKING) {
				settle(s);
			}
			return status;
		}
	}
	return status;
}

/* Cameras with targets and zooms, music per area, encounter and situation, and the durations actions report. */
static void testCamerasAndMusic(void) {
	psiv_session* s = create(NULL, -1, PSIV_PRESENTATION_INSTANT);
	const psiv_camera* cameras;
	int32_t count = 0;
	CHECK_OK(psiv_cameras(s, &cameras, &count));
	CHECK(count == 4);
	if (count == 4) {
		CHECK(strcmp(cameras[0].id, "lookout_east") == 0 && cameras[0].target == PSIV_CAMERA_MARKER && strcmp(cameras[0].area, "reed_path") == 0);
		CHECK(strcmp(cameras[0].marker, "lookout_east") == 0 && cameras[0].zoom == 1.25 && strcmp(cameras[0].follow, "") == 0);
		CHECK(strcmp(cameras[2].id, "lookout_west") == 0 && cameras[2].zoom == 1.0);
		CHECK(strcmp(cameras[3].id, "peddler") == 0 && cameras[3].target == PSIV_CAMERA_FOLLOW && strcmp(cameras[3].follow, "peddler") == 0 && cameras[3].zoom == 1.5);
	}
	CHECK(strcmp(psiv_name_of(PSIV_NAME_CAMERA_TARGET, PSIV_CAMERA_LEADER), "leader") == 0);
	psiv_game_info game;
	CHECK_OK(psiv_game_info_get(s, &game));
	CHECK(strcmp(game.title_music, "title_harbor_dawn") == 0 && strcmp(game.victory_music, "victory_bright_sail") == 0 && strcmp(game.defeat_music, "defeat_low_tide") == 0);

	CHECK_OK(psiv_new_game(s));
	settle(s);
	psiv_field_info field;
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.music, "town_tide_bells") == 0);
	CHECK(walkOut(s, "reed_path") == PSIV_WALK_LEFT_AREA);
	settle(s);
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.area_id, "reed_path") == 0 && strcmp(field.music, "field_marsh_reeds") == 0);
	/* The lookout stands on the wall in the corner and is talked to from the floor below it. */
	const uint8_t* grid;
	const psiv_point* points;
	int32_t cells = 0;
	CHECK_OK(psiv_field_grid(s, &grid, &cells));
	CHECK(cells == field.width * field.height && (grid[1 * field.width + 1] & PSIV_CELL_TERRAIN_MASK) == PSIV_CELL_WALL);
	CHECK_OK(psiv_field_points(s, &points, &count));
	int32_t lookout = 0;
	for (int32_t i = 0; i < count; ++i) {
		lookout += points[i].kind == PSIV_POINT_TALK && strcmp(points[i].id, "lookout") == 0 && points[i].x == 1 && points[i].y == 1;
	}
	CHECK(lookout == 1);
	CHECK(walkToMarker(s, "lookout_front") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	const psiv_action* actions;
	CHECK_OK(psiv_actions_poll(s, &actions, &count));
	CHECK_OK(psiv_interact(s));
	settle(s);
	CHECK(psiv_mode_get(s) == PSIV_MODE_BATTLE);
	psiv_battle_info info;
	CHECK_OK(psiv_battle_info_get(s, &info));
	CHECK(strcmp(info.encounter, "marsh") == 0 && strcmp(info.music, "battle_marsh_ambush") == 0);
	CHECK_OK(psiv_actions_poll(s, &actions, &count));
	int32_t found = 0;
	for (int32_t i = 0; i < count; ++i) {
		if (actions[i].type == PSIV_ACTION_PANEL_SHOW && strcmp(actions[i].subject, "ladder") == 0) {
			found += actions[i].duration == 0.5;
		} else if (actions[i].type == PSIV_ACTION_PANEL_SHOW) {
			found += actions[i].duration == 0.35;
		} else if (actions[i].type == PSIV_ACTION_CAMERA_MOVE) {
			found += actions[i].duration == 1.5 && actions[i].seconds == 1.5;
		} else if (actions[i].type == PSIV_ACTION_MUSIC_PLAY) {
			found += actions[i].duration == 0;
		}
	}
	CHECK(found == 6);
	psiv_session_destroy(s);
}

/* Party talk coming up by itself as the party arrives, once per area under each set, and not at all when it is off. */
static int32_t ambientCount(psiv_session* s, const char* keyPrefix) {
	const psiv_notice* notices;
	int32_t count = 0;
	int32_t ambient = 0;
	CHECK_OK(psiv_notices_drain(s, &notices, &count));
	for (int32_t i = 0; i < count; ++i) {
		if (notices[i].kind == PSIV_NOTICE_AMBIENT) {
			++ambient;
			CHECK(strncmp(notices[i].key, keyPrefix, strlen(keyPrefix)) == 0);
		}
	}
	return ambient;
}

/* The flashing level from the data and in the profile, a rest point's price and voice, and member profile rows (none in
 * the shipped data). */
static void testHostData(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/host_data", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	psiv_game_info game;
	CHECK_OK(psiv_game_info_get(s, &game));
	CHECK(game.flash_default == PSIV_FLASH_REDUCED && game.flash_original == 1);
	psiv_profile profile;
	CHECK_OK(psiv_profile_get(s, &profile));
	CHECK(profile.flashing == PSIV_FLASH_UNSET && !profile.flash_notice_seen);
	profile.flashing = PSIV_FLASH_REDUCED;
	profile.flash_notice_seen = 1;
	profile.reduced_flash = 0;
	CHECK_OK(psiv_profile_set(s, &profile));
	CHECK_OK(psiv_profile_get(s, &profile));
	CHECK(profile.flashing == PSIV_FLASH_REDUCED && profile.flash_notice_seen && profile.reduced_flash);
	profile.flashing = 9;
	CHECK(psiv_profile_set(s, &profile) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_profile_set_value(s, "flashing", "standard"));
	CHECK_OK(psiv_profile_get(s, &profile));
	CHECK(profile.flashing == PSIV_FLASH_STANDARD && !profile.reduced_flash);

	CHECK_OK(psiv_new_game(s));
	settle(s);
	psiv_member_detail detail;
	CHECK_OK(psiv_member_detail_get(s, "fighter", &detail));
	CHECK(detail.profile_count == 0);
	CHECK(walkToMarker(s, "inn_front") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_WEST));
	CHECK_OK(psiv_interact(s));
	psiv_inn_info inn;
	CHECK_OK(psiv_inn_get(s, &inn));
	CHECK(strcmp(inn.id, "tidewater_inn") == 0 && inn.cost == 10 && strcmp(inn.voice, "") == 0 && !inn.per_member);
	psiv_session_destroy(s);
}

static void testAmbientTalk(void) {
	psiv_session* s = afterGuide("ambient");
	ambientCount(s, "");
	CHECK_OK(psiv_profile_set_value(s, "ambient_talk", "off"));
	CHECK(walkOut(s, "reed_path") == PSIV_WALK_LEFT_AREA);
	settle(s);
	CHECK(ambientCount(s, "") == 0);
	CHECK_OK(psiv_profile_set_value(s, "ambient_talk", "on"));
	CHECK(walkOut(s, "tidewater") == PSIV_WALK_LEFT_AREA);
	settle(s);
	CHECK(ambientCount(s, "talk.chapter_one.") == 1);
	CHECK(walkOut(s, "reed_path") == PSIV_WALK_LEFT_AREA);
	settle(s);
	CHECK(ambientCount(s, "talk.reeds.") == 2);
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	CHECK(walkOut(s, "tidewater") == PSIV_WALK_LEFT_AREA);
	settle(s);
	CHECK(ambientCount(s, "") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_NOTICE_KIND, PSIV_NOTICE_AMBIENT), "ambient") == 0);
	psiv_session_destroy(s);
}

/* Walks to (x, y) with the game's path walk, running events and playing any battle on the way to its end. */
static void walkThrough(psiv_session* s, int32_t x, int32_t y) {
	int32_t status = PSIV_WALK_WALKING;
	if (psiv_walk_to(s, x, y) != PSIV_OK) {
		return;
	}
	for (int guard = 0; guard < 4000; ++guard) {
		settle(s);
		if (psiv_mode_get(s) == PSIV_MODE_BATTLE) {
			psiv_auto_battle played;
			psiv_battle_play(s, 0, &played);
			if (psiv_walk_to(s, x, y) != PSIV_OK) {
				return;
			}
			continue;
		}
		if (psiv_walk_step(s, &status) != PSIV_OK || status != PSIV_WALK_WALKING) {
			return;
		}
	}
}

/* One step at a time in `direction` until the leader's x is `x` or it leaves the area; returns the vehicle battles
 * fought on the way, each checked to be fought by `vehicle`. */
static int driveTo(psiv_session* s, int32_t direction, int32_t x, const char* vehicle) {
	int battles = 0;
	psiv_field_info field;
	char area[64];
	psiv_field_info_get(s, &field);
	snprintf(area, sizeof area, "%s", field.area_id);
	for (int guard = 0; guard < 100; ++guard) {
		if (psiv_field_info_get(s, &field) != PSIV_OK || field.leader_x == x || strcmp(field.area_id, area) != 0) {
			break;
		}
		psiv_walk(s, direction);
		if (psiv_mode_get(s) == PSIV_MODE_BATTLE) {
			psiv_battle_info info;
			psiv_auto_battle played;
			CHECK(psiv_battle_info_get(s, &info) == PSIV_OK && strcmp(info.vehicle, vehicle) == 0 && info.source == PSIV_BATTLE_RANDOM);
			battles += strcmp(info.vehicle, vehicle) == 0;
			psiv_battle_play(s, 0, &played);
		}
	}
	return battles;
}

/* Vehicles in the field and travel: the skimmer given by a script, boarded, driven over water with vehicle battles,
 * left on dry ground, saved and loaded aboard, parked outside a town; the ferry between ports, a return to a town of the
 * region and the way out of a dungeon. */
static void testVehiclesAndTravel(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/travel", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	psiv_session_options options;
	psiv_session* s = NULL;
	psiv_session_options_init(&options);
	options.data_dir = dataDir;
	options.save_dir = saves;
	options.seed = 6;
	CHECK(psiv_session_create(&options, &s) == PSIV_OK);
	psiv_field_info field;
	psiv_error e;
	const psiv_vehicle* vehicles;
	const psiv_travel_destination* places;
	int32_t count = 0;

	CHECK(strcmp(psiv_name_of(PSIV_NAME_TRAVEL_KIND, PSIV_TRAVEL_DUNGEON_EXIT), "dungeon_exit") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_MENU_KIND, PSIV_MENU_PORT), "port") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_POINT_KIND, PSIV_POINT_PORT), "port") == 0);
	CHECK(psiv_sizeof("psiv_vehicle") == sizeof(psiv_vehicle) && psiv_sizeof("psiv_travel_destination") == sizeof(psiv_travel_destination));

	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK(walkToMarker(s, "guide_front") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	settle(s);
	CHECK_OK(psiv_enter_name(s, "Bryn"));
	settle(s);
	walkThrough(s, 13, 4);
	walkThrough(s, 10, 4);
	walkThrough(s, 19, 4);
	walkThrough(s, 6, 0);
	walkThrough(s, 0, 10);
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.area_id, "brine_flats") == 0 && strcmp(field.region, "coast") == 0 && strcmp(field.aboard, "") == 0);
	CHECK(field.leader_step_ms == field.step_ms);
	CHECK_OK(psiv_field_vehicles(s, &vehicles, &count));
	CHECK(count == 0);

	/* The mooring post's script gives the skimmer. */
	walkThrough(s, 6, 2);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_EAST));
	CHECK_OK(psiv_interact(s));
	settle(s);
	CHECK_OK(psiv_field_vehicles(s, &vehicles, &count));
	CHECK(count == 1 && strcmp(vehicles[0].id, "reed_skimmer") == 0 && strcmp(vehicles[0].name, "Reed Skimmer") == 0);
	CHECK(count == 1 && vehicles[0].x == 7 && vehicles[0].y == 4 && !vehicles[0].aboard);
	CHECK(psiv_leave_vehicle(s) == PSIV_REFUSED && psiv_last_error(s, &e) == PSIV_OK && strcmp(e.key, "game.refuse.not_aboard") == 0);

	walkThrough(s, 6, 4);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_EAST));
	CHECK_OK(psiv_board(s));
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.aboard, "reed_skimmer") == 0 && field.leader_x == 7 && field.leader_step_ms == field.step_ms / 2);
	CHECK(psiv_interact(s) == PSIV_REFUSED && psiv_last_error(s, &e) == PSIV_OK && strcmp(e.key, "game.refuse.aboard") == 0);
	CHECK(driveTo(s, PSIV_DIRECTION_EAST, 19, "reed_skimmer") >= 1);
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(field.leader_x == 19 && field.leader_y == 4);
	CHECK_OK(psiv_leave_vehicle(s));
	CHECK_OK(psiv_field_vehicles(s, &vehicles, &count));
	CHECK(count == 1 && vehicles[0].x == 19 && !vehicles[0].aboard);
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(field.leader_x == 20 && strcmp(field.aboard, "") == 0);

	/* Behind the water: the crate, and the beacon that opens the ferry; then aboard again, a manual save. */
	walkThrough(s, 21, 2);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	const psiv_inventory_entry* items;
	int32_t itemCount = 0;
	int travelling = 0;
	CHECK_OK(psiv_inventory(s, &items, &itemCount));
	for (int32_t i = 0; i < itemCount; ++i) {
		if (strcmp(items[i].id, "homing_shell") == 0) {
			travelling += items[i].travel == PSIV_TRAVEL_TOWN && items[i].count == 2;
		} else if (strcmp(items[i].id, "waymark_chalk") == 0) {
			travelling += items[i].travel == PSIV_TRAVEL_DUNGEON_EXIT;
		} else {
			travelling += items[i].travel == -1 ? 0 : 10;
		}
	}
	CHECK(travelling == 2);
	walkThrough(s, 22, 4);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_EAST));
	CHECK_OK(psiv_interact(s));
	settle(s);
	CHECK(psiv_flag(s, "travel.isles_open"));
	walkThrough(s, 20, 4);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_WEST));
	CHECK_OK(psiv_interact(s));
	CHECK_OK(psiv_open_menu(s));
	CHECK_OK(psiv_save_manual(s, "manual_0"));
	CHECK_OK(psiv_close_menu(s));
	CHECK_OK(psiv_quit_to_title(s));
	CHECK_OK(psiv_load(s, "manual_0"));
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.aboard, "reed_skimmer") == 0 && field.leader_x == 19);
	CHECK_OK(psiv_field_vehicles(s, &vehicles, &count));
	CHECK(count == 1 && vehicles[0].aboard && vehicles[0].x == 19);

	/* Driving into the market leaves the skimmer outside. */
	driveTo(s, PSIV_DIRECTION_WEST, -1, "reed_skimmer");
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.area_id, "saltgrass") == 0 && strcmp(field.aboard, "") == 0);
	CHECK_OK(psiv_field_vehicles(s, &vehicles, &count));
	CHECK(count == 0);

	/* The ferry. */
	CHECK(psiv_travel_destinations(s, 7, &places, &count) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_travel(s, PSIV_TRAVEL_PORT, "quay_pier", NULL, NULL) == PSIV_REFUSED && psiv_last_error(s, &e) == PSIV_OK && strcmp(e.key, "game.refuse.no_port") == 0);
	CHECK(walkToMarker(s, "pier_front") == PSIV_WALK_ARRIVED);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	CHECK(psiv_menu_kind_get(s) == PSIV_MENU_PORT);
	CHECK_OK(psiv_travel_destinations(s, PSIV_TRAVEL_PORT, &places, &count));
	CHECK(count == 1 && strcmp(places[0].id, "quay_pier") == 0 && strcmp(places[0].area_name, "Cinder Quay") == 0 && strcmp(places[0].region, "isles") == 0);
	CHECK_OK(psiv_travel(s, PSIV_TRAVEL_PORT, "quay_pier", NULL, NULL));
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.area_id, "cinder_quay") == 0 && strcmp(field.region, "isles") == 0 && field.leader_x == 6 && field.leader_y == 5);

	/* Returns reach visited towns of the region only, never the town the party stands in. */
	CHECK_OK(psiv_open_menu(s));
	CHECK_OK(psiv_travel_destinations(s, PSIV_TRAVEL_TOWN, &places, &count));
	CHECK(count == 0);
	CHECK(psiv_travel(s, PSIV_TRAVEL_TOWN, "saltgrass", NULL, "homing_shell") == PSIV_REFUSED && psiv_last_error(s, &e) == PSIV_OK &&
		strcmp(e.key, "game.refuse.no_town") == 0);
	CHECK(psiv_use_item(s, "homing_shell", NULL) == PSIV_REFUSED && psiv_last_error(s, &e) == PSIV_OK && strcmp(e.key, "game.refuse.travel_item") == 0);
	CHECK_OK(psiv_close_menu(s));
	walkThrough(s, 13, 4);
	CHECK_OK(psiv_open_menu(s));
	CHECK_OK(psiv_travel_destinations(s, PSIV_TRAVEL_TOWN, &places, &count));
	CHECK(count == 1 && strcmp(places[0].id, "cinder_quay") == 0 && places[0].x == 6 && places[0].y == 5);
	CHECK_OK(psiv_travel(s, PSIV_TRAVEL_TOWN, "cinder_quay", NULL, "homing_shell"));
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.area_id, "cinder_quay") == 0);

	/* Back by ferry, then into the kiln and straight out again. */
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_SOUTH));
	CHECK_OK(psiv_interact(s));
	CHECK_OK(psiv_travel(s, PSIV_TRAVEL_PORT, "saltgrass_pier", NULL, NULL));
	walkThrough(s, 47, 12);
	settle(s);
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.area_id, "kiln_upper") == 0);
	CHECK_OK(psiv_open_menu(s));
	CHECK(psiv_travel(s, PSIV_TRAVEL_TOWN, "tidewater", NULL, "homing_shell") == PSIV_REFUSED && psiv_last_error(s, &e) == PSIV_OK &&
		strcmp(e.key, "game.refuse.in_dungeon") == 0);
	CHECK_OK(psiv_travel_destinations(s, PSIV_TRAVEL_DUNGEON_EXIT, &places, &count));
	CHECK(count == 1 && strcmp(places[0].area, "saltgrass") == 0 && places[0].x == 46 && places[0].y == 12);
	CHECK_OK(psiv_travel(s, PSIV_TRAVEL_DUNGEON_EXIT, NULL, NULL, "waymark_chalk"));
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.area_id, "saltgrass") == 0 && field.leader_x == 46 && field.leader_y == 12);
	psiv_session_destroy(s);
}

static void testDevTools(void) {
	char saves[512];
	char clearPath[600];
	snprintf(saves, sizeof saves, "%s/dev", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	snprintf(clearPath, sizeof clearPath, "%s/clear.sav", saves);
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	const char* const* ids = NULL;
	int32_t count = 0;
	CHECK(psiv_sizeof("psiv_event_position") == sizeof(psiv_event_position) && psiv_sizeof("psiv_zone") == sizeof(psiv_zone));
	CHECK(psiv_sizeof("psiv_world_info") == sizeof(psiv_world_info) && psiv_sizeof("psiv_save_summary") == sizeof(psiv_save_summary));
	CHECK(strcmp(psiv_name_of(PSIV_NAME_DEV_LIST, PSIV_DEV_MARKERS), "markers") == 0);
	CHECK_OK(psiv_dev_ids(s, PSIV_DEV_MARKERS, &ids, &count));
	CHECK(hasName(ids, count, "reed_path:west_gate"));
	CHECK_OK(psiv_dev_ids(s, PSIV_DEV_EVENTS, &ids, &count));
	CHECK(hasName(ids, count, "cove_quiet"));
	CHECK(psiv_dev_ids(s, 99, &ids, &count) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_dev_warp(s, "reed_path", "west_gate") == PSIV_REFUSED && errorIs(s, "game.refuse.no_game"));
	const char* text = NULL;
	CHECK(psiv_save_text(s, &text) == PSIV_REFUSED);
	psiv_event_position at;
	CHECK(psiv_event_position_get(s, &at) == PSIV_NOT_FOUND);

	/* A reload mid-event resumes the event at the line it waits on, and marks the game. */
	CHECK_OK(psiv_new_game(s));
	CHECK_OK(psiv_event_position_get(s, &at));
	CHECK(strcmp(at.event, "tidewater_arrival") == 0 && strcmp(at.file, "events/tidewater.ev") == 0 && at.line > 4);
	const int32_t line = at.line;
	psiv_world_info world;
	CHECK_OK(psiv_world_info_get(s, &world));
	CHECK(!world.dev && world.encounters);
	CHECK_OK(psiv_session_reload(s));
	CHECK(psiv_mode_get(s) == PSIV_MODE_EVENT && psiv_line_pending(s));
	CHECK_OK(psiv_event_position_get(s, &at));
	CHECK(at.line == line);
	CHECK_OK(psiv_world_info_get(s, &world));
	CHECK(world.dev);
	settle(s);
	CHECK(psiv_event_position_get(s, &at) == PSIV_NOT_FOUND);

	CHECK_OK(psiv_dev_warp(s, "reed_path", "west_gate"));
	psiv_field_info field;
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.area_id, "reed_path") == 0 && field.leader_x == 1 && field.leader_y == 4);
	CHECK(psiv_dev_warp_cell(s, "reed_path", 0, 0) == PSIV_REFUSED && errorIs(s, "game.refuse.dev_bad_cell"));
	const psiv_zone* zones = NULL;
	CHECK_OK(psiv_field_zones(s, &zones, &count));
	CHECK(count == 2 && strcmp(zones[0].id, "west_reeds") == 0 && zones[0].entry_count > 0 && zones[0].from_x <= zones[0].to_x);
	CHECK_OK(psiv_dev_encounters(s, 0));
	CHECK_OK(psiv_world_info_get(s, &world));
	CHECK(!world.encounters);
	CHECK_OK(psiv_dev_give(s, "tonic", 3));
	CHECK(psiv_dev_give(s, "tonic", 0) == PSIV_REFUSED && errorIs(s, "game.refuse.dev_bad_count"));
	CHECK_OK(psiv_dev_money(s, 1000));
	CHECK_OK(psiv_dev_rest(s));
	CHECK(psiv_dev_rest(NULL) != PSIV_OK);
	CHECK_OK(psiv_world_info_get(s, &world));
	CHECK(world.money == 1050);
	CHECK_OK(psiv_dev_set_level(s, "fighter", 9));
	const psiv_member* party = NULL;
	CHECK_OK(psiv_party(s, &party, &count));
	CHECK(count >= 1 && party[0].level == 9 && party[0].hp == party[0].max_hp);
	CHECK_OK(psiv_dev_set_flag(s, "reed.lookout_seen", 1));
	CHECK(psiv_flag(s, "reed.lookout_seen"));
	CHECK_OK(psiv_dev_vehicle(s, "reed_skimmer", "reed_path", "skimmer_front"));
	const psiv_vehicle* vehicles = NULL;
	CHECK_OK(psiv_field_vehicles(s, &vehicles, &count));
	CHECK(count == 1 && strcmp(vehicles[0].id, "reed_skimmer") == 0);

	CHECK_OK(psiv_dev_battle(s, "training"));
	CHECK(psiv_mode_get(s) == PSIV_MODE_BATTLE);
	const psiv_battle_event* events = NULL;
	CHECK_OK(psiv_dev_end_battle(s, 1, &events, &count));
	CHECK(count > 0 && events[count - 1].type == PSIV_EVENT_BATTLE_ENDED && events[count - 1].outcome == PSIV_OUTCOME_VICTORY);
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	CHECK(psiv_dev_end_battle(s, 1, &events, &count) == PSIV_REFUSED && errorIs(s, "game.refuse.no_battle"));

	CHECK_OK(psiv_open_menu(s));
	CHECK_OK(psiv_save_manual(s, "manual_0"));
	CHECK_OK(psiv_close_menu(s));
	const psiv_save_summary* list = NULL;
	CHECK_OK(psiv_saves(s, &list, &count));
	CHECK(count >= 1 && strcmp(list[0].slot, "manual_0") == 0 && list[0].dev);

	/* A reload in the field keeps the leader where it stood. */
	CHECK_OK(psiv_walk(s, PSIV_DIRECTION_EAST));
	CHECK_OK(psiv_field_info_get(s, &field));
	const int32_t x = field.leader_x;
	CHECK_OK(psiv_session_reload(s));
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD && strcmp(field.area_id, "reed_path") == 0 && field.leader_x == x);
	CHECK_OK(psiv_world_info_get(s, &world));
	CHECK(world.dev && !world.encounters);

	CHECK_OK(psiv_save_text(s, &text));
	char* copy = malloc(strlen(text) + 1);
	strcpy(copy, text);
	psiv_session* other = create(NULL, -1, PSIV_PRESENTATION_INSTANT);
	CHECK(psiv_load_text(other, "not a save") == PSIV_REFUSED);
	CHECK(psiv_load_text(other, copy) == PSIV_OK);
	CHECK(psiv_field_info_get(other, &field) == PSIV_OK && field.leader_x == x);
	CHECK(psiv_world_info_get(other, &world) == PSIV_OK && world.dev);
	free(copy);
	psiv_session_destroy(other);

	CHECK_OK(psiv_dev_run_event(s, "cove_quiet"));
	CHECK(psiv_mode_get(s) == PSIV_MODE_EVENT);
	settle(s);
	CHECK(psiv_flag(s, "cove.quiet_seen"));
	FILE* clear = fopen(clearPath, "rb");
	CHECK(clear == NULL);
	if (clear) {
		fclose(clear);
	}
	psiv_session_destroy(s);
}

static void testFollowersKindsAndUses(void) {
	char saves[512];
	char clearPath[600];
	snprintf(saves, sizeof saves, "%s/follow", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	snprintf(clearPath, sizeof clearPath, "%s/clear.sav", saves);
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	int32_t value = -1;
	CHECK_OK(psiv_clear_record_exists(s, &value));
	CHECK(value == 0);
	FILE* damaged = fopen(clearPath, "wb");
	if (damaged) {
		fputs("PSR-CLEAR 1\nnot a record\n", damaged);
		fclose(damaged);
	}
	CHECK_OK(psiv_clear_record_exists(s, &value));
	CHECK(value == 0);
	const char* const* ids = NULL;
	int32_t count = 0;
	CHECK_OK(psiv_audio_ids(s, &ids, &count));
	CHECK(count > 0 && hasName(ids, count, "title_harbor_dawn"));
	CHECK_OK(psiv_character_kind(s, "cogwright", &value));
	CHECK(value == PSIV_CREATURE_MACHINE);
	CHECK_OK(psiv_character_kind(s, "fighter", &value));
	CHECK(value == PSIV_CREATURE_LIVING);
	CHECK(psiv_character_kind(s, "nobody", &value) == PSIV_NOT_FOUND);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_CREATURE_KIND, PSIV_CREATURE_SPIRIT), "spirit") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_JOB_STATE, PSIV_JOB_MISSED), "missed") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_ELEMENT, PSIV_ELEMENT_HOLY), "holy") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_EVENT_TYPE, PSIV_EVENT_ENEMY_TRANSFORMED), "enemy_transformed") == 0);

	CHECK_OK(psiv_new_game(s));
	settle(s);
	const char* follower = NULL;
	CHECK_OK(psiv_follower(s, &follower));
	CHECK(follower && follower[0] == '\0');
	CHECK_OK(psiv_dev_warp(s, "ember_steps", "west_gate"));
	settle(s);
	CHECK_OK(psiv_dev_run_event(s, "steps_wayfarer"));
	settle(s);
	CHECK_OK(psiv_follower(s, &follower));
	CHECK(follower && strcmp(follower, "wayfarer") == 0);

	CHECK(psiv_vehicle_skill_uses(s, "reed_skimmer", "hull_vent", &value) == PSIV_NOT_FOUND);
	CHECK_OK(psiv_dev_vehicle(s, "reed_skimmer", "reed_path", "skimmer_front"));
	CHECK_OK(psiv_vehicle_skill_uses(s, "reed_skimmer", "hull_vent", &value));
	CHECK(value == 3);
	CHECK(psiv_vehicle_skill_uses(s, "reed_skimmer", "spark", &value) == PSIV_NOT_FOUND);
	psiv_session_destroy(s);
}

static const psiv_point* findPoint(const psiv_point* points, int32_t count, int32_t kind, const char* id) {
	for (int32_t i = 0; i < count; ++i) {
		if (points[i].kind == kind && (!id || strcmp(points[i].id, id) == 0)) {
			return &points[i];
		}
	}
	return NULL;
}

/* The Ashen Fells: hands, shop kinds, the ship, the vehicles' call items and terrain, flying enemies, names in Japanese. */
static void testFells(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/fells", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_EQUIP_SLOT, PSIV_SLOT_LEFT_HAND), "left_hand") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_SHOP_KIND, PSIV_SHOP_GIFT), "gift") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_AREA_KIND, PSIV_AREA_VESSEL), "vessel") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_POINT_KIND, PSIV_POINT_GANGWAY), "gangway") == 0);
	CHECK_OK(psiv_new_game(s));
	settle(s);

	/* A dock that its bell raises, walked aboard; the helm, the gangway and the port it came from. */
	CHECK_OK(psiv_dev_warp(s, "cinder_quay", "dock_front"));
	settle(s);
	const psiv_point* points = NULL;
	int32_t count = 0;
	CHECK_OK(psiv_field_points(s, &points, &count));
	const psiv_point* dock = findPoint(points, count, PSIV_POINT_PORT, "quay_dock");
	CHECK(dock && !dock->active);
	CHECK_OK(psiv_dev_set_flag(s, "travel.dock_open", 1));
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_SOUTH));
	CHECK_OK(psiv_interact(s));
	settle(s);
	psiv_field_info info;
	CHECK_OK(psiv_field_info_get(s, &info));
	CHECK(info.area_kind == PSIV_AREA_VESSEL && strcmp(info.ship_port, "quay_dock") == 0);
	CHECK_OK(psiv_field_points(s, &points, &count));
	CHECK(findPoint(points, count, PSIV_POINT_HELM, "barge_helm") && findPoint(points, count, PSIV_POINT_GANGWAY, NULL));

	/* Shop kinds and the stock a story flag adds. */
	CHECK_OK(psiv_dev_warp(s, "ashfall_market", "tools_front"));
	settle(s);
	CHECK_OK(psiv_field_points(s, &points, &count));
	const psiv_point* arms = findPoint(points, count, PSIV_POINT_SHOP, "ashfall_arms");
	const psiv_point* fine = findPoint(points, count, PSIV_POINT_SHOP, "ashfall_fine_arms");
	CHECK(arms && arms->shop_kind == PSIV_SHOP_WEAPON && arms->active);
	CHECK(fine && !fine->active);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	psiv_shop_info shop;
	CHECK_OK(psiv_shop_get(s, &shop));
	CHECK(shop.kind == PSIV_SHOP_TOOL && shop.stock_count == 2);
	CHECK_OK(psiv_close_menu(s));
	CHECK_OK(psiv_dev_set_flag(s, "fells.bridge_mended", 1));
	CHECK_OK(psiv_interact(s));
	CHECK_OK(psiv_shop_get(s, &shop));
	CHECK(shop.stock_count == 3);
	CHECK_OK(psiv_close_menu(s));

	/* Two hands: a piece in the hand named, the preview of a two-handed weapon, and a refused hand. */
	CHECK_OK(psiv_dev_give(s, "gill_knife", 1));
	CHECK_OK(psiv_dev_give(s, "kelp_cleaver", 1));
	CHECK_OK(psiv_open_menu(s));
	CHECK(psiv_equip_at(s, "fighter", "gill_knife", 99) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_equip_at(s, "fighter", "gill_knife", PSIV_SLOT_LEFT_HAND));
	const psiv_member* party = NULL;
	CHECK_OK(psiv_party(s, &party, &count));
	CHECK(count > 0 && strcmp(party[0].equipment[PSIV_SLOT_LEFT_HAND], "gill_knife") == 0 && strcmp(party[0].equipment[PSIV_SLOT_WEAPON], "copper_blade") == 0);
	psiv_equip_preview preview;
	CHECK_OK(psiv_equip_preview_get(s, "fighter", "kelp_cleaver", &preview));
	CHECK(preview.hands == 2 && preview.position == PSIV_SLOT_WEAPON && strcmp(preview.replaces, "copper_blade") == 0 && strcmp(preview.also_replaces, "gill_knife") == 0);
	CHECK_OK(psiv_equip_preview_at(s, "fighter", "kelp_cleaver", PSIV_SLOT_LEFT_HAND, &preview));
	CHECK(!preview.can_wear && strcmp(preview.refusal_key, "equip.wrong_position") == 0);
	CHECK(psiv_equip_at(s, "fighter", "kelp_cleaver", PSIV_SLOT_LEFT_HAND) == PSIV_REFUSED);
	/* Magic defense comes with armor, though these rules do not use it. */
	psiv_game_info game;
	CHECK_OK(psiv_game_info_get(s, &game));
	CHECK(game.magic_defense == 0);
	CHECK_OK(psiv_dev_give(s, "ward_mantle", 1));
	CHECK_OK(psiv_equip_preview_get(s, "fighter", "ward_mantle", &preview));
	CHECK(preview.bonus.magic_defense == 8 && preview.after.stats.magic_defense - preview.before.stats.magic_defense == 8);
	CHECK_OK(psiv_equip(s, "fighter", "ward_mantle"));
	psiv_member_detail detail;
	CHECK_OK(psiv_member_detail_get(s, "fighter", &detail));
	CHECK(detail.magic_defense == 8 && detail.equipment_bonus[PSIV_SLOT_BODY].magic_defense == 8);
	CHECK_OK(psiv_close_menu(s));

	/* The moor's terrain and a vehicle called from the item list. */
	CHECK_OK(psiv_dev_warp(s, "fell_moor", "town_gate"));
	settle(s);
	const uint8_t* cells = NULL;
	int32_t cellCount = 0;
	CHECK_OK(psiv_field_info_get(s, &info));
	CHECK_OK(psiv_field_grid(s, &cells, &cellCount));
	CHECK((cells[1 * info.width + 8] & PSIV_CELL_TERRAIN_MASK) == PSIV_CELL_SOFT && !(cells[1 * info.width + 8] & PSIV_CELL_WALKABLE));
	CHECK((cells[1 * info.width + 13] & PSIV_CELL_TERRAIN_MASK) == PSIV_CELL_WALL && (cells[1 * info.width + 13] & PSIV_CELL_BARRIER));
	CHECK_OK(psiv_dev_vehicle(s, "dune_crawler", "fell_moor", "crawler_bay"));
	CHECK_OK(psiv_dev_give(s, "dune_horn", 1));
	const psiv_inventory_entry* items = NULL;
	CHECK_OK(psiv_inventory(s, &items, &count));
	int32_t horn = 0;
	for (int32_t i = 0; i < count; ++i) {
		horn = horn || (strcmp(items[i].id, "dune_horn") == 0 && strcmp(items[i].calls_vehicle, "dune_crawler") == 0);
	}
	CHECK(horn);
	CHECK_OK(psiv_open_menu(s));
	CHECK_OK(psiv_use_item(s, "dune_horn", NULL));
	CHECK_OK(psiv_field_info_get(s, &info));
	CHECK(strcmp(info.aboard, "dune_crawler") == 0);

	/* Flying enemies, and every name in the battle in Japanese. */
	psiv_profile profile;
	CHECK_OK(psiv_profile_get(s, &profile));
	profile.text_language = "ja";
	CHECK_OK(psiv_profile_set(s, &profile));
	CHECK_OK(psiv_leave_vehicle(s));
	CHECK_OK(psiv_dev_battle(s, "kites"));
	const psiv_combatant* combatants = NULL;
	CHECK_OK(psiv_battle_combatants(s, &combatants, &count));
	CHECK(count >= 2 && combatants[count - 1].flying && strcmp(combatants[count - 1].name, "燃えさしの凧鳥") == 0);
	CHECK(!combatants[0].flying && strcmp(combatants[1].name, "術師") != 0);
	psiv_session_destroy(s);
}

static void testCogwayAndFormations(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/cogway", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_POINT_KIND, PSIV_POINT_CONVEYOR), "conveyor") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_STAT, PSIV_STAT_MAGIC_DEFENSE), "magic_defense") == 0);
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_dev_warp(s, "cogway", "entry"));
	settle(s);
	const psiv_point* points = NULL;
	int32_t count = 0;
	CHECK_OK(psiv_field_points(s, &points, &count));
	int belts = 0;
	for (int32_t i = 0; i < count; ++i) {
		if (points[i].kind == PSIV_POINT_CONVEYOR) {
			CHECK(points[i].direction == PSIV_DIRECTION_EAST && points[i].active && points[i].y == 3);
			++belts;
		}
	}
	CHECK(belts == 4);
	psiv_field_info field;
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.music, "dungeon_kiln_halls") == 0);

	/* A formation with free places: the jelly stands in the middle of three. */
	CHECK_OK(psiv_dev_battle(s, "jelly"));
	psiv_battle_info info;
	CHECK_OK(psiv_battle_info_get(s, &info));
	CHECK(info.formation_size == 3);
	const psiv_combatant* combatants = NULL;
	CHECK_OK(psiv_battle_combatants(s, &combatants, &count));
	CHECK(count == info.party_count + 1 && combatants[count - 1].position == 1 && combatants[0].position == -1);
	psiv_session_destroy(s);
}

/* Wren Gallery: story pictures, the scene camera, music fades from the command or game.json, an exit's sound, and when
 * the next tick and a step's landing come. */
static void testScene(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/scene", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	const char* shipped = dataDir;
	dataDir = sceneDataDir;
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	dataDir = shipped;
	CHECK(psiv_sizeof("psiv_scene_camera") == sizeof(psiv_scene_camera));
	CHECK(strcmp(psiv_name_of(PSIV_NAME_ACTION_TYPE, PSIV_ACTION_CAMERA_TO), "camera to") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_ACTION_TYPE, PSIV_ACTION_CAMERA_RESET), "camera reset") == 0);
	psiv_game_info game;
	CHECK_OK(psiv_game_info_get(s, &game));
	CHECK(game.music_fade_ms == 400 && game.party_talk_ambient == 1 && game.map_load_hold_ms == 0 && game.map_load_hold_on_load == 0);
	const char* const* pictures = NULL;
	int32_t count = -1;
	psiv_scene_camera camera;
	CHECK(psiv_pictures(s, NULL, &count) == PSIV_INVALID_ARGUMENT && psiv_scene_camera_get(s, NULL) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_pictures(s, &pictures, &count));
	CHECK(count == 0);
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_dev_warp(s, "wren_gallery", "gallery_door"));
	settle(s);
	const psiv_action* actions;
	CHECK_OK(psiv_actions_poll(s, &actions, &count));
	psiv_field_info field;
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.exit_sound, "") == 0 && field.next_tick_us == 50000 && field.step_lands_in_us == 250000 && field.run_step_lands_in_us == 250000);
	const psiv_exit* exits;
	CHECK_OK(psiv_field_exits(s, &exits, &count));
	CHECK(count == 1 && strcmp(exits[0].sound, "gallery_door") == 0);

	walkThrough(s, 2, 1);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	settle(s);
	CHECK_OK(psiv_pictures(s, &pictures, &count));
	CHECK(count == 2 && strcmp(pictures[0], "harbor_dawn") == 0 && strcmp(pictures[1], "beacon") == 0);
	CHECK_OK(psiv_scene_camera_get(s, &camera));
	CHECK(camera.held && camera.x == 6 && camera.y == 1 && camera.ms == 1500 && camera.total_ms == 1500);
	int fades = 0;
	int cameras = 0;
	CHECK_OK(psiv_actions_poll(s, &actions, &count));
	for (int32_t i = 0; i < count; ++i) {
		if (actions[i].type == PSIV_ACTION_MUSIC_PLAY) {
			CHECK(actions[i].fade_ms == (fades == 0 ? 800 : 0));
			++fades;
		} else if (actions[i].type == PSIV_ACTION_CAMERA_TO) {
			CHECK(cameras == 1 ? strcmp(actions[i].target, "window_front") == 0 && actions[i].seconds == 1.5 : actions[i].has_position && actions[i].x == 8 && actions[i].y == 3);
			++cameras;
		} else {
			CHECK(actions[i].fade_ms == -1);
		}
	}
	CHECK(fades == 2 && cameras == 2);
	CHECK_OK(psiv_update(s, 500, PSIV_DIRECTION_NONE));
	CHECK_OK(psiv_scene_camera_get(s, &camera));
	CHECK(camera.ms == 1000);

	walkThrough(s, 6, 1);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_NORTH));
	CHECK_OK(psiv_interact(s));
	settle(s);
	CHECK_OK(psiv_actions_poll(s, &actions, &count));
	fades = 0;
	for (int32_t i = 0; i < count; ++i) {
		if (actions[i].type == PSIV_ACTION_MUSIC_PLAY) {
			CHECK(actions[i].fade_ms == 400);
			++fades;
		}
	}
	CHECK(fades == 1);
	CHECK_OK(psiv_pictures(s, &pictures, &count));
	CHECK(count == 1 && strcmp(pictures[0], "farewell") == 0);
	CHECK_OK(psiv_scene_camera_get(s, &camera));
	CHECK(!camera.held && camera.ms == 0);

	walkThrough(s, 4, 3);
	CHECK_OK(psiv_walk(s, PSIV_DIRECTION_SOUTH));
	settle(s);
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.area_id, "wren_loft") == 0 && strcmp(field.exit_sound, "gallery_door") == 0);
	psiv_session_destroy(s);
}

/* A battle aboard the skimmer driven the way a battle screen would: a macro with the vehicle's step, auto play with a
 * named macro and with the save's auto macro, then the battle played out. */
static void testVehicleBattleFromAMenu(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/vehicle_battle", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	const char* shipped = dataDir;
	dataDir = interactDataDir;
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	dataDir = shipped;
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_dev_warp(s, "wren_cellar", "cellar_door"));
	settle(s);
	CHECK_OK(psiv_dev_vehicle(s, "reed_skimmer", "wren_cellar", "skiff_post"));
	CHECK_OK(psiv_open_menu(s));
	CHECK_OK(psiv_macro_create(s, "hull"));
	CHECK_OK(psiv_macro_set_step(s, "hull", 0, "reed_skimmer", PSIV_COMMAND_ATTACK, NULL, PSIV_MACRO_TARGET_DEFAULT, NULL, -1));
	CHECK_OK(psiv_settings_set_auto_macro(s, "hull"));
	CHECK_OK(psiv_close_menu(s));
	const psiv_marker* markers;
	int32_t count = 0;
	CHECK_OK(psiv_field_markers(s, &markers, &count));
	int32_t frontX = -1;
	int32_t frontY = -1;
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(markers[i].name, "skiff_front") == 0) {
			frontX = markers[i].x;
			frontY = markers[i].y;
		}
	}
	walkThrough(s, frontX, frontY);
	CHECK_OK(psiv_face(s, PSIV_DIRECTION_EAST));
	CHECK_OK(psiv_board(s));
	CHECK_OK(psiv_dev_battle(s, "marsh"));
	CHECK(psiv_mode_get(s) == PSIV_MODE_BATTLE);
	psiv_battle_info info;
	CHECK_OK(psiv_battle_info_get(s, &info));
	CHECK(strcmp(info.vehicle, "reed_skimmer") == 0);
	/* A save made aboard mid-battle holds the macro with the vehicle's step and loads again. */
	const char* text = NULL;
	CHECK_OK(psiv_save_text(s, &text));
	CHECK(text && strstr(text, "step reed_skimmer attack - default - -1\n") != NULL);
	dataDir = interactDataDir;
	psiv_session* loaded = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	dataDir = shipped;
	CHECK_OK(psiv_load_text(loaded, text));
	const psiv_macro* macros = NULL;
	CHECK_OK(psiv_macro_list(loaded, &macros, &count));
	const psiv_macro* hull = NULL;
	for (int32_t i = 0; i < count; ++i) {
		hull = strcmp(macros[i].name, "hull") == 0 ? &macros[i] : hull;
	}
	CHECK(hull && hull->step_count == 1 && strcmp(hull->steps[0].member, "reed_skimmer") == 0);
	psiv_session_destroy(loaded);

	const psiv_macro_step_result* steps;
	CHECK_OK(psiv_battle_apply_macro(s, "hull", &steps, &count));
	CHECK(count == 1 && strcmp(steps[0].member, "reed_skimmer") == 0 && steps[0].status == PSIV_MACRO_APPLIED);
	const psiv_battle_event* events;
	CHECK_OK(psiv_battle_resolve_round(s, &events, &count));
	if (psiv_mode_get(s) == PSIV_MODE_BATTLE) {
		CHECK_OK(psiv_battle_apply_macro_fallback(s, "hull", PSIV_MACRO_FALLBACK_AUTOMATIC, &steps, &count));
		CHECK(count == 1 && strcmp(steps[0].member, "reed_skimmer") == 0);
		CHECK_OK(psiv_battle_resolve_round(s, &events, &count));
	}
	if (psiv_mode_get(s) == PSIV_MODE_BATTLE) {
		CHECK_OK(psiv_battle_set_auto_play_macro(s, "hull"));
		CHECK_OK(psiv_battle_fill_auto_round(s));
		CHECK_OK(psiv_battle_resolve_round(s, &events, &count));
		CHECK_OK(psiv_battle_set_auto_play_macro(s, NULL));
	}
	if (psiv_mode_get(s) == PSIV_MODE_BATTLE) {
		int32_t filled = 0;
		CHECK_OK(psiv_battle_start_auto(s));
		CHECK_OK(psiv_battle_fill_from_auto(s, &filled));
		CHECK(filled == 1);
		CHECK_OK(psiv_battle_resolve_round(s, &events, &count));
	}
	psiv_auto_battle played;
	if (psiv_mode_get(s) == PSIV_MODE_BATTLE) {
		CHECK_OK(psiv_battle_play(s, 0, &played));
	}
	CHECK(psiv_mode_get(s) == PSIV_MODE_FIELD);
	psiv_session_destroy(s);
}

/* Wren Mill: a patrol, a pixel path with a footprint, the triggered raft carrying the party, and their field info. */
/* Macro options, trouble modes, a step check and the trouble report, on the shipped data's training battle. */
static void testMacroTrouble(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/macro_trouble", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_open_menu(s));
	CHECK_OK(psiv_macro_create(s, "solo"));
	CHECK_OK(psiv_macro_set_step(s, "solo", 0, "fighter", PSIV_COMMAND_ATTACK, NULL, PSIV_MACRO_TARGET_ENEMY, NULL, 0));
	CHECK(psiv_macro_set_options(s, "solo", 9, PSIV_RECOVER_DEFAULT, PSIV_UNNAMED_DEFAULT) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_macro_set_options(s, "nosuch", PSIV_MACRO_TROUBLE_RECOVER, PSIV_RECOVER_DEFAULT, PSIV_UNNAMED_DEFAULT) == PSIV_REFUSED);
	CHECK_OK(psiv_macro_set_options(s, "solo", PSIV_MACRO_TROUBLE_RECOVER, PSIV_RECOVER_UNCOVERED, PSIV_UNNAMED_NONE));
	const psiv_macro* macros = NULL;
	int32_t count = 0;
	CHECK_OK(psiv_macro_list(s, &macros, &count));
	CHECK(count > 0 && strcmp(macros[count - 1].name, "solo") == 0 && macros[count - 1].on_trouble == PSIV_MACRO_TROUBLE_RECOVER &&
		macros[count - 1].recover_who == PSIV_RECOVER_UNCOVERED && macros[count - 1].unnamed == PSIV_UNNAMED_NONE);
	const char* text = NULL;
	CHECK_OK(psiv_save_text(s, &text));
	CHECK(text && strstr(text, "macros 3\n") && strstr(text, "options recover uncovered none\n"));
	CHECK(psiv_macro_set_policy(s, "solo", 5) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_macro_set_policy(s, "solo", PSIV_POLICY_EXPECTED));
	CHECK_OK(psiv_macro_list(s, &macros, &count));
	CHECK(count > 0 && macros[count - 1].policy == PSIV_POLICY_EXPECTED);
	CHECK_OK(psiv_save_text(s, &text));
	CHECK(text && strstr(text, "macros 4\n") && strstr(text, "options recover uncovered none expected\n"));
	CHECK_OK(psiv_macro_set_policy(s, "solo", PSIV_POLICY_DEFAULT));
	CHECK(strcmp(psiv_name_of(PSIV_NAME_MACRO_POLICY, PSIV_POLICY_POWER), "power") == 0);
	CHECK_OK(psiv_close_menu(s));
	CHECK(strcmp(psiv_name_of(PSIV_NAME_MACRO_TROUBLE, PSIV_MACRO_TROUBLE_STOP), "stop") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_MACRO_RECOVER_WHO, PSIV_RECOVER_UNCOVERED), "uncovered") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_MACRO_UNNAMED, PSIV_UNNAMED_AUTO), "auto") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_MACRO_FALLBACK, PSIV_MACRO_FALLBACK_LOSE_TURN), "lose_turn") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_MACRO_STEP_STATUS, PSIV_MACRO_LOST_TURN), "lost_turn") == 0);
	CHECK(strcmp(psiv_name_of(PSIV_NAME_STAT, PSIV_STAT_ATTACK), "attack") == 0);

	CHECK_OK(psiv_dev_battle(s, "training"));
	CHECK(psiv_mode_get(s) == PSIV_MODE_BATTLE);
	const psiv_macro_step_check* stepChecks = NULL;
	CHECK(psiv_battle_macro_check(s, "nosuch", &stepChecks, &count) == PSIV_REFUSED);
	CHECK_OK(psiv_battle_macro_check(s, "solo", &stepChecks, &count));
	CHECK(count == 1 && stepChecks[0].ok == 1 && stepChecks[0].actor == 0 && stepChecks[0].issue == PSIV_ISSUE_NONE && strcmp(stepChecks[0].refusal_key, "") == 0);
	psiv_macro_trouble_report trouble;
	CHECK_OK(psiv_battle_macro_trouble(s, "solo", &trouble));
	CHECK(trouble.holds == 0 && trouble.mode == PSIV_MACRO_TROUBLE_DEFAULT && trouble.troubled_step_count == 0);
	CHECK(psiv_battle_set_auto_play_macro_trouble(s, "solo", 7) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_battle_set_auto_play_macro_trouble(s, "solo", PSIV_MACRO_TROUBLE_CONTINUE));
	CHECK(psiv_battle_set_auto_play_macro_policy(s, "solo", PSIV_MACRO_TROUBLE_CONTINUE, 9) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_battle_set_auto_play_macro_policy(s, "solo", PSIV_MACRO_TROUBLE_CONTINUE, PSIV_POLICY_DEFAULT));
	CHECK_OK(psiv_battle_fill_auto_round(s));
	CHECK_OK(psiv_battle_macro_trouble(s, NULL, &trouble));
	CHECK(trouble.holds == 0 && trouble.mode == PSIV_MACRO_TROUBLE_CONTINUE);
	const psiv_macro_step_result* results = NULL;
	CHECK_OK(psiv_battle_apply_macro_fallback(s, "solo", PSIV_MACRO_FALLBACK_LOSE_TURN, &results, &count));
	CHECK(count == 1 && results[0].status == PSIV_MACRO_APPLIED);
	psiv_auto_battle played;
	CHECK_OK(psiv_battle_play(s, 60, &played));
	CHECK(played.ended && played.round_count > 0 && played.rounds[0].trouble.mode == PSIV_MACRO_TROUBLE_CONTINUE);
	CHECK_OK(psiv_battle_set_auto_play_macro_trouble(s, NULL, PSIV_MACRO_TROUBLE_DEFAULT));
	psiv_session_destroy(s);
}

static void testMotion(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/motion", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	const char* shipped = dataDir;
	dataDir = motionDataDir;
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	dataDir = shipped;
	CHECK(psiv_sizeof("psiv_platform") == sizeof(psiv_platform) && psiv_sizeof("psiv_battle_keys") == sizeof(psiv_battle_keys));
	psiv_game_info game;
	CHECK_OK(psiv_game_info_get(s, &game));
	CHECK(game.cell_pixels == 16);
	const psiv_platform* platforms = NULL;
	int32_t count = -1;
	CHECK(psiv_platforms(s, &platforms, &count) == PSIV_NOT_FOUND);
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_dev_warp(s, "wren_mill", "mill_door"));
	settle(s);
	CHECK(psiv_platforms(s, NULL, &count) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_platforms(s, &platforms, &count));
	CHECK(count == 2 && strcmp(platforms[0].id, "raft") == 0 && platforms[0].mode == PSIV_PLATFORM_TRIGGERED && platforms[0].end == 0);
	CHECK(platforms[0].pixel_x == 6 * 16 && platforms[0].pixel_y == 2 * 16 && platforms[0].run_us == 600000);
	const psiv_actor* actors;
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	int sawPatrol = 0;
	int sawPath = 0;
	for (int32_t i = 0; i < count; ++i) {
		sawPatrol = sawPatrol || (strcmp(actors[i].id, "mill_hand") == 0 && actors[i].patrols && !actors[i].on_path);
		sawPath = sawPath || (strcmp(actors[i].id, "mill_wheel") == 0 && actors[i].on_path && actors[i].home_x == 11 && actors[i].home_y == 4 &&
			actors[i].footprint_w == 1 && actors[i].footprint_h == 1 && actors[i].path_x == 0);
	}
	CHECK(sawPatrol && sawPath);
	CHECK_OK(psiv_update(s, 100, PSIV_DIRECTION_NONE));
	CHECK_OK(psiv_field_actors(s, &actors, &count));
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(actors[i].id, "mill_wheel") == 0) {
			CHECK(actors[i].path_x == 4 && actors[i].path_y == -2 && actors[i].x == 11 && actors[i].y == 3);
		}
	}
	walkThrough(s, 5, 2);
	CHECK_OK(psiv_walk(s, PSIV_DIRECTION_EAST));
	psiv_field_info field;
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.riding_platform, "raft") == 0 && field.step_mask == 0 && strcmp(field.area_name_key, "area.wren_mill") == 0);
	CHECK_OK(psiv_update(s, 1000, PSIV_DIRECTION_NONE));
	CHECK_OK(psiv_platforms(s, &platforms, &count));
	CHECK(platforms[0].end == 1 && platforms[0].carrying && platforms[0].x == 9);
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(field.leader_x == 9 && field.leader_y == 2);
	CHECK_OK(psiv_walk(s, PSIV_DIRECTION_EAST));
	CHECK_OK(psiv_field_info_get(s, &field));
	CHECK(strcmp(field.riding_platform, "") == 0 && field.leader_x == 10);
	psiv_session_destroy(s);
}

/* Load from the title only, the keys a battle can carry, the member detail's added fields and a skill's maximum. */
static void testLoadKeysAndDetail(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/load_keys", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	CHECK(psiv_can_load(s) == 1 && psiv_can_load(NULL) == 0);
	psiv_battle_keys keys;
	CHECK(psiv_battle_keys_get(s, NULL) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_battle_keys_get(s, &keys));
	CHECK(keys.message_count > 9 && keys.notice_count > 0 && keys.cancel_reason_count == 6);
	CHECK(strcmp(keys.cancel_reasons[0], "actor_unable") == 0);
	int sawInstant = 0;
	for (int32_t i = 0; i < keys.message_count; ++i) {
		sawInstant = sawInstant || strcmp(keys.messages[i], "battle.instant_defeat") == 0;
	}
	CHECK(sawInstant);
	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK(psiv_can_load(s) == 0);
	psiv_member_detail detail;
	CHECK_OK(psiv_member_detail_get(s, "fighter", &detail));
	CHECK(detail.attack >= detail.base_stats.strength && detail.creature_kind == PSIV_CREATURE_LIVING && detail.experience_for_level >= detail.experience_to_next);
	CHECK(detail.total_experience >= 0);
	const psiv_ability_info* abilities;
	int32_t count;
	CHECK_OK(psiv_member_abilities(s, "fighter", &abilities, &count));
	for (int32_t i = 0; i < count; ++i) {
		if (strcmp(abilities[i].id, "focus_shot") == 0) {
			CHECK(abilities[i].max_uses == 3);
		}
	}
	psiv_session_destroy(s);
}

/* The built-in speed levels (the game lists none) by index, the profile's enum following; a chest's kind on its point. */
static void testOptionLevels(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/option_levels", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	CHECK(psiv_sizeof("psiv_option_level") == sizeof(psiv_option_level) && psiv_sizeof("psiv_option_levels") == sizeof(psiv_option_levels));
	psiv_option_levels levels;
	CHECK_OK(psiv_option_levels_get(s, PSIV_OPTION_TEXT_SPEED, &levels));
	CHECK(levels.count == 4 && levels.current == PSIV_TEXT_NORMAL && levels.default_index == PSIV_TEXT_NORMAL && levels.declared == 0);
	CHECK(levels.count == 4 && strcmp(levels.levels[3].id, "instant") == 0 && levels.levels[3].value == 0 && strcmp(levels.levels[0].label_key, "ui.text_speed.slow") == 0);
	CHECK(levels.count == 4 && strcmp(levels.levels[0].label, "Slow") == 0);
	CHECK_OK(psiv_option_level_set(s, PSIV_OPTION_TEXT_SPEED, PSIV_TEXT_FAST));
	psiv_profile p;
	CHECK_OK(psiv_profile_get(s, &p));
	CHECK(p.text_speed == PSIV_TEXT_FAST);
	CHECK_OK(psiv_option_levels_get(s, PSIV_OPTION_BATTLE_SPEED, &levels));
	CHECK(levels.count == 4 && levels.levels[1].value == 150 && strcmp(levels.levels[1].label, "1.5x") == 0);
	p.battle_speed = PSIV_BATTLE_X2;
	CHECK_OK(psiv_profile_set(s, &p));
	CHECK_OK(psiv_option_levels_get(s, PSIV_OPTION_BATTLE_SPEED, &levels));
	CHECK(levels.current == PSIV_BATTLE_X2);
	psiv_error e;
	CHECK(psiv_option_level_set(s, PSIV_OPTION_BATTLE_SPEED, 4) == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.unknown_option_level") == 0);
	CHECK(psiv_option_level_set(s, 2, 0) == PSIV_INVALID_ARGUMENT);
	/* No choice to back out of. */
	CHECK(psiv_choose_back(s) == PSIV_REFUSED);
	psiv_last_error(s, &e);
	CHECK(strcmp(e.key, "game.refuse.no_choice") == 0);
	CHECK(psiv_option_levels_get(s, PSIV_OPTION_TEXT_SPEED, NULL) == PSIV_INVALID_ARGUMENT);

	CHECK_OK(psiv_new_game(s));
	settle(s);
	CHECK_OK(psiv_dev_warp(s, "reed_path", "cache_front"));
	settle(s);
	const psiv_point* points = NULL;
	int32_t count = 0;
	int chests = 0;
	CHECK_OK(psiv_field_points(s, &points, &count));
	for (int32_t i = 0; i < count; ++i) {
		if (points[i].kind == PSIV_POINT_CHEST) {
			++chests;
			CHECK(strcmp(points[i].chest_kind, "") == 0);
		} else {
			CHECK(points[i].chest_kind && points[i].chest_kind[0] == 0);
		}
	}
	CHECK(chests == 1);
	psiv_session_destroy(s);
}

static void testHostCalls(void) {
	char saves[512];
	snprintf(saves, sizeof saves, "%s/host_calls", scratch);
	makeDirectory(saves);
	removeFiles(saves);
	psiv_session* s = create(saves, -1, PSIV_PRESENTATION_INSTANT);
	CHECK(psiv_sizeof("psiv_talk_line") == sizeof(psiv_talk_line) && psiv_sizeof("psiv_inventory_entry") == sizeof(psiv_inventory_entry));
	const psiv_talk_line* lines = NULL;
	const char* const* allies = NULL;
	int32_t count = -1;
	CHECK(psiv_party_talk_lines(s, NULL, &count) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_party_talk_lines(s, &lines, &count));
	CHECK(count == 0);
	CHECK_OK(psiv_new_game(s));
	settle(s);
	/* The fighter alone says nothing; the query shows nothing either way. */
	CHECK_OK(psiv_party_talk_lines(s, &lines, &count));
	CHECK(count == 0);

	/* A revive reaches nobody standing, a tonic reaches the fighter. */
	CHECK_OK(psiv_allies_out_of_reach(s, PSIV_COMMAND_ITEM, "second_wind", NULL, &allies, &count));
	CHECK(count == 1 && strcmp(allies[0], "fighter") == 0);
	CHECK_OK(psiv_allies_out_of_reach(s, PSIV_COMMAND_ITEM, "tonic", "", &allies, &count));
	CHECK(count == 0);
	CHECK(psiv_allies_out_of_reach(s, 9, "tonic", "", &allies, &count) == PSIV_INVALID_ARGUMENT);
	CHECK(psiv_allies_out_of_reach(s, PSIV_COMMAND_ITEM, NULL, "", &allies, &count) == PSIV_INVALID_ARGUMENT);

	/* Discard from the field menu; the inventory says what may go. */
	CHECK(psiv_discard(s, "tonic", 1) == PSIV_REFUSED && errorIs(s, "game.refuse.discard_menu_only"));
	CHECK_OK(psiv_open_menu(s));
	const psiv_inventory_entry* bag = NULL;
	CHECK_OK(psiv_inventory(s, &bag, &count));
	CHECK(count >= 1 && strcmp(bag[0].id, "tonic") == 0 && bag[0].discardable == 1);
	CHECK(psiv_discard(s, "tonic", 0) == PSIV_REFUSED && errorIs(s, "game.refuse.discard_one"));
	CHECK_OK(psiv_discard(s, "tonic", 1));
	CHECK(psiv_discard(s, "tonic", 1) == PSIV_REFUSED && errorIs(s, "game.refuse.discard_not_held"));

	/* The newest save is marked. */
	CHECK_OK(psiv_save_manual(s, "manual_1"));
	CHECK_OK(psiv_save_manual(s, "manual_0"));
	const psiv_save_summary* list = NULL;
	CHECK_OK(psiv_saves(s, &list, &count));
	int newest = 0;
	for (int32_t i = 0; i < count; ++i) {
		newest += list[i].newest;
		CHECK(!list[i].newest || strcmp(list[i].slot, "manual_0") == 0);
	}
	CHECK(newest == 1);
	CHECK_OK(psiv_close_menu(s));

	/* dev combo needs a battle and a member for each part. */
	CHECK(psiv_dev_combo(s, "storm_spark") == PSIV_REFUSED && errorIs(s, "game.refuse.no_battle"));
	CHECK_OK(psiv_dev_battle(s, "training"));
	CHECK(psiv_dev_combo(s, "nothing") == PSIV_REFUSED && errorIs(s, "game.refuse.combo_not_found"));
	CHECK(psiv_dev_combo(s, "storm_spark") == PSIV_REFUSED && errorIs(s, "game.refuse.combo_no_member"));
	CHECK(psiv_dev_combo(s, NULL) == PSIV_INVALID_ARGUMENT);
	CHECK_OK(psiv_allies_out_of_reach(s, PSIV_COMMAND_ITEM, "second_wind", "fighter", &allies, &count));
	CHECK(count == 1 && strcmp(allies[0], "fighter") == 0);
	psiv_session_destroy(s);
}

int main(int argc, char** argv) {
	for (int i = 1; i + 1 < argc; i += 2) {
		if (strcmp(argv[i], "--data") == 0) {
			dataDir = argv[i + 1];
		} else if (strcmp(argv[i], "--actor-data") == 0) {
			actorDataDir = argv[i + 1];
		} else if (strcmp(argv[i], "--interact-data") == 0) {
			interactDataDir = argv[i + 1];
		} else if (strcmp(argv[i], "--background-data") == 0) {
			backgroundDataDir = argv[i + 1];
		} else if (strcmp(argv[i], "--scene-data") == 0) {
			sceneDataDir = argv[i + 1];
		} else if (strcmp(argv[i], "--motion-data") == 0) {
			motionDataDir = argv[i + 1];
		} else if (strcmp(argv[i], "--scratch") == 0) {
			scratch = argv[i + 1];
		}
	}
	makeDirectory(scratch);
	testVersion();
	testCreateErrors();
	testRefusals();
	testStringOwnership();
	testSaveAndLoad();
	testBattleStream();
	testPresentation();
	testFieldAndMenus();
	testAdventurePaths();
	testActors();
	if (actorDataDir[0]) {
		testScriptedActors();
	}
	if (interactDataDir[0]) {
		testInteraction();
		testVehicleBattleFromAMenu();
	}
	if (sceneDataDir[0]) {
		testScene();
	}
	if (motionDataDir[0]) {
		testMotion();
	}
	testLoadKeysAndDetail();
	testMacroTrouble();
	if (backgroundDataDir[0]) {
		testBackground();
		testDenAndCredits();
	}
	testMenus();
	testEquipPreview();
	testDescriptions();
	testInventorySort();
	testDifficulties();
	testCamerasAndMusic();
	testAmbientTalk();
	testHostData();
	testVehiclesAndTravel();
	testDevTools();
	testFollowersKindsAndUses();
	testFells();
	testCogwayAndFormations();
	testHostCalls();
	testOptionLevels();
	if (failures) {
		fprintf(stderr, "%d of %d checks failed\n", failures, checks);
		return 1;
	}
	printf("c_tests: %d checks passed\n", checks);
	return 0;
}
